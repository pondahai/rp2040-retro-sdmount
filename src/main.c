/* rp2040-retro-sdmount - 把掌機的 SD 卡變成 USB 隨身碟
 *
 * 從 rp2040-retro-loader 的選單啟動,USB 線接電腦,電腦就看得到 SD 卡,
 * 可以直接把新的 .uf2 與封面複製進去。
 *
 * S2 階段(見 rp2040-retro-sdk docs/SD-MOUNT-PLAN.md):
 *   - USB 複合裝置:MSC(SD 卡)+ CDC 序列埠(收指令)
 *   - 回選單三種方式:電腦送 `done`、掌機按 START、或實體 RESET
 *     前兩種是軟體重置,要靠 boot_map.h 的 RETRO_MSG_SHOW_MENU 告訴載入器
 *     「這次要顯示選單」,否則載入器會把 watchdog 重置當成穿透、直接跳回本 APP。
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#include "boot_map.h"   /* rp2040-retro-loader/common:RETRO_MSG_* */
#include "board.h"
#include "lcd.h"
#include "msc_disk.h"

/* ---- 畫面 ----
 *
 * 40x30 文字模式。只在狀態改變時重畫那幾列 —— lcd.c 是 blocking SPI,
 * 一列 40 字約 1.3 ms;重畫期間 tud_task() 沒在跑,太頻繁會拖慢傳輸。 */
enum {
    ROW_TITLE  = 1,
    ROW_CARD   = 4,
    ROW_USB    = 6,
    ROW_IO     = 8,
    ROW_HINT1  = 12,
    ROW_HINT2  = 13,
    ROW_HINT3  = 14,
    ROW_WARN   = 17,
    ROW_FOOT   = 28,
};

typedef enum {
    UI_NO_CARD,
    UI_WAIT_USB,
    UI_MOUNTED,
    UI_EJECTED,
    UI_UNPLUGGED,
} ui_mode;

static const char *const mode_name[] = {
    [UI_NO_CARD]   = "no-card",
    [UI_WAIT_USB]  = "waiting",
    [UI_MOUNTED]   = "mounted",
    [UI_EJECTED]   = "ejected",
    [UI_UNPLUGGED] = "unplugged",
};

static void line(int row, const char *s, uint16_t fg)
{
    lcd_puts_line(row, s, fg, C_BLACK);
}

static void draw_static(void)
{
    lcd_clear(C_BLACK);
    line(ROW_TITLE, "  RETRO SD MOUNT", C_YELLOW);
    line(ROW_FOOT,  "  USB MSC+CDC  /  S2", C_GREY);
}

static void draw_card(const msc_state *s)
{
    char buf[41];
    if (!s->card_ok) {
        line(ROW_CARD, "  SD: NOT FOUND", C_RED);
        return;
    }
    uint32_t mb = (uint32_t)(((uint64_t)s->sectors * 512u) >> 20);
    snprintf(buf, sizeof buf, "  SD: %lu MB", (unsigned long)mb);
    line(ROW_CARD, buf, C_WHITE);
}

static void draw_mode(ui_mode m)
{
    static const char *const usb[] = {
        [UI_NO_CARD]   = "  USB: -",
        [UI_WAIT_USB]  = "  USB: WAITING FOR PC...",
        [UI_MOUNTED]   = "  USB: CONNECTED",
        [UI_EJECTED]   = "  USB: EJECTED",
        [UI_UNPLUGGED] = "  USB: CABLE UNPLUGGED",
    };
    static const uint16_t col[] = {
        [UI_NO_CARD]   = C_GREY,
        [UI_WAIT_USB]  = C_WHITE,
        [UI_MOUNTED]   = C_GREEN,
        [UI_EJECTED]   = C_YELLOW,
        [UI_UNPLUGGED] = C_YELLOW,
    };
    line(ROW_USB, usb[m], col[m]);
    line(ROW_WARN, "", C_WHITE);

    switch (m) {
    case UI_NO_CARD:
        line(ROW_HINT1, "  Insert an SD card.", C_WHITE);
        line(ROW_HINT2, "  START: back to menu", C_WHITE);
        line(ROW_HINT3, "", C_WHITE);
        break;
    case UI_WAIT_USB:
        line(ROW_HINT1, "  Connect the USB cable", C_WHITE);
        line(ROW_HINT2, "  to a computer.", C_WHITE);
        line(ROW_HINT3, "  START: back to menu", C_WHITE);
        break;
    case UI_MOUNTED:
        line(ROW_HINT1, "  Copy files on the computer.", C_WHITE);
        line(ROW_HINT2, "  EJECT the drive there first,", C_YELLOW);
        line(ROW_HINT3, "  then press START.", C_WHITE);
        break;
    case UI_UNPLUGGED:
        /* 沒退出就拔線:每次寫入都是同步寫進 SD 的,但電腦端的快取
         * 不一定送完了,不能說「安全」。 */
        line(ROW_HINT1, "  Unplugged without eject.", C_YELLOW);
        line(ROW_HINT2, "  START: back to menu", C_WHITE);
        line(ROW_HINT3, "", C_WHITE);
        break;
    case UI_EJECTED:
        line(ROW_HINT1, "  Safe to remove.", C_GREEN);
        line(ROW_HINT2, "  START: back to menu", C_WHITE);
        line(ROW_HINT3, "", C_WHITE);
        break;
    }
}

static void draw_io(const msc_state *s)
{
    char buf[41];
    snprintf(buf, sizeof buf, "  R %lu KB   W %lu KB%s",
             (unsigned long)(s->read_bytes >> 10),
             (unsigned long)(s->write_bytes >> 10),
             s->errors ? "   ERR" : "");
    line(ROW_IO, buf, s->errors ? C_RED : C_GREY);
}

/* ---- 回選單 ---- */

/* 電腦還掛著磁碟時不准回選單:FAT 可能只寫了一半。 */
static bool can_leave(ui_mode m)
{
    return m != UI_MOUNTED;
}

static void __attribute__((noreturn)) back_to_menu(void)
{
    line(ROW_WARN, "  Returning to menu...", C_GREEN);

    /* 讓 CDC 的回覆送出去、讓電腦看到裝置正常離線 */
    absolute_time_t until = make_timeout_time_ms(100);
    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
        tud_task();
    }
    tud_disconnect();
    sleep_ms(20);

    watchdog_hw->scratch[RETRO_MSG_SCRATCH] = RETRO_MSG_SHOW_MENU;
    watchdog_reboot(0, 0, 10);
    for (;;) tight_loop_contents();
}

/* ---- CDC 指令 ----
 *
 * 一行一個指令,ASCII,\n 或 \r 結尾。回覆也是一行。
 *
 *   ping     -> "retro-sdmount S2"
 *   status   -> "state=<...> card=<0|1> mb=<n> r=<KB> w=<KB> err=<n>"
 *   done     -> 已退出(或沒掛載):"ok",然後回選單
 *               還掛著:"error: eject the drive first"
 */
static char cmd_buf[32];
static size_t cmd_len;

static void cdc_reply(const char *s)
{
    tud_cdc_write_str(s);
    tud_cdc_write_str("\r\n");
    tud_cdc_write_flush();
}

static void handle_cmd(const char *cmd, ui_mode m, const msc_state *s)
{
    char buf[96];

    if (strcmp(cmd, "ping") == 0) {
        cdc_reply("retro-sdmount S2");
    } else if (strcmp(cmd, "status") == 0) {
        snprintf(buf, sizeof buf, "state=%s card=%d mb=%lu r=%lu w=%lu err=%lu",
                 mode_name[m], s->card_ok ? 1 : 0,
                 (unsigned long)(((uint64_t)s->sectors * 512u) >> 20),
                 (unsigned long)(s->read_bytes >> 10),
                 (unsigned long)(s->write_bytes >> 10),
                 (unsigned long)s->errors);
        cdc_reply(buf);
    } else if (strcmp(cmd, "done") == 0) {
        if (!can_leave(m)) {
            cdc_reply("error: eject the drive first");
            line(ROW_WARN, "  PC asked to finish: EJECT first", C_RED);
            return;
        }
        cdc_reply("ok");
        back_to_menu();
    } else if (cmd[0]) {
        cdc_reply("error: unknown command");
    }
}

static void poll_cdc(ui_mode m, const msc_state *s)
{
    while (tud_cdc_available()) {
        int c = tud_cdc_read_char();
        if (c == '\r' || c == '\n') {
            cmd_buf[cmd_len] = 0;
            handle_cmd(cmd_buf, m, s);
            cmd_len = 0;
        } else if (cmd_len < sizeof cmd_buf - 1) {
            cmd_buf[cmd_len++] = (char)c;
        }
    }
}

/* ---- 按鍵 ---- */

static void buttons_init(void)
{
    gpio_init(BTN_PIN_START);
    gpio_set_dir(BTN_PIN_START, GPIO_IN);
    gpio_pull_up(BTN_PIN_START);
}

/* 回傳 true 表示「剛放開」—— 用放開觸發,免得按住的 START 在載入器
 * 開機時又被讀到。去彈跳靠 20 ms 取樣間隔。 */
static bool start_released(void)
{
    static absolute_time_t next;
    static bool was_down;

    if (absolute_time_diff_us(next, get_absolute_time()) < 0) return false;
    next = make_timeout_time_ms(20);

    bool down = !gpio_get(BTN_PIN_START);
    bool released = was_down && !down;
    was_down = down;
    return released;
}

/* 不掃鍵盤,但 595 的 LATCH 要停在高。浮接的話 LCD 翻 D/C(現行接線下
 * 兼鍵盤 CLOCK)時 595 可能亂鎖存,鍵盤 LED 會閃。 */
static void park_keyboard(void)
{
    gpio_init(MTX_PIN_LATCH);
    gpio_set_dir(MTX_PIN_LATCH, GPIO_OUT);
    gpio_put(MTX_PIN_LATCH, 1);
}

int main(void)
{
    park_keyboard();
    buttons_init();
    lcd_init();
    draw_static();

    msc_disk_init();
    const msc_state *s = msc_disk_state();
    draw_card(s);

    /* SD 不在也照樣開 USB:電腦會看到一台「沒插卡」的讀卡機,
     * 比完全沒反應好判斷。 */
    tusb_init();

    ui_mode shown = (ui_mode)-1;
    uint32_t shown_act = 0, shown_err = 0;
    absolute_time_t next_io = get_absolute_time();

    for (;;) {
        tud_task();

        ui_mode m;
        if (!s->card_ok)            m = UI_NO_CARD;
        else if (s->ejected)        m = UI_EJECTED;
        else if (tud_mounted())     m = UI_MOUNTED;
        else if (shown == UI_MOUNTED || shown == UI_UNPLUGGED)
                                    m = UI_UNPLUGGED;
        else                        m = UI_WAIT_USB;

        if (m != shown) {
            draw_mode(m);
            shown = m;
        }

        poll_cdc(m, s);

        if (start_released()) {
            if (can_leave(m)) back_to_menu();
            line(ROW_WARN, "  EJECT on the computer first!", C_RED);
        }

        /* 讀寫計數每 250 ms 最多重畫一次 */
        if ((s->activity != shown_act || s->errors != shown_err) &&
            absolute_time_diff_us(next_io, get_absolute_time()) >= 0) {
            draw_io(s);
            shown_act = s->activity;
            shown_err = s->errors;
            next_io = make_timeout_time_ms(250);
        }
    }
}

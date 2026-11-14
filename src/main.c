/* rp2040-retro-sdmount - 把掌機的 SD 卡變成 USB 隨身碟
 *
 * 從 rp2040-retro-loader 的選單啟動,USB 線接電腦,電腦就看得到 SD 卡,
 * 可以直接把新的 .uf2 與封面放進去。用完在電腦上「退出」,再按掌機的
 * RESET 回到選單。
 *
 * 這是 S1 階段(見 rp2040-retro-sdk docs/SD-MOUNT-PLAN.md):
 *   - 只有 MSC,沒有 CDC
 *   - 回選單靠實體 RESET(冷重置 → loader 一定顯示選單)
 *   - loader 不用改
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "tusb.h"

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
    ROW_FOOT   = 28,
};

typedef enum {
    UI_NO_CARD,
    UI_WAIT_USB,
    UI_MOUNTED,
    UI_EJECTED,
    UI_UNPLUGGED,
} ui_mode;

static void line(int row, const char *s, uint16_t fg)
{
    lcd_puts_line(row, s, fg, C_BLACK);
}

static void draw_static(void)
{
    lcd_clear(C_BLACK);
    line(ROW_TITLE, "  RETRO SD MOUNT", C_YELLOW);
    line(ROW_FOOT,  "  USB " "MSC" "  /  S1", C_GREY);
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

    switch (m) {
    case UI_NO_CARD:
        line(ROW_HINT1, "  Insert an SD card,", C_WHITE);
        line(ROW_HINT2, "  then press RESET.", C_WHITE);
        line(ROW_HINT3, "", C_WHITE);
        break;
    case UI_WAIT_USB:
        line(ROW_HINT1, "  Connect the USB cable", C_WHITE);
        line(ROW_HINT2, "  to a computer.", C_WHITE);
        line(ROW_HINT3, "", C_WHITE);
        break;
    case UI_MOUNTED:
        line(ROW_HINT1, "  Copy files on the computer.", C_WHITE);
        line(ROW_HINT2, "  EJECT the drive there first,", C_YELLOW);
        line(ROW_HINT3, "  then press RESET.", C_WHITE);
        break;
    case UI_UNPLUGGED:
        /* 沒退出就拔線:資料多半已寫進去(每次寫入都是同步的),
         * 但電腦端的快取不一定送完了,不能說「安全」。 */
        line(ROW_HINT1, "  Unplugged without eject.", C_YELLOW);
        line(ROW_HINT2, "  Press RESET to return", C_WHITE);
        line(ROW_HINT3, "  to the menu.", C_WHITE);
        break;
    case UI_EJECTED:
        line(ROW_HINT1, "  Safe to remove.", C_GREEN);
        line(ROW_HINT2, "  Press RESET to return", C_WHITE);
        line(ROW_HINT3, "  to the menu.", C_WHITE);
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

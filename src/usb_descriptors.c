/* USB 描述元:單一 MSC 介面。
 *
 * VID/PID 是 TinyUSB 範例用的開發測試碼(0xCAFE)。公開散布前要換成
 * 正式的 —— 可以向 Raspberry Pi 免費申請一個 PID(見 SD-MOUNT-PLAN.md §6)。 */
#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"

#define USB_VID   0xCAFE
#define USB_PID   0x4003
#define USB_BCD   0x0200

enum { ITF_NUM_MSC, ITF_NUM_TOTAL };

#define EPNUM_MSC_OUT   0x01
#define EPNUM_MSC_IN    0x81

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,     /* 由介面決定 */
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* 字串描述元。序號用晶片的 unique id,讓同時接兩台時電腦分得開。 */
static const char *const string_desc[] = {
    NULL,               /* 0: 語言,另外處理 */
    "rp2040-retro",     /* 1: 製造商 */
    "Retro SD Mount",   /* 2: 產品 */
    NULL,               /* 3: 序號,執行期產生 */
};

static uint16_t desc_str[33];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t n;

    if (index == 0) {
        desc_str[1] = 0x0409;   /* English (US) */
        n = 1;
    } else if (index == 3) {
        char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(serial, sizeof serial);
        n = (uint8_t)strlen(serial);
        for (uint8_t i = 0; i < n; i++) desc_str[1 + i] = serial[i];
    } else {
        if (index >= sizeof string_desc / sizeof string_desc[0]) return NULL;
        const char *s = string_desc[index];
        n = (uint8_t)strlen(s);
        if (n > 32) n = 32;
        for (uint8_t i = 0; i < n; i++) desc_str[1 + i] = s[i];
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}

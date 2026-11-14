/* TinyUSB 設定:複合裝置。
 *
 * MSC(SD 卡)+ CDC(序列埠,收「完成」等指令)。見
 * rp2040-retro-sdk docs/SD-MOUNT-PLAN.md。 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_NONE
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__ ((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* 一次轉發幾個磁區。必須是 512 的倍數;越大傳輸越順,
 * sdcard.c 的 disk_read/disk_write 一次最多 128 個磁區,8 個綽綽有餘。 */
#define CFG_TUD_MSC_EP_BUFSIZE  (8 * 512)

#define CFG_TUD_CDC_RX_BUFSIZE  64
#define CFG_TUD_CDC_TX_BUFSIZE  256
#define CFG_TUD_CDC_EP_BUFSIZE  64

#endif /* TUSB_CONFIG_H */

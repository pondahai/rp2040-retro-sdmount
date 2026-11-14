/* MSC 回呼:把 SCSI 的讀寫直接轉成 SD 卡的磁區讀寫。
 *
 * 這一層**不開檔案系統**。FAT 完全由電腦那一端管理;掌機這邊只要碰了
 * FatFs,兩邊就會同時改 FAT,卡就壞了。所以這裡只呼叫 sdcard.c 的
 * disk_read / disk_write / disk_ioctl,它們是 FatFs 底下那層的磁區 I/O。 */
#include <string.h>
#include "tusb.h"
#include "ff.h"
#include "diskio.h"
#include "msc_disk.h"

#define SECTOR 512u

static msc_state st;

void msc_disk_init(void)
{
    memset(&st, 0, sizeof st);

    DSTATUS s = disk_initialize(0);
    if (s & (STA_NOINIT | STA_NODISK)) return;

    LBA_t count = 0;
    if (disk_ioctl(0, GET_SECTOR_COUNT, &count) != RES_OK || count == 0) return;

    st.sectors  = (uint32_t)count;
    st.card_ok  = true;
}

const msc_state *msc_disk_state(void) { return &st; }

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    /* 欄位是固定寬度、空白補齊,不是 C 字串 */
    memcpy(vendor_id,   "RETRO   ", 8);
    memcpy(product_id,  "SD Mount        ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    if (!st.card_ok || st.ejected) {
        /* NOT READY, MEDIUM NOT PRESENT —— 電腦會當成「沒插卡」 */
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_count = st.card_ok ? st.sectors : 0;
    *block_size  = SECTOR;
}

/* 電腦端「退出」會送 START STOP UNIT(start=0, load_eject=1)。
 * 收到之後就不再接受讀寫,畫面提示可以按 RESET。 */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                           bool start, bool load_eject)
{
    (void)lun; (void)power_condition;
    if (load_eject) {
        if (start) {
            st.ejected = false;     /* 重新載入 */
        } else {
            disk_ioctl(0, CTRL_SYNC, NULL);
            st.ejected = true;
        }
    }
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize)
{
    (void)lun;
    if (!st.card_ok || st.ejected) return -1;
    if (offset % SECTOR || bufsize % SECTOR) return -1;

    LBA_t first = lba + offset / SECTOR;
    UINT  n     = bufsize / SECTOR;
    if (first + n > st.sectors) return -1;

    st.busy = true;
    DRESULT r = disk_read(0, buffer, first, n);
    st.busy = false;
    if (r != RES_OK) { st.errors++; return -1; }

    st.read_bytes += bufsize;
    st.activity++;
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    if (!st.card_ok || st.ejected) return -1;
    if (offset % SECTOR || bufsize % SECTOR) return -1;

    LBA_t first = lba + offset / SECTOR;
    UINT  n     = bufsize / SECTOR;
    if (first + n > st.sectors) return -1;

    st.busy = true;
    DRESULT r = disk_write(0, buffer, first, n);
    st.busy = false;
    if (r != RES_OK) { st.errors++; return -1; }

    st.write_bytes += bufsize;
    st.activity++;
    st.dirty = true;
    return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

/* 其他 SCSI 指令。TinyUSB 已經內建處理常見的那幾個;
 * 這裡只補 Windows/macOS 會問、但沒有內建的。 */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void *buffer, uint16_t bufsize)
{
    (void)buffer; (void)bufsize;

    switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        return 0;   /* 不鎖,接受就好 */

    case 0x35:      /* SYNCHRONIZE CACHE (10) */
        disk_ioctl(0, CTRL_SYNC, NULL);
        st.dirty = false;
        return 0;

    default:
        /* ILLEGAL REQUEST, INVALID COMMAND OPERATION CODE */
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}

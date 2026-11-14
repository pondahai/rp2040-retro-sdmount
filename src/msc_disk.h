#ifndef MSC_DISK_H
#define MSC_DISK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     card_ok;       /* SD 初始化成功 */
    bool     ejected;       /* 電腦已送「退出」 */
    bool     busy;          /* 正在讀寫 SD */
    bool     dirty;         /* 寫過、但電腦還沒要求同步 */
    uint32_t sectors;       /* 容量(512 bytes 為單位) */
    uint32_t activity;      /* 每次讀寫 +1,畫面用來判斷有沒有在動 */
    uint32_t errors;
    uint64_t read_bytes;
    uint64_t write_bytes;
} msc_state;

/* 初始化 SD 卡。失敗時 card_ok = false,電腦會看到「沒插卡」。 */
void msc_disk_init(void);

const msc_state *msc_disk_state(void);

#endif /* MSC_DISK_H */

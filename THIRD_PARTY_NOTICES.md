# Third-Party Notices

本 repo 的 `vendor/` 目錄收錄了第三方程式碼。散布本專案時，以下聲明必須隨著一起散布。

專案自身的程式碼（`src/`、`tools/`）採 MIT，見 `LICENSE`。

> 只用到 `sdcard.c` 的磁區 I/O；FatFs 本體被一起編譯但未被呼叫，連結時會被丟掉。

---

## 1. FatFs

`vendor/drivers/fatfs/` —— FatFs Generic FAT Filesystem Module R0.14b。

| | |
|---|---|
| 上游 | http://elm-chan.org/fsw/ff/ |
| 授權 | BSD 類(1-clause),全文見 `vendor/drivers/fatfs/ff.h` 檔頭 |
| 著作權 | Copyright (C) 2021, ChaN, all right reserved. |

---

## 2. SD 卡驅動

`vendor/drivers/sdcard/` —— PIO SPI SD 卡驅動。

| 檔案 | 授權 | 著作權 |
|---|---|---|
| `sdcard.c/h`、`spi.pio` 等 | BSD-2-Clause,全文見 `vendor/drivers/sdcard/LICENSE` | Copyright (c) 2020-2021, Elehobica |
| `pio_spi.c/h` | BSD-3-Clause(SPDX 標於檔頭) | Copyright (c) 2020 Raspberry Pi (Trading) Ltd. |

---

## 3. 同生態系專案

下列檔案搬自作者自己的其他 repo，著作權同屬本專案作者，於本專案中以 MIT 散布：

| 檔案 | 來源 |
|---|---|
| `vendor/lcd.c`、`vendor/lcd.h` | [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader)（經 rp2040-retro-editor） |
| `vendor/font8x8.h` | 同上；字型本身是 font8x8_basic（Daniel Hepper，public domain） |
| `src/board.h` | [rp2040-retro-editor](https://github.com/pondahai/rp2040-retro-editor) |

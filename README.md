# rp2040-retro-sdmount

<img src="assets/icon.png" width="96" align="right" alt="封面">

把 RP2040 掌機的 SD 卡變成 **USB 隨身碟**。不用再關機拔卡：從載入器選單啟動這支 APP，
接上電腦，直接把新的 `.uf2` 和封面複製進去。

屬於 [rp2040-retro-handheld](https://github.com/pondahai/rp2040-retro-handheld) 生態系，
在 [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader) 的選單裡是一個普通項目。
設計見 [rp2040-retro-sdk `docs/SD-MOUNT-PLAN.md`](https://github.com/pondahai/rp2040-retro-sdk/blob/main/docs/SD-MOUNT-PLAN.md)。

> 🚧 **S1 階段**：只有隨身碟功能，回選單靠實體 RESET。尚未上機驗證。

## 用法

1. 把 `SDMOUNT.UF2` 與 `SDMOUNT.RAW` 放進 SD 卡**根目錄**（第一次只能用讀卡機放）
2. 開機 → 選單選 **SDMOUNT** → A
3. USB 線接電腦，電腦會出現一個磁碟，就是 SD 卡
4. 複製檔案
5. **先在電腦上「退出」那個磁碟**，掌機畫面變成 `EJECTED / Safe to remove`
6. 按掌機的 **RESET** → 回到選單，剛放進去的 APP 已經在裡面

畫面狀態：

| 顯示 | 意思 |
| :--- | :--- |
| `SD: NOT FOUND` | 沒插卡或卡壞了；電腦會看到一台「沒插卡」的讀卡機 |
| `USB: WAITING FOR PC...` | 還沒接電腦 |
| `USB: CONNECTED` | 電腦已掛載，可以複製 |
| `USB: EJECTED` | 電腦已退出，可以按 RESET |
| `USB: CABLE UNPLUGGED` | 沒退出就拔線了 —— 資料多半已寫入，但不保證 |
| `R ... KB  W ... KB` | 累計讀寫量；出現 `ERR` 表示有磁區讀寫失敗 |

## 設計重點

- **不開檔案系統**：掌機只把 SCSI 讀寫轉成 SD 磁區讀寫（`sdcard.c` 的 `disk_read` / `disk_write`），
  FAT 完全由電腦管。兩邊同時改 FAT 卡就會壞。
- **USB 只有 MSC**（TinyUSB）。S2 會加 CDC 序列埠，讓電腦端送「完成」後自動回選單。
- **不掃鍵盤**，但會把 595 的 LATCH（GPIO 14）停在高 —— 現行接線下鍵盤 CLOCK 與 LCD D/C 共用，
  LATCH 浮接的話鍵盤 LED 會亂閃。
- 速度受限於 USB 1.1 全速，實際約 0.8～1 MB/s。

## 編譯

需要 pico-sdk 與 arm-none-eabi-gcc（可直接用 arduino-pico 附的那一套），旁邊要有 `rp2040-retro-loader`
（取連結腳本與跳板）。

```bash
cmake -S . -B build_offset -G Ninja -DPICO_BOARD=pico -DLOADER_OFFSET_BUILD=ON \
      -DPICO_SDK_PATH=<pico-sdk> -DPICO_TOOLCHAIN_PATH=<arm-none-eabi-gcc 的 bin>
ninja -C build_offset
```

產出（`build_offset/`）：

| 檔案 | 用途 |
| :--- | :--- |
| `SDMOUNT.UF2` | 放 SD 卡根目錄給載入器用。前 16KB 是空的，**不能**單獨 USB 燒錄 |
| `SDMOUNT.RAW` | 選單封面，跟上面一起放 |
| `SDMOUNT_standalone.uf2` | 夾了跳板，可直接拖進 `RPI-RP2` 測試 |

> **uf2 轉換用預編譯的 picotool**：pico-sdk 預設會從原始碼編 picotool，這台開發機上編出來的
> 一執行就 segfault（rp2040-retro-editor 也一樣）。CMakeLists 會自動找 arduino-pico 附的
> `pqt-picotool`，也可以用 `-DPICOTOOL_EXE=<路徑>` 指定。

封面由 `tools/make_icon.py` 產生（需要 Pillow），改完重跑即可。

## 已知事項

- USB VID/PID 是開發測試碼 `0xCAFE:0x4003`，公開散布前要換正式的。
- 封面只有 11 色，比 [ICON-STYLE.md](https://github.com/pondahai/rp2040-retro-handheld/blob/main/docs/ICON-STYLE.md) 建議的少，之後可以加明暗。

## 授權

MIT。`vendor/drivers/` 的 FatFs 與 SD 驅動沿用 rp2040-retro-editor（源自 rp2040-ili9341-infones），各自保留原授權。

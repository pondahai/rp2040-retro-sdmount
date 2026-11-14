# rp2040-retro-sdmount - 把掌機的 SD 卡變成 USB 隨身碟
#
# 骨架抄自 rp2040-retro-editor(載入器 app 範本)。
#
#   偏移編譯 (給載入器用，放 SD 卡) —— 這是正式成品：
#       cmake -S . -B build_offset -G Ninja -DLOADER_OFFSET_BUILD=ON
#       ninja -C build_offset
#
#   一般編譯 (USB BOOTSEL 直燒，測試用)：
#       cmake -S . -B build -G Ninja && ninja -C build
#
# uf2 轉換：pico-sdk 預設會從 git 抓 picotool 原始碼來編，這台機器上編出來的
# picotool 一跑就 segfault(rp2040-retro-editor 也一樣)。所以優先用
# arduino-pico 附的預編譯 picotool，用 -DPICOTOOL_EXE=<路徑> 指定；
# 找不到才退回 pico-sdk 的預設流程。
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(retro_sdmount C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

# 只用到 sdcard.c 的磁區 I/O(disk_read/disk_write)，不用 FatFs 本體；
# sdcard 這個 INTERFACE library 會順手把 fatfs 拉進來，沒用到的部分 --gc-sections 會丟掉。
include(vendor/drivers/fatfs/fatfs.cmake)
include(vendor/drivers/sdcard/sdcard.cmake)

add_executable(retro_sdmount
    src/main.c
    src/msc_disk.c
    src/usb_descriptors.c
    vendor/lcd.c
)

# 載入器 repo:取 common/boot_map.h(RETRO_MSG_* 回選單訊息)、連結腳本與跳板。
# 兩種編譯都需要 —— 回選單訊息的格式只定義在載入器那邊,不在這裡複製一份。
set(LOADER_PATH "${CMAKE_CURRENT_LIST_DIR}/../rp2040-retro-loader"
    CACHE PATH "rp2040-retro-loader 的路徑")
if(NOT EXISTS "${LOADER_PATH}/common/boot_map.h")
    message(FATAL_ERROR "找不到 ${LOADER_PATH}/common/boot_map.h，請用 -DLOADER_PATH=<路徑> 指定。")
endif()

target_include_directories(retro_sdmount PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src        # tusb_config.h 要在這裡被找到
    ${CMAKE_CURRENT_LIST_DIR}/vendor
    ${LOADER_PATH}/common
)

target_link_libraries(retro_sdmount
    pico_stdlib
    pico_unique_id
    hardware_spi
    hardware_watchdog
    tinyusb_device
    sdcard
)

target_compile_options(retro_sdmount PRIVATE -Wall -Wextra)

# USB 歸 TinyUSB MSC 用，不能再讓 stdio 搶。
pico_enable_stdio_usb(retro_sdmount 0)
pico_enable_stdio_uart(retro_sdmount 0)

# 鍵盤接線開關(只用到 LATCH 腳，但 board.h 需要這兩個值)。預設現行接線。
option(KBD_SHARED_DATA   "165 QH shares GPIO 15 with 595 SER"     ON)
option(KBD_CLK_ON_LCD_DC "595/165 CLOCK shares GPIO 20 with LCD DC" ON)
target_compile_definitions(retro_sdmount PRIVATE
    KBD_SHARED_DATA=$<BOOL:${KBD_SHARED_DATA}>
    KBD_CLK_ON_LCD_DC=$<BOOL:${KBD_CLK_ON_LCD_DC}>
)

# ---------------------------------------------------------------------------
# uf2 產生
# ---------------------------------------------------------------------------
file(GLOB _picotool_guess
    "$ENV{LOCALAPPDATA}/Arduino15/packages/rp2040/tools/pqt-picotool/*/picotool.exe")
list(SORT _picotool_guess)
list(REVERSE _picotool_guess)
list(GET _picotool_guess 0 _picotool_default)
set(PICOTOOL_EXE "${_picotool_default}" CACHE FILEPATH "預編譯的 picotool")

if(PICOTOOL_EXE AND EXISTS "${PICOTOOL_EXE}")
    message(STATUS "uf2: 使用預編譯 picotool ${PICOTOOL_EXE}")
    pico_add_dis_output(retro_sdmount)
    pico_add_bin_output(retro_sdmount)
    add_custom_command(TARGET retro_sdmount POST_BUILD
        COMMAND "${PICOTOOL_EXE}" uf2 convert --quiet
                $<TARGET_FILE:retro_sdmount>
                $<TARGET_FILE_DIR:retro_sdmount>/retro_sdmount.uf2
                --family rp2040
        COMMENT "elf -> retro_sdmount.uf2"
        VERBATIM)
else()
    message(STATUS "uf2: 找不到預編譯 picotool，改用 pico-sdk 預設流程")
    pico_add_extra_outputs(retro_sdmount)
endif()

# ---------------------------------------------------------------------------
# 載入器支援(同 rp2040-retro-editor 的必修 1 + 2 + 3)
# ---------------------------------------------------------------------------
option(LOADER_OFFSET_BUILD "Link at 0x10004000 for use with rp2040-retro-loader" OFF)

if(LOADER_OFFSET_BUILD)
    set(LOADER_APP_LD "${LOADER_PATH}/app/memmap_app.ld")
    if(NOT EXISTS "${LOADER_APP_LD}")
        message(FATAL_ERROR "找不到 ${LOADER_APP_LD}，請用 -DLOADER_PATH=<路徑> 指定。")
    endif()
    pico_set_linker_script(retro_sdmount "${LOADER_APP_LD}")
    message("LOADER_OFFSET_BUILD : ON  (link 到 0x10004000)")

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # 選單用的檔名固定是 SDMOUNT.UF2(SD-MOUNT-PLAN.md M4)
    add_custom_command(TARGET retro_sdmount POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE_DIR:retro_sdmount>/retro_sdmount.uf2
                $<TARGET_FILE_DIR:retro_sdmount>/SDMOUNT.UF2
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_LIST_DIR}/assets/SDMOUNT.RAW
                $<TARGET_FILE_DIR:retro_sdmount>/SDMOUNT.RAW
        COMMENT "-> SDMOUNT.UF2 + SDMOUNT.RAW"
        VERBATIM)

    set(LOADER_TRAMPOLINE "${LOADER_PATH}/build/trampoline.uf2"
        CACHE FILEPATH "跳板的 uf2")
    if(EXISTS "${LOADER_TRAMPOLINE}")
        add_custom_command(TARGET retro_sdmount POST_BUILD
            COMMAND ${Python3_EXECUTABLE}
                    "${LOADER_PATH}/tools/merge_uf2.py"
                    "${LOADER_TRAMPOLINE}"
                    $<TARGET_FILE_DIR:retro_sdmount>/retro_sdmount.uf2
                    -o $<TARGET_FILE_DIR:retro_sdmount>/SDMOUNT_standalone.uf2
            COMMENT "合併跳板與本體 -> SDMOUNT_standalone.uf2"
            VERBATIM)
    else()
        message(WARNING "找不到跳板 ${LOADER_TRAMPOLINE}，略過 standalone 版。")
    endif()
else()
    message("LOADER_OFFSET_BUILD : OFF (link 到 0x10000000，可直接 USB 燒錄)")
endif()

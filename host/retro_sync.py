"""retro-sync —— 不拔 SD 卡，直接從電腦把檔案放進 RP2040 掌機。

    python retro_sync.py push RetroFoo.uf2 [更多檔案...]   # 全流程，一行搞定
    python retro_sync.py mount                             # 只掛載，印出磁碟位置
    python retro_sync.py finish                            # 退出磁碟並讓掌機回選單
    python retro_sync.py status                            # 看掌機現在在哪個狀態
    python retro_sync.py run DOOM                          # 叫停在選單的載入器啟動 SD 卡上的 DOOM.UF2
    python retro_sync.py push RetroFoo.uf2 --run           # push 完直接啟動它

push 的流程：
  1. 掌機停在載入器選單 -> 透過 USB HID 叫它啟動 SDMOUNT（rp2040-retro-loader §4.3）
     已經在 SDMOUNT 裡 -> 跳過
  2. 等 SDMOUNT 的序列埠出現、磁碟掛上，找出磁碟機代號
  3. 複製檔案。.uf2 旁邊若有同名的 .RAW 封面會一起複製
     （載入器找封面的規則是「uf2 檔名去掉 .uf2 再接 .RAW」）
  4. 退出磁碟，送 done -> 掌機回到選單，新的 APP 已經在裡面

需要：pip install pyserial hidapi
磁碟機代號自動偵測目前只支援 Windows；其他系統請用 --drive 指定掛載點。

設計見 rp2040-retro-sdk docs/SD-MOUNT-PLAN.md（S4）。
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

try:
    import hid
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("需要套件：pip install pyserial hidapi")

LOADER_VID, LOADER_PID = 0xCAFE, 0x4010     # 載入器選單（HID）
SDMOUNT_VID, SDMOUNT_PID = 0xCAFE, 0x4004   # SDMOUNT（MSC + CDC）
DISK_NAME = "RETRO*SD Mount*"               # SDMOUNT 的 SCSI INQUIRY：vendor "RETRO" product "SD Mount"


class SyncError(Exception):
    pass


def log(msg):
    print(msg, flush=True)


# ------------------------------------------------------------ 載入器（HID）

def loader_present():
    return bool(hid.enumerate(LOADER_VID, LOADER_PID))


LOADER_OUT_LEN = 32     # 協定 2 的 output report；協定 1（舊載入器）是 8


def _loader_xfer(body, report_len):
    dev = hid.device()
    dev.open(LOADER_VID, LOADER_PID)
    try:
        if dev.write(bytes(1) + body.ljust(report_len, bytes(1))) < 0:
            return b""
        return bytes(dev.read(8, timeout_ms=1500))
    finally:
        dev.close()


def loader_cmd(cmd, arg=b""):
    """送一個指令字元（加參數）給停在選單的載入器，回傳 4 byte 的回覆標籤。"""
    if len(arg) > LOADER_OUT_LEN - 2:
        raise SyncError(f"檔名太長（最多 {LOADER_OUT_LEN - 2} 字元）")
    body = cmd + arg
    data = _loader_xfer(body, LOADER_OUT_LEN)
    if not data:
        # 舊載入器（協定 1）的 report 只有 8 byte，收不到較長的 report
        if arg:
            raise SyncError("載入器沒有回覆（可能是不支援 run 的舊版載入器，請更新 rp2040-retro-loader）")
        data = _loader_xfer(body, 8)
    if not data:
        raise SyncError("載入器沒有回覆")
    return data[:4]


def loader_run(name):
    """叫載入器燒錄並執行 SD 卡上的 name（可省略 .uf2）。"""
    tag = loader_cmd(b"L", name.encode("ascii"))
    if tag == b"NONE":
        raise SyncError(f"SD 卡上沒有 {name}")
    if tag != b"OK  ":
        raise SyncError(f"載入器回了看不懂的東西：{tag!r}")
    log(f"載入器回應 OK，開始燒錄並執行 {name}。")


# ------------------------------------------------------------ SDMOUNT（CDC）

def sdmount_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == SDMOUNT_VID and p.pid == SDMOUNT_PID:
            return p.device
    return None


def sdmount_cmd(cmd):
    port = sdmount_port()
    if not port:
        raise SyncError("找不到 SDMOUNT 的序列埠")
    with serial.Serial(port, 115200, timeout=2) as s:
        s.reset_input_buffer()
        s.write((cmd + "\n").encode("ascii"))
        try:
            return s.readline().decode("ascii", "replace").strip()
        except serial.SerialException:
            return "(裝置已斷線)" if cmd == "done" else ""


def sdmount_status():
    """回傳 dict，例如 {'state': 'mounted', 'card': '1', ...}"""
    reply = sdmount_cmd("status")
    out = {}
    for part in reply.split():
        if "=" in part:
            k, v = part.split("=", 1)
            out[k] = v
    return out


def wait_for(cond, timeout, what):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            r = cond()
        except (SyncError, serial.SerialException, OSError):
            r = None
        if r:
            return r
        time.sleep(0.3)
    raise SyncError(f"等待{what}逾時（{timeout} 秒）")


# ------------------------------------------------------------ 磁碟機

def find_drive_windows():
    ps = (
        f"Get-Disk | Where-Object {{ $_.FriendlyName -like '{DISK_NAME}' }} | "
        "Get-Partition | Get-Volume | Where-Object { $_.DriveLetter } | "
        "ForEach-Object { $_.DriveLetter }"
    )
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                       capture_output=True, text=True)
    letters = r.stdout.split()
    if len(letters) > 1:
        raise SyncError(f"找到不只一台掌機的磁碟：{letters}，請用 --drive 指定")
    return f"{letters[0]}:\\" if letters else None


def eject_windows(drive):
    letter = drive[0]
    # 檔案總管的「退出」：會送 SCSI START STOP UNIT，SDMOUNT 收到就進入 EJECTED
    ps = ("(New-Object -ComObject Shell.Application).Namespace(17)"
          f".ParseName('{letter}:').InvokeVerb('Eject')")
    subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                   capture_output=True, text=True)


# ------------------------------------------------------------ 流程

def ensure_mounted(args):
    """讓掌機進入 SDMOUNT 並掛上磁碟，回傳掛載點。"""
    if sdmount_port():
        log("掌機已經在 SDMOUNT。")
    elif loader_present():
        log("掌機在載入器選單，叫它啟動 SDMOUNT ...")
        tag = loader_cmd(b"M")
        if tag == b"NONE":
            raise SyncError("SD 卡上沒有 SDMOUNT.UF2。第一次請用讀卡機把 SDMOUNT.UF2 與 SDMOUNT.RAW 放進根目錄。")
        if tag != b"OK  ":
            raise SyncError(f"載入器回了看不懂的東西：{tag!r}")
        log("  載入器回應 OK，等 SDMOUNT 啟動（第一次要燒錄，會久一點）...")
        wait_for(sdmount_port, 60, " SDMOUNT 的序列埠")
    else:
        raise SyncError("找不到掌機。請確認 USB 已連接，而且掌機停在載入器選單"
                        "（正在跑其他 APP 時請先按 RESET）。")

    st = wait_for(lambda: (s := sdmount_status()) and s.get("state") in ("mounted", "ejected", "no-card") and s,
                  20, "掌機回報狀態")
    if st.get("card") != "1":
        raise SyncError("掌機回報沒有插 SD 卡。")
    if st.get("state") == "ejected":
        raise SyncError("磁碟已被退出。請在掌機按 START 回選單後重來。")

    if args.drive:
        drive = args.drive
    elif os.name == "nt":
        drive = wait_for(find_drive_windows, 20, "磁碟機代號")
    else:
        raise SyncError("非 Windows 系統請用 --drive 指定掛載點，例如 --drive /media/$USER/SDCARD")

    wait_for(lambda: os.path.isdir(drive), 10, "磁碟可讀")
    log(f"SD 卡已掛載在 {drive}")
    return drive


def finish(drive):
    """退出磁碟並讓掌機回選單。"""
    if drive and os.name == "nt":
        log(f"退出 {drive} ...")
        eject_windows(drive)
    else:
        log("請先在系統上退出磁碟 ...")

    try:
        wait_for(lambda: sdmount_status().get("state") in ("ejected", "unplugged"), 30, "磁碟退出")
    except SyncError:
        raise SyncError("磁碟沒有退出（可能有程式還開著上面的檔案）。請手動退出後執行：retro_sync.py finish")

    reply = sdmount_cmd("done")
    if reply.startswith("error"):
        raise SyncError(f"掌機拒絕回選單：{reply}")
    log("完成，掌機回到選單。")


def plan_copies(files):
    """展開成 (來源, 目的檔名) 清單；.uf2 自動帶上同名 .RAW。"""
    out = []
    for f in files:
        if not os.path.isfile(f):
            raise SyncError(f"找不到檔案：{f}")
        out.append(f)
        base, ext = os.path.splitext(f)
        if ext.lower() == ".uf2":
            for raw in (base + ".RAW", base + ".raw"):
                if os.path.isfile(raw) and raw not in files and raw not in out:
                    out.append(raw)
                    break
            else:
                log(f"  注意：{os.path.basename(f)} 旁邊沒有同名的 .RAW 封面，選單裡會沒有圖。")
    return out


def cmd_push(args):
    copies = plan_copies(args.files)
    drive = ensure_mounted(args)
    for src in copies:
        dst = os.path.join(drive, os.path.basename(src))
        log(f"  複製 {os.path.basename(src)}  ({os.path.getsize(src) // 1024} KB)")
        shutil.copyfile(src, dst)
        with open(dst, "rb+") as fh:        # 確保寫進裝置，不是只在快取裡
            os.fsync(fh.fileno())
    finish(drive)
    if args.run:
        uf2s = [f for f in args.files if f.lower().endswith(".uf2")]
        if not uf2s:
            raise SyncError("--run 需要至少一個 .uf2")
        wait_for(loader_present, 30, "掌機回到載入器選單")
        time.sleep(0.5)     # 等選單畫完、USB 列舉完成
        loader_run(os.path.basename(uf2s[0]))


def cmd_run(args):
    if not loader_present():
        raise SyncError("找不到載入器。掌機必須停在載入器選單"
                        "（正在跑其他 APP 時請先按 RESET）。")
    loader_run(args.name)


def cmd_mount(args):
    drive = ensure_mounted(args)
    print(drive)


def cmd_finish(args):
    drive = args.drive
    if not drive and os.name == "nt" and sdmount_port():
        drive = find_drive_windows()
    finish(drive)


def cmd_status(args):
    if sdmount_port():
        st = sdmount_status()
        log(f"SDMOUNT：{st.get('state')}，SD 卡 {st.get('mb', '?')} MB，"
            f"讀 {st.get('r')} KB / 寫 {st.get('w')} KB，錯誤 {st.get('err')}")
    elif loader_present():
        tag = loader_cmd(b"P")
        log("載入器選單" if tag == b"PRTL" else f"載入器（回應 {tag!r}）")
    else:
        log("找不到掌機（沒接 USB，或正在跑其他 APP）")


def main():
    ap = argparse.ArgumentParser(prog="retro_sync.py",
                                 description="不拔 SD 卡，從電腦把檔案放進 RP2040 掌機")
    ap.add_argument("--drive", help="SD 卡的掛載點（非 Windows 必填，例如 /media/me/SDCARD）")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("push", help="掛載、複製、退出、回選單")
    p.add_argument("files", nargs="+")
    p.add_argument("--run", action="store_true", help="完成後直接啟動第一個 .uf2")
    p.set_defaults(fn=cmd_push)
    p = sub.add_parser("run", help="叫載入器啟動 SD 卡上的某個 APP")
    p.add_argument("name", help="SD 卡上的檔名，例如 DOOM.UF2 或 DOOM")
    p.set_defaults(fn=cmd_run)
    sub.add_parser("mount", help="只掛載，印出磁碟位置").set_defaults(fn=cmd_mount)
    sub.add_parser("finish", help="退出磁碟並讓掌機回選單").set_defaults(fn=cmd_finish)
    sub.add_parser("status", help="顯示掌機目前狀態").set_defaults(fn=cmd_status)
    args = ap.parse_args()
    try:
        args.fn(args)
    except SyncError as e:
        sys.exit(f"錯誤：{e}")


if __name__ == "__main__":
    main()

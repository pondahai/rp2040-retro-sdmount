"""跟 rp2040-retro-sdmount 的 CDC 序列埠說話(測試用的小工具)。

    python tools/sdmount_cli.py ping
    python tools/sdmount_cli.py status
    python tools/sdmount_cli.py done          # 先在電腦上退出磁碟,再送這個

會依 USB VID:PID(CAFE:4004)自動找 COM 埠;找不到時用 --port COM7 指定。
需要 pyserial:pip install pyserial

這只是 S2 的驗證工具。正式的電腦端工具 retro-sync 是 S4。
"""
import argparse
import sys

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("需要 pyserial:pip install pyserial")

VID, PID = 0xCAFE, 0x4004


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == VID and p.pid == PID:
            return p.device
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("command", choices=["ping", "status", "done"])
    ap.add_argument("--port", help="COM 埠,例如 COM7(預設自動尋找)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("找不到掌機(USB %04X:%04X)。SDMOUNT 有在跑嗎?" % (VID, PID))

    with serial.Serial(port, 115200, timeout=2) as s:
        s.reset_input_buffer()
        s.write((args.command + "\n").encode("ascii"))
        try:
            reply = s.readline().decode("ascii", "replace").strip()
        except serial.SerialException:
            # done 成功時裝置會馬上斷線重開,有時來不及讀到回覆
            reply = "(裝置已斷線)" if args.command == "done" else ""

    print(f"{port}: {reply or '(沒有回覆)'}")
    if reply.startswith("error"):
        sys.exit(1)


if __name__ == "__main__":
    main()

"""產生 SDMOUNT 的 96x96 封面:一張 SD 卡插著 USB 線(pixel art)。

依 rp2040-retro-handheld docs/ICON-STYLE.md:具體物件、淺色背景、填滿畫面、
硬邊明暗(受光左上)、深褐描邊、一個暖色重點。
輸出 assets/icon.png(檢視用)與 assets/SDMOUNT.RAW(RGB565 big-endian)。
"""
import os, sys
from PIL import Image, ImageDraw

W = H = 96
OUT = os.path.join(os.path.dirname(__file__), "..", "assets")

BG      = (246, 244, 238)
OUTLINE = (58, 48, 44)
SHELL_L = (220, 212, 196)   # 機殼亮面
SHELL   = (196, 186, 166)   # #C4BAA6
SHELL_D = (150, 140, 122)
LABEL_L = (232, 120, 72)    # 暖色重點(標籤)
LABEL   = (204, 84, 48)
LABEL_D = (150, 56, 32)
GOLD_L  = (240, 214, 120)
GOLD    = (206, 170, 72)
GOLD_D  = (150, 118, 44)
CABLE_L = (120, 120, 128)
CABLE   = (82, 82, 90)
CABLE_D = (52, 52, 58)
METAL_L = (226, 228, 232)
METAL   = (178, 182, 190)
METAL_D = (120, 124, 132)

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)

def box(x0, y0, x1, y1, fill, light, dark):
    d.rectangle([x0, y0, x1, y1], fill=fill)
    d.line([x0, y0, x1, y0], fill=light)          # 上緣亮
    d.line([x0, y0, x0, y1], fill=light)          # 左緣亮
    d.line([x0, y1, x1, y1], fill=dark)           # 下緣暗
    d.line([x1, y0, x1, y1], fill=dark)           # 右緣暗

# ---- SD 卡本體(左側,斜角在右上) ----
cx0, cy0, cx1, cy1 = 4, 6, 58, 90
cut = 12
card = [(cx0, cy0), (cx1 - cut, cy0), (cx1, cy0 + cut), (cx1, cy1), (cx0, cy1)]
d.polygon(card, fill=SHELL, outline=OUTLINE)
# 明暗:左/上亮,右/下暗(硬邊兩階)
d.line([cx0 + 1, cy0 + 1, cx1 - cut - 1, cy0 + 1], fill=SHELL_L, width=2)
d.line([cx0 + 1, cy0 + 1, cx0 + 1, cy1 - 1], fill=SHELL_L, width=2)
d.line([cx1 - 1, cy0 + cut + 1, cx1 - 1, cy1 - 1], fill=SHELL_D, width=2)
d.line([cx0 + 1, cy1 - 1, cx1 - 1, cy1 - 1], fill=SHELL_D, width=2)
d.line([cx1 - cut, cy0 + 1, cx1 - 1, cy0 + cut], fill=SHELL_D)
d.polygon(card, outline=OUTLINE)

# 金手指(頂端)
for i in range(5):
    x = cx0 + 6 + i * 8
    box(x, cy0 + 4, x + 5, cy0 + 16, GOLD, GOLD_L, GOLD_D)
    d.rectangle([x, cy0 + 4, x + 5, cy0 + 16], outline=OUTLINE)

# 標籤(暖色重點)
box(cx0 + 6, 34, cx1 - 6, 72, LABEL, LABEL_L, LABEL_D)
d.rectangle([cx0 + 6, 34, cx1 - 6, 72], outline=OUTLINE)
# 標籤上的 "SD" 字樣(像素字)
S = ["0111", "1000", "0110", "0001", "1110"]
D_ = ["1110", "1001", "1001", "1001", "1110"]
def glyph(g, ox, oy, sc=3):
    for r, row in enumerate(g):
        for c, v in enumerate(row):
            if v == "1":
                d.rectangle([ox + c * sc, oy + r * sc, ox + c * sc + sc - 1, oy + r * sc + sc - 1],
                            fill=(250, 238, 222))
glyph(S, 16, 46)
glyph(D_, 32, 46)
# 寫保護滑塊(左側)
box(cx0 - 2, 50, cx0 + 2, 60, METAL, METAL_L, METAL_D)
d.rectangle([cx0 - 2, 50, cx0 + 2, 60], outline=OUTLINE)

# ---- USB 線:從卡右側彎出到右下,接 USB-A 插頭 ----
# 線身
pts = [(58, 78), (68, 78), (74, 72), (74, 40)]
for w, col in ((7, OUTLINE), (5, CABLE), (2, CABLE_L)):
    d.line(pts, fill=col, width=w, joint="curve")
# 插頭外殼(朝上,右上角)
box(66, 26, 82, 40, CABLE, CABLE_L, CABLE_D)
d.rectangle([66, 26, 82, 40], outline=OUTLINE)
# 金屬頭
box(64, 4, 84, 26, METAL, METAL_L, METAL_D)
d.rectangle([64, 4, 84, 26], outline=OUTLINE)
d.rectangle([68, 9, 72, 12], fill=OUTLINE)
d.rectangle([76, 9, 80, 12], fill=OUTLINE)
# 傳輸箭頭(上下各一,暗示雙向)
d.polygon([(90, 52), (86, 58), (94, 58)], fill=LABEL, outline=OUTLINE)
d.polygon([(90, 72), (86, 66), (94, 66)], fill=LABEL, outline=OUTLINE)
d.rectangle([89, 58, 91, 66], fill=OUTLINE)

os.makedirs(OUT, exist_ok=True)
img.save(os.path.join(OUT, "icon.png"))
raw = bytearray()
for r, g, b in img.getdata():
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    raw += bytes((v >> 8, v & 0xFF))
assert len(raw) == W * H * 2
open(os.path.join(OUT, "SDMOUNT.RAW"), "wb").write(raw)
print("colors:", len(set(img.getdata())), "bytes:", len(raw))

#!/usr/bin/env python3
"""Rendert die vom Host-Mock aufgezeichneten Zeichenbefehle (/tmp/ui_*.txt) nach PNG (320x240, 3x vergroessert).
Naeherung: Fonts sind DejaVu statt TFT_eSPI-Fonts, Kreise/Linien ohne Antialiasing - zeigt Layout/Farben, nicht pixelgenau."""
import sys
from PIL import Image, ImageDraw, ImageFont
S = 3
def rgb(c): return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
FD = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FM = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
fonts = {1: ImageFont.truetype(FM, 8 * S), 2: ImageFont.truetype(FD, 12 * S), 4: ImageFont.truetype(FD, 19 * S)}
def render(src, dst):
    im = Image.new("RGB", (320 * S, 240 * S)); d = ImageDraw.Draw(im)
    col = (255, 255, 255); datum = 0
    for ln in open(src):
        p = ln.rstrip("\n").split(" ", 1); cmd = p[0]; a = p[1] if len(p) > 1 else ""
        if cmd == "text":
            x, y, font, s = a.split(" ", 3); x, y, font = int(x), int(y), int(font)
            f = fonts[font]
            anchor = "mm" if datum == 4 else "la"
            d.text((x * S, y * S), s, font=f, fill=col, anchor=anchor)
            continue
        v = [int(t) for t in a.split()] if a else []
        if cmd == "color": col = rgb(v[0])
        elif cmd == "datum": datum = v[0]
        elif cmd == "fillRect": d.rectangle([v[0]*S, v[1]*S, (v[0]+v[2])*S-1, (v[1]+v[3])*S-1], fill=rgb(v[4]))
        elif cmd == "fillRoundRect": d.rounded_rectangle([v[0]*S, v[1]*S, (v[0]+v[2])*S-1, (v[1]+v[3])*S-1], v[4]*S, fill=rgb(v[5]))
        elif cmd == "drawRoundRect": d.rounded_rectangle([v[0]*S, v[1]*S, (v[0]+v[2])*S-1, (v[1]+v[3])*S-1], v[4]*S, outline=rgb(v[5]), width=S)
        elif cmd == "fillCircle": d.ellipse([(v[0]-v[2])*S, (v[1]-v[2])*S, (v[0]+v[2])*S+S-1, (v[1]+v[2])*S+S-1], fill=rgb(v[3]))
        elif cmd == "drawCircle": d.ellipse([(v[0]-v[2])*S, (v[1]-v[2])*S, (v[0]+v[2])*S+S-1, (v[1]+v[2])*S+S-1], outline=rgb(v[3]), width=S)
        elif cmd == "drawLine": d.line([v[0]*S+S//2, v[1]*S+S//2, v[2]*S+S//2, v[3]*S+S//2], fill=rgb(v[4]), width=S)
    im.save(dst)
if __name__ == "__main__":
    for a in sys.argv[1:]:
        out = a.replace(".txt", ".png"); render(a, out); print(out)

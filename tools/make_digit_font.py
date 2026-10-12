"""Vygeneruje src/font_<jmeno>.h: bitmapy cislic 0-9 ve dvou velikostech.

Pouziti: python tools/make_digit_font.py klingon cesta/Klingon-pIqaD-HaSta.ttf
         python tools/make_digit_font.py japan cesta/NotoSansCJKjp-Bold.otf

Klingon pIqaD HaSta (Mike Neff, Michael Everson): https://www.evertype.com/fonts/tlh/
Noto Sans CJK JP (Adobe, Google): https://github.com/notofonts/noto-cjk
Oba fonty maji licenci SIL OFL 1.1.
"""
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

# Velikosti se musi vejit do mist pro cislice v ui.cpp (50x96 a 26x48). Sirka musi byt nasobek 8:
# rychle kresleni bitmap v Arduino_GFX nezarovnava radky na bajty a jinak by se obraz zkosil.
FONTS = {
    "klingon": {
        "chars": [chr(0xF8F0 + d) for d in range(10)],  # pIqaD v soukrome oblasti Unicode
        "sizes": {"BIG": (48, 72), "SMALL": (24, 36)},
        "credit": "Klingon pIqaD HaSta (c) Mike Neff, Michael Everson",
    },
    "japan": {
        "chars": list("〇一二三四五六七八九"),
        "sizes": {"BIG": (48, 64), "SMALL": (24, 32)},
        "credit": "Noto Sans CJK JP (c) Adobe, Google",
    },
}
RENDER_PT = 400
THRESHOLD = 96

name = sys.argv[1]
spec = FONTS[name]
assert all(w % 8 == 0 for w, _ in spec["sizes"].values())
font = ImageFont.truetype(sys.argv[2], RENDER_PT)
chars = spec["chars"]

# spolecny ramecek vsech cislic, aby vuci sobe drzely vysku i ucari
boxes = [font.getbbox(c) for c in chars]
left = min(b[0] for b in boxes)
top = min(b[1] for b in boxes)
right = max(b[2] for b in boxes)
bottom = max(b[3] for b in boxes)


def glyph(ch, size):
    img = Image.new("L", (right - left, bottom - top), 0)
    ImageDraw.Draw(img).text((-left, -top), ch, font=font, fill=255)
    return img.resize(size, Image.LANCZOS).point(lambda v: 255 if v >= THRESHOLD else 0)


def pack(img):
    w, h = img.size
    px = img.load()
    out = []
    for y in range(h):
        for x0 in range(0, w, 8):
            byte = 0
            for bit in range(8):
                if px[x0 + bit, y]:
                    byte |= 0x80 >> bit
            out.append(byte)
    return out


prefix = name.upper()
lines = [
    "// Vygenerovano skriptem tools/make_digit_font.py, needitovat rucne.",
    f"// Cislice z fontu {spec['credit']}, licence SIL OFL 1.1.",
    "#pragma once",
    "#include <Arduino.h>",
    "",
]
for size_name, size in spec["sizes"].items():
    lines.append(f"constexpr int16_t {prefix}_{size_name}_W = {size[0]};")
    lines.append(f"constexpr int16_t {prefix}_{size_name}_H = {size[1]};")
    lines.append(f"const uint8_t {prefix}_{size_name}[10][{size[0] // 8 * size[1]}] PROGMEM = {{")
    for d, ch in enumerate(chars):
        data = pack(glyph(ch, size))
        lines.append(f"    {{  // {d}")
        for i in range(0, len(data), 16):
            lines.append("        " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
        lines.append("    },")
    lines.append("};")
    lines.append("")

out = pathlib.Path(__file__).resolve().parent.parent / "src" / f"font_{name}.h"
out.write_text("\n".join(lines), encoding="ascii", newline="\n")
print("zapsano", out)

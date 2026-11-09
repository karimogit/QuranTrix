#!/usr/bin/env python3
"""Render 400x300 QuranTrix dashboard PNGs (Classic theme)."""
from PIL import Image, ImageDraw, ImageFont

W, H = 400, 300
BLACK = (27, 20, 15)
PAPER = (243, 239, 228)
GOLD = (226, 179, 64)
RED = (212, 83, 58)


def load_fonts():
    paths = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
    ]
    bold = reg = None
    for p in paths:
        try:
            bold = ImageFont.truetype(p, 12)
            reg = ImageFont.truetype(p, 11)
            big = ImageFont.truetype(p, 22)
            sm = ImageFont.truetype(p, 10)
            tiny = ImageFont.truetype(p, 9)
            return bold, reg, big, sm, tiny
        except OSError:
            continue
    d = ImageFont.load_default()
    return d, d, d, d, d


def frame(draw, x, y, w, h, color, t=2):
    for i in range(t):
        draw.rectangle([x + i, y + i, x + w - 1 - i, y + h - 1 - i], outline=color)


def clip_text(draw, xy, text, font, fill, mw):
    x, y = xy
    s = text
    while s and draw.textlength(s, font=font) > mw:
        s = s[:-1]
    if len(s) < len(text) and s:
        s = s[:-1] + "…"
    draw.text((x, y), s, font=font, fill=fill)


def strike(draw, x1, x2, y, color):
    draw.line([(x1, y), (x2, y)], fill=color, width=1)


def wifi(draw, x, y, color):
    for i, h in enumerate([4, 7, 10, 13]):
        draw.rectangle([x + i * 6, y + 14 - h, x + i * 6 + 3, y + 14], fill=color)


def salah_tile(draw, x, y, w, h, name, when, current, done, boxed, bold, reg):
    ink = BLACK
    if current:
        draw.rectangle([x, y, x + w, y + h], fill=RED)
        ink = PAPER
    elif boxed:
        frame(draw, x, y, w, h, RED, 2)
    draw.text((x + w / 2, y + 4), name, font=reg, fill=ink, anchor="ma")
    draw.text((x + w / 2, y + 20), when, font=bold, fill=ink, anchor="ma")
    if done:
        strike(draw, x + 4, x + w - 4, y + 12, ink)
        strike(draw, x + 4, x + w - 4, y + 28, ink)


def draw_board(spec):
    im = Image.new("RGB", (W, H), PAPER)
    draw = ImageDraw.Draw(im)
    bold, reg, big, sm, tiny = load_fonts()

    draw.rectangle([0, 0, W, 28], fill=BLACK)
    bx = 8
    for i, (txt, col) in enumerate(spec["bar"]):
        if txt == " - ":
            draw.text((bx, 7), txt, font=reg, fill=GOLD)
            bx += int(draw.textlength(txt, font=reg))
            continue
        draw.text((bx, 7), txt, font=bold, fill=col)
        bx += int(draw.textlength(txt, font=bold)) + 6
    if spec.get("wifi"):
        wifi(draw, bx, 6, GOLD)
        bx += 28
    draw.text((W - 8, 7), "80%", font=reg, fill=PAPER, anchor="ra")

    gap = 4
    tw = (W - 16 - 4 * gap) // 5
    names = ["FAJR", "DHUHR", "ASR", "MAGHRIB", "ISHA"]
    for i, (tm, st) in enumerate(spec["prayers"]):
        x = 8 + i * (tw + gap)
        salah_tile(draw, x, 32, tw, 58, names[i], tm, st == "now", st == "done", True, bold, tiny)

    y, h = 94, 44
    band, half = W - 16, (W - 16 - 4) // 2
    frame(draw, 8, y, half, h, RED, 2)
    frame(draw, 8 + half + 4, y, half, h, RED, 2)
    num = spec["moon_num"]
    draw.text((16, y + 2), num, font=big, fill=BLACK)
    nw = draw.textlength(num, font=big)
    draw.text((28 + nw, y + 8), spec["moon_line"], font=reg, fill=BLACK)
    draw.text((28 + nw, y + 26), spec["moon_hijri"], font=sm, fill=GOLD)
    sx = 8 + half + 4
    if spec.get("sun_stacked"):
        draw.text((sx + 8, y + 8), spec["sun_left"], font=reg, fill=BLACK)
        draw.text((sx + 8, y + 26), spec["sun_right"], font=reg, fill=BLACK)
    else:
        draw.text((sx + 8, y + 14), spec["sun_left"], font=reg, fill=BLACK)
        draw.text((sx + 8 + (half - 16) // 2, y + 14), spec["sun_right"], font=reg, fill=BLACK)

    hy, hh = 140, 32
    if spec["holy_fill"]:
        draw.rectangle([8, hy, W - 8, hy + hh], fill=spec["holy_fill"])
        ink = BLACK
    else:
        frame(draw, 8, hy, W - 16, hh, RED, 2)
        ink = BLACK
    draw.text((16, hy + 6), spec["holy_tag"], font=tiny, fill=ink)
    clip_text(draw, (90, hy + 2), spec["holy_name"], reg, ink, W - 180)
    draw.text((W - 16, hy + 8), spec["holy_when"], font=sm, fill=ink, anchor="ra")

    y, bh = 176, 44
    frame(draw, 8, y, half, bh, RED, 2)
    frame(draw, 8 + half + 4, y, half, bh, RED, 2)
    draw.text((16, y + 12), "DUA", font=tiny, fill=GOLD)
    draw.text((54, y + 4), spec["dua_w"], font=reg, fill=BLACK)
    draw.text((54, y + 22), spec["dua_m"], font=sm, fill=GOLD)
    ax = 16 + half + 4 + 8
    draw.text((ax, y + 12), "99", font=tiny, fill=GOLD)
    clip_text(draw, (ax + 38, y + 4), spec["asma_w"], reg, BLACK, half - 54)
    clip_text(draw, (ax + 38, y + 22), spec["asma_m"], sm, GOLD, half - 54)

    draw.rectangle([8, 224, W - 8, 262], fill=RED)
    draw.rectangle([11, 236, W - 11, 259], fill=PAPER)
    draw.text((16, 226), "RADIO", font=tiny, fill=GOLD)
    draw.text((16, 238), "Quran / ON", font=bold, fill=BLACK)

    return im


TODAY = {
    "bar": [
        ("Mon", GOLD),
        ("9 Nov", GOLD),
        ("13:40", GOLD),
        (" - ", GOLD),
        ("Your place", PAPER),
    ],
    "wifi": True,
    "prayers": [
        ("05:53", "done"),
        ("12:24", "now"),
        ("14:34", ""),
        ("16:57", ""),
        ("18:48", ""),
    ],
    "moon_num": "91",
    "moon_line": "days to Ramadan",
    "moon_hijri": "28 Jumada I 1448",
    "sun_left": "Sunrise 07:42",
    "sun_right": "Sunset 16:57",
    "sun_stacked": True,
    "holy_fill": None,
    "holy_tag": "NEXT",
    "holy_name": "Isra and Mi'raj",
    "holy_when": "6 Jan",
    "dua_w": "Hasbunallah",
    "dua_m": "Allah is sufficient for us",
    "asma_w": "Al-Wahhab",
    "asma_m": "The Bestower",
}

RAMADAN = {
    "bar": [
        ("Mon", GOLD),
        ("9 Nov", GOLD),
        ("13:40", GOLD),
        (" - ", GOLD),
        ("Your place", PAPER),
    ],
    "wifi": True,
    "prayers": [
        ("05:53", "done"),
        ("12:24", "now"),
        ("14:34", ""),
        ("16:57", ""),
        ("18:48", ""),
    ],
    "moon_num": "16",
    "moon_line": "days left",
    "moon_hijri": "15 Ramadan 1448",
    "sun_left": "Suhoor 05:53",
    "sun_right": "Iftar 16:57",
    "sun_stacked": True,
    "holy_fill": GOLD,
    "holy_tag": "SOON",
    "holy_name": "Laylat al-Qadr",
    "holy_when": "6 Mar",
    "dua_w": "Min at-tawwabin",
    "dua_m": "Among the repentant",
    "asma_w": "Al-Qawi",
    "asma_m": "The Strong",
}

if __name__ == "__main__":
    import os

    out = os.path.join(os.path.dirname(__file__), "preview")
    os.makedirs(out, exist_ok=True)
    for name, spec in [("dashboard-today", TODAY), ("dashboard-ramadan", RAMADAN)]:
        im = draw_board(spec)
        im2 = im.resize((800, 600), Image.NEAREST)
        im2.save(os.path.join(out, f"{name}.png"))
    print(out)

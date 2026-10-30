# SL Translator Pro HUD panel texture generator
# Produces 1024x512 PNGs (SL requires power-of-two sizes).
# Button layout must match BUTTONS in hud_sltranslate.lsl (ST coordinates, origin bottom-left).
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import os

W, H = 1024, 512
OUT = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = "/mnt/c/Windows/Fonts"
F_BOLD = os.path.join(FONT_DIR, "segoeuib.ttf")
F_REG = os.path.join(FONT_DIR, "segoeui.ttf")
F_SYM = os.path.join(FONT_DIR, "seguisym.ttf")

BG_TOP = (18, 28, 48)
BG_BOT = (8, 12, 24)
ACCENT = (64, 156, 255)
TEXT = (235, 242, 255)
SUB = (150, 170, 200)

# Buttons: (id, label, icon, x0, y0, x1, y1) in pixel coords, origin top-left
BUTTONS = [
    ("power",  "AÇ / KAPAT",   "⏻", 40, 170, 330, 330),
    ("swap",   "DEĞİŞTİR",     "⇄", 367, 170, 657, 330),
    ("src",    "ORİJİNAL",     "❝", 694, 170, 984, 330),
    ("my",     "BENİM DİLİM",  "◉", 40, 360, 262, 472),
    ("target", "HEDEF DİL",    "➤", 282, 360, 504, 472),
    ("auto",   "OTOMATİK",     "⟳", 524, 360, 746, 472),
    ("status", "DURUM",        "ℹ", 766, 360, 984, 472),
]


def font(path, size):
    return ImageFont.truetype(path, size)


def gradient(img, top, bot):
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / (H - 1)
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        d.line([(0, y), (W, y)], fill=c + (255,))


def glow_rect(base, box, color, radius, blur):
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).rounded_rectangle(box, radius=radius, fill=color + (150,))
    layer = layer.filter(ImageFilter.GaussianBlur(blur))
    base.alpha_composite(layer)


def center_text(d, cx, cy, text, f, fill):
    l, t, r, b = d.textbbox((0, 0), text, font=f)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), text, font=f, fill=fill)


def render(on: bool):
    img = Image.new("RGBA", (W, H))
    gradient(img, BG_TOP, BG_BOT)
    d = ImageDraw.Draw(img)

    state_color = (60, 220, 120) if on else (235, 70, 70)

    # outer frame
    d.rounded_rectangle((6, 6, W - 7, H - 7), radius=36, outline=ACCENT + (255,), width=4)

    # header
    d.rounded_rectangle((40, 30, 984, 140), radius=24, fill=(30, 44, 70, 255))
    # globe logo
    cx, cy, r = 105, 85, 38
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=ACCENT + (255,), width=5)
    d.ellipse((cx - r / 2.3, cy - r, cx + r / 2.3, cy + r), outline=ACCENT + (255,), width=4)
    d.line((cx - r, cy, cx + r, cy), fill=ACCENT + (255,), width=4)
    d.text((165, 38), "SL Translator Pro", font=font(F_BOLD, 50), fill=TEXT + (255,))
    d.text((168, 98), "Second Life canlı çeviri", font=font(F_REG, 26), fill=SUB + (255,))

    # state pill
    pill = (760, 58, 960, 112)
    glow_rect(img, pill, state_color, 27, 14)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(pill, radius=27, fill=state_color + (255,))
    center_text(d, (pill[0] + pill[2]) / 2, (pill[1] + pill[3]) / 2,
                "AÇIK" if on else "KAPALI", font(F_BOLD, 32), (10, 16, 28, 255))

    # buttons
    for bid, label, icon, x0, y0, x1, y1 in BUTTONS:
        big = (y1 - y0) > 130
        hi = bid == "power"
        dark = (20, 32, 54)
        tint = tuple(int(dark[i] * 0.75 + state_color[i] * 0.25) for i in range(3))
        fill = (tint if hi else (40, 60, 95)) + (255,)
        outline = state_color if hi else ACCENT
        if hi:
            glow_rect(img, (x0, y0, x1, y1), state_color, 26, 10)
            d = ImageDraw.Draw(img)
        d.rounded_rectangle((x0, y0, x1, y1), radius=26, fill=fill, outline=outline + (255,), width=3)
        cx = (x0 + x1) / 2
        if big:
            center_text(d, cx, y0 + 62, icon, font(F_SYM, 64), (outline if hi else TEXT) + (255,))
            center_text(d, cx, y1 - 36, label, font(F_BOLD, 30), TEXT + (255,))
        else:
            center_text(d, cx, y0 + 38, icon, font(F_SYM, 40), TEXT + (255,))
            center_text(d, cx, y1 - 28, label, font(F_BOLD, 24), TEXT + (255,))

    d.text((W - 230, H - 34), "sltranslate.com", font=font(F_REG, 20), fill=SUB + (200,))
    return img


if __name__ == "__main__":
    for on in (True, False):
        name = "hud_panel_on.png" if on else "hud_panel_off.png"
        render(on).save(os.path.join(OUT, name))
        print("wrote", name)
    # print ST rectangles for the LSL script (origin bottom-left, 0..1)
    for bid, _, _, x0, y0, x1, y1 in BUTTONS:
        print(f'"{bid}", {x0/W:.3f}, {1-y1/H:.3f}, {x1/W:.3f}, {1-y0/H:.3f},')

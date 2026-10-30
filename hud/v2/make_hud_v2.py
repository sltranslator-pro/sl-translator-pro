# SL Translator Pro HUD v2 texture generator.
# Produces two 1024x1024 textures (one upload each in Second Life):
#   slt_hud_panel.png : main bar, 4 state variants stacked (1024x256 each)
#                       row 0 = ON / dual off, 1 = ON / dual on,
#                       row 2 = OFF / dual off, 3 = OFF / dual on
#   slt_hud_langs.png : language atlas, 4 columns x 8 rows (256x128 cells)
# The layout constants here must match slt_hud.lsl.
# Run: uv run --with pillow python make_hud_v2.py
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import os

OUT = os.path.dirname(os.path.abspath(__file__))
FD = "/mnt/c/Windows/Fonts"
RAQM = ImageFont.Layout.RAQM

# Same order as CODES in slt_hud.lsl (English-name alphabetical).
LANGS = [
    ("ar", "Arabic", "العربية"), ("bg", "Bulgarian", "Български"),
    ("zh", "Chinese", "中文"), ("hr", "Croatian", "Hrvatski"),
    ("cs", "Czech", "Čeština"), ("da", "Danish", "Dansk"),
    ("nl", "Dutch", "Nederlands"), ("en", "English", "English"),
    ("fi", "Finnish", "Suomi"), ("fr", "French", "Français"),
    ("de", "German", "Deutsch"), ("el", "Greek", "Ελληνικά"),
    ("he", "Hebrew", "עברית"), ("hi", "Hindi", "हिन्दी"),
    ("hu", "Hungarian", "Magyar"), ("id", "Indonesian", "Indonesia"),
    ("it", "Italian", "Italiano"), ("ja", "Japanese", "日本語"),
    ("ko", "Korean", "한국어"), ("ms", "Malay", "Melayu"),
    ("no", "Norwegian", "Norsk"), ("fa", "Persian", "فارسی"),
    ("pl", "Polish", "Polski"), ("pt", "Portuguese", "Português"),
    ("ro", "Romanian", "Română"), ("ru", "Russian", "Русский"),
    ("es", "Spanish", "Español"), ("sv", "Swedish", "Svenska"),
    ("th", "Thai", "ไทย"), ("tr", "Turkish", "Türkçe"),
    ("uk", "Ukrainian", "Українська"), ("vi", "Vietnamese", "Tiếng Việt"),
]
assert len(LANGS) == 32

NATIVE_FONT = {
    "zh": ("msyhbd.ttc", 0), "ja": ("YuGothB.ttc", 0), "ko": ("malgunbd.ttf", 0),
    "hi": ("Nirmala.ttc", 0), "th": ("LeelaUIb.ttf", 0),
}
RTL = {"ar", "fa", "he"}

BG_TOP, BG_BOT = (17, 25, 44), (8, 12, 22)
EDGE = (52, 92, 150)
ACCENT = (64, 156, 255)
GREEN = (52, 211, 153)
RED = (239, 68, 68)
TEXT = (236, 242, 255)
MUTED = (138, 158, 194)
BTN = (26, 38, 64)
BTN_EDGE = (48, 74, 118)
WIN = (10, 16, 30)
WIN_EDGE = (44, 70, 112)


def font(name, size, index=0):
    return ImageFont.truetype(os.path.join(FD, name), size, index=index, layout_engine=RAQM)


def center_text(d, cx, cy, text, f, fill, **kw):
    l, t, r, b = d.textbbox((0, 0), text, font=f, **kw)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), text, font=f, fill=fill, **kw)


def fit_font(d, text, fname, index, max_w, start, stop, **kw):
    for size in range(start, stop - 1, -1):
        f = font(fname, size, index)
        l, t, r, b = d.textbbox((0, 0), text, font=f, **kw)
        if r - l <= max_w:
            return f
    return font(fname, stop, index)


def glow(img, box, color, radius, blur, alpha=140):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).rounded_rectangle(box, radius=radius, fill=color + (alpha,))
    img.alpha_composite(layer.filter(ImageFilter.GaussianBlur(blur)))


def mix(a, b, t):
    return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3))


# ---------------------------------------------------------------- language atlas
def draw_cell(d, x0, y0, code, english, native):
    d.rounded_rectangle((x0 + 6, y0 + 6, x0 + 250, y0 + 122), radius=16,
                        fill=WIN + (255,), outline=WIN_EDGE + (255,), width=2)
    fname, idx = NATIVE_FONT.get(code, ("segoeuib.ttf", 0))
    kw = {"direction": "rtl", "language": code} if code in RTL else {"language": code}
    f = fit_font(d, native, fname, idx, 222, 40, 18, **kw)
    center_text(d, x0 + 128, y0 + 54, native, f, TEXT + (255,), **kw)
    center_text(d, x0 + 128, y0 + 100, f"{english.upper()}  ·  {code.upper()}",
                font("segoeuib.ttf", 17), MUTED + (255,))


def render_langs():
    img = Image.new("RGBA", (1024, 1024), BG_BOT + (255,))
    d = ImageDraw.Draw(img)
    for i, (code, english, native) in enumerate(LANGS):
        col, row = i % 4, i // 4
        draw_cell(d, col * 256, row * 128, code, english, native)
    return img


# ---------------------------------------------------------------- main bar
# x ranges (pixels of the 1024 wide bar); the LSL touch map uses the same.
POWER = (14, 150)
MY = (162, 440)
SWAP = (450, 506)
SEND = (516, 794)
DUAL = (806, 906)
STATUS = (916, 1010)
WIN_MY = (174, 70, 374, 170)
WIN_SEND = (528, 70, 728, 170)


def button(d, x0, x1, y0, y1, fill=BTN, edge=BTN_EDGE, width=2):
    d.rounded_rectangle((x0, y0, x1, y1), radius=20, fill=fill + (255,), outline=edge + (255,), width=width)


def power_icon(d, cx, cy, r, color):
    d.arc((cx - r, cy - r, cx + r, cy + r), start=300, end=240, fill=color + (255,), width=7)
    d.line((cx, cy - r - 4, cx, cy - 6), fill=color + (255,), width=8)


def shield_icon(d, cx, cy, s, color):
    pts = [(cx, cy - s), (cx + s * 0.85, cy - s * 0.62), (cx + s * 0.75, cy + s * 0.35),
           (cx, cy + s), (cx - s * 0.75, cy + s * 0.35), (cx - s * 0.85, cy - s * 0.62)]
    d.polygon(pts, outline=color + (255,), width=5)
    d.line([(cx - s * 0.38, cy + s * 0.02), (cx - s * 0.08, cy + s * 0.32), (cx + s * 0.42, cy - s * 0.3)],
           fill=color + (255,), width=6, joint="curve")


def dual_icon(d, cx, cy, color, fill_second):
    d.rounded_rectangle((cx - 30, cy - 26, cx + 18, cy - 2), radius=7, outline=color + (255,), width=4)
    if fill_second:
        d.rounded_rectangle((cx - 18, cy + 4, cx + 30, cy + 28), radius=7, fill=color + (255,))
    else:
        d.rounded_rectangle((cx - 18, cy + 4, cx + 30, cy + 28), radius=7, outline=color + (255,), width=4)


def lang_block(d, x0, x1, win, caption, hint):
    button(d, x0, x1, 14, 242)
    center_text(d, (x0 + x1) / 2, 40, caption, font("segoeuib.ttf", 22), MUTED + (255,))
    wx0, wy0, wx1, wy1 = win
    d.rounded_rectangle((wx0, wy0, wx1, wy1), radius=16, fill=WIN + (255,), outline=WIN_EDGE + (255,), width=2)
    center_text(d, (wx0 + wx1) / 2, (wy0 + wy1) / 2, "—", font("segoeuib.ttf", 34), MUTED + (255,))
    cx, cy = (wx1 + x1) / 2, (wy0 + wy1) / 2
    d.polygon([(cx - 13, cy - 7), (cx + 13, cy - 7), (cx, cy + 9)], fill=ACCENT + (255,))
    center_text(d, (x0 + x1) / 2, 205, hint, font("segoeui.ttf", 20), MUTED + (255,))


def render_bar(on, dual):
    img = Image.new("RGBA", (1024, 256))
    d = ImageDraw.Draw(img)
    for y in range(256):
        d.line([(0, y), (1024, y)], fill=mix(BG_TOP, BG_BOT, y / 255) + (255,))
    d.rounded_rectangle((2, 2, 1021, 253), radius=26, outline=EDGE + (255,), width=3)

    state = GREEN if on else RED
    # power
    glow(img, (POWER[0], 14, POWER[1], 242), state, 20, 9, 90)
    d = ImageDraw.Draw(img)
    button(d, POWER[0], POWER[1], 14, 242, fill=mix(BTN, state, 0.16), edge=state, width=3)
    cx = (POWER[0] + POWER[1]) / 2
    center_text(d, cx, 40, "POWER", font("segoeuib.ttf", 20), MUTED + (255,))
    power_icon(d, cx, 118, 36, state)
    center_text(d, cx, 200, "ON" if on else "OFF", font("segoeuib.ttf", 32), state + (255,))

    lang_block(d, MY[0], MY[1], WIN_MY, "MY LANGUAGE", "what I read")
    lang_block(d, SEND[0], SEND[1], WIN_SEND, "SEND LANGUAGE", "what I send")

    # swap
    button(d, SWAP[0], SWAP[1], 78, 162)
    center_text(d, (SWAP[0] + SWAP[1]) / 2, 116, "⇄", font("seguisym.ttf", 40), TEXT + (255,))
    center_text(d, (SWAP[0] + SWAP[1]) / 2, 186, "SWAP", font("segoeuib.ttf", 15), MUTED + (255,))

    # dual
    dcol = ACCENT if dual else MUTED
    if dual:
        glow(img, (DUAL[0], 14, DUAL[1], 242), ACCENT, 20, 9, 90)
        d = ImageDraw.Draw(img)
    button(d, DUAL[0], DUAL[1], 14, 242,
           fill=mix(BTN, ACCENT, 0.18) if dual else BTN, edge=ACCENT if dual else BTN_EDGE, width=3 if dual else 2)
    cx = (DUAL[0] + DUAL[1]) / 2
    center_text(d, cx, 40, "DUAL", font("segoeuib.ttf", 22), (TEXT if dual else MUTED) + (255,))
    dual_icon(d, cx, 112, dcol, dual)
    center_text(d, cx, 176, "original", font("segoeui.ttf", 17), MUTED + (255,))
    center_text(d, cx, 198, "+ translation", font("segoeui.ttf", 17), MUTED + (255,))
    center_text(d, cx, 226, "ON" if dual else "OFF", font("segoeuib.ttf", 17), dcol + (255,))

    # status
    button(d, STATUS[0], STATUS[1], 14, 242)
    cx = (STATUS[0] + STATUS[1]) / 2
    center_text(d, cx, 40, "STATUS", font("segoeuib.ttf", 19), MUTED + (255,))
    shield_icon(d, cx, 116, 30, ACCENT)
    center_text(d, cx, 186, "license", font("segoeui.ttf", 17), MUTED + (255,))
    center_text(d, cx, 208, "& days left", font("segoeui.ttf", 15), MUTED + (255,))
    return img


def render_panel():
    img = Image.new("RGBA", (1024, 1024))
    for row, (on, dual) in enumerate([(True, False), (True, True), (False, False), (False, True)]):
        img.alpha_composite(render_bar(on, dual), (0, row * 256))
    return img


if __name__ == "__main__":
    render_panel().save(os.path.join(OUT, "slt_hud_panel.png"))
    render_langs().save(os.path.join(OUT, "slt_hud_langs.png"))
    print("touch map (s ranges):")
    for name, (a, b) in [("power", POWER), ("my", MY), ("swap", SWAP), ("send", SEND),
                         ("dual", DUAL), ("status", STATUS)]:
        print(f"  {name:7s} {a/1024:.4f} .. {b/1024:.4f}")
    for name, (x0, y0, x1, y1) in [("win_my", WIN_MY), ("win_send", WIN_SEND)]:
        print(f"  {name}: center s={(x0+x1)/2048:.4f} t={1-(y0+y1)/512:.4f} "
              f"w={(x1-x0)/1024:.4f} h={(y1-y0)/256:.4f}")

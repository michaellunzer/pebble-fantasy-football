#!/usr/bin/env python3
"""Draw the 720x320 app store banner.

Shows the product rather than just the name: two real watch screens, the
league list in front and the lineup behind it, beside the title. Same navy and
the same red/green bar as the icon and the app itself.

    python3 tools/make_banner.py   ->  banner_720x320.png
"""
from PIL import Image, ImageDraw, ImageFont

W, H, SS = 720, 320, 3
NAVY   = (6, 18, 38)
CARD   = (0, 30, 65)
WHITE  = (240, 244, 250)
MUTED  = (150, 162, 182)
RED    = (255, 0, 85)
GREEN  = (0, 255, 170)
SFNS   = "/System/Library/Fonts/SFNS.ttf"


def font(size, weight=400):
    f = ImageFont.truetype(SFNS, size)
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    return f


def screen(path, scale):
    """A watch screenshot in a rounded bezel, so it reads as a device."""
    img = Image.open(path).convert("RGB")
    w, h = int(img.width * scale), int(img.height * scale)
    img = img.resize((w, h), Image.LANCZOS)

    pad, radius = int(10 * scale), int(22 * scale)
    out = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    d.rounded_rectangle([0, 0, out.width - 1, out.height - 1],
                        radius=radius + pad, fill=(26, 30, 40))

    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], radius=radius,
                                           fill=255)
    out.paste(img, (pad, pad), mask)
    return out


s = SS
img = Image.new("RGB", (W * s, H * s), NAVY)
d = ImageDraw.Draw(img)

# Lay the two devices out first and let the text take what is left, so the
# title can never end up underneath a screenshot.
front = screen("screenshot_emery.png", 1.02 * s)
back = screen("screenshot_detail.png", 0.86 * s)
front_x = int(W * s - 16 * s - back.width * 0.62 - front.width)
back_x = int(W * s - 14 * s - back.width)

# A band of the card colour seats the devices
band_x = front_x - 26 * s
d.rounded_rectangle([band_x, -40 * s, W * s + 40 * s, H * s + 40 * s],
                    radius=40 * s, fill=CARD)

# Title, sized to the column it actually has
x = 44 * s
avail = band_x - x - 22 * s
title_f = font(42 * s, 780)
while d.textlength("Fantasy Football", font=title_f) > avail and title_f.size > 20 * s:
    title_f = font(title_f.size - 2 * s, 780)

d.text((x, 96 * s), "Fantasy Football", font=title_f, fill=WHITE)
tag = font(21 * s, 420)
d.text((x, 152 * s), "All your Sleeper leagues,", font=tag, fill=MUTED)
d.text((x, 180 * s), "on your wrist.", font=tag, fill=MUTED)

# The win bar as a rule under the title, split the way a real matchup splits
bw = min(230 * s, avail)
bx0, bx1, by0, by1 = x, x + bw, 228 * s, 238 * s
split = bx0 + (bx1 - bx0) * 0.38
r = (by1 - by0) / 2
d.rounded_rectangle([bx0, by0, bx1, by1], radius=r, fill=GREEN)
red = Image.new("RGBA", img.size, (0, 0, 0, 0))
ImageDraw.Draw(red).rounded_rectangle([bx0, by0, bx1, by1], radius=r, fill=RED)
piece = red.crop((0, 0, int(split), img.height))
img.paste(piece, (0, 0), piece)

img.paste(back, (back_x, int(H * s / 2 - back.height / 2) - 4 * s), back)
img.paste(front, (front_x, int(H * s / 2 - front.height / 2)), front)

img.resize((W, H), Image.LANCZOS).save("banner_720x320.png")
print("wrote banner_720x320.png  (title fits in %dpx column)" % (avail / s))

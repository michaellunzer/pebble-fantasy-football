#!/usr/bin/env python3
"""Draw the app store icons.

Deliberately not a crop of a screenshot: at 80x80 a slice of the UI is an
unreadable smudge. This draws the two things that identify the app -- a
football, and the red/green win-probability bar -- in the same colours the
watch uses, so the icon and the app look like the same product.

    python3 tools/make_icons.py
    -> icon_80x80.png, icon_144x144.png
"""
import math
from PIL import Image, ImageDraw

SS = 8                      # supersample factor, downsampled for clean edges
NAVY   = (0, 30, 65)        # GColorOxfordBlue, the card colour on the watch
LEATHER= (178, 92, 38)
LACES  = (245, 238, 228)
RED    = (255, 0, 85)       # GColorFolly
GREEN  = (0, 255, 170)      # GColorMediumSpringGreen


def football_outline(a, b, steps=240):
    """A pointed oval: two circular arcs meeting at the tips, not an ellipse."""
    R = (a * a + b * b) / (2 * b)
    d = R - b
    top = [(x, -(math.sqrt(max(R * R - x * x, 0)) - d))
           for x in (-a + 2 * a * i / steps for i in range(steps + 1))]
    return top + [(x, -y) for x, y in reversed(top)]


def render(size):
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.21), fill=NAVY)

    # Football, tilted the way one sits in flight
    fw, fh = s * 0.50, s * 0.315
    cx, cy = s / 2, s * 0.415
    ball = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    bd = ImageDraw.Draw(ball)
    bd.polygon([(x + cx, y + cy) for x, y in football_outline(fw / 2, fh / 2)],
               fill=LEATHER)

    # Laces
    lw = max(1, int(s * 0.018))
    bd.line([(cx - fw * 0.16, cy), (cx + fw * 0.16, cy)], fill=LACES, width=lw)
    for i in range(-2, 3):
        x = cx + i * fw * 0.078
        bd.line([(x, cy - fh * 0.15), (x, cy + fh * 0.15)], fill=LACES, width=lw)

    ball = ball.rotate(-20, resample=Image.BICUBIC, center=(cx, cy))
    img.alpha_composite(ball)

    # Win-probability bar, split the way a real matchup splits it. Drawn as one
    # rounded capsule of green with the red capped over its left end, so the
    # two colours meet in a straight seam instead of two sets of round caps.
    bx0, bx1 = s * 0.19, s * 0.81
    by0, by1 = s * 0.695, s * 0.815
    split = bx0 + (bx1 - bx0) * 0.38
    r = (by1 - by0) / 2
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=r, fill=GREEN)
    red = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    rd = ImageDraw.Draw(red)
    rd.rounded_rectangle([bx0, by0, bx1, by1], radius=r, fill=RED)
    img.alpha_composite(red.crop((0, 0, int(split), s)), (0, 0))

    return img.resize((size, size), Image.LANCZOS)


for n in (80, 144):
    render(n).save(f"icon_{n}x{n}.png")
    print(f"wrote icon_{n}x{n}.png")

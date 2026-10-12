#!/usr/bin/env python3
"""Build demo.gif, the animated preview shown in README.md, from lemmy.png.

Lemmy walks in, types on his laptop, sulks, walks back and falls asleep.
Sheet rows are documented in make_sheet.py.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CELL = 45   # kCellUnits in main.cpp
SCALE = 3
W, H = 480, CELL * SCALE + 6
TRANSPARENT = 255  # palette index reserved for the transparent background
FRAME_MS = 90

WALK_FRONT, WALK_LEFT, WALK_BACK, WALK_RIGHT = 0, 1, 2, 3
SULK_FRONT = 4
SLEEP = 9
TYPE_FRONT = 10
WALK_CYCLE = (0, 1, 2, 1)

sheet = Image.open(ROOT / "lemmy.png").convert("RGBA")


def cell(col, row):
    c = sheet.crop((col * CELL, row * CELL, (col + 1) * CELL, (row + 1) * CELL))
    return c.resize((CELL * SCALE, CELL * SCALE), Image.NEAREST)


def scene(col, row, x):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    img.alpha_composite(cell(col, row), (x, H - CELL * SCALE - 3))
    frame = img.convert("RGB").quantize(TRANSPARENT)
    frame.putpalette(frame.getpalette()[:TRANSPARENT * 3] + [0, 0, 0] * (256 - TRANSPARENT))
    frame.paste(TRANSPARENT, mask=img.getchannel("A").point(lambda a: 255 if a < 128 else 0))
    return frame


def walk(row, x0, x1):
    step = SCALE if x1 > x0 else -SCALE
    return [scene(WALK_CYCLE[i % 4], row, x) for i, x in enumerate(range(x0, x1, step))]


def hold(cols, row, x, loops):
    return [scene(c, row, x) for _ in range(loops) for c in cols]


start, mid, end = -CELL * SCALE, (W - CELL * SCALE) // 2, W - CELL * SCALE - 20
frames = (
    walk(WALK_RIGHT, start, mid)
    + hold((1,), WALK_FRONT, mid, 4)
    + hold((0, 1, 2, 3), TYPE_FRONT, mid, 6)
    + hold((0, 1), SULK_FRONT, mid, 5)
    + walk(WALK_RIGHT, mid, end)
    + hold((1,), WALK_BACK, end, 4)
    + walk(WALK_LEFT, end, mid)
    + hold((2, 2, 3, 3), SLEEP, mid, 6)
)

frames[0].save(ROOT / "demo.gif", save_all=True, append_images=frames[1:],
               duration=FRAME_MS, loop=0, transparency=TRANSPARENT, disposal=2)
print(f"demo.gif: {len(frames)} frames")

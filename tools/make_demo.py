#!/usr/bin/env python3
"""Build demo.gif, the animated preview shown in README.md, from pets/lemmy/lemmy.png.

Lemmy walks in, types on his laptop, sulks, walks back and falls asleep.
Sheet rows are the sections of pets/lemmy/pet.ini.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CELL = 45   # cell in pets/lemmy/pet.ini
SCALE = 3
W, H = 480, CELL * SCALE + 6
TRANSPARENT = 0  # palette index reserved for the transparent background
FRAME_MS = 90

WALK_FRONT, WALK_LEFT, WALK_BACK, WALK_RIGHT = 0, 1, 2, 3
SULK_FRONT = 4
SLEEP = 9
TYPE_FRONT = 10
WALK_CYCLE = (0, 1, 2, 1)

sheet = Image.open(ROOT / "pets" / "lemmy" / "lemmy.png").convert("RGBA")
# One exact palette shared by every frame (the sheet has well under 255 colors), so no
# per-frame quantization can shift or dither a color.
COLORS = sorted({p[:3] for p in sheet.get_flattened_data() if p[3] >= 128})
INDEX = {c: i + 1 for i, c in enumerate(COLORS)}
KEY = (255, 0, 255)  # transparent entry; must not be a sprite color or the GIF encoder merges them
assert KEY not in INDEX
PALETTE = list(KEY) + [v for c in COLORS for v in c]


def cell(col, row):
    c = sheet.crop((col * CELL, row * CELL, (col + 1) * CELL, (row + 1) * CELL))
    return c.resize((CELL * SCALE, CELL * SCALE), Image.NEAREST)


def scene(col, row, x):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    img.alpha_composite(cell(col, row), (x, H - CELL * SCALE - 3))
    frame = Image.new("P", (W, H), TRANSPARENT)
    frame.putpalette(PALETTE)
    frame.putdata([INDEX[p[:3]] if p[3] >= 128 else TRANSPARENT for p in img.get_flattened_data()])
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

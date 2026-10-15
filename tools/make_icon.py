#!/usr/bin/env python3
"""Build src/app.ico from the head of the front-facing standing frame of pets/lemmy/lemmy.png, with a 1 px white halo."""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CELL = 45                         # see make_sheet.py
FRONT_STANDING = (CELL, 0, 2 * CELL, CELL)
HEAD_BOTTOM = 27                  # first row of the vest
SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)]
HALO = (255, 255, 255, 77)        # as drawn by LemmyPet

sheet = Image.open(ROOT / "pets" / "lemmy" / "lemmy.png").convert("RGBA")
frame = sheet.crop(FRONT_STANDING)
frame = frame.crop((0, 0, CELL, HEAD_BOTTOM))
frame = frame.crop(frame.getbbox())
side = max(frame.size)
square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
square.paste(frame, ((side - frame.width) // 2, (side - frame.height) // 2))
icon = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
icon.paste(square.resize((62, 62), Image.NEAREST), (1, 1))
src, px = icon.copy().load(), icon.load()
for y in range(64):
    for x in range(64):
        if not src[x, y][3] and any(src[x + dx, y + dy][3] for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                                    if 0 <= x + dx < 64 and 0 <= y + dy < 64):
            px[x, y] = HALO
icon.save(ROOT / "src" / "app.ico", sizes=SIZES)

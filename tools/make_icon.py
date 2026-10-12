#!/usr/bin/env python3
"""Build src/app.ico from the front-facing standing frame of lemmy.png."""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CELL = 45                         # see make_sheet.py
FRONT_STANDING = (CELL, 0, 2 * CELL, CELL)
SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)]

sheet = Image.open(ROOT / "lemmy.png").convert("RGBA")
frame = sheet.crop(FRONT_STANDING)
frame = frame.crop(frame.getbbox())
side = max(frame.size)
square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
square.paste(frame, ((side - frame.width) // 2, (side - frame.height) // 2))
square = square.resize((64, 64), Image.NEAREST)
square.save(ROOT / "src" / "app.ico", sizes=SIZES)

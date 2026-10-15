#!/usr/bin/env python3
"""Builds pets/lemmy/lemmy.png from a hand-edited sheet with 43 px cells (default: lemmy_save.png).

Every cell is moved into a CELL x CELL cell (1 px margin on each side, so the character
keeps its bottom-center anchor one pixel above the bottom, and LemmyPet has room for its
halo). Semi-transparent leftovers (alpha < 255) are dropped.

Usage: pad_sheet.py [source.png]
"""
import sys
from pathlib import Path

from PIL import Image

from make_sheet import CELL, CLEAR

ROOT = Path(__file__).resolve().parent.parent
SRC_CELL = CELL - 2


def main():
    src = Image.open(sys.argv[1] if len(sys.argv) > 1 else ROOT / "lemmy_save.png").convert("RGBA")
    cols, rows = src.width // SRC_CELL, src.height // SRC_CELL
    sheet = Image.new("RGBA", (CELL * cols, CELL * rows), CLEAR)
    for r in range(rows):
        for c in range(cols):
            cell = src.crop((c * SRC_CELL, r * SRC_CELL, (c + 1) * SRC_CELL, (r + 1) * SRC_CELL))
            cell.putdata([p if p[3] == 255 else CLEAR for p in cell.getdata()])
            sheet.paste(cell, (c * CELL + 1, r * CELL + 1))
    sheet.save(ROOT / "pets" / "lemmy" / "lemmy.png")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Builds lemmy.png from a hand-edited sheet with 43 px cells (default: lemmy_save.png).

Every cell is moved into a CELL x CELL cell (1 px margin on each side, so the character
keeps its bottom-center anchor one pixel above the bottom), then surrounded by the
white halo of make_sheet.py. Semi-transparent leftovers (alpha < 255) are dropped.

Usage: add_halo.py [source.png]
"""
import sys
from pathlib import Path

from PIL import Image

from make_sheet import CELL, CLEAR, HALO

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
            out = Image.new("RGBA", (CELL, CELL), CLEAR)
            out.paste(cell, (1, 1))
            o, s = out.load(), out.copy().load()
            for y in range(CELL):
                for x in range(CELL):
                    if not s[x, y][3] and any(s[x + dx, y + dy][3] for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                                              if 0 <= x + dx < CELL and 0 <= y + dy < CELL):
                        o[x, y] = HALO
            sheet.paste(out, (c * CELL, r * CELL))
    sheet.save(ROOT / "lemmy.png")


if __name__ == "__main__":
    main()

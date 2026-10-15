#!/usr/bin/env python3
"""Build pets/lemmy/lemmy.png, Lemmy's sprite sheet, from the images in sprites/.

The source images are upscaled pixel art (~10 px per art pixel). They are reduced to
their native resolution, then every animation frame is derived from them. All frames
share one CELL x CELL cell, the character anchored bottom-center one pixel above the
bottom, leaving room for the white halo LemmyPet draws around every frame.

Sheet rows (keep in sync with pets/lemmy/pet.ini):
  0-3   walk, one row per view (front, left, back, right), 3 frames (1 = standing)
  4-7   sulk, same views, 2 frames
  8     hurt, one frame per view
  9     sleep: 2 frames head on the right, then 2 frames head on the left
  10-12 typing on a laptop (front, left, right), 4 frames

Pass --preview to also write a zoomed copy (lemmy_preview.png) for inspection.
"""
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SPRITES = ROOT / "sprites"
CELL = 45  # cell in pets/lemmy/pet.ini
FRONT, LEFT, BACK, RIGHT = range(4)

CLEAR = (0, 0, 0, 0)
ANGER = (224, 48, 48, 255)
STAR = (248, 216, 64, 255)
LID = (200, 208, 216, 255)
LID_SHADE = (152, 160, 176, 255)
BASE = (88, 96, 112, 255)
SCREEN = (176, 232, 255, 255)


# --- Reduction to native resolution ---------------------------------------

def edge_histogram(img, vertical):
    """Counts strong color changes per column (vertical=False) or per row."""
    w, h = img.size
    px = img.load()
    n_outer, n_inner = (w, h) if vertical else (h, w)
    hist = [0] * n_inner
    for o in range(0, n_outer, 3):
        prev = None
        for i in range(n_inner):
            c = px[o, i] if vertical else px[i, o]
            if prev and sum(abs(c[k] - prev[k]) for k in range(4)) > 60:
                hist[i] += 1
            prev = c
    return hist


def fit_grid(hist):
    """Returns (pitch, phase) of the pixel grid best matching the edge histogram."""
    best = (-1, 0.0, 0.0)
    for p100 in range(950, 1080):
        pitch = p100 / 100
        for ph10 in range(int(pitch * 10)):
            phase = ph10 / 10
            score = 0
            for i, v in enumerate(hist):
                if v:
                    d = (i - phase) % pitch
                    if min(d, pitch - d) < 1.0:
                        score += v
            if score > best[0]:
                best = (score, pitch, phase)
    return best[1], best[2]


def snap(c):
    return (c[0] // 8 * 8, c[1] // 8 * 8, c[2] // 8 * 8, 255) if c[3] >= 128 else CLEAR


def load_aligned(name):
    """Samples the center of every art pixel of a grid-aligned image."""
    img = Image.open(SPRITES / f"{name}.png").convert("RGBA")
    w, h = img.size
    src = img.load()
    px_w, ph_x = fit_grid(edge_histogram(img, False))
    px_h, ph_y = fit_grid(edge_histogram(img, True))
    x0 = ph_x - px_w if ph_x > 0.5 else ph_x
    y0 = ph_y - px_h if ph_y > 0.5 else ph_y
    nx, ny = int((w - x0) / px_w + 0.999), int((h - y0) / px_h + 0.999)
    out = Image.new("RGBA", (nx, ny))
    o = out.load()
    for j in range(ny):
        for i in range(nx):
            votes = Counter()
            for dy in range(3, 8):
                for dx in range(3, 8):
                    x, y = int(x0 + i * px_w + dx), int(y0 + j * px_h + dy)
                    if 0 <= x < w and 0 <= y < h:
                        votes[snap(src[x, y])] += 1
            o[i, j] = votes.most_common(1)[0][0] if votes else CLEAR
    return out.crop(out.getbbox()), (px_w + px_h) / 2


def load_rotated(name, pitch, palette):
    """Box-downscales an image whose art grid is rotated, snapping to the palette."""
    img = Image.open(SPRITES / f"{name}.png").convert("RGBA")
    img = img.crop(img.getbbox())
    w, h = round(img.width / pitch), round(img.height / pitch)
    small = img.resize((w, h), Image.BOX)
    s = small.load()
    out = Image.new("RGBA", (w, h))
    o = out.load()
    for y in range(h):
        for x in range(w):
            c = s[x, y]
            o[x, y] = min(palette, key=lambda p: sum((p[i] - c[i]) ** 2 for i in range(3))) if c[3] >= 110 else CLEAR
    return out.crop(out.getbbox())


# --- Pixel helpers ----------------------------------------------------------

class Frame:
    """A CELL x CELL frame; coordinates are relative to the sprite's top-left."""

    def __init__(self, sprite, dx=0, dy=0):
        self.img = Image.new("RGBA", (CELL, CELL), CLEAR)
        self.ox = (CELL - sprite.width) // 2
        self.oy = CELL - 1 - sprite.height  # bottom row left for the halo
        self.img.paste(sprite, (self.ox + dx, self.oy + dy))
        self.px = self.img.load()

    def get(self, x, y):
        x, y = x + self.ox, y + self.oy
        return self.px[x, y] if 0 <= x < CELL and 0 <= y < CELL else CLEAR

    def put(self, x, y, c):
        x, y = x + self.ox, y + self.oy
        if 0 <= x < CELL and 0 <= y < CELL:
            self.px[x, y] = c

    def shift_up(self, x0, x1, y0, y1, n=1):
        """Moves the block [x0..x1] x [y0..y1] up by n, filling below from the rows under it."""
        for x in range(x0, x1 + 1):
            for y in range(y0 - n, y1 + 1):
                self.put(x, y, self.get(x, y + n))


def stamp(frame, rows, x0, y0, colors, mirror=False):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row[::-1] if mirror else row):
            if ch in colors:
                frame.put(x0 + i, y0 + j, colors[ch])


def runs(sprite, y):
    """Opaque column runs [(x0, x1)] on row y of a sprite."""
    px = sprite.load()
    out, start = [], None
    for x in range(sprite.width + 1):
        on = x < sprite.width and px[x, y][3] > 0
        if on and start is None:
            start = x
        elif not on and start is not None:
            out.append((start, x - 1))
            start = None
    return out


# --- Animations -------------------------------------------------------------

LEG_ROWS = 4  # rows of legs below the body


def walk_frames(sprite, view, ink):
    h = sprite.height
    stand = Frame(sprite)
    if view in (FRONT, BACK):
        legs = runs(sprite, h - 2)[:2]
        steps = []
        for lifted in legs:
            # Body up one pixel on the planted leg, the other leg raised.
            f = Frame(sprite, dy=-1)
            for y in range(h - LEG_ROWS, h):  # planted leg reaches the ground again
                for x in range(sprite.width):
                    if sprite.getpixel((x, y))[3]:
                        f.put(x, y, sprite.getpixel((x, y)))
            f.shift_up(lifted[0], lifted[1], h - LEG_ROWS - 1, h - 1)
            steps.append(f)
        return [steps[0], stand, steps[1]]

    # Side views: a single leg column, split into a stride, then a passing step.
    (l0, l1), = runs(sprite, h - 2)[:1]
    c = (l0 + l1) // 2
    fw = -1 if view == LEFT else 1  # facing direction
    stride = Frame(sprite, dy=1)
    for y in range(h - LEG_ROWS + 1, h):
        for x in range(l0 - 8, l1 + 9):
            stride.put(x, y, CLEAR)
    for y, spans in ((h - 3, [(-4, 4)]), (h - 2, [(-6, -2), (1, 5)]), (h - 1, [(-7, -2), (2, 5)])):
        for a, b in spans:
            for d in range(a, b + 1):
                stride.put(c + d * -fw, y, ink)
    passing = Frame(sprite, dy=-1)
    for y in range(h - LEG_ROWS - 1, h):
        for x in range(l0, l1 + 1):
            passing.put(x, y, ink)
    for d in range(1, 5):  # back foot lifted
        passing.put(c - d * fw, h - 1, CLEAR)
        passing.put(c - (d + 1) * fw, h - 2, ink)
    return [stride, stand, passing]


ANGER_ROWS = [".X.X.", "XX.XX", ".....", "XX.XX", ".X.X."]
STAR_ROWS = [".X.", "XXX", ".X."]


def angry_brows(f, view, ink, dy):
    if view == FRONT:
        for x, y in ((9, 13), (10, 14), (11, 14), (20, 13), (19, 14), (18, 14)):
            f.put(x, y + dy, ink)
    elif view in (LEFT, RIGHT):
        m = (lambda x: x) if view == LEFT else (lambda x: 28 - x)
        for x, y in ((8, 14), (9, 14), (10, 13)):
            f.put(m(x), y + dy, ink)


def sulk_frames(sprite, view, ink):
    """Frowning, an anger mark over his head; the second frame is a small angry hop."""
    frames = []
    for dy in (0, -1):
        f = Frame(sprite, dy=dy)
        angry_brows(f, view, ink, dy)
        x = 1 if view == RIGHT else sprite.width - 6
        stamp(f, ANGER_ROWS, x, 1 + 2 * dy, {"X": ANGER})
        frames.append(f)
    return frames


def hurt_frame(sprite, view, ink, skin):
    f = Frame(sprite)
    eye = {FRONT: [(9, 15), (18, 15)], LEFT: [(7, 15)], RIGHT: [(19, 15)]}.get(view, [])
    for ex, ey in eye:
        for j in range(3):
            for i in range(3):
                f.put(ex + i, ey + j, ink if i == j or i + j == 2 else skin)
    stamp(f, STAR_ROWS, -4, 2, {"X": STAR})
    stamp(f, STAR_ROWS, sprite.width + 1, 5, {"X": STAR})
    stamp(f, STAR_ROWS, sprite.width - 4, -2, {"X": STAR})
    return f


def sleep_frames(sprite, head_right):
    """Two breathing frames: the chest rises by one pixel."""
    w = sprite.width
    chest = range(6, 17) if head_right else range(w - 17, w - 6)
    still = Frame(sprite)
    breath = Frame(sprite)
    px = sprite.load()
    for x in chest:
        top = next(y for y in range(sprite.height) if px[x, y][3])
        breath.shift_up(x, x, top, top + 2)
    return [still, breath]


def glow(f, skin_colors):
    """Tints the face light blue, as if lit by the laptop screen."""
    for y in range(-2, 26):
        for x in range(-8, 36):
            c = f.get(x, y)
            if c[:3] in skin_colors:
                f.put(x, y, ((c[0] * 3 + SCREEN[0]) // 4, (c[1] * 3 + SCREEN[1]) // 4, (c[2] * 3 + SCREEN[2]) // 4, 255))


def typing_frames(sprite, view, ink, skin_colors, logo):
    frames = []
    for i in range(4):
        f = Frame(sprite)
        if view == FRONT:
            # Back of the laptop lid, held in front of his belly, hands typing on both sides.
            for y in range(23, 33):
                for x in range(8, 22):
                    edge = y in (23, 32) or x in (8, 21)
                    f.put(x, y, ink if edge else (LID_SHADE if y >= 30 else LID))
            stamp(f, ["XXX", "X.X", ".X."], 13, 26, {"X": logo})  # tiny skull
            if i == 0:
                f.shift_up(5, 7, 27, 32)
            elif i == 2:
                f.shift_up(22, 24, 27, 32)
        else:
            m = (lambda x: x) if view == LEFT else (lambda x: 28 - x)
            for x in range(-1, 17):  # keyboard base under the hand
                f.put(m(x), 32, BASE)
                f.put(m(x), 33, ink)
            for y in range(22, 32):  # lid in profile, screen facing him
                slant = 1 if y < 27 else 0
                f.put(m(-1 - slant), y, ink)
                f.put(m(0 - slant), y, SCREEN if i % 2 == 0 or y % 3 else LID)
            f.put(m(13 if i == 0 else 15 if i == 2 else 14), 32, skin_colors[0] + (255,))  # finger on a key
        if i % 2:
            glow(f, set(skin_colors))
        frames.append(f)
    return frames


# --- Assembly ---------------------------------------------------------------

def main():
    views = {}
    pitch = 0.0
    for view, name in ((FRONT, "front"), (LEFT, "left"), (BACK, "back"), (RIGHT, "right")):
        views[view], p = load_aligned(name)
        pitch += p / 4
    palette = sorted({c for v in views.values() for c in v.getdata() if c[3]})
    sleep_right = load_rotated("sleep-left", pitch, palette)  # head on the right
    sleep_left = load_rotated("sleep-right", pitch, palette)

    front = views[FRONT]
    ink = front.getpixel((0, 8))     # outline
    skin = front.getpixel((10, 14))[:3]
    skin_colors = [skin] + [c[:3] for c in palette if sum(c[:3]) > 3 * 170 and c[:3] != skin]

    rows = []
    for v in range(4):
        rows.append(walk_frames(views[v], v, ink))
    for v in range(4):
        rows.append(sulk_frames(views[v], v, ink))
    rows.append([hurt_frame(views[v], v, ink, skin + (255,)) for v in range(4)])
    rows.append(sleep_frames(sleep_right, True) + sleep_frames(sleep_left, False))
    for v in (FRONT, LEFT, RIGHT):
        rows.append(typing_frames(views[v], v, ink, skin_colors, front.getpixel((14, 4))))

    sheet = Image.new("RGBA", (CELL * 4, CELL * len(rows)), CLEAR)
    for r, frames in enumerate(rows):
        for c, f in enumerate(frames):
            sheet.paste(f.img, (c * CELL, r * CELL))
    sheet.save(ROOT / "pets" / "lemmy" / "lemmy.png")
    if "--preview" in sys.argv:
        preview = Image.new("RGBA", sheet.size, (96, 128, 160, 255))
        preview.alpha_composite(sheet)
        preview.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(ROOT / "lemmy_preview.png")


if __name__ == "__main__":
    main()

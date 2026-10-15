#!/usr/bin/env python3
"""Builds pets/<id>/ from a PMDCollab SpriteCollab sprite (https://github.com/PMDCollab/SpriteCollab).

Downloads the species' AnimData.xml and animation sheets, then writes <id>.png (one block
of frames per animation, rows in LemmyPet facing order), pet.ini (frame rectangles) and
credits.txt. The cries file named by `lyrics` is left alone if it already exists.

Within an animation, every frame keeps the SpriteCollab anchor (the frame center) at the
same place of its rectangle, so the pet does not jitter; rectangles are cropped to what the
animation uses, with a 1 px clear margin for the halo.

Usage: import_pmd.py <dex> <id> <name> [--sleep-head left|right]
  e.g. import_pmd.py 0025 pikachu Pikachu
"""
import argparse
import io
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BASE = "https://raw.githubusercontent.com/PMDCollab/SpriteCollab/master/sprite/"
# SpriteCollab sheet rows, from the top.
PMD_ROWS = ["S", "SE", "E", "NE", "N", "NW", "W", "SW"]
FACINGS = ["S", "SW", "W", "NW", "N", "NE", "E", "SE"]  # LemmyPet order
CLEAR = (0, 0, 0, 0)

# LemmyPet section -> (SpriteCollab animation, frame indices or None for all, facings).
ANIMS = [
    ("walk", "Walk", None, FACINGS),
    ("sulk", "Attack", [0, 1], FACINGS),
    ("hurt", "Hurt", [1], FACINGS),
    ("sleep", "Sleep", None, ["S"]),  # a single row, renamed E / W below
    ("type", "Pose", None, ["S", "W", "E"]),
]


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def anim_data(dex):
    root = ET.fromstring(fetch(f"{BASE}{dex}/AnimData.xml"))
    anims = {}
    for a in root.iter("Anim"):
        name = a.findtext("Name")
        if a.find("CopyOf") is not None:
            anims[name] = a.findtext("CopyOf")
        else:
            anims[name] = (int(a.findtext("FrameWidth")), int(a.findtext("FrameHeight")))
    return anims


def load_frames(dex, anims, pmd, picks, facings):
    """{facing: [frame images]} of a SpriteCollab animation, each fw x fh, anchor at the center."""
    while isinstance(anims[pmd], str):
        pmd = anims[pmd]
    fw, fh = anims[pmd]
    sheet = Image.open(io.BytesIO(fetch(f"{BASE}{dex}/{pmd}-Anim.png"))).convert("RGBA")
    count = sheet.width // fw
    frames = {}
    for f in facings:
        row = PMD_ROWS.index(f) if sheet.height > fh else 0
        cols = picks if picks is not None else range(count)
        frames[f] = [sheet.crop((c * fw, row * fh, (c + 1) * fw, (row + 1) * fh)) for c in cols]
    return frames


def anchored_box(frames):
    """Smallest box around the anchor holding every frame, horizontally centered on it."""
    fw, fh = next(iter(frames.values()))[0].size
    ax, ay = fw // 2, fh // 2
    half, top, bottom = 0, ay, ay
    for imgs in frames.values():
        for im in imgs:
            box = im.getbbox()
            if box:
                half = max(half, ax - box[0], box[2] - ax)
                top, bottom = min(top, box[1]), max(bottom, box[3])
    return (ax - half, top, ax + half, bottom)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dex")
    ap.add_argument("id")
    ap.add_argument("name")
    ap.add_argument("--sleep-head", choices=["left", "right"], default="left")
    args = ap.parse_args()

    anims = anim_data(args.dex)
    blocks = []  # (section, {facing: [cropped frames]})
    for section, pmd, picks, facings in ANIMS:
        frames = load_frames(args.dex, anims, pmd, picks, facings)
        if picks is not None and len(picks) == 1:  # a still: nothing to keep aligned
            blocks.append((section, {f: [im.crop(im.getbbox())] for f, (im,) in frames.items()}))
            continue
        box = anchored_box(frames)
        blocks.append((section, {f: [im.crop(box) for im in imgs] for f, imgs in frames.items()}))

    # Pack: one row per facing, one column per frame, 1 px margin around each frame.
    rows = [imgs for _, fr in blocks for imgs in fr.values()]
    width = max(sum(im.width + 2 for im in imgs) for imgs in rows)
    height = sum(max(im.height + 2 for im in imgs) for imgs in rows)
    sheet = Image.new("RGBA", (width, height), CLEAR)
    rects, y = {}, 0
    for section, frames in blocks:
        rects[section] = {}
        for f, imgs in frames.items():
            rects[section][f], x = [], 0
            for im in imgs:
                sheet.paste(im, (x + 1, y + 1))
                rects[section][f].append((x, y, im.width + 2, im.height + 2))
                x += im.width + 2
            y += max(im.height + 2 for im in imgs)

    # The sleep row lies with its head on one side: that is E (head on the right) or W.
    sleep = rects["sleep"].pop("S")
    rects["sleep"]["E" if args.sleep_head == "right" else "W"] = sleep

    # Z's just above the head of the head-on-the-right sleeping frame, bottom-centered in the cell.
    cell_w = max(r[2] for fr in rects.values() for v in fr.values() for r in v)
    cell_h = max(r[3] for fr in rects.values() for v in fr.values() for r in v)
    _, _, sw, sh = sleep[0]
    left = (cell_w - sw) // 2
    zzz_x = left + sw * 0.65
    zzz_y = max(0, cell_h - sh - 1)

    out = ROOT / "pets" / args.id
    out.mkdir(parents=True, exist_ok=True)
    sheet.save(out / f"{args.id}.png")
    credits = fetch(f"{BASE}{args.dex}/credits.txt").decode()
    (out / "credits.txt").write_text(
        f"{args.name} sprites from PMDCollab SpriteCollab, sprite/{args.dex}\n"
        "https://github.com/PMDCollab/SpriteCollab (CC BY-NC 4.0)\n\n" + credits)

    lines = [
        f"; {args.name} (format: see \"Adding a pet\" in README.md), built by tools/import_pmd.py",
        f"; from PMDCollab SpriteCollab sprite {args.dex} (see credits.txt).",
        "",
        "[pet]",
        f"name = {args.name}",
        f"sheet = {args.id}.png",
        f"zzz = {round(zzz_x)},{round(zzz_y)}",
        "lyrics = cries.txt",
        "halo = 0",
    ]
    for section, frames in rects.items():
        lines += ["", f"[{section}]"]
        if section == "walk":
            lines.append("stand = 1")
        for f in FACINGS:
            if f in frames:
                lines.append(f"{f} = " + " ".join(",".join(map(str, r)) for r in frames[f]))
    (out / "pet.ini").write_text("\n".join(lines) + "\n")
    if not (out / "cries.txt").exists():
        (out / "cries.txt").write_text(f"# Lines {args.name} types in his bubble.\n{args.name}!\n")


if __name__ == "__main__":
    main()

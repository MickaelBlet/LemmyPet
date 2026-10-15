#!/usr/bin/env python3
"""Builds pets/<id>/ from a PMDCollab SpriteCollab sprite (https://github.com/PMDCollab/SpriteCollab).

Downloads the species' AnimData.xml, every animation sheet (<Name>-Anim.png) with its body
marks (<Name>-Offsets.png) and credits.txt as they are, LemmyPet reading them directly, then writes pet.ini, naming the animations each section
plays; [play] gets every animation no other section uses, and Rotate also turns him around. Re-importing keeps the `lyrics` file and
a hand-made [type] section (one with its own sheet) of an existing pet.ini; the lyrics file is
created if missing.

Usage: import_pmd.py <dex> <id> <name> [--sleep-head left|right]
  e.g. import_pmd.py 0025 pikachu Pikachu
"""
import argparse
import configparser
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = "https://raw.githubusercontent.com/PMDCollab/SpriteCollab/master/sprite/"

# LemmyPet section -> SpriteCollab animations (those the sprite has) and extra keys.
SECTIONS = [
    ("walk", ["Walk"], {}),
    ("idle", ["Idle"], {}),
    ("sulk", ["Attack", "Shoot", "Double", "Strike", "Kick"], {"inplace": "xy"}),
    ("hurt", ["Hurt"], {"loop": "0"}),
    ("sleep", ["Sleep"], {}),  # a single row: its facing is set from --sleep-head
    ("type", ["Pose"], {}),
    ("play", None, {"inplace": "x"}),  # the rest (still jumping)
    ("turn", ["Rotate"], {}),  # also played on its own
]


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def anim_data(xml):
    """{name: (frame width, frame height) or the name it copies}."""
    anims = {}
    for a in ET.fromstring(xml).iter("Anim"):
        name = a.findtext("Name")
        if a.find("CopyOf") is not None:
            anims[name] = a.findtext("CopyOf")
        else:
            anims[name] = (int(a.findtext("FrameWidth")), int(a.findtext("FrameHeight")))
    return anims


def resolve(anims, name):
    while isinstance(anims[name], str):
        name = anims[name]
    return name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dex")
    ap.add_argument("id")
    ap.add_argument("name")
    ap.add_argument("--sleep-head", choices=["left", "right"], default="left")
    args = ap.parse_args()

    out = ROOT / "pets" / args.id
    out.mkdir(parents=True, exist_ok=True)
    old = configparser.ConfigParser(inline_comment_prefixes=(";", "#"), strict=False)
    old.optionxform = str
    if (out / "pet.ini").exists():
        old.read(out / "pet.ini", encoding="utf-8")
    lyrics = old.get("pet", "lyrics", fallback="cries.txt")
    own_type = old.has_section("type") and old.has_option("type", "sheet")

    xml = fetch(f"{BASE}{args.dex}/AnimData.xml")
    (out / "AnimData.xml").write_bytes(xml)
    anims = anim_data(xml)
    for name, frame in anims.items():
        if isinstance(frame, tuple):
            for kind in ("Anim", "Offsets"):
                (out / f"{name}-{kind}.png").write_bytes(fetch(f"{BASE}{args.dex}/{name}-{kind}.png"))
    credits = fetch(f"{BASE}{args.dex}/credits.txt").decode()
    (out / "credits.txt").write_text(
        f"{args.name} sprites from PMDCollab SpriteCollab, sprite/{args.dex}\n"
        "https://github.com/PMDCollab/SpriteCollab (CC BY-NC 4.0)\n\n" + credits)

    sections, used = [], set()
    for section, names, keys in SECTIONS:
        if section == "type" and own_type:
            sections.append((section, [f"{k} = {v}" for k, v in old.items("type")]))
            continue
        if names is None:  # every real animation not used yet (a copy shows the same frames)
            names = [n for n in anims if isinstance(anims[n], tuple) and n not in used]
        names = [n for n in names if n in anims]
        if not names:
            continue
        if section != "turn":
            used.update(resolve(anims, n) for n in names)
        lines = ["anim = " + ", ".join(names)] + [f"{k} = {v}" for k, v in keys.items()]
        if section == "sleep":
            lines.append("facing = " + ("E" if args.sleep_head == "right" else "W"))
        sections.append((section, lines))

    lines = [
        f"; {args.name} (format: see \"Adding a pet\" in README.md): PMDCollab SpriteCollab sprite {args.dex}",
        "; (see credits.txt), downloaded by tools/import_pmd.py.",
        "",
        "[pet]",
        f"name = {args.name}",
        "pmd = AnimData.xml",
        f"lyrics = {lyrics}",
        "halo = 0",
    ]
    for section, keys in sections:
        lines += ["", f"[{section}]"] + keys
    (out / "pet.ini").write_text("\n".join(lines) + "\n")
    if not (out / lyrics).exists():
        (out / lyrics).write_text(f"# Lines {args.name} types in his bubble.\n{args.name}!\n")


if __name__ == "__main__":
    main()

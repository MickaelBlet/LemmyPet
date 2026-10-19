#!/usr/bin/env python3
"""Builds pets/<id>/ from a PMDCollab SpriteCollab sprite (https://github.com/PMDCollab/SpriteCollab).

The Pokemon is found by dex number or name in SpriteCollab's tracker.json, a form (Cosplay, Mega_X,
Female, Shiny, Galar/Shiny...) picked with --form or --shiny; the pet id and name default to the
Pokemon's, plus the form.

Downloads the species' AnimData.xml, every animation sheet (<Name>-Anim.png) with its body
marks (<Name>-Offsets.png) and credits.txt as they are, LemmyPet reading them directly, then writes pet.ini, naming the animations each section
plays; [play] gets every animation no other section uses, and Rotate also turns him around. Re-importing keeps the `lyrics` file and
a hand-made [type] section (one with its own sheet) of an existing pet.ini; the lyrics file is
created if missing.

Usage: import_pmd.py <dex|name> [id] [name] [--form FORM] [--shiny] [--sleep-head left|right]
       import_pmd.py --list [text]     (Pokemon with sprites, and their forms)
  e.g. import_pmd.py pikachu
       import_pmd.py 25 --form cosplay --shiny
       import_pmd.py "mr. mime" mime Mimey
"""
import argparse
import configparser
import difflib
import json
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = "https://raw.githubusercontent.com/PMDCollab/SpriteCollab/master/"
BASE = REPO + "sprite/"

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


def norm(text):
    return re.sub(r"[^a-z0-9]", "", text.lower())


def sprites(entry, path):
    """[(sprite folder, form names)] of a tracker entry, those with sprites."""
    found = []

    def walk(node, path, names):
        if node["name"]:
            names = names + [node["name"]]
        if node["sprite_files"]:
            found.append((path, names))
        for key, sub in node["subgroups"].items():
            walk(sub, f"{path}/{key}", names)

    walk(entry, path, [])
    for i, (folder, names) in enumerate(found):
        found[i] = (folder, names[1:])  # names[0]: the Pokemon
    return found


def find(tracker, query):
    """Dex number of a Pokemon given by number or name."""
    if query.isdigit():
        dex = query.zfill(4)
        if dex in tracker:
            return dex
        sys.exit(f"no Pokemon #{dex} in SpriteCollab")
    q = norm(query)
    for dex, entry in tracker.items():
        if norm(entry["name"]) == q:
            return dex
    names = {norm(e["name"]): e["name"] for e in tracker.values() if e["name"]}
    close = [names[n] for n in names if q in n] or [names[n] for n in difflib.get_close_matches(q, names, 5)]
    sys.exit(f"no Pokemon named {query!r}" + (f"; did you mean: {', '.join(close[:10])}?" if close else ""))


def list_sprites(tracker, query):
    q = norm(query or "")
    for dex, entry in tracker.items():
        if q and q not in norm(entry["name"]) and q != dex.lstrip("0"):
            continue
        found = sprites(entry, dex)
        if found:
            forms = ["/".join(names) for _, names in found if names]
            print(f"{dex} {entry['name']}" + (f": {', '.join(forms)}" if forms else ""))


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


def tracker_node(entry, folder):
    node = entry
    for key in folder.split("/")[1:]:
        node = node["subgroups"][key]
    return node


def main():
    ap = argparse.ArgumentParser(description="Imports a PMDCollab SpriteCollab sprite into pets/<id>/.")
    ap.add_argument("pokemon", nargs="?", help="dex number or name")
    ap.add_argument("id", nargs="?", help="pet folder (default: from the Pokemon and form)")
    ap.add_argument("name", nargs="?", help="name in the settings (default: from the Pokemon and form)")
    ap.add_argument("--form", default="", help="form names separated by '/', e.g. Cosplay, Mega_X, Female")
    ap.add_argument("--shiny", action="store_true", help="adds Shiny to --form")
    ap.add_argument("--sleep-head", choices=["left", "right"], default="left")
    ap.add_argument("--list", action="store_true", help="lists the Pokemon whose name holds <pokemon>, with their forms")
    args = ap.parse_args()

    tracker = json.loads(fetch(REPO + "tracker.json"))
    if args.list:
        list_sprites(tracker, args.pokemon)
        return
    if not args.pokemon:
        ap.error("missing <pokemon>")
    dex = find(tracker, args.pokemon)
    entry = tracker[dex]
    found = sprites(entry, dex)
    form = [f for f in args.form.split("/") if f] + (["Shiny"] if args.shiny else [])
    want = sorted(norm(f) for f in form)
    match = [(folder, names) for folder, names in found if sorted(norm(n) for n in names) == want]
    if not match:
        forms = ", ".join("/".join(names) or "(default)" for _, names in found) or "none"
        sys.exit(f"{entry['name']} has no {'/'.join(form) or 'default'} sprite; forms with sprites: {forms}")
    folder, names = match[0]
    if "Walk" not in tracker_node(entry, folder)["sprite_files"]:
        sys.exit(f"sprite/{folder} has no Walk animation")
    args.dex = folder
    args.id = args.id or "_".join(norm(n) for n in [entry["name"]] + names)
    args.name = args.name or " ".join([entry["name"]] + names).replace("_", " ")
    print(f"sprite/{folder} -> pets/{args.id} ({args.name})")

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
            print(f"  {name}")
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

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Size and speed set with sliders in the settings window: size x0.5 to x8
  in 0.05 steps, speed 20% to 400% in 5% steps (former presets carried
  over).
- LemmyPet Win32 desktop pet (`src/main.cpp`): click-through Lemmy wandering
  the desktop, controlled from a notification-area icon.
- Windows resources: icon, manifest and embedded sprite sheet (`src/app.rc`).
- Sprite sources in `sprites/` and generated sheet `lemmy.png`.
- Python tools to build the sprite sheet and icon (`tools/`).
- MinGW cross-compilation `Makefile`.
- Typing bubble shows Lemmy lines (`lyrics.txt` next to the exe, or
  built-in Motorhead song titles) instead of the typed keys, one
  character per keystroke; any key, modifiers included, triggers it.
- README with features, usage, build instructions and an animated demo.
- `tools/make_demo.py` builds `demo.gif` (transparent background) from
  the sprite sheet.
- Several pets at once: every `pets/<id>/pet.ini` (name, sprite sheets,
  frame rectangles or grid cells per animation and facing, halo, Zzz
  position, bubble lines) adds a pet with its own tray submenu and
  settings. Lemmy moves to `pets/lemmy/`; `pets/snorlax/` is a second
  example with irregular frames.
- `tools/import_pmd.py` builds a `pets/<id>/` folder from a PMDCollab
  SpriteCollab sprite (CC BY-NC 4.0, credits in the generated
  `credits.txt`).
- Spider-Man mode: Lemmy rolls around corners (a quarter turn with his
  feet on a rounded path) instead of snapping from one edge to the next,
  and keeps the edge he came from while resting in a corner.
- "Collide with other pets" per-pet option: pets that both have it bump
  into each other while walking or fleeing, bounce off, then sulk facing
  each other before going back to idle.
- PMD sprites read natively: `[pet] pmd = AnimData.xml`, then
  `anim = <Name>, ...` in a section plays those SpriteCollab animations
  (all facings, frame durations, anchored frames, one picked at random
  each time; frames going below his feet are raised onto the floor);
  `cycle`, `durations` and `facing` adjust them.
  `tools/import_pmd.py` now downloads the raw sprite folder and writes
  such a `pet.ini` using every animation.
- `[idle]` animation, played while standing still, and `[play]`
  animations, played once now and then after an idle pause.
- `inplace = x | xy` for PMD animations: each frame follows his body mark
  (`<Name>-Offsets.png`), so lunges and swings happen on the spot (Snorlax
  sulks on the spot and plays on the spot, still jumping).
- `zzz` defaults to just above the head of the first sleep frame.
- `[turn]` animation: he turns on the spot, the shorter way through the
  facings in between, before walking off in another direction (Snorlax
  uses PMD's Rotate).
- `durations` (ms of each cycle step) and `loop = 0` (play once, hold the
  last frame) per animation; animations start over with each state.
- Settings window (`src/options.cpp`), opened by clicking the tray icon:
  pets listed on the left, drop-down lists and checkboxes applied right away,
  plus "Run at startup" and "Quit LemmyPet".

### Changed

- Snorlax is now the PMDCollab SpriteCollab sprite 0143 read as is, with
  all its animations (Walk, Idle, Attack / Shoot / Double when sulking,
  Hurt, Sleep, Stomp / Swing / Hop / Charge / Rotate to play, and Rotate
  to turn around), keeping his hand-made laptop frames.
- The tray menu is reduced to "Settings...", show / hide all pets and
  "Quit"; the per-pet submenus move to the settings window, and a click
  on the icon opens it instead of double-click showing / hiding the pets.
- The app icon shows only Lemmy's head.
- Tray menu translated from French to English.
- Lemmy's sprite sheet is read from `pets/lemmy/` instead of being
  embedded in the exe, and his bubble lines from `pets/lemmy/lyrics.txt`
  (no more `lyrics.txt` next to the exe nor built-in titles).
- Settings are stored per pet under `HKCU\Software\LemmyPet\Pets\<id>`,
  defaulting to the former ones; double-clicking the tray icon shows or
  hides every pet.
- The white halo is a per-pet menu option ("White halo"), on by default
  unless the pet's `pet.ini` says `halo = 0`.
- The white halo is drawn by LemmyPet, 1 px wide at any size, instead of
  being baked into `lemmy.png` (where it grew with the scale);
  `tools/add_halo.py` becomes `tools/pad_sheet.py`.
- Flee to a corner: Lemmy runs faster the farther the corner is, reaching
  it within 1.5 s.
- Edges zone without Spider-Man mode: Lemmy walks flush against the left
  and right screen edges (no more transparent gap).
- `src/main.cpp` split into modules: `pet.h` (shared state), `settings`,
  `render`, `behavior`, `typing`, `window` and `tray`.
- Lower CPU use: moving or fading no longer re-sends the bitmap, scaled
  frames are cached (16 MB per pet at most), ticks wait for the next frame
  change (polling the cursor every 100 ms at most) on coalescable timers,
  and the pets pause while the session is locked or the display is off.

### Fixed

- The cell a pet is drawn in is made square, so a pet with wider than tall
  frames stays against walls in Spider-Man mode and rolls around corners
  without jumping.
- Multi-monitor: Lemmy only moves to an adjacent screen, walking through
  the shared border (along the floors in the bottom and edges zones)
  instead of cutting straight across gaps and other screens.

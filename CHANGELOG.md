# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- LemmyPet Win32 desktop pet (`src/main.cpp`): click-through Lemmy wandering
  the desktop, controlled from a notification-area icon.
- Windows resources: icon, manifest and embedded sprite sheet (`src/app.rc`).
- Sprite sources in `sprites/` and generated sheet `lemmy.png`.
- Python tools to build the sprite sheet, icon and halo (`tools/`).
- MinGW cross-compilation `Makefile`.
- Typing bubble shows Lemmy lines (`lyrics.txt` next to the exe, or
  built-in Motorhead song titles) instead of the typed keys, one
  character per keystroke; any key, modifiers included, triggers it.
- README with features, usage, build instructions and an animated demo.
- `tools/make_demo.py` builds `demo.gif` (transparent background) from
  the sprite sheet.

### Changed

- Tray menu translated from French to English.

### Fixed

- Multi-monitor: Lemmy only moves to an adjacent screen, walking through
  the shared border (along the floors in the bottom and edges zones)
  instead of cutting straight across gaps and other screens.

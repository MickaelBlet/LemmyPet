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

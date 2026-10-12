# LemmyPet
Lemmy 🤘

<p align="center"><img src="demo.gif" alt="LemmyPet demo"></p>

A tiny pixel-art Lemmy that lives on your Windows desktop. He wanders around,
naps, sulks when the mouse gets too close and types on his laptop while you type.
He never steals focus and clicks go right through him.

## Features

- **Wanders the desktop**: walks, idles, sleeps (with floating "Zzz") and bumps
  into screen edges.
- **Reacts to the mouse**: fades out when the cursor comes near, then teleports
  to the bottom-right corner, flees to a corner, or ignores it.
- **Types along with you**: when you type, he pulls out his laptop and a speech
  bubble spells Lemmy lines, one character per keystroke. The real keys are
  never shown.
- **Spider-Man mode**: in the "screen edges" zone, he walks on the walls and the
  ceiling.
- **Draggable** (optional): grab him with the mouse and drop him elsewhere.
- **Single instance**, settings saved in the registry
  (`HKCU\Software\LemmyPet`), optional start with Windows.

## Usage

Run `LemmyPet.exe`. Everything is controlled from the notification-area icon:

- **Double-click**: show / hide Lemmy.
- **Right-click**: settings menu.

| Menu                                | Options                                   |
| ----------------------------------- | ----------------------------------------- |
| Size                                | x1 to x6 (default x2)                     |
| Speed                               | Slow, Normal, Fast                        |
| Activity                            | Lazy, Normal, Hyperactive                 |
| When the mouse approaches           | Teleport bottom-right, flee, ignore       |
| Opacity                             | 100% to 20%                               |
| Roaming zone                        | Anywhere, screen edges, bottom of screen  |
| Zzz while sleeping                  | Show "Zzz" while sleeping                 |
| Draggable with the mouse            | Allow dragging him                        |
| Sleep when the mouse is idle        | Falls asleep after 30 s of mouse idle     |
| Type on his laptop when I type      | Types while you type                      |
| Bubble with Lemmy lyrics            | Speech bubble while typing                |
| Spider-Man mode (feet on the edges) | Walls and ceiling (screen edges zone)     |
| Run at startup                      | Start with Windows                        |

### Custom lyrics

By default the bubble cycles through Motörhead song titles. To use your own
lines, put a `lyrics.txt` file next to the exe: UTF-8, one line per bubble,
lines starting with `#` are comments.

## Build

Cross-compiled with MinGW-w64 (e.g. from Linux or WSL):

```sh
make
```

Requires `x86_64-w64-mingw32-g++`, and Python 3 with Pillow to regenerate the
assets. The sprite sheet `lemmy.png` and the icon are rebuilt from `sprites/`
when needed:

| Script                  | Output                                    |
| ----------------------- | ----------------------------------------- |
| `tools/make_sheet.py`   | `lemmy.png` sprite sheet (`--preview` for a zoomed copy) |
| `tools/make_icon.py`    | `src/app.ico`                             |
| `tools/make_demo.py`    | `demo.gif` shown above                    |

## License

See [LICENSE](LICENSE).

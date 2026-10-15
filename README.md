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
  ceiling, rolling smoothly around corners from one edge to the next.
- **Draggable** (optional): grab him with the mouse and drop him elsewhere.
- **More pets**: any pet described in `pets/` lives on the desktop at the
  same time, with its own settings (see [Adding a pet](#adding-a-pet)).
- **Collisions** (optional): pets bump into each other, bounce off and sulk
  at each other for a moment.
- **Single instance**, settings saved in the registry
  (`HKCU\Software\LemmyPet\Pets\<id>`), optional start with Windows.

## Usage

Run `LemmyPet.exe`, with the `pets` folder next to it. Everything is
controlled from the notification-area icon:

- **Click**: opens the settings window, pets listed on the left
  (Ctrl+Tab to switch); changes apply right away. "Run at startup" and "Quit
  LemmyPet" are at the bottom.
- **Right-click**: "Settings...", show / hide all pets, "Quit".

| Pet setting                         | Options                                   |
| ----------------------------------- | ----------------------------------------- |
| Visible                             | Show or hide this pet                     |
| Size                                | Slider, x0.5 to x8 by 0.05 (default x2)   |
| Speed                               | Slider, 20% to 400% by 5% (default 100%)  |
| Activity                            | Lazy, Normal, Hyperactive                 |
| When the mouse approaches           | Teleport bottom-right, flee, ignore       |
| Opacity                             | 100% to 20%                               |
| Roaming zone                        | Anywhere, screen edges, bottom of screen  |
| White halo                          | 1 px white outline around him             |
| Zzz while sleeping                  | Show "Zzz" while sleeping                 |
| Draggable with the mouse            | Allow dragging him                        |
| Sleep when the mouse is idle        | Falls asleep after 30 s of mouse idle     |
| Type on his laptop when I type      | Types while you type                      |
| Bubble with his lines               | Speech bubble while typing                |
| Spider-Man mode (feet on the edges) | Walls and ceiling (screen edges zone)     |
| Collide with other pets             | Bumps into pets that have it too, angry   |

### Custom lyrics

Lemmy's bubble cycles through the Motörhead song titles of
`pets/lemmy/lyrics.txt`: edit it to use your own lines (UTF-8, one line per
bubble, lines starting with `#` are comments).

## Adding a pet

Each pet is a folder `pets/<id>/` holding a `pet.ini` and the PNG sprite
sheets it names; LemmyPet loads them all at startup and reports the broken
ones. `pets/lemmy/pet.ini` uses a grid of cells, `pets/snorlax/pet.ini`
free rectangles.

`tools/import_pmd.py <dex> <id> <name>` builds such a folder from a
[PMDCollab SpriteCollab](https://github.com/PMDCollab/SpriteCollab) sprite
(CC BY-NC 4.0, credits in the generated `credits.txt`). Edit `cries.txt`
for its bubble lines.

```ini
; ';' or '#' start comments.
[pet]
name = Snorlax          ; shown in the settings list (default: the folder name)
sheet = snorlax.png     ; default sheet of every animation
cell = 33               ; frame box in sprite px ("w,h" also works); default: the largest frame
halo = 1                ; default of his "White halo" setting (0: off)
zzz = 22,11             ; where the Z's start in the cell, head on the right
lyrics = growls.txt     ; lines typed in his bubble (none: no bubble)

[walk]                  ; required
cycle = 1,2,3,2         ; frame order (default: each frame in turn)
stand = 2               ; frame shown while idle (default: 1)
S = 145,20,25,27 172,20,28,27 202,20,25,27
W = 145,83,21,31 168,82,21,32 191,83,21,31
```

- **Animations**: `[walk]`, `[sulk]` (cursor came too close), `[hurt]`
  (bumped or dragged), `[sleep]` and `[type]` (laptop). Each one may set its
  own `sheet` and `cycle`. A missing one shows him standing; without
  `[type]` he never types.
- **Facings**: one key per direction, `S SW W NW N NE E SE`, each listing
  frames separated by spaces. Missing facings borrow from the closest one:
  `SW` and `NW` from `W`, then `W` from `E` mirrored, and so on. `[sleep]`
  uses `E` (head on the right) and `W`; `[type]` uses `S`, `W` and `E`.
- **Frames**: `x,y,w,h` (a rectangle of the sheet) or `col,row` (a grid cell,
  needs `cell`). A leading `~` mirrors the frame. Frames are drawn
  bottom-centered in the cell.
- **Scale**: the settings size is a multiple of the sheet pixels (crisp at
  whole multiples).

## Build

Cross-compiled with MinGW-w64 (e.g. from Linux or WSL):

```sh
make
```

Requires `x86_64-w64-mingw32-g++`, and Python 3 with Pillow to regenerate the
assets. Lemmy's sprite sheet `pets/lemmy/lemmy.png` and the icon are rebuilt
from `sprites/` when needed:

| Script                  | Output                                    |
| ----------------------- | ----------------------------------------- |
| `tools/make_sheet.py`   | `pets/lemmy/lemmy.png` (`--preview` for a zoomed copy) |
| `tools/pad_sheet.py`    | `pets/lemmy/lemmy.png` from the hand-edited `lemmy_save.png` |
| `tools/make_icon.py`    | `src/app.ico`                             |
| `tools/make_demo.py`    | `demo.gif` shown above                    |
| `tools/import_pmd.py`   | `pets/<id>/` from a SpriteCollab sprite   |

## License

See [LICENSE](LICENSE).

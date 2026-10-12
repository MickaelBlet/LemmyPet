"""Generate Lemmy animation frames from the base sprite map."""
from pathlib import Path

from PIL import Image

PAL = {
    'K': (0, 0, 0), 'k': (7, 7, 7), 'D': (20, 20, 20), 'd': (27, 27, 27),
    'n': (34, 32, 33), 'h': (39, 34, 30), 'H': (81, 68, 62), 'S': (238, 222, 188),
    's': (194, 181, 148), 'G': (153, 142, 96), 'B': (79, 83, 108), 'b': (68, 73, 93),
    'g': (142, 143, 147), 'm': (67, 67, 67), 'W': (255, 255, 255),
}

BASE = """
.....KKKKK...KKKKK.....
.....KDdDDKKKDDdDK.....
.....KdddDGGGDdddK.....
....KDdddGnGnGdddDK....
....KDddDGGGGGDddDK....
K...KdddDDGDGDDdddK...K
KKKKDddGDDDDDDDGddDKKKK
KKKKDDdnGGGGGGGDdDDKKKK
.KKKDDDGdDDDDDdGDDDKKK.
..KKKKKKKKKKKKKKKKKKK..
..khSSSSSSSSSSSSSSShk..
..khSSSKKSSSSSKKSSShk..
..khHSSKKSSSSSKKSSHhk..
..khHSSSSSSSSSSSSSHhk..
..khHHSSSHHSHHSsSHHhk..
..khKHHSHSSSSSHSsHKhk..
..khKHHSHSSSSSHSHHKhhk.
.khhhhKKHKKKKKHKKhhhhk.
..khkhKBBbSSSbBBKhkhk..
...k.KdbbgSSSbbbdK.k...
....kddBBbdSdbBBddK....
...KSSKBBbdddbBBKSSK...
...KSSKBbgdddbbBKSSK...
....KKKbBbdddbBbKKK....
......KgmgmgmgmgK......
""".split()

LEGS = """
......KDDDDDDDDDK......
......KDDDKKKDDDK......
......KDKK...KKDK......
......KkkK...KkkK......
.......KK.....KK.......
""".split()

LEGS_LEFT_UP = """
......KDDDDDDDDDK......
......KDDDKKKDDDK......
......KkkK...KKDK......
.......KK....KkkK......
...............KK......
""".split()

LEGS_RIGHT_UP = [r[::-1] for r in LEGS_LEFT_UP]

LEGS_CROUCH = """
......KDDDDDDDDDK......
.....KkkKKKKKKKkkK.....
......KK.......KK......
""".split()

LEGS_STRETCH = """
......KDDDDDDDDDK......
......KDDDKKKDDDK......
......KDDK...KDDK......
......KDKK...KKDK......
......KkkK...KkkK......
.......KK.....KK.......
""".split()

LEGS_SPREAD = """
......KDDDDDDDDDK......
.....KDDDKKKKKDDDK.....
.....KDKK.....KKDK.....
.....KkkK.....KkkK.....
......KK.......KK......
""".split()

LEGS_SPREAD_KICK = """
......KDDDDDDDDDK......
.....KDDDKKKKKDDDK.....
.....KkkK.....KKDK.....
......KK......KkkK.....
...............KK......
""".split()

LEGS_SIT = """
......KDDDDDDDDDK......
....KKKDDDDDDDDDKKK....
...KkkkKKKKKKKKKkkkK...
....KKK.........KKK....
""".split()

Z_SMALL = """
WWWW
..W.
.W..
WWWW
""".split()

Z_BIG = """
WWWWW
...W.
..W..
.W...
WWWWW
""".split()


SIZE = 32
OFFSET_X = 4


def grid(rows):
    return [list(r) for r in rows]


def close_eyes(g):
    for x in (7, 8, 14, 15):
        g[11][x] = 'S'


def look_right(g):
    """Shift eyes and mustache one pixel to the right."""
    feats = [(x, y, g[y][x]) for y in range(11, 17) for x in range(7, 16) if g[y][x] != 'S']
    for x, y, _ in feats:
        g[y][x] = 'S'
    for x, y, c in feats:
        g[y][x + 1] = c


def move_hand(g, side, dy):
    """Move a hand up (dy=-1) or down (dy=+1); side is 'L' or 'R'."""
    cols = [3, 4, 5] if side == 'L' else [19, 18, 17]

    def put(y, vals):
        for x, v in zip(cols, vals):
            g[y][x] = v

    hand = [['K', 'S', 'S'], ['K', 'S', 'S'], ['.', 'K', 'K']]
    if dy > 0:
        put(21, ['.', 'K', 'd'])
        for i, row in enumerate(hand):
            put(22 + i, row)
    else:
        put(19, [g[19][cols[0]], 'K', g[19][cols[2]]])
        for i, row in enumerate(hand):
            put(20 + i, row)
        put(23, ['.', '.', '.'])


def raise_arms(g):
    for side in 'LR':
        cols = [3, 4, 5] if side == 'L' else [19, 18, 17]
        for y in (21, 22):
            g[y][cols[0]] = '.'
            g[y][cols[1]] = '.'
            g[y][cols[2]] = 'K'
        g[23][cols[1]] = '.'
        g[23][cols[2]] = 'K'
        m = (lambda x: x) if side == 'L' else (lambda x: 22 - x)
        for x, y, c in [
            (1, 14, 'K'), (2, 14, 'K'),
            (0, 15, 'K'), (1, 15, 'S'), (2, 15, 'S'), (3, 15, 'K'),
            (0, 16, 'K'), (1, 16, 'S'), (2, 16, 'S'), (3, 16, 'K'),
            (1, 17, 'K'), (2, 17, 'd'), (3, 17, 'K'),
            (2, 18, 'K'), (3, 18, 'd'), (4, 18, 'K'),
            (3, 19, 'K'), (4, 19, 'd'), (5, 19, 'K'),
        ]:
            g[y][m(x)] = c


def compose(top, legs, bob=False, extras=()):
    """Stack top + legs, feet on the bottom row. bob drops the first leg row."""
    if bob:
        legs = legs[1:]
    rows = [''.join(r) for r in top] + list(legs)
    canvas = [['.'] * SIZE for _ in range(SIZE)]
    oy = SIZE - len(rows)
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c != '.':
                canvas[oy + y][OFFSET_X + x] = c
    for glyph, gx, gy in extras:
        cells = [(gx + x, gy + y) for y, row in enumerate(glyph) for x, c in enumerate(row) if c != '.']
        for x, y in cells:
            for nx in range(x - 1, x + 2):
                for ny in range(y - 1, y + 2):
                    if 0 <= nx < SIZE and 0 <= ny < SIZE:
                        canvas[ny][nx] = 'K'
        for x, y in cells:
            canvas[y][x] = 'W'
    return canvas


def to_image(canvas):
    img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    px = img.load()
    for y, row in enumerate(canvas):
        for x, c in enumerate(row):
            if c != '.':
                px[x, y] = PAL[c] + (255,)
    return img


def build():
    anims = {}

    blink = grid(BASE)
    close_eyes(blink)
    anims['idle'] = [
        compose(grid(BASE), LEGS),
        compose(grid(BASE), LEGS, bob=True),
        compose(grid(BASE), LEGS),
        compose(blink, LEGS),
    ]

    def walk_top(hand_l=0, hand_r=0):
        g = grid(BASE)
        look_right(g)
        if hand_l:
            move_hand(g, 'L', hand_l)
        if hand_r:
            move_hand(g, 'R', hand_r)
        return g

    anims['walk_right'] = [
        compose(walk_top(), LEGS, bob=True),
        compose(walk_top(hand_l=1, hand_r=-1), LEGS_LEFT_UP),
        compose(walk_top(), LEGS, bob=True),
        compose(walk_top(hand_l=-1, hand_r=1), LEGS_RIGHT_UP),
    ]
    anims['walk_left'] = [[row[::-1] for row in f] for f in anims['walk_right']]

    sleep = grid(BASE)
    close_eyes(sleep)
    anims['sleep'] = [
        compose(sleep, LEGS_SIT, extras=[(Z_SMALL, 27, 7)]),
        compose(sleep, LEGS_SIT, bob=True, extras=[(Z_SMALL, 27, 4)]),
        compose(sleep, LEGS_SIT, extras=[(Z_BIG, 26, 1)]),
        compose(sleep, LEGS_SIT, bob=True),
    ]

    up = grid(BASE)
    raise_arms(up)
    anims['jump'] = [
        compose(grid(BASE), LEGS_CROUCH),
        compose(up, LEGS_STRETCH),
    ]
    anims['fall'] = [
        compose(up, LEGS_SPREAD),
        compose(up, LEGS_SPREAD_KICK),
    ]
    anims['land'] = [compose(grid(BASE), LEGS_CROUCH)]
    return anims


def main():
    out = Path(__file__).resolve().parent.parent / 'sprites'
    out.mkdir(exist_ok=True)
    anims = build()
    cols = max(len(f) for f in anims.values())
    sheet = Image.new('RGBA', (cols * SIZE, len(anims) * SIZE), (0, 0, 0, 0))
    for row, (name, frames) in enumerate(anims.items()):
        strip = Image.new('RGBA', (len(frames) * SIZE, SIZE), (0, 0, 0, 0))
        for i, f in enumerate(frames):
            img = to_image(f)
            img.save(out / f'{name}_{i}.png')
            strip.paste(img, (i * SIZE, 0))
            sheet.paste(img, (i * SIZE, row * SIZE))
        strip.save(out / f'{name}.png')
    sheet.save(out / 'sheet.png')
    print('rows:', ', '.join(f'{n}({len(f)})' for n, f in anims.items()))


if __name__ == '__main__':
    main()

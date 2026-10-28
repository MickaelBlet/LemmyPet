#include "behavior.h"

#include <algorithm>
#include <cmath>

#include "render.h"
#include "typing.h"

namespace {

constexpr UINT kSlowTickMs = 50;  // slowest walking tick
constexpr ULONGLONG kPollMs = 100;  // longest wait between ticks otherwise (cursor checks)
constexpr ULONGLONG kFarPollMs = 500;    // longest wait between cursor checks while it is far
constexpr double kCursorPxPerMs = 4.0;   // fast cursor flick, to space out the checks while it is far
constexpr int kWalkRangeUnits = 200;     // max walk distance per axis
constexpr double kFleeSpeedFactor = 6.0; // min flee speed relative to walk speed
constexpr double kFleeMaxMs = 1500.0;    // runs faster on long flees to reach the corner within this time
constexpr double kNearRadiusUnits = 55.0;
constexpr ULONGLONG kReactDelayMs = 300; // time the cursor may stay near before he reacts
constexpr ULONGLONG kSulkMs = 3000;      // stays angry after reacting, then sleeps
constexpr ULONGLONG kTopmostMs = 2000;
constexpr ULONGLONG kBumpMs = 900;       // dazed after walking into a screen edge
constexpr ULONGLONG kBumpShakeMs = 350;
constexpr int kBumpChancePct = 50;       // chance a walk cut short by an edge ends in a bump
constexpr int kCrossChancePct = 15;      // chance a walk heads to a neighboring screen
constexpr double kBumpKnockUnits = 8.0;  // knockback distance
constexpr DWORD kInputIdleMs = 30000;  // mouse and keyboard stillness before he falls asleep (if enabled)
constexpr ULONGLONG kDragLeanMs = 150;   // drag lean resets after this long without movement
constexpr double kFadedRatio = 0.47;    // opacity multiplier while the cursor is near
constexpr double kFadeSpeed = 1500.0;    // alpha units per second
constexpr double kTurnMs = 350.0;          // Spider-Man mode: time to roll a quarter turn around a corner
constexpr int kTextAheadLines = 8;       // room kept clear right of the caret, in caret heights

BOOL CALLBACK CollectMonitor(HMONITOR mon, HDC, LPRECT, LPARAM param) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) reinterpret_cast<std::vector<MONITORINFO>*>(param)->push_back(mi);
    return TRUE;
}

MONITORINFO CurrentMonitor(Pet& p) {
    const RECT petRc{static_cast<LONG>(p.x), static_cast<LONG>(p.y),
                     static_cast<LONG>(p.x) + p.cellW, static_cast<LONG>(p.y) + p.cellH};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromRect(&petRc, MONITOR_DEFAULTTONEAREST), &mi);
    return mi;
}

// Area his cell may cover on a work area: in the edges zone, standing upright (no Spider-Man), it
// overhangs the left and right edges by the transparent side gap so he touches the walls.
RECT WalkArea(Pet& p, RECT work) {
    if (p.cfg.zone == kEdges && !p.cfg.spiderMan) {
        const int gap = std::max(0, static_cast<int>(std::lround(p.def.sideGap * Scale(p))) - (p.cfg.halo ? 1 : 0));
        work.left -= gap;
        work.right += gap;
    }
    return work;
}

RECT CurrentWalkArea(Pet& p) { return WalkArea(p, CurrentWorkArea(p)); }

// Rolls the shown orientation toward Orientation() around the corner (snaps on a half turn, e.g. a
// teleport, or outside Spider-Man mode).
void StepTurn(Pet& p, double dt) {
    p.orient = Orientation(p);
    double diff = std::fmod(p.orient - p.angle + 6.0, 4.0) - 2.0;  // shortest way, in (-2, 2]
    if (!p.cfg.spiderMan || p.state == State::Drag || std::fabs(diff) > 1.0 + 1e-9) diff = p.orient - p.angle;
    else {
        const double step = dt * 1000.0 / kTurnMs;
        diff = Clamp(diff, -step, step);
    }
    p.angle = std::fmod(p.angle + diff + 4.0, 4.0);
    if (std::fabs(p.angle - p.orient) < 1e-9 || std::fabs(p.angle - p.orient) > 4.0 - 1e-9) p.angle = p.orient;
}

bool Turning(Pet& p) { return p.angle != p.orient; }

// Maps a screen-space vector to a direction (a Facing).
int DirTowards(double dx, double dy) {
    // Octant 0 = east, increasing clockwise (screen y points down); rows start at south.
    const int octant = (static_cast<int>(std::lround(std::atan2(dy, dx) / (kPi / 4))) + 8) % 8;
    return (octant + 6) % 8;
}

// Steps toward (tx, ty); returns true on arrival.
bool MoveToTarget(Pet& p, double step) {
    const double dx = p.tx - p.x, dy = p.ty - p.y;
    const double dist = std::hypot(dx, dy);
    if (dist <= step) {
        p.x = p.tx;
        p.y = p.ty;
        return true;
    }
    p.x += dx / dist * step;
    p.y += dy / dist * step;
    return false;
}

// Whether the user has left mouse and keyboard alone long enough (if enabled).
bool InputIdle(const Pet& p) {
    if (!p.cfg.sleepOnIdle) return false;
    LASTINPUTINFO li{sizeof(li)};
    return GetLastInputInfo(&li) && GetTickCount() - li.dwTime >= kInputIdleMs;
}

// Lies down, head toward the side he was facing (on a wall, always head up).
void EnterSleep(Pet& p, ULONGLONG now) {
    const int orient = Orientation(p);
    const int facing = ViewDir(p);
    const bool right = facing >= kNE, left = facing >= kSW && facing <= kNW;
    p.sleepSide = orient == 1 ? 1 : orient == 3 ? 0 : right ? 0 : (left ? 1 : RandRange(0, 1));
    p.state = State::Sleep;
    p.stateEnd = now + RandRange(Activity(p).sleepMinMs, Activity(p).sleepMaxMs);
    p.sleepStart = now;
}

// The facing he types in: in profile or toward the user.
int TypingView(Pet& p) {
    const int v = ViewDir(p);
    return v == kW || v == kE ? v : kS;
}

struct Shown {
    const Animation* anim;
    ULONGLONG stepMs;  // its frame time, unless it has its own timing
    int facing;
    double rate;       // animation speed, 1 at 100 %
};

// The animation his state shows: the variant picked when the state started.
Shown Showing(Pet& p) {
    const Pace sp = Speed(p);
    int anim = kIdle, facing = ViewDir(p);
    double rate = sp.rate;
    switch (p.state) {
    case State::Idle: break;
    case State::Walk: anim = kWalk, rate = sp.walkRate; break;
    case State::Flee: anim = kWalk, rate = sp.walkRate * 2.0; break;
    case State::Sulk: anim = kSulk; break;
    case State::Sleep: anim = kSleep, facing = p.sleepSide == 0 ? kE : kW; break;
    // Dazed while carried, leaning toward where he is dragged.
    case State::Drag: anim = kHurt, facing = p.dragLean < 0 ? kSW : (p.dragLean > 0 ? kSE : kS); break;
    case State::Bump: anim = kHurt; break;
    case State::Type: anim = kType, facing = TypingView(p); break;
    case State::Play: anim = kPlay; break;
    }
    const std::vector<Animation>& variants = p.def.anims[anim];
    return {&variants[p.variant % variants.size()], kFrameMs[anim], facing, rate};
}

// Animation time played by `now` (ms at 100 % speed), from the start of his state.
ULONGLONG Clock(Pet& p, ULONGLONG now, double rate) { return static_cast<ULONGLONG>((now - p.animStart) * rate); }

// Starts his animation over, picking a variant, when his state changes.
void SyncAnim(Pet& p, ULONGLONG now) {
    if (p.state == p.animState) return;
    p.animState = p.state;
    p.animStart = now;
    p.variant = static_cast<size_t>(std::rand());
    p.turnStart = 0;
}

// Turn on the spot: frame i of facing f of the [turn] animation shows facing f + i (clockwise, like PMD's
// Rotate), so he plays the row he turns from forward, or the one he turns to backward, whichever is shorter.
struct Turn {
    const Animation* anim;
    int steps;         // facings to go through
    bool clockwise;
    double stepMs;     // real time per facing
};

Turn FacingTurn(Pet& p, int from, int to) {
    const std::vector<Animation>& variants = p.def.anims[kTurn];
    const Animation& anim = variants[p.variant % variants.size()];
    const int k = (to - from + kFacingCount) % kFacingCount;
    const size_t frames = std::max<size_t>(1, anim.frames[kS].size());
    const double stepMs = static_cast<double>(anim.Length(kS, kFrameMs[kTurn])) / frames / Speed(p).rate;
    return {&anim, k <= 4 ? k : kFacingCount - k, k <= 4, stepMs};
}

// Still turning toward his walking direction (see ShownFrame).
bool TurningAround(Pet& p, ULONGLONG now) {
    if (!p.turnStart) return false;
    const Turn turn = FacingTurn(p, p.turnFrom, p.turnTo);
    if (now - p.turnStart < turn.steps * turn.stepMs) return true;
    p.turnStart = 0;
    p.lastFacing = p.turnTo;
    return false;
}

const Sprite& TurnFrame(Pet& p, ULONGLONG now) {
    const Turn turn = FacingTurn(p, p.turnFrom, p.turnTo);
    const int i = std::min(turn.steps - 1, static_cast<int>((now - p.turnStart) / turn.stepMs));
    const std::vector<Sprite>& v = turn.anim->frames[turn.clockwise ? p.turnFrom : p.turnTo];
    return v[static_cast<size_t>(turn.clockwise ? i : turn.steps - i) % v.size()];
}

// Plays one of his [play] animations once, where he stands.
void EnterPlay(Pet& p, ULONGLONG now) {
    p.state = State::Play;
    SyncAnim(p, now);
    const Shown shown = Showing(p);
    p.stateEnd = now + static_cast<ULONGLONG>(std::ceil(shown.anim->Length(shown.facing, shown.stepMs) / shown.rate));
}

// Plans a walk onto a random screen next to the current one, through their shared border, so he never
// cuts across the void between screens. In the bottom and edges zones he walks along the floors (from
// the floor only), climbing or dropping at the border when they differ. Returns false when there is
// no such screen.
bool PlanCrossing(Pet& p) {
    std::vector<MONITORINFO> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
    const MONITORINFO cur = CurrentMonitor(p);
    const RECT a = WalkArea(p, cur.rcWork);
    const bool onFloor = p.cfg.zone != kAnywhere;
    if (onFloor && std::fabs(p.y - (a.bottom - p.cellH)) > 0.5) return false;
    std::vector<std::vector<Waypoint>> options;
    for (const MONITORINFO& m : monitors) {
        const RECT b = WalkArea(p, m.rcWork);
        const RECT& ma = cur.rcMonitor;
        const RECT& mb = m.rcMonitor;
        const int side = mb.left == ma.right ? 1 : (mb.right == ma.left ? -1 : 0);
        const int vside = mb.top == ma.bottom ? 1 : (mb.bottom == ma.top ? -1 : 0);
        if (side != 0) {
            // Crosses at a height where he fits on both sides; along the floors, the higher one.
            const int lo = std::max(a.top, b.top), hi = std::min(a.bottom, b.bottom) - p.cellH;
            if (hi < lo) continue;
            const int fromX = side > 0 ? a.right - p.cellW : a.left, toX = side > 0 ? b.left : b.right - p.cellW;
            const int floorA = a.bottom - p.cellH, floorB = b.bottom - p.cellH;
            const int lane = onFloor ? hi : Clamp(static_cast<int>(p.y), lo, hi);
            const Waypoint end{RandRange(b.left, b.right - p.cellW), onFloor ? floorB : RandRange(b.top, floorB), -1};
            // Standing on the floor while stepping over the border, whichever screen he counts as on.
            options.push_back({{fromX, onFloor ? floorA : lane, -1}, {fromX, lane, -1}, {toX, lane, 0},
                               {toX, onFloor ? floorB : lane, -1}, end});
        } else if (vside != 0 && !onFloor) {
            const int lo = std::max(a.left, b.left), hi = std::min(a.right, b.right) - p.cellW;
            if (hi < lo) continue;
            const int lane = Clamp(static_cast<int>(p.x), lo, hi);
            const int fromY = vside > 0 ? a.bottom - p.cellH : a.top, toY = vside > 0 ? b.top : b.bottom - p.cellH;
            const Waypoint end{RandRange(b.left, b.right - p.cellW), RandRange(b.top, b.bottom - p.cellH), -1};
            options.push_back({{lane, fromY, -1}, {lane, toY, -1}, end});
        }
    }
    if (options.empty()) return false;
    p.path.clear();
    POINT last{std::lround(p.x), std::lround(p.y)};
    for (const Waypoint& wp : options[RandRange(0, static_cast<int>(options.size()) - 1)]) {
        if (wp.x != last.x || wp.y != last.y) p.path.push_back(wp);
        last = {wp.x, wp.y};
    }
    if (p.path.empty()) return false;
    p.bumpOnArrival = false;
    p.turnAxis = -1;
    return true;
}

// Heads to the next point of p.path.
void NextWaypoint(Pet& p) {
    p.tx = p.path.front().x;
    p.ty = p.path.front().y;
    p.legOrient = p.path.front().orient;
    p.path.erase(p.path.begin());
    p.dir = DirTowards(p.tx - p.x, p.ty - p.y);
}

// Edges zone: walks along the edge he is on (a random one in a corner), or to a random edge point when
// off the work area. Returns true when instead heading straight to the nearest edge (e.g. after a drag).
bool PickEdgeTarget(Pet& p, int minX, int maxX, int minY, int maxY, int range) {
    const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
    const int turnAxis = p.turnAxis;
    p.turnAxis = -1;
    p.bumpOnArrival = false;
    if (x < minX || x > maxX || y < minY || y > maxY) {
        if (RandRange(0, 1)) {
            p.tx = RandRange(minX, maxX);
            p.ty = RandRange(0, 1) ? minY : maxY;
        } else {
            p.tx = RandRange(0, 1) ? minX : maxX;
            p.ty = RandRange(minY, maxY);
        }
        return false;
    }
    const bool onV = x == minX || x == maxX, onH = y == minY || y == maxY;
    if (!onV && !onH) {
        const int dl = x - minX, dr = maxX - x, dt = y - minY, db = maxY - y;
        const int m = std::min({dl, dr, dt, db});
        p.tx = m == dl ? minX : (m == dr ? maxX : x);
        p.ty = m == dl || m == dr ? y : (m == dt ? minY : maxY);
        return true;
    }
    // After a corner bump he turns onto the other edge, walking away from the corner.
    const bool turning = onH && onV && turnAxis >= 0;
    const bool alongX = turning ? turnAxis == 0 : onH && (!onV || RandRange(0, 1));
    const int delta = turning ? (alongX ? (x == minX ? 1 : -1) : (y == minY ? 1 : -1)) * RandRange(range / 4, range)
                              : RandRange(-range, range);
    const int raw = (alongX ? x : y) + delta;
    const int clamped = alongX ? Clamp(raw, minX, maxX) : Clamp(raw, minY, maxY);
    p.tx = alongX ? clamped : x;
    p.ty = alongX ? y : clamped;
    p.bumpOnArrival = raw != clamped && RandRange(0, 99) < kBumpChancePct;
    return false;
}

// Sulks for a while, facing the user (or the pet he ran into, keeping p.dir), then falls asleep (or
// idles after running into a pet).
void EnterSulk(Pet& p, ULONGLONG now, bool atPet = false) {
    if (!atPet) p.dir = ToScreenDir(p, 0);
    p.petBump = atPet;
    p.state = State::Sulk;
    p.stateEnd = now + kSulkMs;
    if (!atPet) p.nearSince = 0;
}

// Bounces off the screen edge(s) he is touching, dazed, then idles (or sulks after a flee).
void EnterBump(Pet& p, ULONGLONG now, bool sulkAfter) {
    const RECT area = CurrentWalkArea(p);
    const double minX = area.left, maxX = area.right - p.cellW;
    const double minY = area.top, maxY = area.bottom - p.cellH;
    int nx = p.x <= minX + 0.5 ? 1 : (p.x >= maxX - 0.5 ? -1 : 0);
    int ny = p.y <= minY + 0.5 ? 1 : (p.y >= maxY - 0.5 ? -1 : 0);
    // Restricted zones: only bounce along the edge he walks on.
    if (p.cfg.zone == kBottom || (p.cfg.zone == kEdges && (p.dir == 2 || p.dir == 6))) ny = 0;
    else if (p.cfg.zone == kEdges) nx = 0;
    if (nx == 0 && ny == 0) {
        if (sulkAfter) EnterSulk(p, now);
        else EnterIdle(p, now);
        return;
    }
    // In the edges zone he stays in the corner (no knockback) so the next walk can turn.
    const double knock = p.cfg.zone == kEdges ? 0.0 : kBumpKnockUnits * Scale(p);
    if (p.cfg.zone == kEdges) p.turnAxis = nx != 0 ? 1 : 0;
    p.dir = DirTowards(-nx, -ny);  // facing the wall he hit
    p.tx = Clamp(p.x + nx * knock, minX, maxX);
    p.ty = Clamp(p.y + ny * knock, minY, maxY);
    p.state = State::Bump;
    p.bumpStart = now;
    p.stateEnd = now + kBumpMs;
    p.sulkAfterBump = sulkAfter;
    p.petBump = false;
}

// Pets that can run into each other right now (fleeing ones slip past the others).
bool Solid(Pet& p) {
    return p.cfg.collisions && p.cfg.visible && p.state != State::Drag && p.state != State::Bump &&
           p.state != State::Flee;
}

// Bounces off the pet he ran into (or that ran into him), dazed, then sulks at it.
void EnterPetBump(Pet& p, const Pet& other, ULONGLONG now) {
    const RECT area = CurrentWalkArea(p);
    const double minX = area.left, maxX = area.right - p.cellW;
    const double minY = area.top, maxY = area.bottom - p.cellH;
    const double dx = other.x + other.cellW / 2.0 - (p.x + p.cellW / 2.0);
    const double dy = other.y + other.cellH / 2.0 - (p.y + p.cellH / 2.0);
    // Restricted zones: only bounces along the edge he walks on.
    double nx = -dx, ny = -dy;
    if (p.cfg.zone == kBottom || (p.cfg.zone == kEdges && (p.y <= minY + 0.5 || p.y >= maxY - 0.5))) ny = 0;
    else if (p.cfg.zone == kEdges) nx = 0;
    const double len = std::hypot(nx, ny);
    const double knock = len < 0.5 ? 0.0 : kBumpKnockUnits * Scale(p) / len;
    p.path.clear();
    p.dir = DirTowards(dx, dy);  // facing the other pet
    p.tx = Clamp(p.x + nx * knock, minX, maxX);
    p.ty = Clamp(p.y + ny * knock, minY, maxY);
    p.state = State::Bump;
    p.bumpStart = now;
    p.stateEnd = now + kBumpMs;
    p.sulkAfterBump = true;
    p.petBump = true;
}

// While walking into another pet (both with collisions on), both bounce off and get angry.
void CheckCollisions(Pet& p, ULONGLONG now) {
    if (!Solid(p) || p.state != State::Walk) return;
    const RECT body = BodyRect(p);
    for (const auto& o : g.pets) {
        Pet& q = *o;
        if (&q == &p || !Solid(q)) continue;
        const RECT other = BodyRect(q);
        RECT overlap;
        if (!IntersectRect(&overlap, &body, &other)) continue;
        const double dx = q.x + q.cellW / 2.0 - (p.x + p.cellW / 2.0);
        const double dy = q.y + q.cellH / 2.0 - (p.y + p.cellH / 2.0);
        if ((p.tx - p.x) * dx + (p.ty - p.y) * dy <= 0.0) continue;  // walking away from it
        EnterPetBump(p, q, now);
        EnterPetBump(q, p, now);
        ArmTimer(q, kTickMs);  // he may be idling on a slow tick
        return;
    }
}

// Moves to the bottom-right corner, or to the top-left one (bottom-left in the bottom zone) when he would
// still be in the way there.
template <typename InTheWay>
void TeleportToCorner(Pet& p, InTheWay inTheWay) {
    const RECT area = CurrentWalkArea(p);
    p.x = area.right - p.cellW;
    p.y = area.bottom - p.cellH;
    if (inTheWay()) {
        p.x = area.left;
        if (p.cfg.zone != kBottom) p.y = area.top;
    }
}

// Teleports to a corner away from the cursor, then sulks.
void TeleportAway(Pet& p, POINT cursor, ULONGLONG now) {
    TeleportToCorner(p, [&] {
        return std::hypot(cursor.x - (p.x + p.cellW / 2.0), cursor.y - (p.y + p.cellH / 2.0)) <
               kNearRadiusUnits * Scale(p);
    });
    EnterSulk(p, now);
}

// Runs to the work-area corner farthest from `from` (skipping the one he already sits in), sulking on
// arrival unless calm. In the edges zone, only corners reachable along the edge he is on.
void EnterFlee(Pet& p, POINT from, bool calm = false) {
    const RECT area = CurrentWalkArea(p);
    const bool onEdge = p.cfg.zone == kEdges &&
                        (p.x == area.left || p.x == area.right - p.cellW || p.y == area.top ||
                         p.y == area.bottom - p.cellH);
    const double corners[4][2] = {
        {static_cast<double>(area.left), static_cast<double>(area.top)},
        {static_cast<double>(area.right - p.cellW), static_cast<double>(area.top)},
        {static_cast<double>(area.left), static_cast<double>(area.bottom - p.cellH)},
        {static_cast<double>(area.right - p.cellW), static_cast<double>(area.bottom - p.cellH)},
    };
    double best = -1.0;
    for (const auto& c : corners) {
        if (p.cfg.zone == kBottom && c[1] == area.top) continue;
        if (onEdge && c[0] != p.x && c[1] != p.y) continue;
        if (std::hypot(c[0] - p.x, c[1] - p.y) < 10.0 * Scale(p)) continue;
        const double away = std::hypot(c[0] + p.cellW / 2.0 - from.x, c[1] + p.cellH / 2.0 - from.y);
        if (away > best) {
            best = away;
            p.tx = c[0];
            p.ty = c[1];
        }
    }
    p.dir = DirTowards(p.tx - p.x, p.ty - p.y);
    p.fleeSpeed = std::max(Speed(p).walkSpeed * Scale(p) * kFleeSpeedFactor,
                           std::hypot(p.tx - p.x, p.ty - p.y) * 1000.0 / kFleeMaxMs);
    p.state = State::Flee;
    p.calmFlee = calm;
    p.nearSince = 0;
}

void NextState(Pet& p, ULONGLONG now) {
    if (p.state == State::Sleep) {
        p.dir = ToScreenDir(p, 0);
        EnterIdle(p, now);
        return;
    }
    if (InputIdle(p)) {
        EnterSleep(p, now);
        return;
    }
    const ActivityPreset& act = Activity(p);
    if (p.state == State::Idle && p.def.canPlay && RandRange(0, 99) < act.playPct) {
        EnterPlay(p, now);
        return;
    }
    const int r = RandRange(0, 99);
    if (r < act.walkPct) EnterWalk(p, now);
    else if (r < act.walkPct + act.sleepPct) EnterSleep(p, now);
    else if (r < act.walkPct + act.sleepPct + act.typePct && p.def.canType)
        EnterType(p, now, RandRange(act.typeMinMs, act.typeMaxMs));
    else EnterIdle(p, now);
}

// Longest wait before his next tick for the cursor's or the user's sake: none for a cursor he ignores,
// sparser while it is far.
ULONGLONG PollWait(Pet& p, ULONGLONG now) {
    if (p.state == State::Sleep && now >= p.stateEnd) return kPollMs;  // kept asleep by idle input
    if (p.cfg.reaction == kIgnore) return kTopmostMs;
    return Clamp(static_cast<ULONGLONG>(std::max(0.0, p.cursorGap) / kCursorPxPerMs), kPollMs, kFarPollMs);
}

// Time until his next tick: about a sprite pixel per tick while walking, fine while moving fast or fading,
// otherwise until his frame (or the "Zzz", or the bubble) next changes, his state ends, or the cursor is
// due a check.
UINT TickPeriod(Pet& p, ULONGLONG now) {
    if (p.state == State::Walk && p.turnStart) return kTickMs;
    if (p.state == State::Walk) {
        // About one sprite pixel per tick: pixel art moves on its own grid.
        const double px = std::max(1.0, std::round(Scale(p)));
        return Clamp(static_cast<UINT>(1000.0 * px / (Speed(p).walkSpeed * Scale(p))), kTickMs, kSlowTickMs);
    }
    if (p.state == State::Flee || p.state == State::Bump || p.fading || Turning(p)) return kTickMs;
    const Shown shown = Showing(p);
    const ULONGLONG left = shown.anim->Until(shown.facing, Clock(p, now, shown.rate), shown.stepMs);
    const ULONGLONG poll = PollWait(p, now);
    ULONGLONG wait = left == kNever ? poll : std::min(poll, static_cast<ULONGLONG>(std::ceil(left / shown.rate)));
    const auto until = [&](ULONGLONG at) {
        if (at > now) wait = std::min(wait, at - now);
    };
    switch (p.state) {
    case State::Sleep:
        if (p.cfg.zzz) wait = std::min(wait, ZzzStepMs(p) - (now - p.sleepStart) % ZzzStepMs(p));
        break;
    case State::Drag:
        if (p.dragLean) until(p.lastDragMove + kDragLeanMs);
        break;
    case State::Type: wait = std::min(wait, BubbleWait(p, now)); break;
    default: break;
    }
    until(p.stateEnd);
    if (p.nearSince) until(p.nearSince + kReactDelayMs);
    return static_cast<UINT>(std::max<ULONGLONG>(wait, USER_TIMER_MINIMUM));
}

}  // namespace

RECT CurrentWorkArea(Pet& p) { return CurrentMonitor(p).rcWork; }

// Screen area his body covers: the cell minus the transparent room around his standing frame, turned
// with him toward the edge his feet stick to.
RECT BodyRect(Pet& p) {
    const Sprite& s = p.def.Stand(kS);
    const int gap = static_cast<int>(std::lround(p.def.sideGap * Scale(p)));
    const int head = static_cast<int>(std::lround((p.def.cellH - s.lift - (s.h - s.top)) * Scale(p)));
    int inset[4];  // by edge, numbered as orientations: 0 bottom, 1 left, 2 top, 3 right
    inset[p.orient] = 0;
    inset[(p.orient + 2) % 4] = head;
    inset[(p.orient + 1) % 4] = inset[(p.orient + 3) % 4] = gap;
    const LONG x = std::lround(p.x), y = std::lround(p.y);
    return {x + inset[1], y + inset[2], x + p.cellW - inset[3], y + p.cellH - inset[0]};
}

// Spider-Man mode: the edge his feet stick to (see BlitCanvas); anywhere off an edge he stands upright.
// In a corner, the edge he is walking along, else the one he came from (by default bottom corners count
// as floor, top corners as walls).
int Orientation(Pet& p) {
    if (!p.cfg.spiderMan || p.cfg.zone != kEdges || p.state == State::Drag) return 0;
    if (p.state == State::Walk && p.legOrient >= 0) return p.legOrient;
    const RECT area = CurrentWorkArea(p);
    const bool floor = p.y >= area.bottom - p.cellH, ceiling = p.y <= area.top;
    const bool left = p.x <= area.left, right = p.x >= area.right - p.cellW;
    const int h = floor ? 0 : (ceiling ? 2 : -1), v = left ? 1 : (right ? 3 : -1);
    if (h < 0 || v < 0) return h >= 0 ? h : (v >= 0 ? v : 0);
    if (p.state == State::Walk || p.state == State::Flee) {
        if (p.tx == p.x && p.ty != p.y) return v;
        if (p.ty == p.y && p.tx != p.x) return h;
    }
    if (p.orient == h || p.orient == v) return p.orient;
    return floor ? h : v;
}

// p.dir is a screen direction; these convert to and from the sprite row shown once rotated.
int ViewDir(Pet& p) { return (p.dir + 8 - 2 * Orientation(p)) % 8; }
int ToScreenDir(Pet& p, int viewDir) { return (viewDir + 2 * Orientation(p)) % 8; }

void EnterIdle(Pet& p, ULONGLONG now) {
    p.state = State::Idle;
    p.stateEnd = now + RandRange(Activity(p).idleMinMs, Activity(p).idleMaxMs);
}

// Sits down with his laptop for at least durationMs, in profile if he was facing sideways.
void EnterType(Pet& p, ULONGLONG now, ULONGLONG durationMs) {
    if (p.state != State::Type) {
        p.stateEnd = 0;
        const int v = ViewDir(p);
        p.dir = ToScreenDir(p, v >= 1 && v <= 3 ? 2 : (v >= 5 ? 6 : 0));
    }
    p.state = State::Type;
    p.stateEnd = std::max(p.stateEnd, now + durationMs);
}

void EnterWalk(Pet& p, ULONGLONG now) {
    p.path.clear();
    p.legOrient = -1;
    if (RandRange(0, 99) < kCrossChancePct && PlanCrossing(p)) {
        NextWaypoint(p);
        p.state = State::Walk;
        return;
    }
    const RECT area = CurrentWalkArea(p);
    const int range = static_cast<int>(kWalkRangeUnits * Scale(p));
    const int minX = area.left, maxX = area.right - p.cellW;
    const int maxY = area.bottom - p.cellH, minY = p.cfg.zone == kBottom ? maxY : area.top;
    bool toEdge = false;
    if (p.cfg.zone == kEdges) {
        toEdge = PickEdgeTarget(p, minX, maxX, minY, maxY, range);
    } else {
        const bool bottomOnly = p.cfg.zone == kBottom;
        const int cx = Clamp(static_cast<int>(p.x), minX, maxX);
        const int cy = Clamp(static_cast<int>(p.y), minY, maxY);
        // In bottom-only mode, being above the bottom edge (e.g. after a drag) still counts as on the area.
        const bool onArea = cx == static_cast<int>(p.x) && (bottomOnly || cy == static_cast<int>(p.y));
        const int rawX = cx + RandRange(-range, range), rawY = cy + RandRange(-range, range);
        p.tx = onArea ? Clamp(rawX, minX, maxX) : RandRange(minX, maxX);
        p.ty = onArea ? Clamp(rawY, minY, maxY) : RandRange(minY, maxY);
        p.bumpOnArrival = onArea && (rawX != p.tx || (!bottomOnly && rawY != p.ty)) &&
                          RandRange(0, 99) < kBumpChancePct;
    }

    const double dx = p.tx - p.x, dy = p.ty - p.y;
    // Even a short hop back onto an edge is taken, or he would never leave a spot just off it.
    if (!toEdge && std::hypot(dx, dy) < 10.0 * Scale(p)) {
        EnterIdle(p, now);
        return;
    }
    p.dir = DirTowards(dx, dy);
    p.state = State::Walk;
}

const Sprite& ShownFrame(Pet& p, ULONGLONG now) {
    SyncAnim(p, now);
    // Setting off in another direction: turns on the spot first (the walk waits, see Tick).
    if (p.state == State::Walk && p.def.canTurn && !p.turnStart && ViewDir(p) != p.lastFacing) {
        p.turnFrom = p.lastFacing;
        p.turnTo = ViewDir(p);
        p.turnStart = now;
    }
    if (p.turnStart && TurningAround(p, now)) return TurnFrame(p, now);
    const Shown shown = Showing(p);
    if (p.state != State::Sleep && p.state != State::Drag) p.lastFacing = shown.facing;
    return shown.anim->Frame(shown.facing, Clock(p, now, shown.rate), shown.stepMs);
}

void Tick(Pet& p) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsed = now - p.lastTick;
    const double dt = (elapsed > 100 ? 100 : elapsed) / 1000.0;
    p.lastTick = now;
    const double walkSpeed = Speed(p).walkSpeed * Scale(p);

    POINT cursor;
    GetCursorPos(&cursor);
    const double cdx = cursor.x - (p.x + p.cellW / 2.0);
    const double cdy = cursor.y - (p.y + p.cellH / 2.0);
    p.cursorGap = std::hypot(cdx, cdy) - kNearRadiusUnits * Scale(p);
    const bool cursorNear = p.cursorGap < 0.0;
    // In draggable mode, hovering the sprite itself never scares him, so he can be grabbed.
    const bool hovered = cursorNear && p.cfg.draggable && WindowFromPoint(cursor) == p.hwnd;
    // Being dazed or sulking never holds him back from reacting to the cursor.
    const auto wary = [&p, hovered] {
        return p.cfg.reaction != kIgnore && !hovered && p.state != State::Flee && p.state != State::Drag;
    };

    if (!cursorNear) {
        p.nearSince = 0;
    } else if (wary()) {
        if (p.nearSince == 0) p.nearSince = now;
        else if (now - p.nearSince >= kReactDelayMs) {
            p.shakeX = 0;
            p.petBump = false;
            if (p.cfg.reaction == kTeleport) TeleportAway(p, cursor, now);
            else EnterFlee(p, cursor);
        }
    }

    switch (p.state) {
    case State::Walk:
        if (Turning(p) || TurningAround(p, now)) break;  // rolls around the corner / turns before walking on
        if (MoveToTarget(p, walkSpeed * dt)) {
            if (!p.path.empty()) NextWaypoint(p);
            else if (p.bumpOnArrival) EnterBump(p, now, false);
            else if (!InputIdle(p) && RandRange(0, 99) < Activity(p).chainWalkPct) EnterWalk(p, now);
            else EnterIdle(p, now);
        }
        break;
    case State::Flee:
        if (Turning(p)) break;
        if (MoveToTarget(p, p.fleeSpeed * dt)) EnterBump(p, now, !p.calmFlee);
        break;
    case State::Bump:
        MoveToTarget(p, kBumpKnockUnits * Scale(p) * 5.0 * dt);  // quick hop back, ~0.2 s
        p.shakeX = now - p.bumpStart < kBumpShakeMs
                       ? ((now / 40) % 2 ? 1 : -1) * std::max(1, static_cast<int>(std::lround(Scale(p))))
                       : 0;
        if (now >= p.stateEnd) {
            p.shakeX = 0;
            if (p.sulkAfterBump) EnterSulk(p, now, p.petBump);
            else EnterIdle(p, now);
        }
        break;
    case State::Sulk:
        if (now >= p.stateEnd) {
            if (p.petBump) EnterIdle(p, now);
            else EnterSleep(p, now);
        }
        break;
    case State::Drag:
        if (now - p.lastDragMove >= kDragLeanMs) p.dragLean = 0;
        break;
    default:
        // While mouse and keyboard rest (if enabled), he keeps sleeping until either is used again.
        if (now >= p.stateEnd && !(p.state == State::Sleep && InputIdle(p))) NextState(p, now);
        break;
    }
    CheckCollisions(p, now);

    // Fades while the cursor lingers nearby; fully opaque again once reacting or alone.
    const double baseAlpha = 255.0 * kOpacityPct[p.cfg.opacity] / 100.0;
    const double targetAlpha = cursorNear && wary() ? baseAlpha * kFadedRatio : baseAlpha;
    const double fade = kFadeSpeed * dt;
    p.alpha = p.alpha < targetAlpha ? std::min(p.alpha + fade, targetAlpha) : std::max(p.alpha - fade, targetAlpha);
    p.fading = p.alpha != targetAlpha;

    StepTurn(p, dt);
    Render(p, ShownFrame(p, now), now);
    UpdateBubble(p, now);

    if (now - p.lastTopmost >= kTopmostMs) {
        p.lastTopmost = now;
        for (HWND hwnd : {p.hwnd, p.bubble})
            if (hwnd != p.bubble || p.bubbleShown)
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    const UINT period = TickPeriod(p, now);
    if (period != p.tickPeriod) ArmTimer(p, period);
}

// (Re)starts his tick timer. The system may delay it a little to group wakeups with other timers.
void ArmTimer(Pet& p, UINT period) {
    p.tickPeriod = period;
    SetCoalescableTimer(p.hwnd, kTimerId, period, nullptr, period / 8);
}

// Flees from `zone`, or teleports to a corner carrying on with what he was doing (walks end there), when
// he or his bubble covers it. Returns whether he moved.
bool MoveOutOfTheWay(Pet& p, const RECT& zone, ULONGLONG now) {
    if (p.state == State::Drag || p.state == State::Flee) return false;
    // The bubble keeps its place above his head as he moves (near enough, it is kept on screen).
    const POINT bubbleOff{p.bubblePos.x - std::lround(p.x), p.bubblePos.y - std::lround(p.y)};
    const auto inTheWay = [&] {
        RECT hit;
        const RECT body = BodyRect(p);
        if (IntersectRect(&hit, &body, &zone)) return true;
        if (!p.bubbleShown) return false;
        const LONG bx = std::lround(p.x) + bubbleOff.x, by = std::lround(p.y) + bubbleOff.y;
        const RECT bubble{bx, by, bx + p.bubbleW, by + p.bubbleH};
        return IntersectRect(&hit, &bubble, &zone) != FALSE;
    };
    if (!inTheWay()) return false;
    p.shakeX = 0;
    p.petBump = false;
    p.nearSince = 0;
    if (p.cfg.reaction == kFlee) {
        EnterFlee(p, {(zone.left + zone.right) / 2, (zone.top + zone.bottom) / 2}, true);
        return true;
    }
    TeleportToCorner(p, inTheWay);
    p.path.clear();
    if (p.state == State::Walk || p.state == State::Bump) EnterIdle(p, now);
    return true;
}

// Gets out of the way of the caret and of the room right of it.
void AvoidText(Pet& p, const RECT& caret, ULONGLONG now) {
    if (!p.cfg.visible || !p.cfg.avoidText) return;
    const LONG line = std::max<LONG>(1, caret.bottom - caret.top);
    MoveOutOfTheWay(p, {caret.left - line, caret.top - line, caret.right + kTextAheadLines * line, caret.bottom + line},
                    now);
}

// Changes the display scale, keeping his feet where they were.
void ApplyScale(Pet& p, int scalePct) {
    const double footX = p.x + p.cellW / 2.0, footY = p.y + p.cellH;
    p.cfg.scale = scalePct;
    BuildScaledAssets(p);
    const RECT area = CurrentWalkArea(p);
    p.x = Clamp(footX - p.cellW / 2.0, static_cast<double>(area.left), static_cast<double>(area.right - p.cellW));
    p.y = Clamp(footY - p.cellH, static_cast<double>(area.top), static_cast<double>(area.bottom - p.cellH));
    if (p.state == State::Walk || p.state == State::Flee) EnterIdle(p, GetTickCount64());
}

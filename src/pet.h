// Shared state of the pets and the app, plus the presets their settings pick from.
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "petdef.h"

constexpr double kPi = 3.14159265358979323846;
constexpr UINT kTickMs = 16;      // timer period while moving or fading
constexpr UINT_PTR kTimerId = 1;
constexpr int kZzzRiseUnits = 33;       // sprite px a "Z" rises over its life

struct Pace {
    double walkSpeed;       // sprite px per second
    double walkRate, rate;  // animation speed of walking and of everything else, 1 at 100 %
    ULONGLONG zzzLifeMs;
};
constexpr double kBaseWalkSpeed = 30.0;  // at 100 % speed
constexpr ULONGLONG kBaseZzzLifeMs = 2400;
// Frame time at 100 % speed of the animations without their own timing, by Anim.
inline constexpr ULONGLONG kFrameMs[kAnimCount] = {150, 250, 250, 250, 700, 110, 150, 40};
// Speed setting, in percent of the base pace.
constexpr int kMinSpeedPct = 20, kMaxSpeedPct = 400, kSpeedStepPct = 5, kDefaultSpeedPct = 100;

struct ActivityPreset {
    const wchar_t* name;
    int idleMinMs, idleMaxMs;
    int walkPct, sleepPct, typePct;  // chances after an idle pause (the rest: idle again)
    int playPct;            // chance to play a [play] animation instead, if he has some
    int sleepMinMs, sleepMaxMs;
    int typeMinMs, typeMaxMs;
    int chainWalkPct;       // chance to start a new walk right after arriving
};
inline constexpr ActivityPreset kActivities[] = {
    {L"Lazy", 2000, 5000, 50, 30, 10, 8, 15000, 40000, 5000, 15000, 20},
    {L"Normal", 600, 1500, 80, 8, 7, 12, 10000, 30000, 5000, 12000, 50},
    {L"Hyperactive", 200, 700, 88, 3, 6, 15, 6000, 15000, 4000, 8000, 75},
};

enum Reaction { kTeleport, kFlee, kIgnore };
enum Zone { kAnywhere, kEdges, kBottom };
inline constexpr int kOpacityPct[] = {100, 80, 60, 40, 20};
// Size setting, in percent of the sheet pixels. Integer scales keep pixels crisp; others use slightly
// uneven nearest-neighbor pixels.
constexpr int kMinScalePct = 50, kMaxScalePct = 800, kScaleStepPct = 5, kDefaultScalePct = 200;

struct Settings {
    int scale = kDefaultScalePct;  // percent of the sheet pixels
    int speed = kDefaultSpeedPct;  // percent of the base pace (see Speed)
    int activity = 1;  // index in kActivities
    int reaction = kTeleport;
    int opacity = 0;   // index in kOpacityPct
    bool zzz = true;
    bool draggable = false;
    bool sleepOnIdle = false;  // sleeps while mouse and keyboard are not used
    bool typeWithUser = true;       // types on his laptop while the user types
    bool keyBubble = true;          // types his lines in a bubble while doing so (never the real keys)
    int zone = kAnywhere;  // where on the work area he may walk
    bool spiderMan = false;  // edges zone: keeps his feet on the edge he walks on (walls, ceiling)
    bool halo = true;        // 1 px white halo around him (default: his pet.ini)
    bool collisions = false; // bumps into the other pets that have it too, and gets angry
    bool avoidText = true;   // moves away from the text caret he or his bubble covers while the user types
    bool avoidChanges = false;  // moves away when what is behind him changes (hides him from screen captures)
    bool visible = true;
};

enum class State { Idle, Walk, Sleep, Flee, Sulk, Drag, Bump, Type, Play };

struct Waypoint {
    int x, y;
    int orient;  // Orientation() forced on the way there, or -1
};

struct Pet {
    explicit Pet(const PetDef& d) : def(d) {}

    const PetDef& def;
    HWND hwnd = nullptr;
    HDC memDc = nullptr;
    HBITMAP dib = nullptr;
    uint32_t* dibBits = nullptr;
    int dibSize = 0;  // square back buffer side, fits the canvas in any orientation
    Pixels canvas;    // winW x winH, upright; rotated into the back buffer
    std::unordered_map<const Sprite*, Pixels> frames;  // cellW x cellH scaled frames (see DrawFrame)
    size_t framesBytes = 0;

    Settings cfg;
    int cellW = 0, cellH = 0, padX = 0, padTop = 0, winW = 0, winH = 0;
    const Sprite* shown = nullptr;
    POINT shownPos{INT_MIN, INT_MIN};
    BYTE shownAlpha = 0;
    long long shownOverlay = -1;
    double shownAngle = -1.0;
    double alpha = 255.0;

    double x = 0, y = 0;    // sprite cell top-left (the window extends by padX/padTop)
    double tx = 0, ty = 0;  // walk target
    int legOrient = -1;     // Orientation() forced while walking to (tx, ty), or -1
    int orient = 0;         // Orientation() as of the last tick (the edge he keeps in a corner)
    double angle = 0.0;     // shown orientation in quarter turns, [0, 4); eases toward Orientation()
    std::vector<Waypoint> path;  // targets after the current one (crossing to another screen)
    int dir = 0;            // screen direction, a Facing
    State state = State::Idle;
    State animState = State::Idle;  // state the current animation variant was picked for
    ULONGLONG animStart = 0;        // animations start over with each state
    size_t variant = 0;             // of the state's animation
    int lastFacing = kS;            // facing last shown, standing or walking
    int turnFrom = kS, turnTo = kS; // facings of the turn on the spot before walking (see TurnFrame)
    ULONGLONG turnStart = 0;        // 0 when not turning
    ULONGLONG stateEnd = 0;
    ULONGLONG sleepStart = 0;
    int sleepSide = 0;      // 0 head on the right, 1 head on the left
    ULONGLONG lastTick = 0;
    UINT tickPeriod = 0;
    bool fading = false;    // alpha still heading to its target
    ULONGLONG lastTopmost = 0;
    ULONGLONG nearSince = 0;  // 0 while the cursor is away
    double cursorGap = 0.0;   // screen px from the cursor to his reaction radius, as of the last tick
    bool bumpOnArrival = false;  // current walk ends against a screen edge
    bool sulkAfterBump = false;
    bool petBump = false;        // current bump or sulk comes from running into another pet
    int turnAxis = -1;           // edges zone: axis (0 = x, 1 = y) of the next walk after a corner bump
    double fleeSpeed = 0.0;      // screen px per second of the current flee
    bool calmFlee = false;       // current flee dodges the text being typed: ends idle, not sulking
    ULONGLONG bumpStart = 0;
    int shakeX = 0;              // render-only horizontal jitter
    POINT dragOffset{};       // cursor position relative to the sprite cell while dragging
    int dragLean = 0;         // -1 left, 0 none, 1 right
    ULONGLONG lastDragMove = 0;
    ULONGLONG lastKeystroke = 0;

    HWND bubble = nullptr;
    HDC bubbleDc = nullptr;
    HBITMAP bubbleDib = nullptr;
    uint32_t* bubbleBits = nullptr;
    HFONT bubbleFont = nullptr;
    int bubbleW = 0, bubbleH = 0;
    POINT bubbleTip{};         // tail tip in the bubble bitmap
    int bubbleSide = 0;        // side its tail is drawn on (see DrawBubble)
    size_t lyric = 0;          // current line in def.lyrics
    size_t lyricPos = 0;       // characters of it typed so far
    std::wstring typed;        // end of the current line typed so far, shown in the bubble
    std::wstring bubbleText;   // text currently drawn in the bubble bitmap
    int bubbleDots = -1;       // lit dots drawn in the bubble bitmap, -1 when it shows text
    ULONGLONG dotsStart = 0, dotsUntil = 0;  // "..." bubble after Enter (as if waiting for a reply)
    bool bubbleStale = true;   // bitmap must be redrawn (e.g. after a scale change)
    POINT bubblePos{INT_MIN, INT_MIN};
    BYTE bubbleAlpha = 0;
    bool bubbleShown = false;

    bool hiddenFromCapture = false;  // screen captures see through his windows (see UpdateBehindWatch)
    HDC behindDc = nullptr;
    HBITMAP behindDib = nullptr;
    uint32_t* behindBits = nullptr;
    int behindW = 0, behindH = 0;
    std::vector<uint32_t> behindLast;  // last sample of what is behind his body
    RECT behindRect{};                 // screen area of that sample; empty: none yet
    ULONGLONG behindChangeAt = 0;      // last change seen behind him, 0 when none lately
};

struct App {
    HWND tray = nullptr;
    HICON icon = nullptr;
    NOTIFYICONDATAW nid{};
    UINT taskbarCreatedMsg = 0;
    bool keyDown[256] = {};
    std::vector<std::unique_ptr<PetDef>> defs;
    std::vector<std::unique_ptr<Pet>> pets;
    std::map<std::wstring, unsigned long long> stamps;  // of the pets/ folders, as of the last scan
    bool locked = false;      // session locked
    bool displayOff = false;  // console display off
    bool Paused() const { return locked || displayOff; }  // nothing to see: the pets stop ticking
};

inline double Scale(Pet& p) { return p.cfg.scale / 100.0; }
// Walking scales linearly with the speed (no sliding feet); the other animations by its square root, so
// they stay readable at the extremes.
inline Pace Speed(Pet& p) {
    const double f = p.cfg.speed / 100.0, r = std::sqrt(f);
    return {kBaseWalkSpeed * f, f, r, static_cast<ULONGLONG>(std::max(1.0, kBaseZzzLifeMs / r))};
}
// "Zzz" overlay redraw period: about one sprite pixel of rise.
inline ULONGLONG ZzzStepMs(Pet& p) { return std::max<ULONGLONG>(1, Speed(p).zzzLifeMs / kZzzRiseUnits); }
inline const ActivityPreset& Activity(Pet& p) { return kActivities[p.cfg.activity]; }

inline int RandRange(int lo, int hi) {
    return hi <= lo ? lo : lo + std::rand() % (hi - lo + 1);
}

template <typename T>
inline T Clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

extern App g;

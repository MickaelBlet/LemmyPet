// LemmyPet: a click-through, non-focusable Lemmy wandering the desktop,
// controlled from a notification-area icon.

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "resource.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
// Layout in sprite pixels; multiplied by the display scale.
constexpr int kCellUnits = 45;    // frame cell of lemmy.png (tools/make_sheet.py)
constexpr int kPadXUnits = 12;    // transparent room around the cell for the floating "Zzz"
constexpr int kPadTopUnits = 23;
constexpr UINT kTickMs = 16;      // timer period while moving or fading
constexpr UINT kSlowTickMs = 50;  // otherwise (animation frames last longer anyway)
constexpr UINT_PTR kTimerId = 1;
constexpr int kWalkRangeUnits = 200;     // max walk distance per axis
constexpr double kFleeSpeedFactor = 6.0; // flee speed relative to walk speed
constexpr double kNearRadiusUnits = 55.0;
constexpr ULONGLONG kReactDelayMs = 300; // time the cursor may stay near before he reacts
constexpr ULONGLONG kSulkMs = 3000;      // stays angry after reacting, then sleeps
constexpr ULONGLONG kTopmostMs = 2000;
constexpr ULONGLONG kBumpMs = 900;       // dazed after walking into a screen edge
constexpr ULONGLONG kBumpShakeMs = 350;
constexpr int kBumpChancePct = 50;       // chance a walk cut short by an edge ends in a bump
constexpr int kCrossChancePct = 15;      // chance a walk heads to a neighboring screen
constexpr double kBumpKnockUnits = 8.0;  // knockback distance
constexpr ULONGLONG kMouseIdleMs = 30000;  // cursor stillness before he falls asleep (if enabled)
constexpr ULONGLONG kDragLeanMs = 150;   // drag lean resets after this long without movement
constexpr ULONGLONG kZzzStepMs = 40;     // overlay redraw granularity
constexpr int kZzzCount = 3;
constexpr double kFadedRatio = 0.47;    // opacity multiplier while the cursor is near
constexpr double kFadeSpeed = 1500.0;    // alpha units per second
constexpr ULONGLONG kTypeLingerMs = 2000;  // keeps typing this long after the user's last keystroke
constexpr ULONGLONG kTypeBurstMs = 1500;   // two keystrokes this close start a typing session
constexpr ULONGLONG kBubbleLingerMs = 5000;  // bubble text stays this long after the last keystroke
constexpr size_t kBubbleMaxChars = 12;     // last characters of the current line shown in the bubble
constexpr int kBubbleFontUnits = 4;
constexpr ULONGLONG kDotsMs = 10000;       // "..." bubble shown after each finished line
constexpr ULONGLONG kDotsStepMs = 300;     // one more dot lit per step, then none
constexpr int kHeadTopUnits = 3;           // first sprite row of his hat in a standing cell
constexpr UINT WM_APP_TRAY = WM_APP + 1;

constexpr UINT IDM_TOGGLE = 1;
constexpr UINT IDM_QUIT = 2;
constexpr UINT IDM_STARTUP = 3;
constexpr UINT IDM_ZZZ = 4;
constexpr UINT IDM_DRAG = 5;
constexpr UINT IDM_MOUSESLEEP = 6;
constexpr UINT IDM_TYPEWITHUSER = 7;
constexpr UINT IDM_BUBBLE = 8;
constexpr UINT IDM_SPIDERMAN = 9;
constexpr UINT IDM_SCALE = 100;     // + kScales index
constexpr UINT IDM_SPEED = 200;     // + Speed index
constexpr UINT IDM_ACTIVITY = 300;  // + Activity index
constexpr UINT IDM_REACTION = 400;  // + Reaction index
constexpr UINT IDM_OPACITY = 500;   // + kOpacityPct index
constexpr UINT IDM_ZONE = 600;      // + Zone index

const wchar_t kSettingsKey[] = L"Software\\LemmyPet";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"LemmyPet";
const wchar_t kLyricsFile[] = L"lyrics.txt";  // next to the exe: one line per bubble, UTF-8, '#' for comments
// Built-in lines when there is no lyrics file: Motorhead song titles.
const wchar_t* const kDefaultLyrics[] = {
    L"Ace of Spades", L"Overkill", L"Bomber", L"Iron Fist", L"Killed by Death", L"Orgasmatron",
    L"Born to Raise Hell", L"Rock 'n' Roll", L"Eat the Rich", L"Hellraiser", L"Stay Clean", L"No Class",
    L"Damage Case", L"Metropolis", L"We Are Mot\u00f6rhead", L"Deaf Forever", L"Built for Speed", L"Sacrifice",
    L"1916", L"Love Me Forever", L"The Chase Is Better Than the Catch", L"Dancing on Your Grave",
    L"Stone Dead Forever", L"Mot\u00f6rhead",
};

struct SpeedPreset {
    const wchar_t* name;
    double walkSpeed;  // sprite px per second
    ULONGLONG walkFrameMs, sleepFrameMs, sulkFrameMs, typeFrameMs, zzzLifeMs;
};
const SpeedPreset kSpeeds[] = {
    {L"Slow", 18.0, 220, 900, 350, 160, 3200},
    {L"Normal", 30.0, 150, 700, 250, 110, 2400},
    {L"Fast", 50.0, 100, 500, 160, 70, 1800},
};

struct ActivityPreset {
    const wchar_t* name;
    int idleMinMs, idleMaxMs;
    int walkPct, sleepPct, typePct;  // chances after an idle pause (the rest: idle again)
    int sleepMinMs, sleepMaxMs;
    int typeMinMs, typeMaxMs;
    int chainWalkPct;       // chance to start a new walk right after arriving
};
const ActivityPreset kActivities[] = {
    {L"Lazy", 2000, 5000, 50, 30, 10, 15000, 40000, 5000, 15000, 20},
    {L"Normal", 600, 1500, 80, 8, 7, 10000, 30000, 5000, 12000, 50},
    {L"Hyperactive", 200, 700, 88, 3, 6, 6000, 15000, 4000, 8000, 75},
};

enum Reaction { kTeleport, kFlee, kIgnore };
const wchar_t* const kReactionNames[] = {L"Teleport to bottom-right",
                                         L"Flee to a corner", L"Ignore it"};
enum Zone { kAnywhere, kEdges, kBottom };
const wchar_t* const kZoneNames[] = {L"Anywhere", L"Screen edges", L"Bottom of screen"};
const int kOpacityPct[] = {100, 80, 60, 40, 20};
const wchar_t* const kOpacityNames[] = {L"100% (opaque)", L"80%", L"60%", L"40%", L"20%"};
// Integer scales keep pixels crisp; half steps use slightly uneven nearest-neighbor pixels.
const double kScales[] = {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0};
const wchar_t* const kScaleNames[] = {L"x1", L"x1.5", L"x2", L"x2.5", L"x3", L"x4", L"x5", L"x6"};
constexpr int kDefaultScale = 2;  // x2

// lemmy.png is a grid of kCellUnits cells (see tools/make_sheet.py for the row layout).
enum View { kFront, kLeft, kBack, kRight };
constexpr int kWalkRow = 0, kSulkRow = 4, kHurtRow = 8, kSleepRow = 9, kTypingRow = 10;
// View drawn for each direction row: S, SW, W, NW, N, NE, E, SE.
const int kDirView[8] = {kFront, kLeft, kLeft, kLeft, kBack, kRight, kRight, kRight};
const int kWalkCycle[4] = {0, 1, 2, 1};

// A sprite frame: a cell of lemmy.png, scaled on the fly when shown (no per-scale frame cache).
using Frame = int;
constexpr Frame CellFrame(int col, int row) { return row * 16 + col; }
Frame WalkFrame(int view, int f) { return CellFrame(f, kWalkRow + view); }  // frame 1 = standing
Frame SulkFrame(int view, int f) { return CellFrame(f, kSulkRow + view); }
Frame HurtFrame(int view) { return CellFrame(view, kHurtRow); }
Frame SleepFrame(int side, int f) { return CellFrame(2 * side + f, kSleepRow); }  // side 0: head on the right
Frame TypingFrame(int view, int f) { return CellFrame(f, kTypingRow + view); }   // front, facing left, facing right

using Pixels = std::vector<uint32_t>;  // premultiplied BGRA, top-down

struct Sheet {
    int w = 0;
    int h = 0;
    Pixels px;
};

struct Settings {
    int scale = kDefaultScale;  // index in kScales
    int speed = 1;     // index in kSpeeds
    int activity = 1;  // index in kActivities
    int reaction = kTeleport;
    int opacity = 0;   // index in kOpacityPct
    bool zzz = true;
    bool draggable = false;
    bool sleepOnMouseIdle = false;  // sleeps while the mouse does not move
    bool typeWithUser = true;       // types on his laptop while the user types
    bool keyBubble = true;          // types Lemmy lines in a bubble while doing so (never the real keys)
    int zone = kAnywhere;  // where on the work area he may walk
    bool spiderMan = false;  // edges zone: keeps his feet on the edge he walks on (walls, ceiling)
};

enum class State { Idle, Walk, Sleep, Flee, Sulk, Drag, Bump, Type };

struct Waypoint {
    int x, y;
    int orient;  // Orientation() forced on the way there, or -1
};

struct App {
    HWND pet = nullptr;
    HWND tray = nullptr;
    HDC memDc = nullptr;
    HBITMAP dib = nullptr;
    uint32_t* dibBits = nullptr;
    int dibSize = 0;  // square back buffer side, fits the canvas in any orientation
    Pixels canvas;    // winW x winH, upright; rotated into the back buffer
    HICON icon = nullptr;
    NOTIFYICONDATAW nid{};
    UINT taskbarCreatedMsg = 0;

    Settings cfg;
    Sheet sheet;
    int cellW = 0, cellH = 0, padX = 0, padTop = 0, winW = 0, winH = 0;
    std::vector<int> cellSrc;  // sheet cell coordinate for each scaled cell coordinate (nearest neighbor)
    Frame shown = -1;
    POINT shownPos{INT_MIN, INT_MIN};
    BYTE shownAlpha = 0;
    long long shownOverlay = -1;
    int shownOrient = -1;
    double alpha = 255.0;

    double x = 0, y = 0;    // sprite cell top-left (the window extends by padX/padTop)
    double tx = 0, ty = 0;  // walk target
    int legOrient = -1;     // Orientation() forced while walking to (tx, ty), or -1
    std::vector<Waypoint> path;  // targets after the current one (crossing to another screen)
    int dir = 0;            // direction, index in kDirView
    State state = State::Idle;
    ULONGLONG stateEnd = 0;
    ULONGLONG sleepStart = 0;
    int sleepSide = 0;      // 0 head on the right, 1 head on the left
    ULONGLONG lastTick = 0;
    UINT tickPeriod = 0;
    bool fading = false;    // alpha still heading to its target
    ULONGLONG lastTopmost = 0;
    ULONGLONG nearSince = 0;  // 0 while the cursor is away
    POINT lastCursor{INT_MIN, INT_MIN};
    ULONGLONG lastCursorMove = 0;
    bool bumpOnArrival = false;  // current walk ends against a screen edge
    bool sulkAfterBump = false;
    int turnAxis = -1;           // edges zone: axis (0 = x, 1 = y) of the next walk after a corner bump
    ULONGLONG bumpStart = 0;
    int shakeX = 0;              // render-only horizontal jitter
    POINT dragOffset{};       // cursor position relative to the sprite cell while dragging
    int dragLean = 0;         // -1 left, 0 none, 1 right
    ULONGLONG lastDragMove = 0;
    bool keyDown[256] = {};
    ULONGLONG lastKeystroke = 0;

    HWND bubble = nullptr;
    HDC bubbleDc = nullptr;
    HBITMAP bubbleDib = nullptr;
    uint32_t* bubbleBits = nullptr;
    HFONT bubbleFont = nullptr;
    int bubbleW = 0, bubbleH = 0;
    POINT bubbleTip{};         // tail tip in the bubble bitmap
    int bubbleSide = 0;        // side its tail is drawn on (see DrawBubble)
    std::vector<std::wstring> lyrics;  // lines typed in the bubble (kLyricsFile or kDefaultLyrics)
    size_t lyric = 0;          // current line in lyrics
    size_t lyricPos = 0;       // characters of it typed so far
    std::wstring typed;        // end of the current line typed so far, shown in the bubble
    std::wstring bubbleText;   // text currently drawn in the bubble bitmap
    int bubbleDots = -1;       // lit dots drawn in the bubble bitmap, -1 when it shows text
    ULONGLONG dotsStart = 0, dotsUntil = 0;  // "..." bubble after Enter (as if waiting for a reply)
    bool bubbleStale = true;   // bitmap must be redrawn (e.g. after a scale change)
    POINT bubblePos{INT_MIN, INT_MIN};
    BYTE bubbleAlpha = 0;
    bool bubbleShown = false;
    bool visible = false;
};

App g;

double Scale() { return kScales[g.cfg.scale]; }
const SpeedPreset& Speed() { return kSpeeds[g.cfg.speed]; }
const ActivityPreset& Activity() { return kActivities[g.cfg.activity]; }

int RandRange(int lo, int hi) {
    return hi <= lo ? lo : lo + std::rand() % (hi - lo + 1);
}

template <typename T>
T Clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// --- Settings -------------------------------------------------------------

DWORD ReadDword(const wchar_t* name, DWORD fallback) {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
                   ERROR_SUCCESS
               ? value
               : fallback;
}

void WriteDword(const wchar_t* name, DWORD value) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

void LoadSettings() {
    const Settings def;
    g.cfg.scale = Clamp(static_cast<int>(ReadDword(L"ScaleIndex", def.scale)), 0, static_cast<int>(ARRAYSIZE(kScales)) - 1);
    g.cfg.speed = Clamp(static_cast<int>(ReadDword(L"Speed", def.speed)), 0, static_cast<int>(ARRAYSIZE(kSpeeds)) - 1);
    g.cfg.activity =
        Clamp(static_cast<int>(ReadDword(L"Activity", def.activity)), 0, static_cast<int>(ARRAYSIZE(kActivities)) - 1);
    g.cfg.reaction = Clamp(static_cast<int>(ReadDword(L"Reaction", def.reaction)), 0, static_cast<int>(kIgnore));
    g.cfg.opacity =
        Clamp(static_cast<int>(ReadDword(L"Opacity", def.opacity)), 0, static_cast<int>(ARRAYSIZE(kOpacityPct)) - 1);
    g.cfg.zzz = ReadDword(L"Zzz", def.zzz) != 0;
    g.cfg.draggable = ReadDword(L"Draggable", def.draggable) != 0;
    g.cfg.sleepOnMouseIdle = ReadDword(L"SleepOnMouseIdle", def.sleepOnMouseIdle) != 0;
    g.cfg.typeWithUser = ReadDword(L"TypeWithUser", def.typeWithUser) != 0;
    g.cfg.keyBubble = ReadDword(L"KeyBubble", def.keyBubble) != 0;
    // Falls back to the former bottom-only toggle.
    const DWORD legacyZone = ReadDword(L"BottomOnly", 0) != 0 ? kBottom : def.zone;
    g.cfg.zone = Clamp(static_cast<int>(ReadDword(L"Zone", legacyZone)), 0, static_cast<int>(kBottom));
    g.cfg.spiderMan = ReadDword(L"SpiderMan", def.spiderMan) != 0;
}

void SaveSettings() {
    WriteDword(L"ScaleIndex", g.cfg.scale);
    WriteDword(L"Speed", g.cfg.speed);
    WriteDword(L"Activity", g.cfg.activity);
    WriteDword(L"Reaction", g.cfg.reaction);
    WriteDword(L"Opacity", g.cfg.opacity);
    WriteDword(L"Zzz", g.cfg.zzz);
    WriteDword(L"Draggable", g.cfg.draggable);
    WriteDword(L"SleepOnMouseIdle", g.cfg.sleepOnMouseIdle);
    WriteDword(L"TypeWithUser", g.cfg.typeWithUser);
    WriteDword(L"KeyBubble", g.cfg.keyBubble);
    WriteDword(L"Zone", g.cfg.zone);
    WriteDword(L"SpiderMan", g.cfg.spiderMan);
}

bool StartupEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS;
}

void SetStartup(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (enabled) {
        wchar_t path[MAX_PATH];
        const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            wchar_t cmd[MAX_PATH + 2];
            wsprintfW(cmd, L"\"%s\"", path);
            RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd),
                           static_cast<DWORD>((lstrlenW(cmd) + 1) * sizeof(wchar_t)));
        }
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}

// --- Sprites and rendering ------------------------------------------------

template <typename F>
F Proc(HMODULE lib, const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(lib, name)));
}

// Decodes the embedded PNG with GDI+'s flat API, loaded just for this and unloaded afterward.
bool LoadSheet(Sheet& sheet, WORD resId) {
    using Status = Gdiplus::Status;
    using Bitmap = void*;
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!res) return false;
    const BYTE* data = static_cast<const BYTE*>(LockResource(LoadResource(nullptr, res)));
    HMODULE lib = LoadLibraryW(L"gdiplus.dll");
    if (!lib) return false;
    const auto startup = Proc<Status(WINAPI*)(ULONG_PTR*, const Gdiplus::GdiplusStartupInput*, void*)>(
        lib, "GdiplusStartup");
    const auto shutdown = Proc<void(WINAPI*)(ULONG_PTR)>(lib, "GdiplusShutdown");
    const auto fromStream = Proc<Status(WINAPI*)(IStream*, Bitmap*)>(lib, "GdipCreateBitmapFromStream");
    const auto width = Proc<Status(WINAPI*)(Bitmap, UINT*)>(lib, "GdipGetImageWidth");
    const auto height = Proc<Status(WINAPI*)(Bitmap, UINT*)>(lib, "GdipGetImageHeight");
    const auto lockBits =
        Proc<Status(WINAPI*)(Bitmap, const void*, UINT, INT, Gdiplus::BitmapData*)>(lib, "GdipBitmapLockBits");
    const auto unlockBits = Proc<Status(WINAPI*)(Bitmap, Gdiplus::BitmapData*)>(lib, "GdipBitmapUnlockBits");
    const auto dispose = Proc<Status(WINAPI*)(Bitmap)>(lib, "GdipDisposeImage");
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (!startup || !shutdown || !fromStream || !width || !height || !lockBits || !unlockBits || !dispose ||
        startup(&token, &input, nullptr) != Gdiplus::Ok) {
        FreeLibrary(lib);
        return false;
    }

    bool ok = false;
    if (IStream* stream = SHCreateMemStream(data, SizeofResource(nullptr, res))) {
        Bitmap bmp = nullptr;
        UINT w = 0, h = 0;
        if (fromStream(stream, &bmp) == Gdiplus::Ok) {
            Gdiplus::BitmapData bd{};
            if (width(bmp, &w) == Gdiplus::Ok && height(bmp, &h) == Gdiplus::Ok &&
                lockBits(bmp, nullptr, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
                sheet.w = static_cast<int>(w);
                sheet.h = static_cast<int>(h);
                sheet.px.resize(static_cast<size_t>(sheet.w) * sheet.h);
                for (int row = 0; row < sheet.h; ++row) {
                    std::memcpy(&sheet.px[static_cast<size_t>(row) * sheet.w],
                                static_cast<const BYTE*>(bd.Scan0) + static_cast<ptrdiff_t>(row) * bd.Stride,
                                static_cast<size_t>(sheet.w) * 4);
                }
                unlockBits(bmp, &bd);
                ok = true;
            }
            dispose(bmp);
        }
        stream->Release();
    }
    shutdown(token);
    FreeLibrary(lib);
    return ok;
}

// Draws a sprite cell, scaled (nearest neighbor), into the cell area of the canvas.
void DrawFrame(Frame frame) {
    const int sx0 = frame % 16 * kCellUnits, sy0 = frame / 16 * kCellUnits;
    for (int y = 0; y < g.cellH; ++y) {
        const uint32_t* src = &g.sheet.px[static_cast<size_t>(sy0 + g.cellSrc[y]) * g.sheet.w + sx0];
        uint32_t* dst = &g.canvas[static_cast<size_t>(g.padTop + y) * g.winW + g.padX];
        for (int x = 0; x < g.cellW; ++x) dst[x] = src[g.cellSrc[x]];
    }
}

// (Re)creates a top-down 32-bit DIB of w x h selected into dc (created on first use).
bool CreateDib(HDC& dc, HBITMAP& dib, uint32_t*& bits, int w, int h) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    if (!dc) dc = CreateCompatibleDC(screen);
    void* newBits = nullptr;
    HBITMAP newDib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &newBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dc || !newDib) return false;
    SelectObject(dc, newDib);
    if (dib) DeleteObject(dib);  // only deletable once deselected
    dib = newDib;
    bits = static_cast<uint32_t*>(newBits);
    return true;
}

bool CreateBackBuffer() {
    g.canvas.assign(static_cast<size_t>(g.winW) * g.winH, 0);
    g.dibSize = std::max(g.winW, g.winH);
    return CreateDib(g.memDc, g.dib, g.dibBits, g.dibSize, g.dibSize);
}

// Rebuilds the scaling map and back buffer for the current scale.
bool BuildScaledAssets() {
    const double s = Scale();
    g.cellW = g.cellH = static_cast<int>(std::lround(kCellUnits * s));
    g.padX = static_cast<int>(std::lround(kPadXUnits * s));
    g.padTop = static_cast<int>(std::lround(kPadTopUnits * s));
    g.winW = g.cellW + 2 * g.padX;
    g.winH = g.cellH + g.padTop;
    g.cellSrc.resize(g.cellW);
    for (int i = 0; i < g.cellW; ++i) g.cellSrc[i] = std::min(kCellUnits - 1, static_cast<int>(i / s));
    g.shown = -1;
    if (g.bubbleFont) DeleteObject(g.bubbleFont);
    g.bubbleFont = CreateFontW(-std::max(10, static_cast<int>(std::lround(kBubbleFontUnits * s))), 0, 0, 0, FW_BOLD,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    g.bubbleStale = true;
    return CreateBackBuffer();
}

// Composites a straight-alpha color over a premultiplied back-buffer pixel.
void BlendPixel(int x, int y, uint32_t r, uint32_t gr, uint32_t b, double a) {
    if (x < 0 || y < 0 || x >= g.dibSize || y >= g.dibSize || a <= 0.0) return;
    uint32_t& d = g.dibBits[y * g.dibSize + x];
    const uint32_t sa = static_cast<uint32_t>(std::lround(a * 255.0));
    const auto mix = [&](int shift, uint32_t c) {
        return ((c * sa + ((d >> shift) & 0xFF) * (255 - sa)) / 255) << shift;
    };
    d = mix(24, 255) | mix(16, r) | mix(8, gr) | mix(0, b);
}

// Draws a pixel-art "Z" (5x5 glyph, s px per glyph pixel) with a 1 px dark outline.
void DrawZ(int x0, int y0, int s, double a) {
    static const char* const kGlyph[5] = {"#####", "...#.", "..#..", ".#...", "#####"};
    const int n = 5 * s;
    const auto inGlyph = [&](int px, int py) {
        return px >= 0 && py >= 0 && px < n && py < n && kGlyph[py / s][px / s] == '#';
    };
    for (int py = -1; py <= n; ++py) {
        for (int px = -1; px <= n; ++px) {
            if (inGlyph(px, py)) {
                BlendPixel(x0 + px, y0 + py, 240, 248, 255, a);
                continue;
            }
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
                for (int dx = -1; dx <= 1 && !edge; ++dx) edge = inGlyph(px + dx, py + dy);
            if (edge) BlendPixel(x0 + px, y0 + py, 40, 48, 72, a);
        }
    }
}

// Maps a canvas point to the back buffer once the canvas is turned toward the given edge (see BlitCanvas).
POINT CanvasToWindow(int cx, int cy, int orient) {
    const int w = g.winW, h = g.winH;
    return orient == 0 ? POINT{cx, cy}
         : orient == 1 ? POINT{h - 1 - cy, cx}
         : orient == 2 ? POINT{w - 1 - cx, h - 1 - cy}
                       : POINT{cy, w - 1 - cx};
}

// Z's spawn one after another above the sleeping head, rising and drifting away from his feet while fading.
// Drawn into the back buffer after rotation so the glyphs stay upright (a turned Z reads as N).
void DrawZzz(ULONGLONG sleptMs, int orient) {
    const double u = Scale();
    const ULONGLONG life = Speed().zzzLifeMs;
    for (int i = 0; i < kZzzCount; ++i) {
        const long long age = static_cast<long long>(sleptMs) - i * static_cast<long long>(life / kZzzCount);
        if (age < 0) continue;
        const double t = static_cast<double>(age % life) / life;
        const double a = t < 0.15 ? t / 0.15 : (t > 0.7 ? (1.0 - t) / 0.3 : 1.0);
        const int s = std::max(1, static_cast<int>(u * (t < 0.5 ? 1.0 : 1.5)));
        const int dx = static_cast<int>((26.0 + t * 10.0 + std::sin(t * 2.0 * kPi) * 2.0) * u);
        const int x = g.padX + (g.sleepSide == 0 ? dx : g.cellW - dx - 5 * s);
        const int y = g.padTop + static_cast<int>((12.0 - t * 33.0) * u);
        const int half = 5 * s / 2;
        const POINT c = CanvasToWindow(x + half, y + half, orient);
        DrawZ(c.x - half, c.y - half, s, a);
    }
}

int Orientation();

// Copies the canvas into the back buffer, its bottom (his feet) turned toward the given edge:
// 0 floor, 1 left wall (clockwise), 2 ceiling, 3 right wall (counterclockwise).
void BlitCanvas(int orient) {
    const int w = g.winW, h = g.winH, s = g.dibSize;
    if (orient == 0) {
        for (int cy = 0; cy < h; ++cy)
            std::memcpy(&g.dibBits[cy * s], &g.canvas[cy * w], w * sizeof(uint32_t));
        return;
    }
    for (int cy = 0; cy < h; ++cy) {
        for (int cx = 0; cx < w; ++cx) {
            const POINT p = CanvasToWindow(cx, cy, orient);
            g.dibBits[p.y * s + p.x] = g.canvas[cy * w + cx];
        }
    }
}

// Shows a sprite frame (plus the "Zzz" overlay while sleeping) at the current position.
void Render(Frame frame, ULONGLONG now) {
    const int orient = Orientation();
    const LONG x = std::lround(g.x), y = std::lround(g.y);
    // Window top-left so that the rotated canvas puts the cell at (x, y).
    POINT pos = orient == 0 ? POINT{x - g.padX, y - g.padTop}
              : orient == 1 ? POINT{x, y - g.padX}
              : orient == 2 ? POINT{x - g.padX, y}
                            : POINT{x - g.padTop, y - g.padX};
    (orient % 2 ? pos.y : pos.x) += g.shakeX;  // along the edge he stands on
    const BYTE alpha = static_cast<BYTE>(std::lround(g.alpha));
    const ULONGLONG slept = now - g.sleepStart;
    const long long overlay =
        g.state == State::Sleep && g.cfg.zzz ? static_cast<long long>(slept / kZzzStepMs) : -1;
    if (frame == g.shown && overlay == g.shownOverlay && orient == g.shownOrient && pos.x == g.shownPos.x &&
        pos.y == g.shownPos.y && alpha == g.shownAlpha)
        return;
    const bool recompose = frame != g.shown || overlay != g.shownOverlay;
    if (frame != g.shown) DrawFrame(frame);  // the canvas padding stays transparent
    if (recompose || orient != g.shownOrient) {
        GdiFlush();
        BlitCanvas(orient);
        if (overlay >= 0) DrawZzz(slept, orient);
    }
    SIZE size = orient % 2 ? SIZE{g.winH, g.winW} : SIZE{g.winW, g.winH};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(g.pet, nullptr, const_cast<POINT*>(&pos), &size, g.memDc, &src, 0, &blend, ULW_ALPHA);
    g.shown = frame;
    g.shownPos = pos;
    g.shownAlpha = alpha;
    g.shownOverlay = overlay;
    g.shownOrient = orient;
}

// --- Behavior -------------------------------------------------------------

BOOL CALLBACK CollectMonitor(HMONITOR mon, HDC, LPRECT, LPARAM param) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) reinterpret_cast<std::vector<MONITORINFO>*>(param)->push_back(mi);
    return TRUE;
}

MONITORINFO CurrentMonitor() {
    const RECT petRc{static_cast<LONG>(g.x), static_cast<LONG>(g.y),
                     static_cast<LONG>(g.x) + g.cellW, static_cast<LONG>(g.y) + g.cellH};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromRect(&petRc, MONITOR_DEFAULTTONEAREST), &mi);
    return mi;
}

RECT CurrentWorkArea() { return CurrentMonitor().rcWork; }

// Spider-Man mode: the edge his feet stick to (see BlitCanvas). Bottom corners count as floor,
// top corners as walls; anywhere off an edge he stands upright.
int Orientation() {
    if (!g.cfg.spiderMan || g.cfg.zone != kEdges || g.state == State::Drag) return 0;
    if (g.state == State::Walk && g.legOrient >= 0) return g.legOrient;
    const RECT area = CurrentWorkArea();
    if (g.y >= area.bottom - g.cellH) return 0;
    if (g.x <= area.left) return 1;
    if (g.x >= area.right - g.cellW) return 3;
    if (g.y <= area.top) return 2;
    return 0;
}

// g.dir is a screen direction; these convert to and from the sprite row shown once rotated.
int ViewDir() { return (g.dir + 8 - 2 * Orientation()) % 8; }
int ToScreenDir(int viewDir) { return (viewDir + 2 * Orientation()) % 8; }

// Maps a screen-space vector to a direction index (see kDirView).
int DirTowards(double dx, double dy) {
    // Octant 0 = east, increasing clockwise (screen y points down); rows start at south.
    const int octant = (static_cast<int>(std::lround(std::atan2(dy, dx) / (kPi / 4))) + 8) % 8;
    return (octant + 6) % 8;
}

// Steps toward (tx, ty); returns true on arrival.
bool MoveToTarget(double step) {
    const double dx = g.tx - g.x, dy = g.ty - g.y;
    const double dist = std::hypot(dx, dy);
    if (dist <= step) {
        g.x = g.tx;
        g.y = g.ty;
        return true;
    }
    g.x += dx / dist * step;
    g.y += dy / dist * step;
    return false;
}

bool MouseIdle(ULONGLONG now) {
    return g.cfg.sleepOnMouseIdle && now - g.lastCursorMove >= kMouseIdleMs;
}

void EnterIdle(ULONGLONG now) {
    g.state = State::Idle;
    g.stateEnd = now + RandRange(Activity().idleMinMs, Activity().idleMaxMs);
}

// Lies down, head toward the side he was facing (on a wall, always head up).
void EnterSleep(ULONGLONG now) {
    const int orient = Orientation();
    const int view = kDirView[ViewDir()];
    g.sleepSide = orient == 1 ? 1 : orient == 3 ? 0 : view == kRight ? 0 : (view == kLeft ? 1 : RandRange(0, 1));
    g.state = State::Sleep;
    g.stateEnd = now + RandRange(Activity().sleepMinMs, Activity().sleepMaxMs);
    g.sleepStart = now;
}

// View for TypingFrame: the direction he faces while typing.
int TypingView() {
    const int v = ViewDir();
    return v == 2 ? 1 : (v == 6 ? 2 : 0);
}

// Sits down with his laptop for at least durationMs, in profile if he was facing sideways.
void EnterType(ULONGLONG now, ULONGLONG durationMs) {
    if (g.state != State::Type) {
        g.stateEnd = 0;
        const int v = ViewDir();
        g.dir = ToScreenDir(v >= 1 && v <= 3 ? 2 : (v >= 5 ? 6 : 0));
    }
    g.state = State::Type;
    g.stateEnd = std::max(g.stateEnd, now + durationMs);
}

// Loads kLyricsFile from the exe folder, falling back to kDefaultLyrics.
void LoadLyrics() {
    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH && PathRemoveFileSpecW(path) && PathAppendW(path, kLyricsFile)) {
        if (FILE* f = _wfopen(path, L"rb")) {
            std::string data;
            char buf[4096];
            for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) data.append(buf, n);
            std::fclose(f);
            if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
            std::wstring text(data.size(), L'\0');
            text.resize(MultiByteToWideChar(CP_UTF8, 0, data.data(), static_cast<int>(data.size()), text.data(),
                                            static_cast<int>(text.size())));
            for (size_t start = 0; start < text.size();) {
                size_t end = text.find(L'\n', start);
                if (end == std::wstring::npos) end = text.size();
                std::wstring line = text.substr(start, end - start);
                start = end + 1;
                const size_t first = line.find_first_not_of(L" \t\r"), last = line.find_last_not_of(L" \t\r");
                if (first == std::wstring::npos || line[first] == L'#') continue;
                g.lyrics.push_back(line.substr(first, last - first + 1));
            }
        }
    }
    if (g.lyrics.empty()) g.lyrics.assign(std::begin(kDefaultLyrics), std::end(kDefaultLyrics));
}

// Starts a random line, other than the current one when possible.
void NextLyric() {
    const int count = static_cast<int>(g.lyrics.size());
    g.lyric = (g.lyric + RandRange(count > 1 ? 1 : 0, count - 1)) % count;
    g.lyricPos = 0;
}

// Every keystroke types the next character of the current line, whatever the key: what the user
// types is never read. A finished line gives way to "..." for a while, then to the next one.
void RecordKeystroke(ULONGLONG now) {
    if (now - g.lastKeystroke > kBubbleLingerMs) {  // the bubble has faded: start afresh
        g.typed.clear();
        NextLyric();
    }
    const std::wstring& line = g.lyrics[g.lyric];
    if (g.lyricPos >= line.size()) {
        g.typed.clear();
        g.dotsStart = now;
        g.dotsUntil = now + kDotsMs;
        NextLyric();
        return;
    }
    g.dotsUntil = 0;
    g.typed += line[g.lyricPos++];
    if (g.typed.size() > kBubbleMaxChars) g.typed.erase(0, g.typed.size() - kBubbleMaxChars);
}

// Mirrors the user typing: a burst of keystrokes opens the laptop, each further one keeps it open.
void OnKeystroke() {
    const ULONGLONG now = GetTickCount64();
    if (g.cfg.keyBubble) RecordKeystroke(now);
    const bool burst = now - g.lastKeystroke <= kTypeBurstMs;
    g.lastKeystroke = now;
    if (g.state == State::Type ||
        (burst && (g.state == State::Idle || g.state == State::Walk || g.state == State::Sleep)))
        EnterType(now, kTypeLingerMs);
    // He stays at the laptop while the bubble shows text.
    if (g.state == State::Type && !g.typed.empty()) EnterType(now, kBubbleLingerMs);
    // After a finished line he stays at the laptop while the "..." bubble lasts.
    if (g.state == State::Type && g.dotsUntil > now) EnterType(now, g.dotsUntil - now);
}

// Keystrokes arrive as raw input (no polling), only while he is shown and types along with the user.
void UpdateKeyboardListener() {
    const bool on = g.visible && g.cfg.typeWithUser;
    RAWINPUTDEVICE rid{0x01, 0x06, static_cast<DWORD>(on ? RIDEV_INPUTSINK : RIDEV_REMOVE), on ? g.tray : nullptr};
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    if (!on) std::fill(std::begin(g.keyDown), std::end(g.keyDown), false);
}

// Detects key-down edges on any key, modifiers included (auto-repeat ignored).
void OnRawInput(HRAWINPUT handle) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData(handle, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        ri.header.dwType != RIM_TYPEKEYBOARD)
        return;
    const int vk = ri.data.keyboard.VKey;
    const USHORT flags = ri.data.keyboard.Flags;
    if (vk <= 0 || vk >= 255) return;  // 255: part of an escaped sequence
    const bool down = !(flags & RI_KEY_BREAK);
    if (down && !g.keyDown[vk] && g.visible && g.cfg.typeWithUser) OnKeystroke();
    g.keyDown[vk] = down;
}

// Plans a walk onto a random screen next to the current one, through their shared border, so he never
// cuts across the void between screens. In the bottom and edges zones he walks along the floors (from
// the floor only), climbing or dropping at the border when they differ. Returns false when there is
// no such screen.
bool PlanCrossing() {
    std::vector<MONITORINFO> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
    const MONITORINFO cur = CurrentMonitor();
    const RECT& a = cur.rcWork;
    const bool onFloor = g.cfg.zone != kAnywhere;
    if (onFloor && std::fabs(g.y - (a.bottom - g.cellH)) > 0.5) return false;
    std::vector<std::vector<Waypoint>> options;
    for (const MONITORINFO& m : monitors) {
        const RECT& b = m.rcWork;
        const RECT& ma = cur.rcMonitor;
        const RECT& mb = m.rcMonitor;
        const int side = mb.left == ma.right ? 1 : (mb.right == ma.left ? -1 : 0);
        const int vside = mb.top == ma.bottom ? 1 : (mb.bottom == ma.top ? -1 : 0);
        if (side != 0) {
            // Crosses at a height where he fits on both sides; along the floors, the higher one.
            const int lo = std::max(a.top, b.top), hi = std::min(a.bottom, b.bottom) - g.cellH;
            if (hi < lo) continue;
            const int fromX = side > 0 ? a.right - g.cellW : a.left, toX = side > 0 ? b.left : b.right - g.cellW;
            const int floorA = a.bottom - g.cellH, floorB = b.bottom - g.cellH;
            const int lane = onFloor ? hi : Clamp(static_cast<int>(g.y), lo, hi);
            const Waypoint end{RandRange(b.left, b.right - g.cellW), onFloor ? floorB : RandRange(b.top, floorB), -1};
            // Standing on the floor while stepping over the border, whichever screen he counts as on.
            options.push_back({{fromX, onFloor ? floorA : lane, -1}, {fromX, lane, -1}, {toX, lane, 0},
                               {toX, onFloor ? floorB : lane, -1}, end});
        } else if (vside != 0 && !onFloor) {
            const int lo = std::max(a.left, b.left), hi = std::min(a.right, b.right) - g.cellW;
            if (hi < lo) continue;
            const int lane = Clamp(static_cast<int>(g.x), lo, hi);
            const int fromY = vside > 0 ? a.bottom - g.cellH : a.top, toY = vside > 0 ? b.top : b.bottom - g.cellH;
            const Waypoint end{RandRange(b.left, b.right - g.cellW), RandRange(b.top, b.bottom - g.cellH), -1};
            options.push_back({{lane, fromY, -1}, {lane, toY, -1}, end});
        }
    }
    if (options.empty()) return false;
    g.path.clear();
    POINT last{std::lround(g.x), std::lround(g.y)};
    for (const Waypoint& p : options[RandRange(0, static_cast<int>(options.size()) - 1)]) {
        if (p.x != last.x || p.y != last.y) g.path.push_back(p);
        last = {p.x, p.y};
    }
    if (g.path.empty()) return false;
    g.bumpOnArrival = false;
    g.turnAxis = -1;
    return true;
}

// Heads to the next point of g.path.
void NextWaypoint() {
    g.tx = g.path.front().x;
    g.ty = g.path.front().y;
    g.legOrient = g.path.front().orient;
    g.path.erase(g.path.begin());
    g.dir = DirTowards(g.tx - g.x, g.ty - g.y);
}

// Edges zone: walks along the edge he is on (a random one in a corner), or to a random edge point when
// off the work area. Returns true when instead heading straight to the nearest edge (e.g. after a drag).
bool PickEdgeTarget(int minX, int maxX, int minY, int maxY, int range) {
    const int x = static_cast<int>(g.x), y = static_cast<int>(g.y);
    const int turnAxis = g.turnAxis;
    g.turnAxis = -1;
    g.bumpOnArrival = false;
    if (x < minX || x > maxX || y < minY || y > maxY) {
        if (RandRange(0, 1)) {
            g.tx = RandRange(minX, maxX);
            g.ty = RandRange(0, 1) ? minY : maxY;
        } else {
            g.tx = RandRange(0, 1) ? minX : maxX;
            g.ty = RandRange(minY, maxY);
        }
        return false;
    }
    const bool onV = x == minX || x == maxX, onH = y == minY || y == maxY;
    if (!onV && !onH) {
        const int dl = x - minX, dr = maxX - x, dt = y - minY, db = maxY - y;
        const int m = std::min({dl, dr, dt, db});
        g.tx = m == dl ? minX : (m == dr ? maxX : x);
        g.ty = m == dl || m == dr ? y : (m == dt ? minY : maxY);
        return true;
    }
    // After a corner bump he turns onto the other edge, walking away from the corner.
    const bool turning = onH && onV && turnAxis >= 0;
    const bool alongX = turning ? turnAxis == 0 : onH && (!onV || RandRange(0, 1));
    const int delta = turning ? (alongX ? (x == minX ? 1 : -1) : (y == minY ? 1 : -1)) * RandRange(range / 4, range)
                              : RandRange(-range, range);
    const int raw = (alongX ? x : y) + delta;
    const int clamped = alongX ? Clamp(raw, minX, maxX) : Clamp(raw, minY, maxY);
    g.tx = alongX ? clamped : x;
    g.ty = alongX ? y : clamped;
    g.bumpOnArrival = raw != clamped && RandRange(0, 99) < kBumpChancePct;
    return false;
}

void EnterWalk(ULONGLONG now) {
    g.path.clear();
    g.legOrient = -1;
    if (RandRange(0, 99) < kCrossChancePct && PlanCrossing()) {
        NextWaypoint();
        g.state = State::Walk;
        return;
    }
    const RECT area = CurrentWorkArea();
    const int range = static_cast<int>(kWalkRangeUnits * Scale());
    const int minX = area.left, maxX = area.right - g.cellW;
    const int maxY = area.bottom - g.cellH, minY = g.cfg.zone == kBottom ? maxY : area.top;
    bool toEdge = false;
    if (g.cfg.zone == kEdges) {
        toEdge = PickEdgeTarget(minX, maxX, minY, maxY, range);
    } else {
        const bool bottomOnly = g.cfg.zone == kBottom;
        const int cx = Clamp(static_cast<int>(g.x), minX, maxX);
        const int cy = Clamp(static_cast<int>(g.y), minY, maxY);
        // In bottom-only mode, being above the bottom edge (e.g. after a drag) still counts as on the area.
        const bool onArea = cx == static_cast<int>(g.x) && (bottomOnly || cy == static_cast<int>(g.y));
        const int rawX = cx + RandRange(-range, range), rawY = cy + RandRange(-range, range);
        g.tx = onArea ? Clamp(rawX, minX, maxX) : RandRange(minX, maxX);
        g.ty = onArea ? Clamp(rawY, minY, maxY) : RandRange(minY, maxY);
        g.bumpOnArrival = onArea && (rawX != g.tx || (!bottomOnly && rawY != g.ty)) &&
                          RandRange(0, 99) < kBumpChancePct;
    }

    const double dx = g.tx - g.x, dy = g.ty - g.y;
    // Even a short hop back onto an edge is taken, or he would never leave a spot just off it.
    if (!toEdge && std::hypot(dx, dy) < 10.0 * Scale()) {
        EnterIdle(now);
        return;
    }
    g.dir = DirTowards(dx, dy);
    g.state = State::Walk;
}

// Sulks facing the user for a while, then falls asleep.
void EnterSulk(ULONGLONG now) {
    g.dir = ToScreenDir(0);
    g.state = State::Sulk;
    g.stateEnd = now + kSulkMs;
    g.nearSince = 0;
}

// Bounces off the screen edge(s) he is touching, dazed, then idles (or sulks after a flee).
void EnterBump(ULONGLONG now, bool sulkAfter) {
    const RECT area = CurrentWorkArea();
    const double minX = area.left, maxX = area.right - g.cellW;
    const double minY = area.top, maxY = area.bottom - g.cellH;
    int nx = g.x <= minX + 0.5 ? 1 : (g.x >= maxX - 0.5 ? -1 : 0);
    int ny = g.y <= minY + 0.5 ? 1 : (g.y >= maxY - 0.5 ? -1 : 0);
    // Restricted zones: only bounce along the edge he walks on.
    if (g.cfg.zone == kBottom || (g.cfg.zone == kEdges && (g.dir == 2 || g.dir == 6))) ny = 0;
    else if (g.cfg.zone == kEdges) nx = 0;
    if (nx == 0 && ny == 0) {
        if (sulkAfter) EnterSulk(now);
        else EnterIdle(now);
        return;
    }
    // In the edges zone he stays in the corner (no knockback) so the next walk can turn.
    const double knock = g.cfg.zone == kEdges ? 0.0 : kBumpKnockUnits * Scale();
    if (g.cfg.zone == kEdges) g.turnAxis = nx != 0 ? 1 : 0;
    g.dir = DirTowards(-nx, -ny);  // facing the wall he hit
    g.tx = Clamp(g.x + nx * knock, minX, maxX);
    g.ty = Clamp(g.y + ny * knock, minY, maxY);
    g.state = State::Bump;
    g.bumpStart = now;
    g.stateEnd = now + kBumpMs;
    g.sulkAfterBump = sulkAfter;
}

void TeleportAway(ULONGLONG now) {
    const RECT area = CurrentWorkArea();
    g.x = area.right - g.cellW;
    g.y = area.bottom - g.cellH;
    EnterSulk(now);
}

// Runs to the work-area corner farthest from the cursor (skipping the one he already sits in).
// In the edges zone, only corners reachable along the edge he is on.
void EnterFlee(POINT cursor) {
    const RECT area = CurrentWorkArea();
    const bool onEdge = g.cfg.zone == kEdges &&
                        (g.x == area.left || g.x == area.right - g.cellW || g.y == area.top ||
                         g.y == area.bottom - g.cellH);
    const double corners[4][2] = {
        {static_cast<double>(area.left), static_cast<double>(area.top)},
        {static_cast<double>(area.right - g.cellW), static_cast<double>(area.top)},
        {static_cast<double>(area.left), static_cast<double>(area.bottom - g.cellH)},
        {static_cast<double>(area.right - g.cellW), static_cast<double>(area.bottom - g.cellH)},
    };
    double best = -1.0;
    for (const auto& c : corners) {
        if (g.cfg.zone == kBottom && c[1] == area.top) continue;
        if (onEdge && c[0] != g.x && c[1] != g.y) continue;
        if (std::hypot(c[0] - g.x, c[1] - g.y) < 10.0 * Scale()) continue;
        const double fromCursor = std::hypot(c[0] + g.cellW / 2.0 - cursor.x, c[1] + g.cellH / 2.0 - cursor.y);
        if (fromCursor > best) {
            best = fromCursor;
            g.tx = c[0];
            g.ty = c[1];
        }
    }
    g.dir = DirTowards(g.tx - g.x, g.ty - g.y);
    g.state = State::Flee;
    g.nearSince = 0;
}

void NextState(ULONGLONG now) {
    if (g.state == State::Sleep) {
        g.dir = ToScreenDir(0);
        EnterIdle(now);
        return;
    }
    if (MouseIdle(now)) {
        EnterSleep(now);
        return;
    }
    const ActivityPreset& act = Activity();
    const int r = RandRange(0, 99);
    if (r < act.walkPct) EnterWalk(now);
    else if (r < act.walkPct + act.sleepPct) EnterSleep(now);
    else if (r < act.walkPct + act.sleepPct + act.typePct) EnterType(now, RandRange(act.typeMinMs, act.typeMaxMs));
    else EnterIdle(now);
}

// --- Lyrics bubble -------------------------------------------------------

// Draws a pixel-art speech bubble around text or, when dots >= 0, three dots of which the first `dots`
// are lit. Its tail points toward his head, on the side matching Orientation(): 0 bottom (left end),
// 1 left, 2 top (left end), 3 right (top end of both walls).
bool DrawBubble(const std::wstring& text, int dots, int side) {
    const int u = std::max(1, static_cast<int>(std::lround(Scale())));  // bubble pixel size
    if (!g.bubbleDc) g.bubbleDc = CreateCompatibleDC(nullptr);
    if (!g.bubbleDc) return false;
    SelectObject(g.bubbleDc, g.bubbleFont);
    SIZE ext{8 * u, 2 * u};  // dots: 2x2 bubble pixels each, 1 apart
    if (dots < 0) GetTextExtentPoint32W(g.bubbleDc, text.c_str(), static_cast<int>(text.size()), &ext);
    const int pad = u;
    // In bubble pixels: outline, padding, text; the tail adds 2 rows or columns on its side.
    const int bw = (ext.cx + 2 * pad + u - 1) / u + 2, bh = (ext.cy + 2 * pad + u - 1) / u + 2;
    const int ox = side == 1 ? 2 : 0, oy = side == 2 ? 2 : 0;  // body offset
    const int w = (bw + (side % 2 ? 2 : 0)) * u, h = (bh + (side % 2 ? 0 : 2)) * u;
    if (w != g.bubbleW || h != g.bubbleH) {
        if (!CreateDib(g.bubbleDc, g.bubbleDib, g.bubbleBits, w, h)) return false;
        g.bubbleW = w;
        g.bubbleH = h;
    }
    GdiFlush();
    std::memset(g.bubbleBits, 0, static_cast<size_t>(w) * h * sizeof(uint32_t));
    constexpr uint32_t kInk = 0xFF181820, kPaper = 0xFFFFFFFF;
    const auto put = [&](int bx, int by, uint32_t c) {
        for (int y = by * u; y < (by + 1) * u; ++y)
            for (int x = bx * u; x < (bx + 1) * u; ++x) g.bubbleBits[y * w + x] = c;
    };
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const bool edgeX = bx == 0 || bx == bw - 1, edgeY = by == 0 || by == bh - 1;
            if (edgeX && edgeY) continue;  // rounded corners
            put(ox + bx, oy + by, edgeX || edgeY ? kInk : kPaper);
        }
    }
    // Tail: `along` its edge from the corner, `out` away from the body (0 on its outline).
    const auto tail = [&](int along, int out, uint32_t c) {
        if (side == 0) put(ox + along, oy + bh - 1 + out, c);
        else if (side == 1) put(ox - out, oy + along, c);
        else if (side == 2) put(ox + along, oy - out, c);
        else put(ox + bw - 1 + out, oy + along, c);
    };
    constexpr int tc = 2;
    tail(tc, 0, kPaper);
    tail(tc + 1, 0, kPaper);
    tail(tc - 1, 1, kInk);
    tail(tc, 1, kPaper);
    tail(tc + 1, 1, kInk);
    tail(tc - 1, 2, kInk);
    tail(tc, 2, kInk);
    const int tip = (tc - 1) * u;
    g.bubbleTip = side == 0 ? POINT{tip, h} : side == 1 ? POINT{0, tip} : side == 2 ? POINT{tip, 0} : POINT{w, tip};
    if (dots >= 0) {
        for (int i = 0; i < dots; ++i)
            for (int d = 0; d < 4; ++d) put(ox + 2 + 3 * i + d % 2, oy + 2 + d / 2, kInk);
        return true;
    }

    SetBkMode(g.bubbleDc, TRANSPARENT);
    SetTextColor(g.bubbleDc, RGB(0x18, 0x18, 0x20));
    TextOutW(g.bubbleDc, (ox + 1) * u + pad, (oy + 1) * u + pad, text.c_str(), static_cast<int>(text.size()));
    GdiFlush();
    // GDI text leaves alpha at 0; everything drawn is opaque (the background stays fully transparent).
    for (int i = 0; i < w * h; ++i)
        if (g.bubbleBits[i] & 0xFFFFFF) g.bubbleBits[i] |= 0xFF000000;
    return true;
}

void HideBubble() {
    if (g.bubbleShown) ShowWindow(g.bubble, SW_HIDE);
    g.bubbleShown = false;
}

// Shows the line he is typing in a bubble above his head, then "..." once it is finished.
void UpdateBubble(ULONGLONG now) {
    if (!g.typed.empty() && now - g.lastKeystroke > kBubbleLingerMs) g.typed.clear();
    const bool waiting = g.typed.empty() && now < g.dotsUntil;
    if (!g.visible || !g.cfg.keyBubble || g.state != State::Type || (g.typed.empty() && !waiting)) {
        HideBubble();
        return;
    }
    const int dots = waiting ? static_cast<int>((now - g.dotsStart) / kDotsStepMs % 4) : -1;
    const int orient = Orientation();
    const bool redraw = g.bubbleStale || dots != g.bubbleDots || orient != g.bubbleSide ||
                        (dots < 0 && g.typed != g.bubbleText);
    if (redraw) {
        if (!DrawBubble(g.typed, dots, orient)) return;
        g.bubbleText = g.typed;
        g.bubbleDots = dots;
        g.bubbleSide = orient;
        g.bubbleStale = false;
    }
    const RECT area = CurrentWorkArea();
    // Top of his head on screen, wherever his feet stick (see BlitCanvas).
    const LONG headOff = std::lround(kHeadTopUnits * Scale());
    const LONG x = std::lround(g.x), y = std::lround(g.y);
    const LONG headX = orient == 1 ? x + g.cellW - headOff : (orient == 3 ? x + headOff : x + g.cellW / 2);
    const LONG headY = orient == 0 ? y + headOff : (orient == 2 ? y + g.cellH - headOff : y + g.cellH / 2);
    // Beyond his head, tail tip on it.
    const LONG bx = headX - g.bubbleTip.x, by = headY - g.bubbleTip.y;
    const POINT pos{Clamp(bx, area.left, area.right - g.bubbleW), Clamp(by, area.top, area.bottom - g.bubbleH)};
    const BYTE alpha = static_cast<BYTE>(std::lround(g.alpha));
    if (redraw || pos.x != g.bubblePos.x || pos.y != g.bubblePos.y || alpha != g.bubbleAlpha) {
        SIZE size{g.bubbleW, g.bubbleH};
        POINT src{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        UpdateLayeredWindow(g.bubble, nullptr, const_cast<POINT*>(&pos), &size, g.bubbleDc, &src, 0, &blend,
                            ULW_ALPHA);
        g.bubblePos = pos;
        g.bubbleAlpha = alpha;
    }
    if (!g.bubbleShown) {
        ShowWindow(g.bubble, SW_SHOWNOACTIVATE);
        g.bubbleShown = true;
    }
}

// Dazed while carried, leaning toward where he is dragged.
Frame DragFrame() { return HurtFrame(g.dragLean < 0 ? kLeft : (g.dragLean > 0 ? kRight : kFront)); }

// Timer period for what he is doing: about a pixel per tick while walking, fine while moving fast or
// fading, coarse otherwise.
UINT TickPeriod() {
    if (g.state == State::Walk)
        return Clamp(static_cast<UINT>(1000.0 / (Speed().walkSpeed * Scale())), kTickMs, kSlowTickMs);
    if (g.state == State::Flee || g.state == State::Bump || g.fading) return kTickMs;
    if (g.state == State::Sleep && g.cfg.zzz) return static_cast<UINT>(kZzzStepMs);
    return kSlowTickMs;
}

void Tick() {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsed = now - g.lastTick;
    const double dt = (elapsed > 100 ? 100 : elapsed) / 1000.0;
    g.lastTick = now;
    const double walkSpeed = Speed().walkSpeed * Scale();

    POINT cursor;
    GetCursorPos(&cursor);
    if (cursor.x != g.lastCursor.x || cursor.y != g.lastCursor.y) {
        g.lastCursor = cursor;
        g.lastCursorMove = now;
    }
    const double cdx = cursor.x - (g.x + g.cellW / 2.0);
    const double cdy = cursor.y - (g.y + g.cellH / 2.0);
    const bool cursorNear = std::hypot(cdx, cdy) < kNearRadiusUnits * Scale();
    // In draggable mode, hovering the sprite itself never scares him, so he can be grabbed.
    const bool hovered = cursorNear && g.cfg.draggable && WindowFromPoint(cursor) == g.pet;
    const auto wary = [hovered] {
        return g.cfg.reaction != kIgnore && !hovered && g.state != State::Sulk && g.state != State::Flee &&
               g.state != State::Drag && g.state != State::Bump;
    };

    if (!cursorNear) {
        g.nearSince = 0;
    } else if (wary()) {
        if (g.nearSince == 0) g.nearSince = now;
        else if (now - g.nearSince >= kReactDelayMs) {
            if (g.cfg.reaction == kTeleport) TeleportAway(now);
            else EnterFlee(cursor);
        }
    }

    switch (g.state) {
    case State::Walk:
        if (MoveToTarget(walkSpeed * dt)) {
            if (!g.path.empty()) NextWaypoint();
            else if (g.bumpOnArrival) EnterBump(now, false);
            else if (!MouseIdle(now) && RandRange(0, 99) < Activity().chainWalkPct) EnterWalk(now);
            else EnterIdle(now);
        }
        break;
    case State::Flee:
        if (MoveToTarget(walkSpeed * kFleeSpeedFactor * dt)) EnterBump(now, true);
        break;
    case State::Bump:
        MoveToTarget(kBumpKnockUnits * Scale() * 5.0 * dt);  // quick hop back, ~0.2 s
        g.shakeX = now - g.bumpStart < kBumpShakeMs
                       ? ((now / 40) % 2 ? 1 : -1) * std::max(1, static_cast<int>(std::lround(Scale())))
                       : 0;
        if (now >= g.stateEnd) {
            g.shakeX = 0;
            if (g.sulkAfterBump) EnterSulk(now);
            else EnterIdle(now);
        }
        break;
    case State::Sulk:
        if (now >= g.stateEnd) EnterSleep(now);
        break;
    case State::Drag:
        if (now - g.lastDragMove >= kDragLeanMs) g.dragLean = 0;
        break;
    default:
        // While the mouse rests (if enabled), he keeps sleeping until it moves again.
        if (now >= g.stateEnd && !(g.state == State::Sleep && MouseIdle(now))) NextState(now);
        break;
    }

    // Fades while the cursor lingers nearby; fully opaque again once reacting or alone.
    const double baseAlpha = 255.0 * kOpacityPct[g.cfg.opacity] / 100.0;
    const double targetAlpha = cursorNear && wary() ? baseAlpha * kFadedRatio : baseAlpha;
    const double fade = kFadeSpeed * dt;
    g.alpha = g.alpha < targetAlpha ? std::min(g.alpha + fade, targetAlpha) : std::max(g.alpha - fade, targetAlpha);
    g.fading = g.alpha != targetAlpha;

    const SpeedPreset& sp = Speed();
    const int view = kDirView[ViewDir()];
    switch (g.state) {
    case State::Walk: Render(WalkFrame(view, kWalkCycle[(now / sp.walkFrameMs) % 4]), now); break;
    case State::Flee: Render(WalkFrame(view, kWalkCycle[(now / (sp.walkFrameMs / 2)) % 4]), now); break;
    case State::Sulk: Render(SulkFrame(view, static_cast<int>((now / sp.sulkFrameMs) % 2)), now); break;
    case State::Idle: Render(WalkFrame(view, 1), now); break;
    case State::Sleep: Render(SleepFrame(g.sleepSide, static_cast<int>((now / sp.sleepFrameMs) % 2)), now); break;
    case State::Drag: Render(DragFrame(), now); break;
    case State::Bump: Render(HurtFrame(view), now); break;
    case State::Type: Render(TypingFrame(TypingView(), static_cast<int>((now / sp.typeFrameMs) % 4)), now); break;
    }
    UpdateBubble(now);

    if (now - g.lastTopmost >= kTopmostMs) {
        g.lastTopmost = now;
        for (HWND hwnd : {g.pet, g.bubble})
            if (hwnd != g.bubble || g.bubbleShown)
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    const UINT period = TickPeriod();
    if (period != g.tickPeriod) {
        g.tickPeriod = period;
        SetTimer(g.pet, kTimerId, period, nullptr);
    }
}

// Changes the display scale, keeping his feet where they were.
void ApplyScale(int scaleIndex) {
    const double footX = g.x + g.cellW / 2.0, footY = g.y + g.cellH;
    g.cfg.scale = scaleIndex;
    BuildScaledAssets();
    const RECT area = CurrentWorkArea();
    g.x = Clamp(footX - g.cellW / 2.0, static_cast<double>(area.left), static_cast<double>(area.right - g.cellW));
    g.y = Clamp(footY - g.cellH, static_cast<double>(area.top), static_cast<double>(area.bottom - g.cellH));
    if (g.state == State::Walk || g.state == State::Flee) EnterIdle(GetTickCount64());
}

void ApplyClickThrough() {
    LONG_PTR ex = GetWindowLongPtrW(g.pet, GWL_EXSTYLE);
    ex = g.cfg.draggable ? (ex & ~WS_EX_TRANSPARENT) : (ex | WS_EX_TRANSPARENT);
    SetWindowLongPtrW(g.pet, GWL_EXSTYLE, ex);
}

void BeginDrag() {
    POINT cursor;
    GetCursorPos(&cursor);
    g.dragOffset = {cursor.x - std::lround(g.x), cursor.y - std::lround(g.y)};
    g.dragLean = 0;
    g.shakeX = 0;
    g.state = State::Drag;
    SetCapture(g.pet);
}

void DragTo(POINT cursor) {
    const double nx = cursor.x - g.dragOffset.x;
    const double delta = nx - g.x;
    if (std::fabs(delta) >= 2.0) {
        g.dragLean = delta > 0 ? 1 : -1;
        g.lastDragMove = GetTickCount64();
    }
    g.x = nx;
    g.y = cursor.y - g.dragOffset.y;
    Render(DragFrame(), GetTickCount64());
}

void EndDrag() {
    g.nearSince = 0;
    EnterIdle(GetTickCount64());
    g.dir = ToScreenDir(0);
}

void SetVisible(bool visible) {
    g.visible = visible;
    if (visible) {
        g.lastTick = GetTickCount64();
        g.tickPeriod = kTickMs;
        SetTimer(g.pet, kTimerId, kTickMs, nullptr);
        ShowWindow(g.pet, SW_SHOWNOACTIVATE);
    } else {
        KillTimer(g.pet, kTimerId);
        ShowWindow(g.pet, SW_HIDE);
        HideBubble();
        g.typed.clear();
    }
    UpdateKeyboardListener();
}

// --- Tray -----------------------------------------------------------------

void AddTrayIcon() {
    g.nid = NOTIFYICONDATAW{};
    g.nid.cbSize = sizeof(g.nid);
    g.nid.hWnd = g.tray;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_APP_TRAY;
    g.nid.hIcon = g.icon;
    lstrcpynW(g.nid.szTip, L"Lemmy", ARRAYSIZE(g.nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g.nid);
}

void AppendRadioMenu(HMENU parent, const wchar_t* title, UINT base, const wchar_t* const* names, int count,
                     int selected) {
    HMENU sub = CreatePopupMenu();
    for (int i = 0; i < count; ++i) AppendMenuW(sub, MF_STRING, base + i, names[i]);
    CheckMenuRadioItem(sub, base, base + count - 1, base + selected, MF_BYCOMMAND);
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), title);
}

void ShowTrayMenu() {
    const wchar_t* speedNames[ARRAYSIZE(kSpeeds)];
    for (size_t i = 0; i < ARRAYSIZE(kSpeeds); ++i) speedNames[i] = kSpeeds[i].name;
    const wchar_t* activityNames[ARRAYSIZE(kActivities)];
    for (size_t i = 0; i < ARRAYSIZE(kActivities); ++i) activityNames[i] = kActivities[i].name;

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, g.visible ? L"Hide" : L"Show");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendRadioMenu(menu, L"Size", IDM_SCALE, kScaleNames, ARRAYSIZE(kScales), g.cfg.scale);
    AppendRadioMenu(menu, L"Speed", IDM_SPEED, speedNames, ARRAYSIZE(kSpeeds), g.cfg.speed);
    AppendRadioMenu(menu, L"Activity", IDM_ACTIVITY, activityNames, ARRAYSIZE(kActivities), g.cfg.activity);
    AppendRadioMenu(menu, L"When the mouse approaches", IDM_REACTION, kReactionNames, ARRAYSIZE(kReactionNames),
                    g.cfg.reaction);
    AppendRadioMenu(menu, L"Opacity", IDM_OPACITY, kOpacityNames, ARRAYSIZE(kOpacityNames), g.cfg.opacity);
    AppendRadioMenu(menu, L"Roaming zone", IDM_ZONE, kZoneNames, ARRAYSIZE(kZoneNames), g.cfg.zone);
    AppendMenuW(menu, MF_STRING | (g.cfg.zzz ? MF_CHECKED : 0), IDM_ZZZ, L"Zzz while sleeping");
    AppendMenuW(menu, MF_STRING | (g.cfg.draggable ? MF_CHECKED : 0), IDM_DRAG,
                L"Draggable with the mouse");
    AppendMenuW(menu, MF_STRING | (g.cfg.sleepOnMouseIdle ? MF_CHECKED : 0), IDM_MOUSESLEEP,
                L"Sleep when the mouse is idle");
    AppendMenuW(menu, MF_STRING | (g.cfg.typeWithUser ? MF_CHECKED : 0), IDM_TYPEWITHUSER,
                L"Type on his laptop when I type");
    AppendMenuW(menu, MF_STRING | (g.cfg.keyBubble ? MF_CHECKED : 0) | (g.cfg.typeWithUser ? 0 : MF_GRAYED),
                IDM_BUBBLE, L"Bubble with Lemmy lyrics");
    AppendMenuW(menu, MF_STRING | (g.cfg.spiderMan ? MF_CHECKED : 0) | (g.cfg.zone == kEdges ? 0 : MF_GRAYED),
                IDM_SPIDERMAN, L"Spider-Man mode (feet on the edges)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), IDM_STARTUP, L"Run at startup");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit");

    POINT pt;
    GetCursorPos(&pt);
    // Required so the menu closes when clicking elsewhere; the pet itself never gets focus.
    SetForegroundWindow(g.tray);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                    pt.x, pt.y, 0, g.tray, nullptr);
    PostMessageW(g.tray, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == IDM_TOGGLE) {
        SetVisible(!g.visible);
    } else if (cmd == IDM_QUIT) {
        DestroyWindow(g.tray);
    } else if (cmd == IDM_STARTUP) {
        SetStartup(!StartupEnabled());
    } else if (cmd == IDM_ZZZ) {
        g.cfg.zzz = !g.cfg.zzz;
    } else if (cmd == IDM_DRAG) {
        g.cfg.draggable = !g.cfg.draggable;
        ApplyClickThrough();
    } else if (cmd == IDM_MOUSESLEEP) {
        g.cfg.sleepOnMouseIdle = !g.cfg.sleepOnMouseIdle;
    } else if (cmd == IDM_TYPEWITHUSER) {
        g.cfg.typeWithUser = !g.cfg.typeWithUser;
        UpdateKeyboardListener();
    } else if (cmd == IDM_SPIDERMAN) {
        g.cfg.spiderMan = !g.cfg.spiderMan;
    } else if (cmd == IDM_BUBBLE) {
        g.cfg.keyBubble = !g.cfg.keyBubble;
        g.typed.clear();
    } else if (cmd >= IDM_SCALE && cmd < IDM_SCALE + ARRAYSIZE(kScales)) {
        ApplyScale(static_cast<int>(cmd - IDM_SCALE));
    } else if (cmd >= IDM_SPEED && cmd < IDM_SPEED + ARRAYSIZE(kSpeeds)) {
        g.cfg.speed = static_cast<int>(cmd - IDM_SPEED);
    } else if (cmd >= IDM_ACTIVITY && cmd < IDM_ACTIVITY + ARRAYSIZE(kActivities)) {
        g.cfg.activity = static_cast<int>(cmd - IDM_ACTIVITY);
    } else if (cmd >= IDM_REACTION && cmd < IDM_REACTION + ARRAYSIZE(kReactionNames)) {
        g.cfg.reaction = static_cast<int>(cmd - IDM_REACTION);
        g.nearSince = 0;
    } else if (cmd >= IDM_OPACITY && cmd < IDM_OPACITY + ARRAYSIZE(kOpacityPct)) {
        g.cfg.opacity = static_cast<int>(cmd - IDM_OPACITY);
    } else if (cmd >= IDM_ZONE && cmd < IDM_ZONE + ARRAYSIZE(kZoneNames)) {
        g.cfg.zone = static_cast<int>(cmd - IDM_ZONE);
        // Heads into the new zone right away when idle or walking.
        if (g.cfg.zone != kAnywhere && (g.state == State::Idle || g.state == State::Walk)) EnterWalk(GetTickCount64());
    }
    if (cmd != 0) SaveSettings();
}

LRESULT CALLBACK PetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId) Tick();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return g.cfg.draggable ? HTCLIENT : HTTRANSPARENT;
    case WM_SETCURSOR:
        if (g.cfg.draggable) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (g.cfg.draggable) BeginDrag();
        return 0;
    case WM_MOUSEMOVE:
        if (g.state == State::Drag) {
            POINT cursor;
            GetCursorPos(&cursor);
            DragTo(cursor);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();  // ends the drag via WM_CAPTURECHANGED
        return 0;
    case WM_CAPTURECHANGED:
        if (g.state == State::Drag) EndDrag();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INPUT:
        OnRawInput(reinterpret_cast<HRAWINPUT>(lp));
        break;  // DefWindowProc releases the input
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        case WM_LBUTTONDBLCLK:
            SetVisible(!g.visible);
            break;
        }
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        DestroyWindow(g.bubble);
        DestroyWindow(g.pet);
        PostQuitMessage(0);
        return 0;
    }
    if (g.taskbarCreatedMsg != 0 && msg == g.taskbarCreatedMsg) {
        AddTrayIcon();  // Explorer restarted
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\LemmyPet.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    std::srand(GetTickCount());
    LoadSettings();
    LoadLyrics();
    if (StartupEnabled()) SetStartup(true);  // keep the path current if the exe moved

    if (!LoadSheet(g.sheet, IDR_SPRITES) || !BuildScaledAssets()) {
        MessageBoxW(nullptr, L"Failed to initialize sprites.", L"LemmyPet", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = PetProc;
    wc.lpszClassName = L"LemmyPetWindow";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = TrayProc;
    wc.lpszClassName = L"LemmyPetTray";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"LemmyPetBubble";
    RegisterClassExW(&wc);

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g.x = (work.left + work.right - g.cellW) / 2;
    g.y = work.bottom - g.cellH;

    g.pet = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                            L"LemmyPetWindow", L"Lemmy", WS_POPUP,
                            static_cast<int>(g.x) - g.padX, static_cast<int>(g.y) - g.padTop, g.winW, g.winH,
                            nullptr, nullptr, inst, nullptr);
    g.tray = CreateWindowExW(0, L"LemmyPetTray", L"LemmyPet", WS_OVERLAPPED,
                             0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    // Always click-through, even when the pet is draggable.
    g.bubble = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                               L"LemmyPetBubble", L"Lemmy", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g.pet || !g.tray || !g.bubble) return 1;
    ApplyClickThrough();

    g.icon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    g.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();

    const ULONGLONG start = GetTickCount64();
    EnterIdle(start);
    Render(WalkFrame(kDirView[g.dir], 1), start);
    SetVisible(true);
    // Drops pages only touched during startup (sprite decoding); they are not needed again.
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteDC(g.memDc);
    DeleteObject(g.dib);
    if (g.bubbleDc) DeleteDC(g.bubbleDc);
    if (g.bubbleDib) DeleteObject(g.bubbleDib);
    DeleteObject(g.bubbleFont);
    DestroyIcon(g.icon);
    CloseHandle(mutex);
    return 0;
}

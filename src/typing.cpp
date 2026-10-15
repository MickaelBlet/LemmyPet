#include "typing.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "behavior.h"
#include "render.h"

namespace {

constexpr ULONGLONG kTypeLingerMs = 2000;  // keeps typing this long after the user's last keystroke
constexpr ULONGLONG kTypeBurstMs = 1500;   // two keystrokes this close start a typing session
constexpr ULONGLONG kBubbleLingerMs = 5000;  // bubble text stays this long after the last keystroke
constexpr size_t kBubbleMaxChars = 12;     // last characters of the current line shown in the bubble
constexpr ULONGLONG kDotsMs = 10000;       // "..." bubble shown after each finished line
constexpr ULONGLONG kDotsStepMs = 300;     // one more dot lit per step, then none

// Whether he types along with the user.
bool TypesAlong(const Pet& p) { return p.cfg.visible && p.cfg.typeWithUser && p.def.canType; }

// Starts a random line, other than the current one when possible.
void NextLyric(Pet& p) {
    const int count = static_cast<int>(p.def.lyrics.size());
    p.lyric = (p.lyric + RandRange(count > 1 ? 1 : 0, count - 1)) % count;
    p.lyricPos = 0;
}

// Every keystroke types the next character of the current line, whatever the key: what the user
// types is never read. A finished line gives way to "..." for a while, then to the next one.
void RecordKeystroke(Pet& p, ULONGLONG now) {
    if (now - p.lastKeystroke > kBubbleLingerMs) {  // the bubble has faded: start afresh
        p.typed.clear();
        NextLyric(p);
    }
    const std::wstring& line = p.def.lyrics[p.lyric];
    if (p.lyricPos >= line.size()) {
        p.typed.clear();
        p.dotsStart = now;
        p.dotsUntil = now + kDotsMs;
        NextLyric(p);
        return;
    }
    p.dotsUntil = 0;
    p.typed += line[p.lyricPos++];
    if (p.typed.size() > kBubbleMaxChars) p.typed.erase(0, p.typed.size() - kBubbleMaxChars);
}

// Mirrors the user typing: a burst of keystrokes opens the laptop, each further one keeps it open.
void OnKeystroke(Pet& p) {
    const ULONGLONG now = GetTickCount64();
    if (p.cfg.keyBubble && HasBubble(p)) RecordKeystroke(p, now);
    const bool burst = now - p.lastKeystroke <= kTypeBurstMs;
    p.lastKeystroke = now;
    if (p.state == State::Type ||
        (burst && (p.state == State::Idle || p.state == State::Walk || p.state == State::Sleep)))
        EnterType(p, now, kTypeLingerMs);
    // He stays at the laptop while the bubble shows text.
    if (p.state == State::Type && !p.typed.empty()) EnterType(p, now, kBubbleLingerMs);
    // After a finished line he stays at the laptop while the "..." bubble lasts.
    if (p.state == State::Type && p.dotsUntil > now) EnterType(p, now, p.dotsUntil - now);
}

// Draws a pixel-art speech bubble around text or, when dots >= 0, three dots of which the first `dots`
// are lit. Its tail points toward his head, on the side matching Orientation(): 0 bottom (left end),
// 1 left, 2 top (left end), 3 right (top end of both walls).
bool DrawBubble(Pet& p, const std::wstring& text, int dots, int side) {
    const int u = std::max(1, static_cast<int>(std::lround(Scale(p))));  // bubble pixel size
    if (!p.bubbleDc) p.bubbleDc = CreateCompatibleDC(nullptr);
    if (!p.bubbleDc) return false;
    SelectObject(p.bubbleDc, p.bubbleFont);
    SIZE ext{8 * u, 2 * u};  // dots: 2x2 bubble pixels each, 1 apart
    if (dots < 0) GetTextExtentPoint32W(p.bubbleDc, text.c_str(), static_cast<int>(text.size()), &ext);
    const int pad = u;
    // In bubble pixels: outline, padding, text; the tail adds 2 rows or columns on its side.
    const int bw = (ext.cx + 2 * pad + u - 1) / u + 2, bh = (ext.cy + 2 * pad + u - 1) / u + 2;
    const int ox = side == 1 ? 2 : 0, oy = side == 2 ? 2 : 0;  // body offset
    const int w = (bw + (side % 2 ? 2 : 0)) * u, h = (bh + (side % 2 ? 0 : 2)) * u;
    if (w != p.bubbleW || h != p.bubbleH) {
        if (!CreateDib(p.bubbleDc, p.bubbleDib, p.bubbleBits, w, h)) return false;
        p.bubbleW = w;
        p.bubbleH = h;
    }
    GdiFlush();
    std::memset(p.bubbleBits, 0, static_cast<size_t>(w) * h * sizeof(uint32_t));
    constexpr uint32_t kInk = 0xFF181820, kPaper = 0xFFFFFFFF;
    const auto put = [&](int bx, int by, uint32_t c) {
        for (int y = by * u; y < (by + 1) * u; ++y)
            for (int x = bx * u; x < (bx + 1) * u; ++x) p.bubbleBits[y * w + x] = c;
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
    p.bubbleTip = side == 0 ? POINT{tip, h} : side == 1 ? POINT{0, tip} : side == 2 ? POINT{tip, 0} : POINT{w, tip};
    if (dots >= 0) {
        for (int i = 0; i < dots; ++i)
            for (int d = 0; d < 4; ++d) put(ox + 2 + 3 * i + d % 2, oy + 2 + d / 2, kInk);
        return true;
    }

    SetBkMode(p.bubbleDc, TRANSPARENT);
    SetTextColor(p.bubbleDc, RGB(0x18, 0x18, 0x20));
    TextOutW(p.bubbleDc, (ox + 1) * u + pad, (oy + 1) * u + pad, text.c_str(), static_cast<int>(text.size()));
    GdiFlush();
    // GDI text leaves alpha at 0; everything drawn is opaque (the background stays fully transparent).
    for (int i = 0; i < w * h; ++i)
        if (p.bubbleBits[i] & 0xFFFFFF) p.bubbleBits[i] |= 0xFF000000;
    return true;
}

}  // namespace

bool HasBubble(const Pet& p) { return p.def.canType && !p.def.lyrics.empty(); }

// Keystrokes arrive as raw input (no polling), only while a pet is shown and types along with the user.
void UpdateKeyboardListener() {
    const bool on = std::any_of(g.pets.begin(), g.pets.end(), [](const auto& p) { return TypesAlong(*p); });
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
    if (down && !g.keyDown[vk])
        for (const auto& p : g.pets)
            if (TypesAlong(*p)) {
                OnKeystroke(*p);
                if (!g.Paused()) Tick(*p);  // shows it now rather than on his next (maybe slow) tick
            }
    g.keyDown[vk] = down;
}

void HideBubble(Pet& p) {
    if (p.bubbleShown) ShowWindow(p.bubble, SW_HIDE);
    p.bubbleShown = false;
}

// Time until the bubble's content changes on its own (next dot, end of the "..." or of the text linger).
ULONGLONG BubbleWait(const Pet& p, ULONGLONG now) {
    ULONGLONG wait = ULLONG_MAX;
    if (!p.typed.empty() && p.lastKeystroke + kBubbleLingerMs >= now) wait = p.lastKeystroke + kBubbleLingerMs + 1 - now;
    if (p.typed.empty() && now < p.dotsUntil)
        wait = std::min({wait, p.dotsUntil - now, kDotsStepMs - (now - p.dotsStart) % kDotsStepMs});
    return wait;
}

// Shows the line he is typing in a bubble above his head, then "..." once it is finished.
void UpdateBubble(Pet& p, ULONGLONG now) {
    if (!p.typed.empty() && now - p.lastKeystroke > kBubbleLingerMs) p.typed.clear();
    const bool waiting = p.typed.empty() && now < p.dotsUntil;
    if (!p.cfg.visible || !p.cfg.keyBubble || !HasBubble(p) || p.state != State::Type || (p.typed.empty() && !waiting)) {
        HideBubble(p);
        return;
    }
    const int dots = waiting ? static_cast<int>((now - p.dotsStart) / kDotsStepMs % 4) : -1;
    const int orient = Orientation(p);
    const bool redraw = p.bubbleStale || dots != p.bubbleDots || orient != p.bubbleSide ||
                        (dots < 0 && p.typed != p.bubbleText);
    if (redraw) {
        if (!DrawBubble(p, p.typed, dots, orient)) return;
        p.bubbleText = p.typed;
        p.bubbleDots = dots;
        p.bubbleSide = orient;
        p.bubbleStale = false;
    }
    const RECT area = CurrentWorkArea(p);
    // Top of his head on screen, wherever his feet stick (see BlitCanvas).
    const Sprite& f = *p.shown;
    const LONG headOff = std::lround((p.def.cellH - f.h + f.top) * Scale(p));
    const LONG x = std::lround(p.x), y = std::lround(p.y);
    const LONG headX = orient == 1 ? x + p.cellW - headOff : (orient == 3 ? x + headOff : x + p.cellW / 2);
    const LONG headY = orient == 0 ? y + headOff : (orient == 2 ? y + p.cellH - headOff : y + p.cellH / 2);
    // Beyond his head, tail tip on it.
    const LONG bx = headX - p.bubbleTip.x, by = headY - p.bubbleTip.y;
    const POINT pos{Clamp(bx, area.left, area.right - p.bubbleW), Clamp(by, area.top, area.bottom - p.bubbleH)};
    const BYTE alpha = static_cast<BYTE>(std::lround(p.alpha));
    if (redraw || pos.x != p.bubblePos.x || pos.y != p.bubblePos.y || alpha != p.bubbleAlpha) {
        SIZE size{p.bubbleW, p.bubbleH};
        POINT src{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        // Same bitmap: only moves or fades it.
        UpdateLayeredWindow(p.bubble, nullptr, const_cast<POINT*>(&pos), redraw ? &size : nullptr,
                            redraw ? p.bubbleDc : nullptr, redraw ? &src : nullptr, 0, &blend, ULW_ALPHA);
        p.bubblePos = pos;
        p.bubbleAlpha = alpha;
    }
    if (!p.bubbleShown) {
        ShowWindow(p.bubble, SW_SHOWNOACTIVATE);
        p.bubbleShown = true;
    }
}

#include "behind.h"

#include <climits>
#include <cstdlib>

#include "behavior.h"
#include "render.h"

namespace {

constexpr UINT kBehindMs = 250;            // sampling period
constexpr int kChannelTolerance = 40;      // per color channel, below which a pixel counts as unchanged
constexpr int kMinChangedPermille = 10;    // changed pixels of his body area that count as a change
constexpr ULONGLONG kChangeWindowMs = 2000;  // a second change within this time sends him away

bool Watches(const Pet& p) { return p.cfg.visible && p.cfg.avoidChanges && p.hiddenFromCapture; }

bool Differs(uint32_t a, uint32_t b) {
    for (int shift = 0; shift < 24; shift += 8)
        if (std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF)) >
            kChannelTolerance)
            return true;
    return false;
}

// Bounding box (sample px) of the pixels that changed since the last sample, which it then replaces.
// Empty when too few changed.
RECT TakeChanges(Pet& p, int w, int h) {
    RECT box{LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    size_t count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            if (!Differs(p.behindBits[i], p.behindLast[i])) continue;
            ++count;
            box = {std::min<LONG>(box.left, x), std::min<LONG>(box.top, y), std::max<LONG>(box.right, x + 1),
                   std::max<LONG>(box.bottom, y + 1)};
        }
    }
    p.behindLast.assign(p.behindBits, p.behindBits + static_cast<size_t>(w) * h);
    if (count * 1000 < static_cast<size_t>(w) * h * kMinChangedPermille) return {};
    return box;
}

}  // namespace

void UpdateBehindWatch() {
    bool any = false;
    for (const auto& o : g.pets) {
        Pet& p = *o;
        if (p.cfg.avoidChanges != p.hiddenFromCapture) {
            const DWORD affinity = p.cfg.avoidChanges ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
            // Fails before Windows 10 2004: he would then see himself, so he does not watch.
            if (SetWindowDisplayAffinity(p.hwnd, affinity) && SetWindowDisplayAffinity(p.bubble, affinity))
                p.hiddenFromCapture = p.cfg.avoidChanges;
        }
        if (!Watches(p)) FreeBehind(p);
        any = any || Watches(p);
    }
    if (any) SetTimer(g.tray, kBehindTimerId, kBehindMs, nullptr);
    else KillTimer(g.tray, kBehindTimerId);
}

void CheckBehind() {
    if (g.Paused()) {  // the screen may look different afterwards: starts over
        for (const auto& p : g.pets) p->behindRect = {};
        return;
    }
    const ULONGLONG now = GetTickCount64();
    HDC screen = nullptr;
    for (const auto& o : g.pets) {
        Pet& p = *o;
        if (!Watches(p)) continue;
        // Moved (walking, dragged, teleported): what is behind him differs anyway; starts over once he stops.
        const RECT r = BodyRect(p);
        const int w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0) continue;
        if (!EqualRect(&r, &p.behindRect) || p.state == State::Drag || p.state == State::Flee) {
            p.behindRect = {};
            p.behindChangeAt = 0;
        }
        if (w != p.behindW || h != p.behindH) {
            if (!CreateDib(p.behindDc, p.behindDib, p.behindBits, w, h)) continue;
            p.behindW = w;
            p.behindH = h;
        }
        if (!screen) screen = GetDC(nullptr);
        if (!BitBlt(p.behindDc, 0, 0, w, h, screen, r.left, r.top, SRCCOPY)) continue;
        GdiFlush();
        if (IsRectEmpty(&p.behindRect)) {
            p.behindLast.assign(p.behindBits, p.behindBits + static_cast<size_t>(w) * h);
            p.behindRect = r;
            continue;
        }
        RECT changed = TakeChanges(p, w, h);
        if (IsRectEmpty(&changed)) continue;
        // A single change (a hover highlight, a tooltip) is let go; a second one soon after is not.
        if (!p.behindChangeAt || now - p.behindChangeAt > kChangeWindowMs) {
            p.behindChangeAt = now;
            continue;
        }
        p.behindChangeAt = 0;
        OffsetRect(&changed, r.left, r.top);
        if (MoveOutOfTheWay(p, changed, now)) Tick(p);  // moves now rather than on his next (maybe slow) tick
    }
    if (screen) ReleaseDC(nullptr, screen);
}

void FreeBehind(Pet& p) {
    if (p.behindDc) DeleteDC(p.behindDc);
    if (p.behindDib) DeleteObject(p.behindDib);
    p.behindDc = nullptr;
    p.behindDib = nullptr;
    p.behindBits = nullptr;
    p.behindW = p.behindH = 0;
    p.behindLast.clear();
    p.behindLast.shrink_to_fit();
    p.behindRect = {};
    p.behindChangeAt = 0;
}

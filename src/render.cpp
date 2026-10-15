#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Layout in sprite pixels; multiplied by the display scale.
constexpr int kPadXUnits = 12;    // transparent room around the cell for the floating "Zzz"
constexpr int kPadTopUnits = 23;
constexpr uint32_t kHalo = 0x4D4D4D4D;  // premultiplied white at 30 % opacity
constexpr int kZzzCount = 3;
constexpr int kBubbleFontUnits = 4;

constexpr size_t kFramesMaxBytes = 16 << 20;  // scaled frame cache budget, per pet

// Scales a sprite (nearest neighbor), bottom-centered, into a cell-sized buffer, with a 1 px white halo
// around him at any scale (8-neighborhood) if enabled.
Pixels ScaleFrame(Pet& p, const Sprite& f) {
    const double s = Scale(p);
    const int fw = static_cast<int>(std::lround(f.w * s)), fh = static_cast<int>(std::lround(f.h * s));
    const int ox = (p.cellW - fw) / 2, oy = p.cellH - fh;
    // Sprite pixel at a cell point (0 outside the sprite).
    const auto src = [&](int x, int y) -> uint32_t {
        x -= ox;
        y -= oy;
        if (x < 0 || y < 0 || x >= fw || y >= fh) return 0;
        const int sx = std::min(f.w - 1, static_cast<int>(x / s)), sy = std::min(f.h - 1, static_cast<int>(y / s));
        return f.sheet->px[static_cast<size_t>(f.y + sy) * f.sheet->w + f.x + (f.mirror ? f.w - 1 - sx : sx)];
    };
    Pixels cell(static_cast<size_t>(p.cellW) * p.cellH);
    for (int y = 0; y < p.cellH; ++y) {
        uint32_t* dst = &cell[static_cast<size_t>(y) * p.cellW];
        for (int x = 0; x < p.cellW; ++x) {
            dst[x] = src(x, y);
            if (dst[x] >> 24 || !p.cfg.halo) continue;
            for (int dy = -1; dy <= 1 && !dst[x]; ++dy)
                for (int dx = -1; dx <= 1 && !dst[x]; ++dx)
                    if (src(x + dx, y + dy) >> 24) dst[x] = kHalo;
        }
    }
    return cell;
}

// Draws a sprite into the cell area of the canvas, scaled once then cached (dropped whole past the budget).
void DrawFrame(Pet& p, const Sprite& f) {
    auto it = p.frames.find(&f);
    if (it == p.frames.end()) {
        const size_t bytes = static_cast<size_t>(p.cellW) * p.cellH * sizeof(uint32_t);
        if (p.framesBytes + bytes > kFramesMaxBytes) {
            p.frames.clear();
            p.framesBytes = 0;
        }
        it = p.frames.emplace(&f, ScaleFrame(p, f)).first;
        p.framesBytes += bytes;
    }
    for (int y = 0; y < p.cellH; ++y)
        std::memcpy(&p.canvas[static_cast<size_t>(p.padTop + y) * p.winW + p.padX],
                    &it->second[static_cast<size_t>(y) * p.cellW], p.cellW * sizeof(uint32_t));
}

bool CreateBackBuffer(Pet& p) {
    p.canvas.assign(static_cast<size_t>(p.winW) * p.winH, 0);
    // Room for the canvas turned to any angle around the cell center (see BlitCanvasTurned).
    const double reach = std::hypot(p.winW / 2.0, p.padTop + p.cellH / 2.0);
    p.dibSize = 2 * static_cast<int>(std::ceil(reach)) + 2;
    return CreateDib(p.memDc, p.dib, p.dibBits, p.dibSize, p.dibSize);
}

// Composites a straight-alpha color over a premultiplied back-buffer pixel.
void BlendPixel(Pet& p, int x, int y, uint32_t r, uint32_t gr, uint32_t b, double a) {
    if (x < 0 || y < 0 || x >= p.dibSize || y >= p.dibSize || a <= 0.0) return;
    uint32_t& d = p.dibBits[y * p.dibSize + x];
    const uint32_t sa = static_cast<uint32_t>(std::lround(a * 255.0));
    const auto mix = [&](int shift, uint32_t c) {
        return ((c * sa + ((d >> shift) & 0xFF) * (255 - sa)) / 255) << shift;
    };
    d = mix(24, 255) | mix(16, r) | mix(8, gr) | mix(0, b);
}

// Draws a pixel-art "Z" (5x5 glyph, s px per glyph pixel) with a 1 px dark outline.
void DrawZ(Pet& p, int x0, int y0, int s, double a) {
    static const char* const kGlyph[5] = {"#####", "...#.", "..#..", ".#...", "#####"};
    const int n = 5 * s;
    const auto inGlyph = [&](int px, int py) {
        return px >= 0 && py >= 0 && px < n && py < n && kGlyph[py / s][px / s] == '#';
    };
    for (int py = -1; py <= n; ++py) {
        for (int px = -1; px <= n; ++px) {
            if (inGlyph(px, py)) {
                BlendPixel(p, x0 + px, y0 + py, 240, 248, 255, a);
                continue;
            }
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
                for (int dx = -1; dx <= 1 && !edge; ++dx) edge = inGlyph(px + dx, py + dy);
            if (edge) BlendPixel(p, x0 + px, y0 + py, 40, 48, 72, a);
        }
    }
}

// Maps a canvas point to the back buffer once the canvas is turned toward the given edge (see BlitCanvas).
POINT CanvasToWindow(Pet& p, int cx, int cy, int orient) {
    const int w = p.winW, h = p.winH;
    return orient == 0 ? POINT{cx, cy}
         : orient == 1 ? POINT{h - 1 - cy, cx}
         : orient == 2 ? POINT{w - 1 - cx, h - 1 - cy}
                       : POINT{cy, w - 1 - cx};
}

// Z's spawn one after another above the sleeping head, rising and drifting away from his feet while fading.
// Drawn into the back buffer after rotation so the glyphs stay upright (a turned Z reads as N).
void DrawZzz(Pet& p, ULONGLONG sleptMs, int orient) {
    const double u = Scale(p);
    const ULONGLONG life = Speed(p).zzzLifeMs;
    for (int i = 0; i < kZzzCount; ++i) {
        const long long age = static_cast<long long>(sleptMs) - i * static_cast<long long>(life / kZzzCount);
        if (age < 0) continue;
        const double t = static_cast<double>(age % life) / life;
        const double a = t < 0.15 ? t / 0.15 : (t > 0.7 ? (1.0 - t) / 0.3 : 1.0);
        const int s = std::max(1, static_cast<int>(u * (t < 0.5 ? 1.0 : 1.5)));
        const int dx = static_cast<int>((p.def.zzzX + t * 10.0 + std::sin(t * 2.0 * kPi) * 2.0) * u);
        const int x = p.padX + (p.sleepSide == 0 ? dx : p.cellW - dx - 5 * s);
        const int y = p.padTop + static_cast<int>((p.def.zzzY - t * 33.0) * u);
        const int half = 5 * s / 2;
        const POINT c = CanvasToWindow(p, x + half, y + half, orient);
        DrawZ(p, c.x - half, c.y - half, s, a);
    }
}

// Copies the canvas into the back buffer, its bottom (his feet) turned toward the given edge:
// 0 floor, 1 left wall (clockwise), 2 ceiling, 3 right wall (counterclockwise).
void BlitCanvas(Pet& p, int orient) {
    const int w = p.winW, h = p.winH, s = p.dibSize;
    if (orient == 0) {
        for (int cy = 0; cy < h; ++cy)
            std::memcpy(&p.dibBits[cy * s], &p.canvas[cy * w], w * sizeof(uint32_t));
        return;
    }
    for (int cy = 0; cy < h; ++cy) {
        for (int cx = 0; cx < w; ++cx) {
            const POINT q = CanvasToWindow(p, cx, cy, orient);
            p.dibBits[q.y * s + q.x] = p.canvas[cy * w + cx];
        }
    }
}

// Fills the whole back buffer, shown at pos on screen, with the canvas turned clockwise by `angle` quarter
// turns around the cell center (nearest neighbor): his feet roll on a quarter circle touching both edges.
void BlitCanvasTurned(Pet& p, double angle, POINT pos) {
    const int s = p.dibSize;
    const LONG x = std::lround(p.x), y = std::lround(p.y);
    const double pivotX = x + p.cellW / 2.0 - pos.x, pivotY = y + p.cellH / 2.0 - pos.y;
    const double srcX = p.padX + p.cellW / 2.0, srcY = p.padTop + p.cellH / 2.0;
    const double c = std::cos(angle * kPi / 2), sn = std::sin(angle * kPi / 2);
    for (int py = 0; py < s; ++py) {
        const double vy = py + 0.5 - pivotY;
        uint32_t* dst = &p.dibBits[py * s];
        for (int px = 0; px < s; ++px) {
            const double vx = px + 0.5 - pivotX;
            const int cx = static_cast<int>(std::floor(srcX + vx * c + vy * sn));
            const int cy = static_cast<int>(std::floor(srcY - vx * sn + vy * c));
            dst[px] = cx >= 0 && cy >= 0 && cx < p.winW && cy < p.winH ? p.canvas[cy * p.winW + cx] : 0;
        }
    }
}

}  // namespace

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

// Rebuilds the scaling map and back buffer for the current scale.
bool BuildScaledAssets(Pet& p) {
    const double s = Scale(p);
    p.cellW = static_cast<int>(std::lround(p.def.cellW * s));
    p.cellH = static_cast<int>(std::lround(p.def.cellH * s));
    p.padX = static_cast<int>(std::lround(kPadXUnits * s));
    p.padTop = static_cast<int>(std::lround(kPadTopUnits * s));
    p.winW = p.cellW + 2 * p.padX;
    p.winH = p.cellH + p.padTop;
    DropFrames(p);
    if (p.bubbleFont) DeleteObject(p.bubbleFont);
    p.bubbleFont = CreateFontW(-std::max(10, static_cast<int>(std::lround(kBubbleFontUnits * s))), 0, 0, 0, FW_BOLD,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    p.bubbleStale = true;
    return CreateBackBuffer(p);
}

// Forgets the scaled frames (e.g. after a halo change); the next Render redraws.
void DropFrames(Pet& p) {
    p.frames.clear();
    p.framesBytes = 0;
    p.shown = nullptr;
}

// Shows a sprite frame (plus the "Zzz" overlay while sleeping) at the current position.
void Render(Pet& p, const Sprite& frame, ULONGLONG now) {
    const double angle = p.angle;
    const bool turned = angle != std::floor(angle);  // rolling around a corner
    const int orient = turned ? -1 : static_cast<int>(angle);
    const LONG x = std::lround(p.x), y = std::lround(p.y);
    // Window top-left so that the rotated canvas puts the cell at (x, y).
    POINT pos = orient == 0 ? POINT{x - p.padX, y - p.padTop}
              : orient == 1 ? POINT{x, y - p.padX}
              : orient == 2 ? POINT{x - p.padX, y}
              : orient == 3 ? POINT{x - p.padTop, y - p.padX}
                            : POINT{x + (p.cellW - p.dibSize) / 2, y + (p.cellH - p.dibSize) / 2};
    if (!turned) (orient % 2 ? pos.y : pos.x) += p.shakeX;  // along the edge he stands on
    const BYTE alpha = static_cast<BYTE>(std::lround(p.alpha));
    const ULONGLONG slept = now - p.sleepStart;
    const long long overlay =
        p.state == State::Sleep && p.cfg.zzz ? static_cast<long long>(slept / kZzzStepMs) : -1;
    if (&frame == p.shown && overlay == p.shownOverlay && angle == p.shownAngle && pos.x == p.shownPos.x &&
        pos.y == p.shownPos.y && alpha == p.shownAlpha)
        return;
    const bool recompose = &frame != p.shown || overlay != p.shownOverlay;
    if (&frame != p.shown) DrawFrame(p, frame);  // the canvas padding stays transparent
    const bool redraw = turned || recompose || angle != p.shownAngle;
    if (turned) {
        GdiFlush();
        BlitCanvasTurned(p, angle, pos);
    } else if (redraw) {
        GdiFlush();
        BlitCanvas(p, orient);
        if (overlay >= 0) DrawZzz(p, slept, orient);
    }
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    if (redraw) {
        SIZE size = turned ? SIZE{p.dibSize, p.dibSize} : orient % 2 ? SIZE{p.winH, p.winW} : SIZE{p.winW, p.winH};
        POINT src{0, 0};
        UpdateLayeredWindow(p.hwnd, nullptr, const_cast<POINT*>(&pos), &size, p.memDc, &src, 0, &blend, ULW_ALPHA);
    } else {
        // Same bitmap: only moves or fades the window, without sending the pixels again.
        UpdateLayeredWindow(p.hwnd, nullptr, const_cast<POINT*>(&pos), nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA);
    }
    p.shown = &frame;
    p.shownPos = pos;
    p.shownAlpha = alpha;
    p.shownOverlay = overlay;
    p.shownAngle = angle;
}

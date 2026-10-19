#include "window.h"

#include <algorithm>
#include <cmath>

#include "behavior.h"
#include "options.h"
#include "render.h"
#include "settings.h"
#include "typing.h"

namespace {

void BeginDrag(Pet& p) {
    POINT cursor;
    GetCursorPos(&cursor);
    p.dragOffset = {cursor.x - std::lround(p.x), cursor.y - std::lround(p.y)};
    p.dragLean = 0;
    p.shakeX = 0;
    p.state = State::Drag;
    SetCapture(p.hwnd);
}

void DragTo(Pet& p, POINT cursor) {
    const double nx = cursor.x - p.dragOffset.x;
    const double delta = nx - p.x;
    if (std::fabs(delta) >= 2.0) {
        p.dragLean = delta > 0 ? 1 : -1;
        p.lastDragMove = GetTickCount64();
    }
    p.x = nx;
    p.y = cursor.y - p.dragOffset.y;
    const ULONGLONG now = GetTickCount64();
    Render(p, ShownFrame(p, now), now);
}

void EndDrag(Pet& p) {
    p.nearSince = 0;
    EnterIdle(p, GetTickCount64());
    p.dir = ToScreenDir(p, 0);
}

// Creates his windows, standing on the floor at `x` (screen px, his cell center).
bool CreatePet(Pet& p, HINSTANCE inst, int x) {
    LoadSettings(p);
    if (!BuildScaledAssets(p)) return false;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    p.x = x - p.cellW / 2;
    p.y = work.bottom - p.cellH;
    p.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                             L"LemmyPetWindow", p.def.name.c_str(), WS_POPUP,
                             static_cast<int>(p.x) - p.padX, static_cast<int>(p.y) - p.padTop, p.winW, p.winH,
                             nullptr, nullptr, inst, nullptr);
    // Always click-through, even when the pet is draggable.
    p.bubble = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                               L"LemmyPetBubble", p.def.name.c_str(), WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst,
                               nullptr);
    if (!p.hwnd || !p.bubble) return false;
    SetWindowLongPtrW(p.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&p));
    ApplyClickThrough(p);
    const ULONGLONG start = GetTickCount64();
    EnterIdle(p, start);
    Render(p, p.def.Stand(p.dir), start);
    SetVisible(p, p.cfg.visible);
    return true;
}

void DestroyPet(Pet& p) {
    if (p.bubble) DestroyWindow(p.bubble);
    if (p.hwnd) DestroyWindow(p.hwnd);
    if (p.memDc) DeleteDC(p.memDc);
    if (p.dib) DeleteObject(p.dib);
    if (p.bubbleDc) DeleteDC(p.bubbleDc);
    if (p.bubbleDib) DeleteObject(p.bubbleDib);
    if (p.bubbleFont) DeleteObject(p.bubbleFont);
}

LRESULT CALLBACK PetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Pet* pet = reinterpret_cast<Pet*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!pet) return DefWindowProcW(hwnd, msg, wp, lp);
    Pet& p = *pet;
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId) Tick(p);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return p.cfg.draggable ? HTCLIENT : HTTRANSPARENT;
    case WM_SETCURSOR:
        if (p.cfg.draggable) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (p.cfg.draggable) BeginDrag(p);
        return 0;
    case WM_MOUSEMOVE:
        if (p.state == State::Drag) {
            POINT cursor;
            GetCursorPos(&cursor);
            DragTo(p, cursor);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) ReleaseCapture();  // ends the drag via WM_CAPTURECHANGED
        return 0;
    case WM_CAPTURECHANGED:
        if (p.state == State::Drag) EndDrag(p);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void ApplyClickThrough(Pet& p) {
    LONG_PTR ex = GetWindowLongPtrW(p.hwnd, GWL_EXSTYLE);
    ex = p.cfg.draggable ? (ex & ~WS_EX_TRANSPARENT) : (ex | WS_EX_TRANSPARENT);
    SetWindowLongPtrW(p.hwnd, GWL_EXSTYLE, ex);
}

void SetVisible(Pet& p, bool visible) {
    p.cfg.visible = visible;
    if (visible) {
        p.lastTick = GetTickCount64();
        if (!g.Paused()) ArmTimer(p, kTickMs);
        ShowWindow(p.hwnd, SW_SHOWNOACTIVATE);
    } else {
        KillTimer(p.hwnd, kTimerId);
        ShowWindow(p.hwnd, SW_HIDE);
        HideBubble(p);
        p.typed.clear();
    }
    UpdateKeyboardListener();
}

// Stops or restarts the shown pets' ticks as the session locks or the display turns off and back.
void UpdatePause() {
    for (const auto& p : g.pets) {
        if (!p->cfg.visible) continue;
        if (g.Paused()) {
            KillTimer(p->hwnd, kTimerId);
        } else {
            p->lastTick = GetTickCount64();
            ArmTimer(*p, kTickMs);
        }
    }
}

// Shows every pet, or hides them all when one is shown.
void ToggleAll() {
    const bool show = std::none_of(g.pets.begin(), g.pets.end(), [](const auto& p) { return p->cfg.visible; });
    for (const auto& p : g.pets) {
        SetVisible(*p, show);
        SaveSettings(*p);
    }
}

void RegisterPetClasses(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = PetProc;
    wc.lpszClassName = L"LemmyPetWindow";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"LemmyPetBubble";
    RegisterClassExW(&wc);
}

bool CreatePets(HINSTANCE inst) {
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int count = static_cast<int>(g.defs.size());
    for (int i = 0; i < count; ++i) {
        g.pets.push_back(std::make_unique<Pet>(*g.defs[i]));
        if (!CreatePet(*g.pets.back(), inst, work.left + (work.right - work.left) * (i + 1) / (count + 1)))
            return false;
    }
    return true;
}

void DestroyPets() {
    for (const auto& p : g.pets) DestroyPet(*p);
}

std::wstring ReloadPets() {
    std::map<std::wstring, unsigned long long> stamps = StampPets();
    if (stamps == g.stamps) return {};
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    const auto find = [](const std::wstring& id) {
        return std::find_if(g.pets.begin(), g.pets.end(), [&](const auto& p) { return p->def.id == id; });
    };
    const auto drop = [](std::vector<std::unique_ptr<Pet>>::iterator it) {
        const PetDef* def = &(*it)->def;
        DestroyPet(**it);
        g.pets.erase(it);
        g.defs.erase(std::find_if(g.defs.begin(), g.defs.end(), [&](const auto& d) { return d.get() == def; }));
    };
    std::wstring errors;
    for (auto it = g.pets.begin(); it != g.pets.end();) {
        if (stamps.count((*it)->def.id)) {
            ++it;
        } else {
            const auto next = it - g.pets.begin();
            drop(it);
            it = g.pets.begin() + next;
        }
    }
    for (const auto& [id, stamp] : stamps) {
        const auto old = g.stamps.find(id);
        if (old != g.stamps.end() && old->second == stamp) continue;
        std::wstring error;
        std::unique_ptr<PetDef> def = LoadPetDef(id, error);
        if (!def) {  // keeps the former version, if any
            errors += L"pets\\" + id + L": " + error + L"\n";
            continue;
        }
        // A changed pet comes back where he stood; a new one somewhere on the floor of the main screen.
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int x = RandRange(work.left + def->cellW, work.right - def->cellW);
        const auto it = find(id);
        if (it != g.pets.end()) {
            x = static_cast<int>(std::lround((*it)->x)) + (*it)->cellW / 2;
            SaveSettings(**it);
            drop(it);
        }
        g.defs.push_back(std::move(def));
        auto pet = std::make_unique<Pet>(*g.defs.back());
        if (!CreatePet(*pet, inst, x)) {
            DestroyPet(*pet);
            g.defs.pop_back();
            errors += L"pets\\" + id + L": cannot create his windows\n";
            continue;
        }
        g.pets.push_back(std::move(pet));
    }
    std::sort(g.pets.begin(), g.pets.end(), [](const auto& a, const auto& b) {
        return _wcsicmp(a->def.id.c_str(), b->def.id.c_str()) < 0;
    });
    g.stamps = std::move(stamps);
    UpdateKeyboardListener();
    RebuildOptions();
    return errors;
}

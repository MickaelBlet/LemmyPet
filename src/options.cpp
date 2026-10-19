#include "options.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>

#include "behavior.h"
#include "render.h"
#include "resource.h"
#include "settings.h"
#include "typing.h"
#include "window.h"

namespace {

enum : int {
    IDC_PETS = 100,
    IDC_STARTUP,
    IDC_QUIT,
    IDC_ALL,
    IDC_LABEL = 200,   // + kCombos index
    IDC_COMBO = 300,   // + kCombos index
    IDC_TOGGLE = 400,  // + kToggles index
    IDC_SLIDER_LABEL = 500,  // + kSliders index
    IDC_SLIDER = 600,        // + kSliders index
    IDC_SLIDER_VALUE = 700,  // + kSliders index
};

constexpr UINT WM_APP_RESELECT = WM_APP + 1;  // wParam: pet index

const wchar_t kClassName[] = L"LemmyPetOptions";
constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr DWORD kExStyle = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;

// Layout, in pixels at 96 dpi.
constexpr int kMargin = 12;     // around the pet list, the page and the buttons
constexpr int kListW = 130;     // pet list
constexpr int kPad = 14;        // between the pet list and the page
constexpr int kLabelW = 190;
constexpr int kComboW = 200;
constexpr int kCheckH = 20;
constexpr int kToggleRowH = 26;
constexpr int kButtonH = 26;
constexpr int kValueW = 50;     // slider value

const wchar_t* const kReactionNames[] = {L"Teleport to bottom-right", L"Flee to a corner", L"Ignore it"};
const wchar_t* const kOpacityNames[] = {L"100% (opaque)", L"80%", L"60%", L"40%", L"20%"};
const wchar_t* const kZoneNames[] = {L"Anywhere", L"Screen edges", L"Bottom of screen"};

// Trackbar positions are steps of `step` percent.
struct Slider {
    const wchar_t* label;
    int Settings::*field;
    int min, max, step, page;  // percent
    const wchar_t* format;     // of the value shown, from the percent
};
const Slider kSliders[] = {
    {L"Size", &Settings::scale, kMinScalePct, kMaxScalePct, kScaleStepPct, 50, L"x%g"},
    {L"Speed", &Settings::speed, kMinSpeedPct, kMaxSpeedPct, kSpeedStepPct, 25, L"%g%%"},
};

struct Combo {
    const wchar_t* label;
    int Settings::*field;
};
const Combo kCombos[] = {
    {L"Activity", &Settings::activity},
    {L"When the mouse approaches", &Settings::reaction},
    {L"Opacity", &Settings::opacity},
    {L"Roaming zone", &Settings::zone},
};

struct Toggle {
    const wchar_t* label;
    bool Settings::*field;
    bool (*enabled)(const Pet& p);  // nullptr: always
};
const Toggle kToggles[] = {
    {L"White halo", &Settings::halo, nullptr},
    {L"Zzz while sleeping", &Settings::zzz, nullptr},
    {L"Draggable with the mouse", &Settings::draggable, nullptr},
    {L"Sleep when mouse and keyboard are idle", &Settings::sleepOnIdle, nullptr},
    {L"Type on his laptop when I type", &Settings::typeWithUser, [](const Pet& p) { return p.def.canType; }},
    {L"Bubble with his lines", &Settings::keyBubble,
     [](const Pet& p) { return p.cfg.typeWithUser && HasBubble(p); }},
    {L"Spider-Man mode (feet on the edges)", &Settings::spiderMan, [](const Pet& p) { return p.cfg.zone == kEdges; }},
    {L"Collide with other pets", &Settings::collisions, nullptr},
};

struct Ui {
    HWND wnd = nullptr;
    HFONT font = nullptr;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool syncing = false;  // the pet list's checkboxes are being set by code, not by the user
};
Ui ui;

HWND Item(int id) { return GetDlgItem(ui.wnd, id); }
int Px(int dip) { return MulDiv(dip, static_cast<int>(ui.dpi), USER_DEFAULT_SCREEN_DPI); }
bool Checked(HWND button) { return Button_GetCheck(button) == BST_CHECKED; }
void SetChecked(HWND button, bool on) { Button_SetCheck(button, on ? BST_CHECKED : BST_UNCHECKED); }

Pet* PetAt(int i) { return i >= 0 && static_cast<size_t>(i) < g.pets.size() ? g.pets[i].get() : nullptr; }
Pet* SelectedPet() { return PetAt(ListView_GetNextItem(Item(IDC_PETS), -1, LVNI_SELECTED)); }

bool AllVisible() {
    return std::all_of(g.pets.begin(), g.pets.end(), [](const auto& p) { return p->cfg.visible; });
}

void SelectPet(int i) {
    const HWND list = Item(IDC_PETS);
    ListView_SetItemState(list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, i, FALSE);
}

std::vector<const wchar_t*> ComboItems(const Combo& c) {
    std::vector<const wchar_t*> items;
    if (c.field == &Settings::activity) {
        for (const auto& a : kActivities) items.push_back(a.name);
    } else if (c.field == &Settings::reaction) {
        items.assign(std::begin(kReactionNames), std::end(kReactionNames));
    } else if (c.field == &Settings::opacity) {
        items.assign(std::begin(kOpacityNames), std::end(kOpacityNames));
    } else if (c.field == &Settings::zone) {
        items.assign(std::begin(kZoneNames), std::end(kZoneNames));
    }
    return items;
}

void ApplyCombo(Pet& p, int Settings::*field, int value) {
    p.cfg.*field = value;
    if (field == &Settings::reaction) {
        p.nearSince = 0;
    } else if (field == &Settings::zone) {
        // Heads into the new zone right away when idle, playing or walking.
        if (p.cfg.zone != kAnywhere && (p.state == State::Idle || p.state == State::Walk || p.state == State::Play))
            EnterWalk(p, GetTickCount64());
    }
}

void ShowSliderValue(size_t i, int pct) {
    const Slider& s = kSliders[i];
    wchar_t text[16];
    std::swprintf(text, ARRAYSIZE(text), s.format, s.field == &Settings::scale ? pct / 100.0 : pct * 1.0);
    SetWindowTextW(Item(IDC_SLIDER_VALUE + static_cast<int>(i)), text);
}

// Applies the slider's position.
void ApplySlider(Pet& p, size_t i) {
    const Slider& s = kSliders[i];
    const int pct = static_cast<int>(SendMessageW(Item(IDC_SLIDER + static_cast<int>(i)), TBM_GETPOS, 0, 0)) * s.step;
    ShowSliderValue(i, pct);
    if (p.cfg.*s.field == pct) return;
    if (s.field == &Settings::scale) ApplyScale(p, pct);
    else p.cfg.*s.field = pct;
}

void ApplyToggle(Pet& p, bool Settings::*field, bool on) {
    p.cfg.*field = on;
    if (field == &Settings::draggable) {
        ApplyClickThrough(p);
    } else if (field == &Settings::typeWithUser) {
        UpdateKeyboardListener();
    } else if (field == &Settings::halo) {
        DropFrames(p);  // redrawn on the next tick
    } else if (field == &Settings::keyBubble) {
        p.typed.clear();
    }
}

// Shows the settings of the selected pet.
void Refresh() {
    SetChecked(Item(IDC_STARTUP), StartupEnabled());
    ui.syncing = true;
    for (size_t i = 0; i < g.pets.size(); ++i)
        ListView_SetCheckState(Item(IDC_PETS), static_cast<UINT>(i), g.pets[i]->cfg.visible);
    ui.syncing = false;
    SetWindowTextW(Item(IDC_ALL), AllVisible() ? L"Deselect all" : L"Select all");
    const Pet* p = SelectedPet();
    if (!p) return;
    for (size_t i = 0; i < ARRAYSIZE(kSliders); ++i) {
        const int pct = p->cfg.*kSliders[i].field;
        SendMessageW(Item(IDC_SLIDER + static_cast<int>(i)), TBM_SETPOS, TRUE, pct / kSliders[i].step);
        ShowSliderValue(i, pct);
    }
    for (size_t i = 0; i < ARRAYSIZE(kCombos); ++i)
        ComboBox_SetCurSel(Item(IDC_COMBO + static_cast<int>(i)), p->cfg.*kCombos[i].field);
    for (size_t i = 0; i < ARRAYSIZE(kToggles); ++i) {
        const HWND button = Item(IDC_TOGGLE + static_cast<int>(i));
        SetChecked(button, p->cfg.*kToggles[i].field);
        EnableWindow(button, !kToggles[i].enabled || kToggles[i].enabled(*p));
    }
}

// Lists g.pets, `selected` selected.
void FillPetList(int selected) {
    const HWND list = Item(IDC_PETS);
    ui.syncing = true;
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < g.pets.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(g.pets[i]->def.name.c_str());
        ListView_InsertItem(list, &item);
    }
    ui.syncing = false;
    SelectPet(selected);
}

HWND AddControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, ui.wnd,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void CreateControls() {
    // One checkbox per pet: its visibility.
    const HWND list = AddControl(WC_LISTVIEWW, L"",
                                 WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
                                     LVS_SHOWSELALWAYS,
                                 IDC_PETS);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    LVCOLUMNW column{};
    ListView_InsertColumn(list, 0, &column);
    FillPetList(0);
    AddControl(WC_BUTTONW, L"Select all", WS_TABSTOP | BS_PUSHBUTTON, IDC_ALL);
    for (size_t i = 0; i < ARRAYSIZE(kSliders); ++i) {
        const Slider& s = kSliders[i];
        AddControl(WC_STATICW, s.label, SS_CENTERIMAGE, IDC_SLIDER_LABEL + static_cast<int>(i));
        const HWND bar = AddControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_DOWNISLEFT,
                                    IDC_SLIDER + static_cast<int>(i));
        SendMessageW(bar, TBM_SETRANGEMIN, FALSE, s.min / s.step);
        SendMessageW(bar, TBM_SETRANGEMAX, FALSE, s.max / s.step);
        SendMessageW(bar, TBM_SETLINESIZE, 0, 1);
        SendMessageW(bar, TBM_SETPAGESIZE, 0, s.page / s.step);
        AddControl(WC_STATICW, L"", SS_CENTERIMAGE | SS_RIGHT, IDC_SLIDER_VALUE + static_cast<int>(i));
    }
    for (size_t i = 0; i < ARRAYSIZE(kCombos); ++i) {
        AddControl(WC_STATICW, kCombos[i].label, SS_CENTERIMAGE, IDC_LABEL + static_cast<int>(i));
        const HWND combo = AddControl(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                      IDC_COMBO + static_cast<int>(i));
        for (const wchar_t* name : ComboItems(kCombos[i])) ComboBox_AddString(combo, name);
    }
    for (size_t i = 0; i < ARRAYSIZE(kToggles); ++i)
        AddControl(WC_BUTTONW, kToggles[i].label, WS_TABSTOP | BS_AUTOCHECKBOX, IDC_TOGGLE + static_cast<int>(i));
    AddControl(WC_BUTTONW, L"Run at startup", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_STARTUP);
    AddControl(WC_BUTTONW, L"Quit LemmyPet", WS_TABSTOP | BS_PUSHBUTTON, IDC_QUIT);
    AddControl(WC_BUTTONW, L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
}

// The system message font at the window's DPI, applied to every control.
void UpdateFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ui.dpi);
    const HFONT old = ui.font;
    ui.font = CreateFontIndirectW(&ncm.lfMessageFont);
    EnumChildWindows(
        ui.wnd,
        [](HWND child, LPARAM font) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(ui.font));
    if (old) DeleteObject(old);
}

void Place(HWND h, int x, int y, int w, int height) {
    SetWindowPos(h, nullptr, x, y, w, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Width a checkbox needs to show its whole label in the current font.
int CheckWidth(HWND h) {
    wchar_t text[128];
    const int len = GetWindowTextW(h, text, ARRAYSIZE(text));
    const HDC dc = GetDC(h);
    const HGDIOBJ old = SelectObject(dc, ui.font);
    SIZE ext{};
    GetTextExtentPoint32W(dc, text, len, &ext);
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    return GetSystemMetricsForDpi(SM_CXMENUCHECK, ui.dpi) + Px(8) + ext.cx;
}

// Positions the controls for the current DPI and font; returns the client size.
SIZE LayoutControls() {
    const int listX = Px(kMargin), top = Px(kMargin);
    // Toggles in two columns, each as wide as its longest label.
    int colW[2] = {};
    for (size_t i = 0; i < ARRAYSIZE(kToggles); ++i)
        colW[i % 2] = std::max(colW[i % 2], CheckWidth(Item(IDC_TOGGLE + static_cast<int>(i))));
    const int x = listX + Px(kListW) + Px(kPad);
    const int innerW = std::max(Px(kLabelW) + Px(kComboW) + Px(40), colW[0] + Px(kPad) + colW[1]);
    const int width = x + innerW + Px(kMargin);
    int y = top;

    RECT comboRect;
    GetWindowRect(Item(IDC_COMBO), &comboRect);
    const int comboH = comboRect.bottom - comboRect.top;  // set by the font
    for (size_t i = 0; i < ARRAYSIZE(kSliders); ++i) {
        Place(Item(IDC_SLIDER_LABEL + static_cast<int>(i)), x, y, Px(kLabelW), comboH);
        Place(Item(IDC_SLIDER + static_cast<int>(i)), x + Px(kLabelW) - Px(4), y, Px(kComboW) - Px(kValueW) + Px(4),
              comboH);
        Place(Item(IDC_SLIDER_VALUE + static_cast<int>(i)), x + Px(kLabelW) + Px(kComboW) - Px(kValueW), y,
              Px(kValueW), comboH);
        y += comboH + Px(7);
    }
    for (size_t i = 0; i < ARRAYSIZE(kCombos); ++i) {
        Place(Item(IDC_LABEL + static_cast<int>(i)), x, y, Px(kLabelW), comboH);
        // The height includes the drop-down list.
        Place(Item(IDC_COMBO + static_cast<int>(i)), x + Px(kLabelW), y, Px(kComboW), Px(200));
        y += comboH + Px(7);
    }
    y += Px(8);

    const int col2X = x + std::max(innerW / 2, colW[0] + Px(kPad));
    for (size_t i = 0; i < ARRAYSIZE(kToggles); ++i)
        Place(Item(IDC_TOGGLE + static_cast<int>(i)), i % 2 ? col2X : x,
              y + static_cast<int>(i / 2) * Px(kToggleRowH), i % 2 ? x + innerW - col2X : col2X - x, Px(kCheckH));
    y += static_cast<int>((ARRAYSIZE(kToggles) + 1) / 2 - 1) * Px(kToggleRowH) + Px(kCheckH);
    const HWND list = Item(IDC_PETS);
    Place(list, listX, top, Px(kListW), y - top - Px(kButtonH) - Px(6));
    Place(Item(IDC_ALL), listX, y - Px(kButtonH), Px(kListW), Px(kButtonH));
    RECT listRect;
    GetClientRect(list, &listRect);
    ListView_SetColumnWidth(list, 0, listRect.right);

    y += Px(kMargin) + Px(kPad);
    const int closeW = Px(80), quitW = Px(110);
    Place(Item(IDOK), width - Px(kMargin) - closeW, y, closeW, Px(kButtonH));
    Place(Item(IDC_QUIT), width - Px(kMargin) - closeW - Px(8) - quitW, y, quitW, Px(kButtonH));
    Place(Item(IDC_STARTUP), listX, y + (Px(kButtonH) - Px(kCheckH)) / 2, Px(200), Px(kCheckH));
    return {width, y + Px(kButtonH) + Px(kMargin)};
}

// Lays the window out for its DPI, at (x, y) or centered in `center`.
void Resize(const RECT* center, int x, int y) {
    UpdateFont();
    const SIZE client = LayoutControls();
    RECT r{0, 0, client.cx, client.cy};
    AdjustWindowRectExForDpi(&r, kStyle, FALSE, kExStyle, ui.dpi);
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (center) {
        x = (center->left + center->right - w) / 2;
        y = (center->top + center->bottom - h) / 2;
    }
    SetWindowPos(ui.wnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDOK || id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_ALL) {
            const bool show = !AllVisible();
            for (const auto& pet : g.pets) {
                if (pet->cfg.visible == show) continue;
                SetVisible(*pet, show);
                SaveSettings(*pet);
            }
            Refresh();
            return 0;
        }
        if (id == IDC_QUIT) {
            DestroyWindow(g.tray);  // also destroys this window, which it owns
            return 0;
        }
        if (id == IDC_STARTUP) {
            SetStartup(Checked(Item(IDC_STARTUP)));
            Refresh();
            return 0;
        }
        Pet* p = SelectedPet();
        if (!p) break;
        if (id >= IDC_COMBO && id < IDC_COMBO + static_cast<int>(ARRAYSIZE(kCombos)) && code == CBN_SELCHANGE) {
            const int value = ComboBox_GetCurSel(Item(id));
            if (value < 0) break;
            ApplyCombo(*p, kCombos[id - IDC_COMBO].field, value);
        } else if (id >= IDC_TOGGLE && id < IDC_TOGGLE + static_cast<int>(ARRAYSIZE(kToggles)) &&
                   code == BN_CLICKED) {
            ApplyToggle(*p, kToggles[id - IDC_TOGGLE].field, Checked(Item(id)));
        } else {
            break;
        }
        SaveSettings(*p);
        Refresh();  // options that depend on this one
        return 0;
    }
    case WM_HSCROLL: {
        Pet* p = SelectedPet();
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        if (!p || id < IDC_SLIDER || id >= IDC_SLIDER + static_cast<int>(ARRAYSIZE(kSliders))) break;
        // Applied live while dragging, saved once released.
        ApplySlider(*p, static_cast<size_t>(id - IDC_SLIDER));
        if (LOWORD(wp) != TB_THUMBTRACK) SaveSettings(*p);
        return 0;
    }
    case WM_NOTIFY: {
        const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
        if (hdr->idFrom != IDC_PETS || hdr->code != LVN_ITEMCHANGED) break;
        const auto* nm = reinterpret_cast<const NMLISTVIEW*>(lp);
        if (!(nm->uChanged & LVIF_STATE) || nm->iItem < 0) return 0;
        const UINT changed = nm->uOldState ^ nm->uNewState;
        if (changed & LVIS_STATEIMAGEMASK) {
            Pet* p = PetAt(nm->iItem);
            const bool visible = ListView_GetCheckState(hdr->hwndFrom, nm->iItem) != 0;
            if (ui.syncing || !p || p->cfg.visible == visible) return 0;
            SetVisible(*p, visible);
            SaveSettings(*p);
            Refresh();
        } else if (changed & nm->uNewState & LVIS_SELECTED) {
            Refresh();
        } else if (changed & nm->uOldState & LVIS_SELECTED) {
            // Keeps a pet selected when the empty part of the list is clicked.
            PostMessageW(hwnd, WM_APP_RESELECT, static_cast<WPARAM>(nm->iItem), 0);
        }
        return 0;
    }
    case WM_APP_RESELECT:
        if (ListView_GetSelectedCount(Item(IDC_PETS)) == 0) SelectPet(static_cast<int>(wp));
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) Refresh();
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        // The window color behind the labels and checkboxes.
        SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DPICHANGED: {
        ui.dpi = HIWORD(wp);
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        Resize(nullptr, suggested->left, suggested->top);
        return 0;
    }
    case WM_NCDESTROY:
        if (ui.font) DeleteObject(ui.font);
        ui = Ui{};
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RegisterOptionsClass(HINSTANCE inst) {
    static bool registered = false;
    if (registered) return;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = g.icon;
    wc.lpfnWndProc = OptionsProc;
    wc.lpszClassName = kClassName;
    registered = RegisterClassExW(&wc) != 0;
}

}  // namespace

void OpenOptions() {
    if (ui.wnd) {
        if (IsIconic(ui.wnd)) ShowWindow(ui.wnd, SW_RESTORE);
        SetForegroundWindow(ui.wnd);
        return;
    }
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    RegisterOptionsClass(inst);

    // On the screen of the tray icon that was just clicked.
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
    ui.wnd = CreateWindowExW(kExStyle, kClassName, L"LemmyPet settings", kStyle, mi.rcWork.left, mi.rcWork.top,
                             100, 100, g.tray, nullptr, inst, nullptr);
    if (!ui.wnd) return;
    ui.dpi = GetDpiForWindow(ui.wnd);
    CreateControls();
    Resize(&mi.rcWork, 0, 0);
    Refresh();
    ShowWindow(ui.wnd, SW_SHOW);
    SetForegroundWindow(ui.wnd);
    SetFocus(Item(IDC_PETS));
}

void RefreshOptions() {
    if (ui.wnd) Refresh();
}

void RebuildOptions() {
    if (!ui.wnd) return;
    // Keeps the same pet selected if he is still there.
    const Pet* p = SelectedPet();
    const std::wstring id = p ? p->def.id : std::wstring();
    const auto it = std::find_if(g.pets.begin(), g.pets.end(), [&](const auto& q) { return q->def.id == id; });
    FillPetList(it == g.pets.end() ? 0 : static_cast<int>(it - g.pets.begin()));
    Refresh();
}

bool IsOptionsMessage(MSG& msg) {
    if (!ui.wnd) return false;
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0 &&
        (msg.hwnd == ui.wnd || IsChild(ui.wnd, msg.hwnd))) {
        const HWND list = Item(IDC_PETS);
        const int count = ListView_GetItemCount(list);
        if (count > 0) {
            const int step = GetKeyState(VK_SHIFT) < 0 ? count - 1 : 1;
            SelectPet((ListView_GetNextItem(list, -1, LVNI_SELECTED) + step) % count);
        }
        return true;
    }
    return IsDialogMessageW(ui.wnd, &msg) != 0;
}

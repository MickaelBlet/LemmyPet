#include "tray.h"

#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <string>

#include "behind.h"
#include "options.h"
#include "resource.h"
#include "typing.h"
#include "window.h"

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 1;

constexpr UINT IDM_SETTINGS = 1;
constexpr UINT IDM_TOGGLE = 2;
constexpr UINT IDM_QUIT = 3;

constexpr UINT_PTR kRescanTimerId = 1;
constexpr UINT kRescanMs = 5000;  // pets/ folder scan period

HANDLE petsWatch = INVALID_HANDLE_VALUE;  // change notification on pets/, local drives only

// Whether pets/ may have changed since the last call. On a local drive a change notification answers
// without walking the folder; elsewhere (network, WSL) notifications may never come, so always true.
bool PetsMayHaveChanged() {
    if (petsWatch == INVALID_HANDLE_VALUE) {
        const std::wstring dir = PetsDir();
        wchar_t volume[MAX_PATH];
        if (!GetVolumePathNameW(dir.c_str(), volume, MAX_PATH)) return true;
        const UINT type = GetDriveTypeW(volume);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) return true;
        petsWatch = FindFirstChangeNotificationW(dir.c_str(), TRUE,
                                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                     FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        return true;  // not watched until now
    }
    if (WaitForSingleObject(petsWatch, 0) != WAIT_OBJECT_0) return false;
    // Rearmed before the scan, so changes made during it show up next time.
    if (!FindNextChangeNotification(petsWatch)) {
        FindCloseChangeNotification(petsWatch);
        petsWatch = INVALID_HANDLE_VALUE;
    }
    return true;
}

// Balloon from the tray icon.
void Notify(const std::wstring& text) {
    NOTIFYICONDATAW nid = g.nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING;
    lstrcpynW(nid.szInfoTitle, L"Some pets could not be loaded", ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text.c_str(), ARRAYSIZE(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void AddTrayIcon() {
    g.nid = NOTIFYICONDATAW{};
    g.nid.cbSize = sizeof(g.nid);
    g.nid.hWnd = g.tray;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_APP_TRAY;
    g.nid.hIcon = g.icon;
    lstrcpynW(g.nid.szTip, L"LemmyPet", ARRAYSIZE(g.nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g.nid);
}

void ShowTrayMenu() {
    const bool anyVisible = std::any_of(g.pets.begin(), g.pets.end(), [](const auto& p) { return p->cfg.visible; });
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, anyVisible ? L"Hide all pets" : L"Show all pets");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit");
    SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);

    POINT pt;
    GetCursorPos(&pt);
    // Required so the menu closes when clicking elsewhere; the pets themselves never get focus.
    SetForegroundWindow(g.tray);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                    pt.x, pt.y, 0, g.tray, nullptr);
    PostMessageW(g.tray, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == IDM_SETTINGS) {
        OpenOptions();
    } else if (cmd == IDM_TOGGLE) {
        ToggleAll();
        RefreshOptions();
    } else if (cmd == IDM_QUIT) {
        DestroyWindow(g.tray);
    }
}

// GUID_CONSOLE_DISPLAY_STATE, defined here to avoid linking libuuid.
constexpr GUID kDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK || wp == WTS_SESSION_UNLOCK) {
            g.locked = wp == WTS_SESSION_LOCK;
            UpdatePause();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_POWERSETTINGCHANGE) {
            const auto* ps = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
            if (IsEqualGUID(ps->PowerSetting, kDisplayState) && ps->DataLength >= sizeof(DWORD)) {
                g.displayOff = *reinterpret_cast<const DWORD*>(ps->Data) == 0;  // 1 on, 2 dimmed
                UpdatePause();
            }
        }
        return TRUE;
    case WM_TIMER:
        if (wp == kCaretTimerId) {
            AvoidCaret();
        } else if (wp == kBehindTimerId) {
            CheckBehind();
        } else if (wp == kRescanTimerId && PetsMayHaveChanged()) {
            const std::wstring errors = ReloadPets();
            if (!errors.empty()) Notify(errors);
        }
        return 0;
    case WM_INPUT:
        OnRawInput(reinterpret_cast<HRAWINPUT>(lp));
        break;  // DefWindowProc releases the input
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        case WM_LBUTTONUP:
            OpenOptions();
            break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kRescanTimerId);
        KillTimer(hwnd, kBehindTimerId);
        if (petsWatch != INVALID_HANDLE_VALUE) FindCloseChangeNotification(petsWatch);
        WTSUnRegisterSessionNotification(hwnd);
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        DestroyPets();
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

bool CreateTray(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = TrayProc;
    wc.lpszClassName = L"LemmyPetTray";
    RegisterClassExW(&wc);
    g.tray = CreateWindowExW(0, L"LemmyPetTray", L"LemmyPet", WS_OVERLAPPED,
                             0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g.tray) return false;
    // The pets pause while nobody can see them.
    WTSRegisterSessionNotification(g.tray, NOTIFY_FOR_THIS_SESSION);
    RegisterPowerSettingNotification(g.tray, &kDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    return true;
}

void ShowTrayIcon(HINSTANCE inst) {
    g.icon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    g.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    SetTimer(g.tray, kRescanTimerId, kRescanMs, nullptr);
}

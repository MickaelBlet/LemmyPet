// LemmyPet: click-through, non-focusable pets wandering the desktop (each one described in
// pets/<id>/pet.ini), controlled from a notification-area icon.

#include <windows.h>

#include <cstdlib>
#include <string>

#include "options.h"
#include "pet.h"
#include "settings.h"
#include "tray.h"
#include "window.h"

App g;

namespace {

// Loads the pet definitions, reporting skipped ones; false when there is none.
bool LoadDefs() {
    std::wstring errors;
    g.stamps = StampPets();  // first: a change made while loading shows up at the next scan
    g.defs = LoadPetDefs(errors);
    if (g.defs.empty() && g.stamps.empty()) {
        errors = L"No pet found: LemmyPet looks for pets\\<name>\\pet.ini next to the exe.\n\n" + errors;
        MessageBoxW(nullptr, errors.c_str(), L"LemmyPet", MB_ICONERROR);
        return false;
    }
    if (!errors.empty()) {
        errors = L"Some pets were skipped:\n\n" + errors;
        MessageBoxW(nullptr, errors.c_str(), L"LemmyPet", MB_ICONWARNING);
    }
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\LemmyPet.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    std::srand(GetTickCount());
    if (StartupEnabled()) SetStartup(true);  // keep the path current if the exe moved
    if (!LoadDefs()) return 1;

    RegisterPetClasses(inst);
    // Before the pets: it receives their keyboard input.
    if (!CreateTray(inst)) return 1;
    if (!CreatePets(inst)) {
        MessageBoxW(nullptr, L"Failed to create the pet windows.", L"LemmyPet", MB_ICONERROR);
        return 1;
    }
    ShowTrayIcon(inst);
    // Drops pages only touched during startup (sprite decoding); they are not needed again.
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsOptionsMessage(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyIcon(g.icon);
    CloseHandle(mutex);
    return 0;
}

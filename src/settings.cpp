#include "settings.h"

namespace {

const wchar_t kSettingsKey[] = L"Software\\LemmyPet";
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"LemmyPet";

DWORD ReadDword(const std::wstring& key, const wchar_t* name, DWORD fallback) {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
                   ERROR_SUCCESS
               ? value
               : fallback;
}

void WriteDword(const std::wstring& key, const wchar_t* name, DWORD value) {
    HKEY hkey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hkey, nullptr) !=
        ERROR_SUCCESS)
        return;
    RegSetValueExW(hkey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hkey);
}

// Former preset settings, as their index.
constexpr int kLegacyScalePct[] = {100, 150, 200, 250, 300, 400, 500, 600};
constexpr int kLegacySpeedPct[] = {60, 100, 165};

std::wstring SettingsKey(const Pet& p) { return std::wstring(kSettingsKey) + L"\\Pets\\" + p.def.id; }

}  // namespace

// Each pet has its own settings, by default those of the former single-pet version.
void LoadSettings(Pet& p) {
    const std::wstring key = SettingsKey(p);
    const auto read = [&](const wchar_t* name, DWORD fallback) {
        return ReadDword(key, name, ReadDword(kSettingsKey, name, fallback));
    };
    const Settings def;
    // Falls back to the former presets.
    const DWORD scaleIndex = read(L"ScaleIndex", ARRAYSIZE(kLegacyScalePct));
    const DWORD speedIndex = read(L"Speed", ARRAYSIZE(kLegacySpeedPct));
    p.cfg.scale = Clamp(static_cast<int>(read(L"ScalePct", scaleIndex < ARRAYSIZE(kLegacyScalePct)
                                                               ? kLegacyScalePct[scaleIndex]
                                                               : def.scale)),
                        kMinScalePct, kMaxScalePct);
    p.cfg.speed = Clamp(static_cast<int>(read(L"SpeedPct", speedIndex < ARRAYSIZE(kLegacySpeedPct)
                                                               ? kLegacySpeedPct[speedIndex]
                                                               : def.speed)),
                        kMinSpeedPct, kMaxSpeedPct);
    p.cfg.activity =
        Clamp(static_cast<int>(read(L"Activity", def.activity)), 0, static_cast<int>(ARRAYSIZE(kActivities)) - 1);
    p.cfg.reaction = Clamp(static_cast<int>(read(L"Reaction", def.reaction)), 0, static_cast<int>(kIgnore));
    p.cfg.opacity =
        Clamp(static_cast<int>(read(L"Opacity", def.opacity)), 0, static_cast<int>(ARRAYSIZE(kOpacityPct)) - 1);
    p.cfg.zzz = read(L"Zzz", def.zzz) != 0;
    p.cfg.draggable = read(L"Draggable", def.draggable) != 0;
    p.cfg.sleepOnIdle = read(L"SleepOnMouseIdle", def.sleepOnIdle) != 0;
    p.cfg.typeWithUser = read(L"TypeWithUser", def.typeWithUser) != 0;
    p.cfg.keyBubble = read(L"KeyBubble", def.keyBubble) != 0;
    // Falls back to the former bottom-only toggle.
    const DWORD legacyZone = read(L"BottomOnly", 0) != 0 ? kBottom : def.zone;
    p.cfg.zone = Clamp(static_cast<int>(read(L"Zone", legacyZone)), 0, static_cast<int>(kBottom));
    p.cfg.spiderMan = read(L"SpiderMan", def.spiderMan) != 0;
    p.cfg.halo = ReadDword(key, L"Halo", p.def.halo) != 0;
    p.cfg.collisions = ReadDword(key, L"Collisions", def.collisions) != 0;
    p.cfg.visible = ReadDword(key, L"Visible", def.visible) != 0;
}

void SaveSettings(Pet& p) {
    const std::wstring key = SettingsKey(p);
    WriteDword(key, L"ScalePct", p.cfg.scale);
    WriteDword(key, L"SpeedPct", p.cfg.speed);
    WriteDword(key, L"Activity", p.cfg.activity);
    WriteDword(key, L"Reaction", p.cfg.reaction);
    WriteDword(key, L"Opacity", p.cfg.opacity);
    WriteDword(key, L"Zzz", p.cfg.zzz);
    WriteDword(key, L"Draggable", p.cfg.draggable);
    WriteDword(key, L"SleepOnMouseIdle", p.cfg.sleepOnIdle);
    WriteDword(key, L"TypeWithUser", p.cfg.typeWithUser);
    WriteDword(key, L"KeyBubble", p.cfg.keyBubble);
    WriteDword(key, L"Zone", p.cfg.zone);
    WriteDword(key, L"SpiderMan", p.cfg.spiderMan);
    WriteDword(key, L"Halo", p.cfg.halo);
    WriteDword(key, L"Collisions", p.cfg.collisions);
    WriteDword(key, L"Visible", p.cfg.visible);
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

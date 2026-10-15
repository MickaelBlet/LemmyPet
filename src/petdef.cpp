#include "petdef.h"

#include <windows.h>
#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <map>

namespace {

const wchar_t* const kFacingNames[kFacingCount] = {L"S", L"SW", L"W", L"NW", L"N", L"NE", L"E", L"SE"};
const wchar_t* const kAnimNames[kAnimCount] = {L"walk", L"sulk", L"hurt", L"sleep", L"type"};

struct LoadError {
    std::wstring what;
};

std::wstring Trim(const std::wstring& s) {
    const size_t first = s.find_first_not_of(L" \t"), last = s.find_last_not_of(L" \t");
    return first == std::wstring::npos ? std::wstring() : s.substr(first, last - first + 1);
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring Join(std::wstring dir, const std::wstring& name) {
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    return dir + name;
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH || !PathRemoveFileSpecW(path)) return L".";
    return path;
}

// Reads a UTF-8 text file (BOM optional) as lines.
bool ReadLines(const std::wstring& path, std::vector<std::wstring>& lines) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
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
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    return true;
}

// --- pet.ini --------------------------------------------------------------

using Section = std::map<std::wstring, std::wstring>;  // lowercase key -> value
using Ini = std::map<std::wstring, Section>;           // lowercase section name -> keys

// "key = value" lines under "[section]" headers; ';' and '#' start comments.
Ini ParseIni(const std::vector<std::wstring>& lines) {
    Ini ini;
    Section* section = nullptr;
    for (const std::wstring& raw : lines) {
        const std::wstring line = Trim(raw.substr(0, raw.find_first_of(L";#")));
        if (line.empty()) continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = &ini[Lower(Trim(line.substr(1, line.size() - 2)))];
            continue;
        }
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || !section) throw LoadError{L"unexpected line \"" + line + L"\""};
        (*section)[Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
    }
    return ini;
}

const std::wstring* Find(const Section& section, const wchar_t* key) {
    const auto it = section.find(Lower(key));
    return it == section.end() ? nullptr : &it->second;
}

std::wstring Value(const Section& section, const wchar_t* key, const std::wstring& fallback) {
    const std::wstring* v = Find(section, key);
    return v ? *v : fallback;
}

// Comma-separated integers.
std::vector<int> Ints(const std::wstring& s, const std::wstring& what) {
    std::vector<int> v;
    for (size_t start = 0;;) {
        size_t end = s.find(L',', start);
        if (end == std::wstring::npos) end = s.size();
        const std::wstring token = Trim(s.substr(start, end - start));
        wchar_t* stop = nullptr;
        const long n = std::wcstol(token.c_str(), &stop, 10);
        if (token.empty() || *stop) throw LoadError{what + L": bad number \"" + token + L"\""};
        v.push_back(static_cast<int>(n));
        if (end == s.size()) return v;
        start = end + 1;
    }
}

std::vector<std::wstring> Words(const std::wstring& s) {
    std::vector<std::wstring> words;
    for (size_t start = 0;;) {
        start = s.find_first_not_of(L" \t", start);
        if (start == std::wstring::npos) return words;
        const size_t end = std::min(s.find_first_of(L" \t", start), s.size());
        words.push_back(s.substr(start, end - start));
        start = end;
    }
}

// --- Sheets ---------------------------------------------------------------

template <typename F>
F Proc(HMODULE lib, const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(lib, name)));
}

// Decodes PNG files with GDI+'s flat API, loaded just for this and unloaded afterward.
class Gdip {
public:
    Gdip() {
        lib_ = LoadLibraryW(L"gdiplus.dll");
        if (!lib_) return;
        const auto startup = Proc<Status(WINAPI*)(ULONG_PTR*, const Gdiplus::GdiplusStartupInput*, void*)>(
            lib_, "GdiplusStartup");
        shutdown_ = Proc<void(WINAPI*)(ULONG_PTR)>(lib_, "GdiplusShutdown");
        fromFile_ = Proc<Status(WINAPI*)(const WCHAR*, Bitmap*)>(lib_, "GdipCreateBitmapFromFile");
        width_ = Proc<Status(WINAPI*)(Bitmap, UINT*)>(lib_, "GdipGetImageWidth");
        height_ = Proc<Status(WINAPI*)(Bitmap, UINT*)>(lib_, "GdipGetImageHeight");
        lockBits_ = Proc<Status(WINAPI*)(Bitmap, const void*, UINT, INT, Gdiplus::BitmapData*)>(
            lib_, "GdipBitmapLockBits");
        unlockBits_ = Proc<Status(WINAPI*)(Bitmap, Gdiplus::BitmapData*)>(lib_, "GdipBitmapUnlockBits");
        dispose_ = Proc<Status(WINAPI*)(Bitmap)>(lib_, "GdipDisposeImage");
        Gdiplus::GdiplusStartupInput input;
        if (startup && shutdown_ && fromFile_ && width_ && height_ && lockBits_ && unlockBits_ && dispose_ &&
            startup(&token_, &input, nullptr) != Gdiplus::Ok)
            token_ = 0;
    }

    ~Gdip() {
        if (token_) shutdown_(token_);
        if (lib_) FreeLibrary(lib_);
    }

    Gdip(const Gdip&) = delete;
    Gdip& operator=(const Gdip&) = delete;

    bool Load(const std::wstring& path, Sheet& sheet) {
        if (!token_) return false;
        Bitmap bmp = nullptr;
        if (fromFile_(path.c_str(), &bmp) != Gdiplus::Ok) return false;
        bool ok = false;
        UINT w = 0, h = 0;
        Gdiplus::BitmapData bd{};
        if (width_(bmp, &w) == Gdiplus::Ok && height_(bmp, &h) == Gdiplus::Ok &&
            lockBits_(bmp, nullptr, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
            sheet.w = static_cast<int>(w);
            sheet.h = static_cast<int>(h);
            sheet.px.resize(static_cast<size_t>(sheet.w) * sheet.h);
            for (int row = 0; row < sheet.h; ++row) {
                std::memcpy(&sheet.px[static_cast<size_t>(row) * sheet.w],
                            static_cast<const BYTE*>(bd.Scan0) + static_cast<ptrdiff_t>(row) * bd.Stride,
                            static_cast<size_t>(sheet.w) * 4);
            }
            unlockBits_(bmp, &bd);
            ok = true;
        }
        dispose_(bmp);
        return ok;
    }

private:
    using Status = Gdiplus::Status;
    using Bitmap = void*;
    HMODULE lib_ = nullptr;
    ULONG_PTR token_ = 0;
    void(WINAPI* shutdown_)(ULONG_PTR) = nullptr;
    Status(WINAPI* fromFile_)(const WCHAR*, Bitmap*) = nullptr;
    Status(WINAPI* width_)(Bitmap, UINT*) = nullptr;
    Status(WINAPI* height_)(Bitmap, UINT*) = nullptr;
    Status(WINAPI* lockBits_)(Bitmap, const void*, UINT, INT, Gdiplus::BitmapData*) = nullptr;
    Status(WINAPI* unlockBits_)(Bitmap, Gdiplus::BitmapData*) = nullptr;
    Status(WINAPI* dispose_)(Bitmap) = nullptr;
};

// --- Animations -----------------------------------------------------------

// Fills the transparent margins of a sprite.
void Measure(Sprite& s) {
    s.top = s.h;
    s.left = s.right = s.w;
    for (int y = 0; y < s.h; ++y) {
        const uint32_t* row = &s.sheet->px[static_cast<size_t>(s.y + y) * s.sheet->w + s.x];
        for (int x = 0; x < s.w; ++x) {
            if (!(row[x] >> 24)) continue;
            s.top = std::min(s.top, y);
            s.left = std::min(s.left, x);
            s.right = std::min(s.right, s.w - 1 - x);
        }
    }
    if (s.mirror) std::swap(s.left, s.right);
}

std::vector<Sprite> Mirrored(std::vector<Sprite> frames) {
    for (Sprite& s : frames) {
        s.mirror = !s.mirror;
        std::swap(s.left, s.right);
    }
    return frames;
}

// Gives every facing without frames those of the closest one given: the same side (SW and NW from W),
// the other side mirrored (W from E), down or up, then any.
void FillFacings(Animation& anim) {
    std::vector<Sprite> given[kFacingCount];
    std::copy(std::begin(anim.frames), std::end(anim.frames), std::begin(given));
    for (int f = 0; f < kFacingCount; ++f) {
        if (!given[f].empty()) continue;
        const int side = f == kSW || f == kNW ? kW : (f == kNE || f == kSE ? kE : f);
        const int tries[] = {side, (8 - f) % 8, (8 - side) % 8, f == kNW || f == kNE ? kN : kS, kS, kN};
        for (int i = 0; i < static_cast<int>(std::size(tries)) && anim.frames[f].empty(); ++i)
            if (!given[tries[i]].empty()) anim.frames[f] = i == 1 || i == 2 ? Mirrored(given[tries[i]]) : given[tries[i]];
        for (int t = 0; t < kFacingCount && anim.frames[f].empty(); ++t) anim.frames[f] = given[t];
    }
}

std::unique_ptr<PetDef> LoadPet(const std::wstring& dir, const std::wstring& id, Gdip& gdip) {
    std::vector<std::wstring> lines;
    if (!ReadLines(Join(dir, L"pet.ini"), lines)) throw LoadError{L"cannot read pet.ini"};
    const Ini ini = ParseIni(lines);
    auto def = std::make_unique<PetDef>();
    def->id = id;
    const auto petIt = ini.find(L"pet");
    const Section pet = petIt == ini.end() ? Section() : petIt->second;
    def->name = Value(pet, L"name", id);
    def->halo = Value(pet, L"halo", L"1") != L"0";

    if (const std::wstring* cell = Find(pet, L"cell")) {
        std::vector<int> v = Ints(*cell, L"[pet] cell");
        if (v.size() == 1) v.push_back(v[0]);
        if (v.size() != 2 || v[0] <= 0 || v[1] <= 0) throw LoadError{L"[pet] cell: expected size or width,height"};
        def->cellW = v[0];
        def->cellH = v[1];
    }

    std::map<std::wstring, const Sheet*> sheets;  // by lowercase file name
    const auto sheetFor = [&](const std::wstring& file, const std::wstring& where) {
        if (file.empty()) throw LoadError{where + L": no sheet (set sheet in [pet] or here)"};
        const auto it = sheets.find(Lower(file));
        if (it != sheets.end()) return it->second;
        auto sheet = std::make_unique<Sheet>();
        if (!gdip.Load(Join(dir, file), *sheet)) throw LoadError{L"cannot load " + file};
        def->sheets.push_back(std::move(sheet));
        return sheets[Lower(file)] = def->sheets.back().get();
    };

    for (int a = 0; a < kAnimCount; ++a) {
        const auto it = ini.find(kAnimNames[a]);
        if (it == ini.end()) continue;
        const Section& section = it->second;
        const std::wstring where = L"[" + std::wstring(kAnimNames[a]) + L"]";
        const std::wstring file = Value(section, L"sheet", Value(pet, L"sheet", L""));
        Animation& anim = def->anims[a];
        for (int f = 0; f < kFacingCount; ++f) {
            const std::wstring* frames = Find(section, kFacingNames[f]);
            if (!frames) continue;
            const Sheet* sheet = sheetFor(file, where);
            for (std::wstring word : Words(*frames)) {
                const std::wstring what = where + L" " + kFacingNames[f] + L" " + word;
                Sprite s;
                s.sheet = sheet;
                if (word[0] == L'~') {
                    s.mirror = true;
                    word.erase(0, 1);
                }
                const std::vector<int> n = Ints(word, what);
                if (n.size() == 2) {
                    if (!def->cellW) throw LoadError{what + L": a grid cell needs [pet] cell"};
                    s = {sheet, n[0] * def->cellW, n[1] * def->cellH, def->cellW, def->cellH, s.mirror};
                } else if (n.size() == 4) {
                    s = {sheet, n[0], n[1], n[2], n[3], s.mirror};
                } else {
                    throw LoadError{what + L": expected col,row or x,y,w,h"};
                }
                if (s.w <= 0 || s.h <= 0 || s.x < 0 || s.y < 0 || s.x + s.w > sheet->w || s.y + s.h > sheet->h)
                    throw LoadError{what + L": outside " + file};
                Measure(s);
                anim.frames[f].push_back(s);
            }
        }
        if (const std::wstring* cycle = Find(section, L"cycle")) {
            for (int i : Ints(*cycle, where + L" cycle")) {
                if (i < 1) throw LoadError{where + L" cycle: frames count from 1"};
                anim.cycle.push_back(i - 1);
            }
        }
    }

    const auto given = [](const Animation& anim) {
        return std::any_of(std::begin(anim.frames), std::end(anim.frames), [](const auto& v) { return !v.empty(); });
    };
    if (!given(def->anims[kWalk])) throw LoadError{L"[walk] has no frames"};
    def->canType = given(def->anims[kType]);

    // The cell holds every frame (by default, just so).
    const bool fixedCell = def->cellW > 0;
    for (const Animation& anim : def->anims) {
        for (const auto& frames : anim.frames) {
            for (const Sprite& s : frames) {
                if (fixedCell && (s.w > def->cellW || s.h > def->cellH))
                    throw LoadError{L"a frame is larger than [pet] cell"};
                def->cellW = std::max(def->cellW, s.w);
                def->cellH = std::max(def->cellH, s.h);
            }
        }
    }

    const auto walkIt = ini.find(L"walk");
    def->stand = std::max(1, Ints(Value(walkIt->second, L"stand", L"1"), L"[walk] stand")[0]) - 1;
    FillFacings(def->anims[kWalk]);
    // Missing animations show him standing.
    for (Animation& anim : def->anims) {
        if (given(anim)) {
            FillFacings(anim);
            continue;
        }
        for (int f = 0; f < kFacingCount; ++f) anim.frames[f] = {def->Stand(f)};
    }

    // Narrowest transparent margin beside his standing frames, bottom-centered in the cell.
    def->sideGap = def->cellW;
    for (int f = 0; f < kFacingCount; ++f) {
        const Sprite& s = def->Stand(f);
        const int ox = (def->cellW - s.w) / 2;
        def->sideGap = std::min({def->sideGap, ox + s.left, def->cellW - ox - s.w + s.right});
    }

    def->zzzX = def->cellW * 0.6;
    def->zzzY = def->cellH * 0.25;
    if (const std::wstring* zzz = Find(pet, L"zzz")) {
        const std::vector<int> v = Ints(*zzz, L"[pet] zzz");
        if (v.size() != 2) throw LoadError{L"[pet] zzz: expected x,y"};
        def->zzzX = v[0];
        def->zzzY = v[1];
    }

    if (const std::wstring* file = Find(pet, L"lyrics")) {
        std::vector<std::wstring> text;
        if (!ReadLines(Join(dir, *file), text)) throw LoadError{L"cannot read " + *file};
        for (const std::wstring& line : text) {
            const std::wstring t = Trim(line);
            if (!t.empty() && t[0] != L'#') def->lyrics.push_back(t);
        }
    }
    return def;
}

}  // namespace

const Sprite& Animation::Frame(int facing, unsigned long long step) const {
    const std::vector<Sprite>& v = frames[facing];
    const size_t i = cycle.empty() ? step % v.size() : cycle[step % cycle.size()] % v.size();
    return v[i];
}

const Sprite& PetDef::Stand(int facing) const {
    const std::vector<Sprite>& v = anims[kWalk].frames[facing];
    return v[stand % v.size()];
}

std::vector<std::unique_ptr<PetDef>> LoadPetDefs(std::wstring& errors) {
    std::vector<std::unique_ptr<PetDef>> defs;
    const std::wstring root = Join(ExeDir(), L"pets");
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(Join(root, L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return defs;
    Gdip gdip;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        try {
            defs.push_back(LoadPet(Join(root, fd.cFileName), fd.cFileName, gdip));
        } catch (const LoadError& e) {
            errors += L"pets\\" + std::wstring(fd.cFileName) + L": " + e.what + L"\n";
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(defs.begin(), defs.end(), [](const auto& a, const auto& b) { return Lower(a->id) < Lower(b->id); });
    return defs;
}

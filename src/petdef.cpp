#include "petdef.h"

#include <windows.h>
#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <map>
#include <utility>

namespace {

const wchar_t* const kFacingNames[kFacingCount] = {L"S", L"SW", L"W", L"NW", L"N", L"NE", L"E", L"SE"};
const wchar_t* const kAnimNames[kAnimCount] = {L"walk", L"idle", L"sulk", L"hurt", L"sleep", L"type", L"play", L"turn"};

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

// --- AnimData.xml (PMDCollab SpriteCollab) -------------------------------

struct PmdAnim {
    std::wstring name;            // as written, names its <name>-Anim.png sheet
    std::wstring copyOf;          // lowercase name of the animation it reuses, if any
    int w = 0, h = 0;             // frame size
    std::vector<int> durations;   // per frame, in 1/60 s
};
using PmdData = std::map<std::wstring, PmdAnim>;  // by lowercase name

// Text of the first <tag>...</tag> in [from, to), with `from` moved past it; false if none.
bool NextTag(const std::wstring& xml, const std::wstring& tag, size_t& from, size_t to, std::wstring& text) {
    const std::wstring open = L"<" + tag + L">", close = L"</" + tag + L">";
    const size_t start = xml.find(open, from);
    if (start == std::wstring::npos || start >= to) return false;
    const size_t end = xml.find(close, start);
    if (end == std::wstring::npos || end > to) throw LoadError{L"AnimData.xml: unclosed <" + tag + L">"};
    text = Trim(xml.substr(start + open.size(), end - start - open.size()));
    from = end + close.size();
    return true;
}

// The <Anim> entries of an AnimData.xml.
PmdData ParseAnimData(const std::vector<std::wstring>& lines) {
    std::wstring xml;
    for (const std::wstring& line : lines) xml += line + L"\n";
    PmdData data;
    std::wstring block, text;
    for (size_t pos = 0; NextTag(xml, L"Anim", pos, xml.size(), block);) {
        PmdAnim a;
        size_t at = 0;
        if (!NextTag(block, L"Name", at, block.size(), a.name)) throw LoadError{L"AnimData.xml: <Anim> without <Name>"};
        const std::wstring what = L"AnimData.xml " + a.name;
        at = 0;
        if (NextTag(block, L"CopyOf", at, block.size(), text)) {
            a.copyOf = Lower(text);
        } else {
            at = 0;
            if (NextTag(block, L"FrameWidth", at, block.size(), text)) a.w = Ints(text, what)[0];
            at = 0;
            if (NextTag(block, L"FrameHeight", at, block.size(), text)) a.h = Ints(text, what)[0];
            if (a.w <= 0 || a.h <= 0) throw LoadError{what + L": no frame size"};
            for (at = 0; NextTag(block, L"Duration", at, block.size(), text);)
                a.durations.push_back(std::max(1, Ints(text, what)[0]));
        }
        data[Lower(a.name)] = a;
    }
    return data;
}

// Animation `name`, following CopyOf.
const PmdAnim& FindPmd(const PmdData& data, const std::wstring& name, const std::wstring& where) {
    std::wstring key = Lower(name);
    for (size_t hops = 0; hops <= data.size(); ++hops) {
        const auto it = data.find(key);
        if (it == data.end()) throw LoadError{where + L": no animation \"" + name + L"\" in AnimData.xml"};
        if (it->second.copyOf.empty()) return it->second;
        key = it->second.copyOf;
    }
    throw LoadError{where + L": CopyOf loop in AnimData.xml"};
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
        s.dx = -s.dx;
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

// Loads the frames of a PMD animation sheet (one row per facing, or a single row shown as `facing`), each
// cropped to what it shows. The sprite anchor (the frame center) is drawn in the middle of the cell, `ground`
// rows above his feet (set by the first PMD animation loaded): higher frames are lifted, lower ones raised
// to the floor. With its Offsets sheet, the anchor follows his body (the green mark) as it moves away from
// the first frame of the row, along the axes in `inplace` (1 x, 2 y), so he acts on the spot.
void LoadPmdFrames(const Sheet& sheet, const Sheet* offsets, int inplace, const PmdAnim& a, int facing, int& ground,
                   Animation& anim, const std::wstring& where) {
    const int rows = sheet.h / a.h;
    int cols = sheet.w / a.w;
    if (!a.durations.empty()) cols = std::min(cols, static_cast<int>(a.durations.size()));
    if (cols < 1 || (rows != 1 && rows != kFacingCount)) throw LoadError{where + L": sheet does not match AnimData.xml"};
    if (offsets && (offsets->w < cols * a.w || offsets->h < rows * a.h))
        throw LoadError{where + L": Offsets sheet does not match AnimData.xml"};
    const int ax = a.w / 2, ay = a.h / 2;
    const auto used = [&](int c) {
        return anim.cycle.empty() || std::any_of(anim.cycle.begin(), anim.cycle.end(), [&](int i) { return i % cols == c; });
    };
    struct Box {
        int left = 0, top = 0, right = 0, bottom = 0;  // visible pixels of a frame; empty when blank
    };
    const auto visible = [&](int r, int c) {
        Box b{a.w, a.h, 0, 0};
        for (int y = 0; y < a.h; ++y) {
            const uint32_t* row = &sheet.px[static_cast<size_t>(r * a.h + y) * sheet.w + c * a.w];
            for (int x = 0; x < a.w; ++x) {
                if (!(row[x] >> 24)) continue;
                b = {std::min(b.left, x), std::min(b.top, y), std::max(b.right, x + 1), std::max(b.bottom, y + 1)};
            }
        }
        return b.right ? b : Box{ax - 1, ay, ax + 1, ay + 1};  // blank: 2 x 1 clear pixels at the anchor
    };
    // His body mark in a frame (opaque green), or the frame center.
    const auto body = [&](int r, int c) {
        for (int y = 0; offsets && y < a.h; ++y) {
            const uint32_t* row = &offsets->px[static_cast<size_t>(r * a.h + y) * offsets->w + c * a.w];
            for (int x = 0; x < a.w; ++x)
                if (row[x] == 0xFF00FF00) return std::pair<int, int>{x, y};
        }
        return std::pair<int, int>{ax, ay};
    };

    if (ground == INT_MIN) {
        int lowest = 0;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                if (used(c)) lowest = std::max(lowest, visible(r, c).bottom);
        ground = lowest - ay;
    }
    for (int r = 0; r < rows; ++r) {
        // Sheet rows go S, SE, E, ... counterclockwise.
        const int f = rows == 1 ? facing : (kFacingCount - r) % kFacingCount;
        const auto [bx0, by0] = body(r, 0);
        std::vector<Sprite> frames(cols);
        int first = -1;
        for (int c = 0; c < cols; ++c) {
            if (!used(c)) continue;
            Box b = visible(r, c);
            if ((b.right - b.left) % 2) (b.left > 0 ? --b.left : ++b.right);  // even, so the anchor stays put
            const auto [bx, by] = body(r, c);
            const int anchorX = ax + (inplace & 1 ? bx - bx0 : 0), anchorY = ay + (inplace & 2 ? by - by0 : 0);
            Sprite& s = frames[c];
            s.sheet = &sheet;
            s.x = c * a.w + b.left;
            s.y = r * a.h + b.top;
            s.w = b.right - b.left;
            s.h = b.bottom - b.top;
            s.dx = b.left + s.w / 2 - anchorX;
            s.lift = std::max(0, anchorY + ground - b.bottom);
            Measure(s);
            if (first < 0) first = c;
        }
        for (int c = 0; c < cols; ++c)
            if (!used(c)) frames[c] = frames[first];  // never shown
        anim.frames[f] = std::move(frames);
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

    PmdData pmd;
    if (const std::wstring* file = Find(pet, L"pmd")) {
        std::vector<std::wstring> xml;
        if (!ReadLines(Join(dir, *file), xml)) throw LoadError{L"cannot read " + *file};
        pmd = ParseAnimData(xml);
    }
    int ground = INT_MIN;  // PMD: rows from the anchor down to his feet (see LoadPmdFrames)

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
        Animation base;  // options shared by its variants
        if (const std::wstring* cycle = Find(section, L"cycle")) {
            for (int i : Ints(*cycle, where + L" cycle")) {
                if (i < 1) throw LoadError{where + L" cycle: frames count from 1"};
                base.cycle.push_back(i - 1);
            }
        }
        base.loop = Value(section, L"loop", L"1") != L"0";
        std::vector<unsigned long long> fixedMs;
        if (const std::wstring* d = Find(section, L"durations")) {
            for (int v : Ints(*d, where + L" durations")) {
                if (v < 1) throw LoadError{where + L" durations: expected ms above 0"};
                fixedMs.push_back(v);
            }
        }
        // Each step of the order lasts its `durations` entry (ms), or its PMD frame's duration (1/60 s).
        const auto time = [&](Animation& anim, const std::vector<int>& ticks) {
            std::vector<unsigned long long> ms = fixedMs;
            if (ms.empty() && !ticks.empty()) {
                const size_t steps = anim.cycle.empty() ? ticks.size() : anim.cycle.size();
                for (size_t i = 0; i < steps; ++i)
                    ms.push_back((ticks[(anim.cycle.empty() ? i : anim.cycle[i]) % ticks.size()] * 1000 + 30) / 60);
            }
            unsigned long long end = 0;
            for (unsigned long long v : ms) anim.ends.push_back(end += v);
        };

        if (const std::wstring* names = Find(section, L"anim")) {
            // PMD animations, comma-separated: one is picked each time.
            if (!Find(pet, L"pmd")) throw LoadError{where + L" anim: needs [pet] pmd"};
            const std::wstring row = Value(section, L"facing", L"S");
            const auto facing = std::find_if(std::begin(kFacingNames), std::end(kFacingNames),
                                             [&](const wchar_t* n) { return Lower(n) == Lower(row); });
            if (facing == std::end(kFacingNames)) throw LoadError{where + L" facing: unknown \"" + row + L"\""};
            const std::wstring axes = Lower(Value(section, L"inplace", L""));
            if (axes != L"" && axes != L"x" && axes != L"xy") throw LoadError{where + L" inplace: expected x or xy"};
            const int inplace = axes.empty() ? 0 : (axes == L"x" ? 1 : 3);
            for (size_t start = 0; start <= names->size();) {
                const size_t comma = std::min(names->find(L',', start), names->size());
                const std::wstring name = Trim(names->substr(start, comma - start));
                start = comma + 1;
                if (name.empty()) throw LoadError{where + L" anim: empty name"};
                const PmdAnim& pa = FindPmd(pmd, name, where);
                Animation anim = base;
                LoadPmdFrames(*sheetFor(pa.name + L"-Anim.png", where),
                              inplace ? sheetFor(pa.name + L"-Offsets.png", where) : nullptr, inplace, pa,
                              static_cast<int>(facing - kFacingNames), ground, anim, where + L" " + name);
                time(anim, pa.durations);
                def->anims[a].push_back(std::move(anim));
            }
            continue;
        }

        Animation anim = base;
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
        time(anim, {});
        def->anims[a].push_back(std::move(anim));
    }

    // Variants without any frame are dropped.
    for (auto& variants : def->anims) {
        variants.erase(std::remove_if(variants.begin(), variants.end(),
                                      [](const Animation& anim) {
                                          return std::all_of(std::begin(anim.frames), std::end(anim.frames),
                                                             [](const auto& v) { return v.empty(); });
                                      }),
                       variants.end());
    }
    if (def->anims[kWalk].empty()) throw LoadError{L"[walk] has no frames"};
    def->canType = !def->anims[kType].empty();
    def->canPlay = !def->anims[kPlay].empty();
    def->canTurn = !def->anims[kTurn].empty();

    // The cell holds every frame (by default, just so).
    const bool fixedCell = def->cellW > 0;
    for (const auto& variants : def->anims) {
        for (const Animation& anim : variants) {
            for (const auto& frames : anim.frames) {
                for (const Sprite& s : frames) {
                    const int w = s.w + 2 * std::abs(s.dx);
                    if (fixedCell && (w > def->cellW || s.h + s.lift > def->cellH))
                        throw LoadError{L"a frame is larger than [pet] cell"};
                    def->cellW = std::max(def->cellW, w);
                    def->cellH = std::max(def->cellH, s.h + s.lift);
                }
            }
        }
    }
    // Square, so it fits the same box turned onto a wall (Spider-Man mode) and rolls around corners on it.
    def->cellW = def->cellH = std::max(def->cellW, def->cellH);

    const auto walkIt = ini.find(L"walk");
    def->stand = std::max(1, Ints(Value(walkIt->second, L"stand", L"1"), L"[walk] stand")[0]) - 1;
    for (Animation& anim : def->anims[kWalk]) FillFacings(anim);
    // Missing animations show him standing.
    for (auto& variants : def->anims) {
        if (!variants.empty()) {
            for (Animation& anim : variants) FillFacings(anim);
            continue;
        }
        Animation& anim = variants.emplace_back();
        for (int f = 0; f < kFacingCount; ++f) anim.frames[f] = {def->Stand(f)};
    }

    // Narrowest transparent margin beside his standing frames, bottom-centered in the cell.
    def->sideGap = def->cellW;
    for (int f = 0; f < kFacingCount; ++f) {
        const Sprite& s = def->Stand(f);
        const int ox = (def->cellW - s.w) / 2 + s.dx;
        def->sideGap = std::min({def->sideGap, ox + s.left, def->cellW - ox - s.w + s.right});
    }

    // By default, just above the head of the first head-on-the-right sleeping frame.
    const Sprite& sleep = def->anims[kSleep][0].frames[kE][0];
    def->zzzX = (def->cellW - sleep.w) / 2 + sleep.dx + sleep.w * 0.65;
    def->zzzY = std::max(0, def->cellH - sleep.lift - sleep.h + sleep.top - 1);
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

size_t Animation::Step(int facing, unsigned long long t, unsigned long long stepMs, unsigned long long* left) const {
    const size_t steps = ends.empty() ? (cycle.empty() ? frames[facing].size() : cycle.size()) : ends.size();
    if (steps <= 1) {
        *left = kNever;
        return 0;
    }
    if (!loop && t >= Length(facing, stepMs)) {
        *left = kNever;
        return steps - 1;
    }
    if (ends.empty()) {
        *left = stepMs - t % stepMs;
        return t / stepMs % steps;
    }
    const unsigned long long u = t % ends.back();
    const size_t i = std::upper_bound(ends.begin(), ends.end(), u) - ends.begin();
    *left = ends[i] - u;
    return i;
}

const Sprite& Animation::Frame(int facing, unsigned long long t, unsigned long long stepMs) const {
    const std::vector<Sprite>& v = frames[facing];
    unsigned long long left;
    const size_t step = Step(facing, t, stepMs, &left);
    return v[(cycle.empty() ? step : cycle[step]) % v.size()];
}

unsigned long long Animation::Until(int facing, unsigned long long t, unsigned long long stepMs) const {
    unsigned long long left;
    Step(facing, t, stepMs, &left);
    return left;
}

unsigned long long Animation::Length(int facing, unsigned long long stepMs) const {
    if (!ends.empty()) return ends.back();
    return (cycle.empty() ? frames[facing].size() : cycle.size()) * stepMs;
}

const Sprite& PetDef::Stand(int facing) const {
    const std::vector<Sprite>& v = anims[kWalk][0].frames[facing];
    return v[stand % v.size()];
}

namespace {

// Mixes every file's path, size and write time below `dir` into `h` (FNV-1a).
void StampDir(const std::wstring& dir, const std::wstring& rel, unsigned long long& h) {
    const auto mix = [&](const void* data, size_t size) {
        for (size_t i = 0; i < size; ++i) h = (h ^ static_cast<const unsigned char*>(data)[i]) * 1099511628211ULL;
    };
    WIN32_FIND_DATAW fd;
    // No short names, bigger batches: fewer, cheaper calls.
    HANDLE find = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
        const std::wstring name = Join(rel, fd.cFileName);
        mix(name.data(), name.size() * sizeof(wchar_t));
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            StampDir(Join(dir, fd.cFileName), name, h);
        } else {
            mix(&fd.nFileSizeLow, sizeof(fd.nFileSizeLow));
            mix(&fd.nFileSizeHigh, sizeof(fd.nFileSizeHigh));
            mix(&fd.ftLastWriteTime, sizeof(fd.ftLastWriteTime));
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

}  // namespace

std::wstring PetsDir() { return Join(ExeDir(), L"pets"); }

std::map<std::wstring, unsigned long long> StampPets() {
    std::map<std::wstring, unsigned long long> stamps;
    const std::wstring root = PetsDir();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(Join(root, L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return stamps;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        const std::wstring dir = Join(root, fd.cFileName);
        if (GetFileAttributesW(Join(dir, L"pet.ini").c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        unsigned long long h = 14695981039346656037ULL;
        StampDir(dir, L"", h);
        stamps[fd.cFileName] = h;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return stamps;
}

std::unique_ptr<PetDef> LoadPetDef(const std::wstring& id, std::wstring& error) {
    Gdip gdip;
    try {
        return LoadPet(Join(PetsDir(), id), id, gdip);
    } catch (const LoadError& e) {
        error = e.what;
        return nullptr;
    }
}

std::vector<std::unique_ptr<PetDef>> LoadPetDefs(std::wstring& errors) {
    std::vector<std::unique_ptr<PetDef>> defs;
    const std::wstring root = PetsDir();
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

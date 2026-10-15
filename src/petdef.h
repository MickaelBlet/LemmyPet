// Pet definitions: pets/<id>/pet.ini next to the exe, plus the sprite sheets it names
// (see README.md for the format).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using Pixels = std::vector<uint32_t>;  // premultiplied BGRA, top-down

struct Sheet {
    int w = 0;
    int h = 0;
    Pixels px;
};

// A rectangle of a sheet, drawn bottom-centered in the pet's cell.
struct Sprite {
    const Sheet* sheet = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    bool mirror = false;               // flipped horizontally
    int top = 0, left = 0, right = 0;  // transparent rows / columns on each side (as drawn)
};

// Facings, in direction order (screen y down): south first, then clockwise.
enum Facing { kS, kSW, kW, kNW, kN, kNE, kE, kSE, kFacingCount };
enum Anim { kWalk, kSulk, kHurt, kSleep, kType, kAnimCount };

struct Animation {
    std::vector<Sprite> frames[kFacingCount];  // every facing filled once loaded (see LoadPetDefs)
    std::vector<int> cycle;                    // frame order; empty: all frames in turn

    // Frame shown at animation step `step` (any number, wraps around).
    const Sprite& Frame(int facing, unsigned long long step) const;
};

struct PetDef {
    std::wstring id;    // folder name, also the settings key
    std::wstring name;  // shown in the settings pet list
    int cellW = 0, cellH = 0;  // frame box, sprite px
    bool halo = true;          // default of his halo setting (1 px white halo around him)
    int sideGap = 0;           // transparent columns on each side of his standing frames
    double zzzX = 0, zzzY = 0; // where the Z's start in the cell, head on the right
    int stand = 0;             // walk frame shown while idle
    bool canType = false;      // has typing frames
    std::vector<std::wstring> lyrics;  // typing bubble lines (none: no bubble)
    std::vector<std::unique_ptr<Sheet>> sheets;
    Animation anims[kAnimCount];

    const Sprite& Stand(int facing) const;  // walk frame `stand`
};

// Loads every pets/*/pet.ini; skipped pets are reported in `errors`, one line each.
std::vector<std::unique_ptr<PetDef>> LoadPetDefs(std::wstring& errors);

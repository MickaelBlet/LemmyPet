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

// A rectangle of a sheet, drawn bottom-centered in the pet's cell, moved by `dx` and `lift`.
struct Sprite {
    const Sheet* sheet = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    bool mirror = false;               // flipped horizontally
    int lift = 0;                      // transparent rows below the rectangle (a PMD frame above his feet)
    int dx = 0;                        // columns to the right of the centered rectangle, as drawn (PMD)
    int top = 0, left = 0, right = 0;  // transparent rows / columns on each side (as drawn)
};

// Facings, in direction order (screen y down): south first, then clockwise.
enum Facing { kS, kSW, kW, kNW, kN, kNE, kE, kSE, kFacingCount };
enum Anim { kWalk, kIdle, kSulk, kHurt, kSleep, kType, kPlay, kTurn, kAnimCount };

constexpr unsigned long long kNever = ~0ULL;

struct Animation {
    std::vector<Sprite> frames[kFacingCount];  // every facing filled once loaded (see LoadPetDefs)
    std::vector<int> cycle;                    // frame order; empty: all frames in turn
    std::vector<unsigned long long> ends;      // own timing (durations or PMD): end of each step of the
                                               // order, in ms; empty: steps of the caller's frame time
    bool loop = true;                          // false: plays once, then holds the last frame

    // Frame shown `t` ms into the animation (any time, wraps around), steps lasting `stepMs` unless timed.
    const Sprite& Frame(int facing, unsigned long long t, unsigned long long stepMs) const;
    // Time from `t` until the next frame, or kNever for a still.
    unsigned long long Until(int facing, unsigned long long t, unsigned long long stepMs) const;
    // Time to play it once.
    unsigned long long Length(int facing, unsigned long long stepMs) const;

private:
    size_t Step(int facing, unsigned long long t, unsigned long long stepMs, unsigned long long* left) const;
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
    bool canPlay = false;      // has [play] animations
    bool canTurn = false;      // has a [turn] animation
    std::vector<std::wstring> lyrics;  // typing bubble lines (none: no bubble)
    std::vector<std::unique_ptr<Sheet>> sheets;
    std::vector<Animation> anims[kAnimCount];  // variants (at least one once loaded), one picked per use

    const Sprite& Stand(int facing) const;  // frame `stand` of the first walk
};

// Loads every pets/*/pet.ini; skipped pets are reported in `errors`, one line each.
std::vector<std::unique_ptr<PetDef>> LoadPetDefs(std::wstring& errors);

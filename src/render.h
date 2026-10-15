// Sprite drawing into the pet's layered window.
#pragma once

#include "pet.h"

// (Re)creates a top-down 32-bit DIB of w x h selected into dc (created on first use).
bool CreateDib(HDC& dc, HBITMAP& dib, uint32_t*& bits, int w, int h);
// Rebuilds the scaling map and back buffer for the current scale.
bool BuildScaledAssets(Pet& p);
// Forgets the scaled frames (e.g. after a halo change); the next Render redraws.
void DropFrames(Pet& p);
// Shows a sprite frame (plus the "Zzz" overlay while sleeping) at the current position.
void Render(Pet& p, const Sprite& frame, ULONGLONG now);

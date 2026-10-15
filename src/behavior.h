// What a pet does: wandering, reacting to the cursor, sleeping, typing, and the per-tick update.
#pragma once

#include "pet.h"

RECT CurrentWorkArea(Pet& p);
// Spider-Man mode: the edge his feet stick to (0 floor, 1 left wall, 2 ceiling, 3 right wall).
int Orientation(Pet& p);
// p.dir is a screen direction; these convert to and from the sprite row shown once rotated.
int ViewDir(Pet& p);
int ToScreenDir(Pet& p, int viewDir);
void EnterIdle(Pet& p, ULONGLONG now);
void EnterWalk(Pet& p, ULONGLONG now);
// Sits down with his laptop for at least durationMs, in profile if he was facing sideways.
void EnterType(Pet& p, ULONGLONG now, ULONGLONG durationMs);
// The frame his current state shows at `now`.
const Sprite& ShownFrame(Pet& p, ULONGLONG now);
void Tick(Pet& p);
// (Re)starts his tick timer. The system may delay it a little to group wakeups with other timers.
void ArmTimer(Pet& p, UINT period);
// Changes the display scale, keeping his feet where they were.
void ApplyScale(Pet& p, int scalePct);

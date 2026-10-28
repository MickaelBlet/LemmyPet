// Watching what is behind the pets, so they get out of the way when it changes (text showing up, a
// video, a window opening).
#pragma once

#include "pet.h"

// Timer on the tray window sampling what is behind the pets that watch it.
constexpr UINT_PTR kBehindTimerId = 3;
// Hides from screen captures the pets that watch what is behind them (so a capture sees through them),
// and runs the sampling timer while one of them is shown.
void UpdateBehindWatch();
// Compares what is behind each watching pet with the last sample; he gets out of the way when it keeps
// changing.
void CheckBehind();
// Frees his samples.
void FreeBehind(Pet& p);

// Per-pet settings and the run-at-startup entry, in the registry.
#pragma once

#include "pet.h"

void LoadSettings(Pet& p);
void SaveSettings(Pet& p);
bool StartupEnabled();
void SetStartup(bool enabled);

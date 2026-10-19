// The pets' windows: creation, visibility and mouse dragging.
#pragma once

#include <string>

#include "pet.h"

void RegisterPetClasses(HINSTANCE inst);
// Creates a pet for each definition, spread along the floor of the main screen.
bool CreatePets(HINSTANCE inst);
void DestroyPets();
// Brings the pets in line with the pets/ folder: new ones appear, deleted ones leave, changed ones are
// reloaded (a pet that fails to load keeps his former version). Returns the failures, one line each.
std::wstring ReloadPets();
void ApplyClickThrough(Pet& p);
void SetVisible(Pet& p, bool visible);
// Stops or restarts the shown pets' ticks as the session locks or the display turns off and back.
void UpdatePause();
// Shows every pet, or hides them all when one is shown.
void ToggleAll();

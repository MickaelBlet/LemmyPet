// Typing along with the user (raw keyboard input) and the lyrics bubble above his head.
#pragma once

#include "pet.h"

// Whether he spells his lines in a bubble while typing.
bool HasBubble(const Pet& p);
// Keystrokes arrive as raw input (no polling), only while a pet is shown and types along with the user.
void UpdateKeyboardListener();
void OnRawInput(HRAWINPUT handle);
void HideBubble(Pet& p);
// Time until the bubble's content changes on its own (ULLONG_MAX: never).
ULONGLONG BubbleWait(const Pet& p, ULONGLONG now);
// Shows the line he is typing in a bubble above his head, then "..." once it is finished.
void UpdateBubble(Pet& p, ULONGLONG now);

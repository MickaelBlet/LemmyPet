// Settings window opened from the notification-area icon: one tab per pet, changes apply right away.
#pragma once

#include "pet.h"

// Creates the window, or brings it to the front.
void OpenOptions();
// Shows settings changed from elsewhere; does nothing while the window is closed.
void RefreshOptions();
// Lists the pets again after some were added, removed or reloaded; does nothing while the window is closed.
void RebuildOptions();
// Keyboard navigation (Tab, Esc, Ctrl+Tab between pets); true when the message was handled.
bool IsOptionsMessage(MSG& msg);

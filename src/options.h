// Settings window opened from the notification-area icon: one tab per pet, changes apply right away.
#pragma once

#include "pet.h"

// Creates the window, or brings it to the front.
void OpenOptions();
// Shows settings changed from elsewhere; does nothing while the window is closed.
void RefreshOptions();
// Keyboard navigation (Tab, Esc, Ctrl+Tab between pets); true when the message was handled.
bool IsOptionsMessage(MSG& msg);

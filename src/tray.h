// Notification-area icon (click: settings window, right-click: short menu); its hidden window also receives the raw keyboard input.
#pragma once

#include "pet.h"

bool CreateTray(HINSTANCE inst);
void ShowTrayIcon(HINSTANCE inst);

// Gripline Lite for Forza Horizon 1: ReImagined - settings shared by the Gripline hooks.
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// Changed in the Gripline menu (F6, hooks/gripline_menu.cpp) and saved to gripline.ini in the
// game's working folder (the runtime folder).
#pragma once

#include <atomic>
#include <cstdint>

namespace forza::gripline {

struct Settings {
  std::atomic<bool> wheel_1to1{true};   // cockpit wheel turns as far as the real wheel
  std::atomic<bool> hide_hands{true};   // hide the driver's hands while the wheel is 1:1
  std::atomic<int> rotation{900};       // the real wheel's rotation, degrees lock to lock
  std::atomic<bool> dashcam{true};      // the Dashboard Cam step in the camera cycle
  std::atomic<float> dash_forward{0.28f};  // metres forward of the driver's eye
  std::atomic<float> dash_up{-0.03f};      // metres up
  std::atomic<float> dash_pitch{0.0f};     // degrees up
  std::atomic<int> traffic_level{2};       // 0 off, 1 less, 2 normal, 3 more, 4 lots, 5 max
  std::atomic<int> drivatar_level{2};      // the same for the Drivatars (festival cars)
};

Settings& Get();
void LoadSettings();  // once, first use

// Traffic and Drivatar limits from the settings (hooks/gripline_traffic.cpp), main loop.
void ApplyTraffic(uint8_t* base);

// The Dashboard Cam is the view drawn now (set by the camera hook every frame).
bool DashCamDrawn();
// The active camera is the in-car view (or the Dashboard Cam).
bool InCarView();

// Player 1's pad, every XamInputGetState (kernel/input_autopress.cpp): the menu and the
// Dashboard Cam step. Returns the buttons the game gets.
uint16_t DashCamPad(uint8_t* base, uint16_t buttons);
void NoteCaps(uint8_t* base, uint32_t result, uint32_t caps);
// The menu is open: the game gets no buttons and no right stick from player 1.
bool MenuOpen();
// Called from DashCamPad: opens/closes the menu, reads its keys and the pad's d-pad.
// Returns true while the menu is open.
bool MenuPoll(uint16_t buttons);

}  // namespace forza::gripline

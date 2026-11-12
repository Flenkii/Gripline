// Gripline Lite for Forza Horizon 1: ReImagined - the walking fade to black.
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// A full-screen black drawn with the SDK's ImGui drawer, over the game's picture, while getting
// out of the car or back in, and the walking HUD while walking (hooks/gripline_walking.cpp). Installed from forza::InstallHud
// (platform/hud_overlay.cpp, one line added by the Gripline Lite installer).
#pragma once

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace forza::gripline {
void InstallFade(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& context);
// Shows or hides the overlay (the black, the walking HUD) as the walking code wants. Any thread.
void FadeUpdate();
// Moves the game's HUD off the screen (true) or back (false). Any thread; done on the UI thread.
void HudHide(bool hide);
}  // namespace forza::gripline

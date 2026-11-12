// Gripline Lite for Forza Horizon 1: ReImagined - the Gripline menu (F6) and its settings.
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// F6 opens and closes the menu (the game window must have focus); Esc closes it too. Up/Down
// (arrow keys or the d-pad) pick a row, Left/Right change it. It is drawn with the HUD overlay
// (platform/hud_overlay.h). While it is open the game gets no buttons and no right stick from
// player 1, so the car can still be steered and driven to look at the Dashboard Cam.
// Settings are saved to gripline.ini in the working folder at each change.
#include "gripline_settings.h"

#include "../platform/hud_overlay.h"
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace forza::gripline {
namespace {
constexpr const char* kFile = "gripline.ini";
constexpr uint16_t kDpadUp = 0x0001, kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008;
constexpr float kDefaultForward = 0.28f, kDefaultUp = -0.03f, kDefaultPitch = 0.0f;

Settings settings;
std::once_flag load_once;
std::atomic<bool> open{false};

bool German() {
#ifdef _WIN32
  return (GetUserDefaultUILanguage() & 0x3FF) == 0x07;  // LANG_GERMAN
#else
  return false;
#endif
}

void Save() {
  std::FILE* f = std::fopen(kFile, "w");
  if (!f) {
    REXLOG_WARN("Gripline: cannot write {}", kFile);
    return;
  }
  std::fprintf(f, "# Gripline Lite settings (F6 menu in game)\n");
  std::fprintf(f, "wheel_1to1=%d\nhide_hands=%d\nrotation=%d\n", int(settings.wheel_1to1.load()),
               int(settings.hide_hands.load()), settings.rotation.load());
  std::fprintf(f, "traffic=%d\ndrivatars=%d\n", settings.traffic_level.load(),
               settings.drivatar_level.load());
  std::fprintf(f, "dashcam=%d\ndash_forward=%.3f\ndash_up=%.3f\ndash_pitch=%.1f\n",
               int(settings.dashcam.load()), settings.dash_forward.load(), settings.dash_up.load(),
               settings.dash_pitch.load());
  std::fprintf(f, "walking=%d\nwalk_eye=%.2f\n", int(settings.walking.load()), settings.walk_eye.load());
  std::fclose(f);
}

// The rows: their label and value text, and what Left (-1) / Right (+1) does.
struct Row {
  const char* de;
  const char* en;
};
constexpr Row kRows[] = {
    {"Lenkrad 1:1", "Wheel 1:1"},
    {"Lenkrad-Drehung", "Wheel rotation"},
    {"Hände bei 1:1", "Hands with 1:1"},
    {"Dashboard Cam", "Dashboard Cam"},
    {"Dashcam vor/zurück", "Dashcam forward/back"},
    {"Dashcam hoch/runter", "Dashcam up/down"},
    {"Dashcam Neigung", "Dashcam tilt"},
    {"Dashcam zurücksetzen", "Reset Dashcam"},
    {"Verkehr", "Traffic"},
    {"Drivatare", "Drivatars"},
    {"Walking (F / linker Stick)", "Walking (F / left stick)"},
    {"Walking Augenhöhe", "Walking eye height"},
};
constexpr int kRowCount = int(sizeof(kRows) / sizeof(kRows[0]));
int row = 0;

std::string Value(int r, bool de) {
  char text[64];
  switch (r) {
    case 0: return settings.wheel_1to1 ? (de ? "An" : "On") : (de ? "Aus (normal FH1)" : "Off (stock FH1)");
    case 1: std::snprintf(text, sizeof(text), "%d°", settings.rotation.load()); return text;
    case 2: return settings.hide_hands ? (de ? "Ausblenden" : "Hidden") : (de ? "Anzeigen" : "Shown");
    case 3: return settings.dashcam ? (de ? "An" : "On") : (de ? "Aus" : "Off");
    case 4: std::snprintf(text, sizeof(text), "%+.0f cm", settings.dash_forward * 100.0f); return text;
    case 5: std::snprintf(text, sizeof(text), "%+.0f cm", settings.dash_up * 100.0f); return text;
    case 6: std::snprintf(text, sizeof(text), "%+.0f°", settings.dash_pitch.load()); return text;
    case 7: return de ? "Rechts drücken" : "Press Right";
    case 10: return settings.walking ? (de ? "An" : "On") : (de ? "Aus" : "Off");
    case 11: std::snprintf(text, sizeof(text), "%.2f m", settings.walk_eye.load()); return text;
    default: {
      static const char* kDe[] = {"Aus", "Wenig", "Normal", "Mehr", "Viel", "Maximal"};
      static const char* kEn[] = {"Off", "Less", "Normal", "More", "Lots", "Maximum"};
      const int level = std::clamp(r == 8 ? settings.traffic_level.load() : settings.drivatar_level.load(), 0, 5);
      return de ? kDe[level] : kEn[level];
    }
  }
}

void Change(int r, int step) {
  auto add = [](std::atomic<float>& v, float d, float lo, float hi) {
    v = std::clamp(std::round((v.load() + d) * 1000.0f) / 1000.0f, lo, hi);
  };
  switch (r) {
    case 0: settings.wheel_1to1 = !settings.wheel_1to1; break;
    case 1: settings.rotation = std::clamp(settings.rotation + step * 90, 180, 1080); break;
    case 2: settings.hide_hands = !settings.hide_hands; break;
    case 3: settings.dashcam = !settings.dashcam; break;
    case 4: add(settings.dash_forward, step * 0.02f, -0.30f, 0.80f); break;
    case 5: add(settings.dash_up, step * 0.01f, -0.30f, 0.30f); break;
    case 6: add(settings.dash_pitch, step * 1.0f, -20.0f, 20.0f); break;
    case 8: settings.traffic_level = std::clamp(settings.traffic_level + step, 0, 5); break;
    case 9: settings.drivatar_level = std::clamp(settings.drivatar_level + step, 0, 5); break;
    case 10: settings.walking = !settings.walking; break;
    case 11: add(settings.walk_eye, step * 0.05f, 0.50f, 2.00f); break;
    default:
      if (step > 0) {
        settings.dash_forward = kDefaultForward;
        settings.dash_up = kDefaultUp;
        settings.dash_pitch = kDefaultPitch;
      }
      break;
  }
  Save();
}

void Draw() {
  const bool de = German();
  std::string body;
  for (int r = 0; r < kRowCount; ++r) {
    if (r) body += '\n';
    if (r == row) body += kHudHighlight;
    body += r == row ? "> " : "   ";
    body += de ? kRows[r].de : kRows[r].en;
    body += ":  ";
    body += Value(r, de);
  }
  body += de ? "\n\nHoch/Runter: wählen   Links/Rechts: ändern   F6/Esc: schließen"
             : "\n\nUp/Down: select   Left/Right: change   F6/Esc: close";
  if (!InCarView())
    body += de ? "\nTipp: Innenansicht wählen, dann siehst du Änderungen sofort."
               : "\nTip: pick the in-car view to see changes right away.";
  HudShow("GRIPLINE LITE 1.0", body, 3600.0, false);
}

bool MenuKeyPressed() {
#ifdef _WIN32
  static bool was_down = false;
  const bool down = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
  const bool edge = down && !was_down;
  was_down = down;
  if (!edge) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
#else
  return false;
#endif
}
}  // namespace

Settings& Get() {
  LoadSettings();
  return settings;
}

void LoadSettings() {
  std::call_once(load_once, [] {
    std::FILE* f = std::fopen(kFile, "r");
    if (!f) {
      REXLOG_INFO("Gripline: no {} yet, defaults", kFile);
      return;
    }
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
      char key[32];
      float v;
      if (std::sscanf(line, " %31[a-z_0-9] = %f", key, &v) != 2) continue;
      const std::string k = key;
      if (k == "wheel_1to1") settings.wheel_1to1 = v != 0;
      else if (k == "hide_hands") settings.hide_hands = v != 0;
      else if (k == "rotation") settings.rotation = std::clamp(int(v), 180, 1080);
      else if (k == "dashcam") settings.dashcam = v != 0;
      else if (k == "traffic") settings.traffic_level = std::clamp(int(v), 0, 5);
      else if (k == "drivatars") settings.drivatar_level = std::clamp(int(v), 0, 5);
      else if (k == "dash_forward") settings.dash_forward = std::clamp(v, -0.30f, 0.80f);
      else if (k == "dash_up") settings.dash_up = std::clamp(v, -0.30f, 0.30f);
      else if (k == "dash_pitch") settings.dash_pitch = std::clamp(v, -20.0f, 20.0f);
      else if (k == "walking") settings.walking = v != 0;
      else if (k == "walk_eye") settings.walk_eye = std::clamp(v, 0.50f, 2.00f);
    }
    std::fclose(f);
    REXLOG_INFO("Gripline: settings loaded from {}", kFile);
  });
}

bool MenuOpen() { return open.load(std::memory_order_relaxed); }

bool MenuPoll(uint16_t buttons) {
  LoadSettings();
  static uint16_t previous = 0;
  const uint16_t pressed = buttons & ~previous;
  previous = buttons;
  const bool key = MenuKeyPressed();
  if (!open) {
    if (!key) return false;
    open = true;
    HudWantKeys(true);
    HudTakeKeys();  // drop old keys
    REXLOG_INFO("Gripline: menu open");
    Draw();
    return true;
  }
  const HudKeys keys = HudTakeKeys();
  if (key || keys.escape) {
    open = false;
    HudWantKeys(false);
    HudClear();
    REXLOG_INFO("Gripline: menu closed");
    return false;
  }
  bool redraw = false;
  if (keys.up || (pressed & kDpadUp)) row = (row + kRowCount - 1) % kRowCount, redraw = true;
  if (keys.down || (pressed & kDpadDown)) row = (row + 1) % kRowCount, redraw = true;
  if (keys.left || (pressed & kDpadLeft)) Change(row, -1), redraw = true;
  if (keys.right || keys.enter || (pressed & kDpadRight)) Change(row, +1), redraw = true;
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  if (redraw || now - last > std::chrono::milliseconds(500)) {
    last = now;
    Draw();
  }
  return true;
}

}  // namespace forza::gripline

// Gripline Lite for Forza Horizon 1: ReImagined - Dashboard Cam.
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// An extra in-car camera: one more step in the camera cycle right after the in-car view
// (CCamDriver). The change-camera button (RB on a pad, Back on a wheel) pressed in the in-car
// view switches the Dashboard Cam on and is hidden from the game; pressed again it switches the
// Dashboard Cam off and reaches the game, which goes on to its next camera.
//
// The Dashboard Cam is the in-car view moved forward and down in camera space after the
// director's step (sub_82855070, TU4 sub_82905418) built it: the view matrix (director +240)
// and the position (+176). The next step gets the camera's own values back first. While it is
// on, the steering wheel's matrices (wheel presentation +4864 and +4960, written by
// sub_82E962A8) are zeroed, so the wheel is not drawn; the hands are behind the camera.
//
// The offsets and switches are in the Gripline menu (F6, hooks/gripline_menu.cpp). While the
// Dashboard Cam or the 1:1 wheel with hidden hands is on, the hands' transform (the matrix
// passed by value to sub_82E50528 and kept at hands +0x10) is shrunk to a point.
#include "gripline_settings.h"

#include "../platform/guest_addr.h"
#include "../platform/guest_memory.h"
#include "../platform/driving_gate.h"
#include "../platform/vr_head_pose_math.h"
#include "../platform/xr.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

REXCVAR_DEFINE_INT32(gripline_dashcam_button, 0, "Gripline",
                     "Dashboard Cam button: 0 auto (Back once a wheel was seen, else RB), 1 RB, 2 Back");

namespace {
constexpr uint16_t kPadRB = 0x0200;
constexpr uint16_t kPadBack = 0x0020;
constexpr uint8_t kSubtypeWheel = 0x02;

std::atomic<bool> dash_on{false};     // the player picked the Dashboard Cam
std::atomic<bool> dash_drawn{false};  // the last view was posed as the Dashboard Cam
std::atomic<bool> wheel_seen{false};  // a controller reported itself as a wheel (sticky)
std::atomic<bool> in_car{false};      // the active camera is the in-car view

bool Valid(uint32_t address) { return address >= 0x00100000 && address < 0xF0000000; }

uint32_t Load32(uint8_t* base, uint32_t address) {
  const auto* p = forza::GuestPtr(base, address);
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
float LoadFloat(uint8_t* base, uint32_t address) {
  const uint32_t bits = Load32(base, address);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}
void StoreFloat(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  auto* p = forza::GuestPtr(base, address);
  p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
}

// View 0's director, as hooks/vr_head_pose.cpp finds it.
uint32_t FirstViewDirector(uint8_t* base) {
  const uint32_t root = Load32(base, FORZA_ADDR(0x832DF024));
  if (!Valid(root)) return 0;
  const uint32_t holder = Load32(base, root + 4);
  if (!Valid(holder)) return 0;
  const uint32_t manager = Load32(base, holder + 4);
  if (!Valid(manager)) return 0;
  return Load32(base, manager + 244 + 8);
}

bool InCarCamera(uint8_t* base, uint32_t director) {
  if (!Valid(director)) return false;
  const uint32_t camera = Load32(base, director + 20);
  if (!Valid(camera)) return false;
  const uint32_t vtable = Load32(base, camera);
  return vtable == FORZA_ADDR(0x82075DD4)     // Camera::CCamDriver (the in-car view)
         || vtable == FORZA_ADDR(0x82075CD4);  // Camera::CCamCockpit
}

struct Offsets {
  float forward, up, pitch;
};
Offsets ReadOffsets() {
  auto& g = forza::gripline::Get();
  return {g.dash_forward.load(), g.dash_up.load(), g.dash_pitch.load()};
}

uint16_t CameraButton() {
  switch (REXCVAR_GET(gripline_dashcam_button)) {
    case 1: return kPadRB;
    case 2: return kPadBack;
    default: return wheel_seen.load(std::memory_order_relaxed) ? kPadBack : kPadRB;
  }
}
}  // namespace

bool forza::gripline::DashCamDrawn() { return dash_drawn.load(std::memory_order_relaxed); }
bool forza::gripline::InCarView() { return in_car.load(std::memory_order_relaxed); }

// Player 1's pad, every XamInputGetState (kernel/input_autopress.cpp).
uint16_t forza::gripline::DashCamPad(uint8_t* base, uint16_t buttons) {
  static uint16_t previous = 0;
  static uint16_t swallowed = 0;  // the button hidden from the game until it is released
  const bool car = InCarCamera(base, FirstViewDirector(base));
  in_car.store(car, std::memory_order_relaxed);
  if (MenuPoll(buttons)) {
    previous = buttons;
    return buttons;  // the menu is open: input_autopress.cpp takes the buttons from the game
  }
  const uint16_t button = CameraButton();
  const bool edge = (buttons & button) && !(previous & button);
  previous = buttons;
  if (swallowed) {
    if (!(buttons & swallowed)) swallowed = 0;
    else buttons &= uint16_t(~swallowed);
    return buttons;
  }
  if (!Get().dashcam || !car || forza::xr::CockpitActive()) {
    if (dash_on.exchange(false)) REXLOG_INFO("Gripline: Dashboard Cam off (left the in-car view)");
    return buttons;
  }
  if (!edge) return buttons;
  if (forza::telemetry::Player().State() != forza::telemetry::Drive::kDriving) return buttons;
  if (!dash_on.load()) {
    dash_on = true;
    swallowed = button;
    buttons &= uint16_t(~button);
    REXLOG_INFO("Gripline: Dashboard Cam on ({})", button == kPadBack ? "Back" : "RB");
  } else {
    dash_on = false;  // the press goes on to the game: its next camera
    REXLOG_INFO("Gripline: Dashboard Cam off, next camera");
  }
  return buttons;
}

// Player 1's capabilities (XamInputGetCapabilities[Ex]): a wheel changes camera with Back.
void forza::gripline::NoteCaps(uint8_t* base, uint32_t result, uint32_t caps) {
  if (result != 0 || !Valid(caps)) return;
  if (forza::GuestPtr(base, caps)[1] != kSubtypeWheel) return;
  if (!wheel_seen.exchange(true))
    REXLOG_INFO("Gripline: a wheel is connected (Dashboard Cam button Back)");
}

#if FORZA_TU4
#define GriplineDirectorStep sub_82905418
#define GriplineDirectorStepOriginal __imp__sub_82905418
#define GriplineWheelPresent sub_82E962A8
#define GriplineWheelPresentOriginal __imp__sub_82E962A8
#define GriplineHandsPlace sub_82E50528
#define GriplineHandsPlaceOriginal __imp__sub_82E50528
extern "C" REX_FUNC(GriplineDirectorStepOriginal);
extern "C" REX_FUNC(GriplineWheelPresentOriginal);
extern "C" REX_FUNC(GriplineHandsPlaceOriginal);

namespace {
constexpr uint32_t kViewMatrix = 240;  // director: world-to-camera, row-major 4x4, big-endian
constexpr uint32_t kPosition = 176;    // director: camera position
constexpr uint32_t kWheelMatrix = 4864;
constexpr uint32_t kWheelMatrix2 = 4960;

struct Applied {
  uint32_t director = 0;
  uint8_t original_view[64], posed_view[64];
  uint8_t original_pos[12], posed_pos[12];
};
Applied applied;  // main loop thread only
}  // namespace

// The camera director's step (main loop): the active camera builds the view, camera physics
// shake it. Afterwards the Dashboard Cam moves it.
extern "C" REX_FUNC(GriplineDirectorStep) {
  const uint32_t director = ctx.r3.u32;
  if (applied.director && applied.director == director) {
    uint8_t* view = forza::GuestPtr(base, director + kViewMatrix);
    uint8_t* pos = forza::GuestPtr(base, director + kPosition);
    if (std::memcmp(view, applied.posed_view, 64) == 0) std::memcpy(view, applied.original_view, 64);
    if (std::memcmp(pos, applied.posed_pos, 12) == 0) std::memcpy(pos, applied.original_pos, 12);
    applied.director = 0;
  }
  GriplineDirectorStepOriginal(ctx, base);
  forza::gripline::ApplyTraffic(base);
  const bool pose = dash_on.load(std::memory_order_relaxed) && forza::gripline::Get().dashcam &&
                    !forza::xr::CockpitActive() && Valid(director) &&
                    director == FirstViewDirector(base) && InCarCamera(base, director);
  dash_drawn.store(pose, std::memory_order_relaxed);
  if (!pose) return;
  const Offsets o = ReadOffsets();
  float view[16];
  for (int i = 0; i < 16; ++i) view[i] = LoadFloat(base, director + kViewMatrix + 4 * i);
  // Rows 0-2 hold the camera's right, up and forward axes as columns (world space).
  float pos[3];
  for (int i = 0; i < 3; ++i) pos[i] = LoadFloat(base, director + kPosition + 4 * i);
  for (int i = 0; i < 3; ++i) pos[i] += o.up * view[i * 4 + 1] + o.forward * view[i * 4 + 2];
  forza::vr::HeadPose head;
  head.pitch = o.pitch;
  head.y = o.up, head.z = o.forward;
  forza::vr::ApplyHeadPose(view, head);
  applied.director = director;
  std::memcpy(applied.original_view, forza::GuestPtr(base, director + kViewMatrix), 64);
  std::memcpy(applied.original_pos, forza::GuestPtr(base, director + kPosition), 12);
  for (int i = 0; i < 16; ++i) StoreFloat(base, director + kViewMatrix + 4 * i, view[i]);
  for (int i = 0; i < 3; ++i) StoreFloat(base, director + kPosition + 4 * i, pos[i]);
  std::memcpy(applied.posed_view, forza::GuestPtr(base, director + kViewMatrix), 64);
  std::memcpy(applied.posed_pos, forza::GuestPtr(base, director + kPosition), 12);
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true))
    REXLOG_INFO("Gripline: Dashboard Cam first posed view (director {:08X})", director);
}

// The cockpit wheel's presentation (f1 = wheel angle): builds the wheel's matrices.
extern "C" REX_FUNC(GriplineWheelPresent) {
  const uint32_t presentation = ctx.r3.u32;
  GriplineWheelPresentOriginal(ctx, base);
  if (!dash_drawn.load(std::memory_order_relaxed) || !Valid(presentation))
    return;
  // The rotation rows to zero: the wheel collapses to its hub and is not seen.
  std::memset(forza::GuestPtr(base, presentation + kWheelMatrix), 0, 48);
  std::memset(forza::GuestPtr(base, presentation + kWheelMatrix2), 0, 48);
}

namespace {
// A 64-bit register holding two big-endian floats of the matrix, both scaled.
uint64_t ScalePair(uint64_t pair, float scale) {
  uint32_t hi = uint32_t(pair >> 32), lo = uint32_t(pair);
  float a, b;
  std::memcpy(&a, &hi, 4);
  std::memcpy(&b, &lo, 4);
  a *= scale, b *= scale;
  std::memcpy(&hi, &a, 4);
  std::memcpy(&lo, &b, 4);
  return uint64_t(hi) << 32 | lo;
}
}  // namespace

// The driver's hands (r3 = hands, f1, the hands' 4x4 transform by value in r4-r10 and the
// caller's stack +0x58): kept at hands +0x10 for the frame. Hidden by shrinking the three axis
// rows to almost nothing (the position stays, so nothing divides by zero).
extern "C" REX_FUNC(GriplineHandsPlace) {
  auto& g = forza::gripline::Get();
  const bool hide = dash_drawn.load(std::memory_order_relaxed) ||
                    (g.wheel_1to1 && g.hide_hands && in_car.load(std::memory_order_relaxed));
  if (hide) {
    constexpr float kTiny = 1e-4f;
    ctx.r4.u64 = ScalePair(ctx.r4.u64, kTiny);  // row 0
    ctx.r5.u64 = ScalePair(ctx.r5.u64, kTiny);
    ctx.r6.u64 = ScalePair(ctx.r6.u64, kTiny);  // row 1
    ctx.r7.u64 = ScalePair(ctx.r7.u64, kTiny);
    ctx.r8.u64 = ScalePair(ctx.r8.u64, kTiny);  // row 2
    ctx.r9.u64 = ScalePair(ctx.r9.u64, kTiny);
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) REXLOG_INFO("Gripline: hands hidden");
  }
  GriplineHandsPlaceOriginal(ctx, base);
}
#endif

// Gripline Lite for Forza Horizon 1: ReImagined - cockpit steering wheel 1:1.
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's cockpit steering filter (TU4 sub_8253ED98) turns a 900 degree wheel's input
// into +-450 degrees (input * 900 * pi/360), then clamps the graphics angle to +-pi/2 with
// the float at 0x83301F60 (read only by that filter). This hook raises that clamp to half
// the wheel's rotation, so the in-car wheel can follow the real one 1:1, and keeps the
// driver's hands animation (sub_82E4C468: t = (angle + pi/2) / pi) inside its range. It also
// sets the wheel model's angle scale to 1 (stock 0.75), so no game file has to be changed.
#include "gripline_settings.h"

#include "../platform/guest_addr.h"
#include "../platform/guest_memory.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_DOUBLE(gripline_wheel_angle_scale, 1.0, "Gripline",
                      "Cockpit wheel angle scale for the 1:1 wheel (stock 0.75 from GlobalCarAttribs)");

#if FORZA_TU4
#define GriplineHandsAnim sub_82E4C468
#define GriplineHandsAnimOriginal __imp__sub_82E4C468
extern "C" REX_FUNC(GriplineHandsAnimOriginal);

namespace {
constexpr uint32_t kClampAddress = 0x83301F60;  // float pi/2, big-endian
constexpr float kHalfPi = 1.57079637f;
// Driver attribs (GlobalCarAttribs.xml, loaded into this struct): SteeringWheelAngleScale at +36,
// read by the wheel's presentation (sub_82E962A8) as the wheel model's turn per steering angle.
constexpr uint32_t kAngleScaleAddress = 0x8339F2F0 + 36;
float stock_scale = 0.0f;  // the game's own value, kept the first time it is replaced
std::atomic<uint32_t> calls{0};

float LoadFloat(uint8_t* base, uint32_t address) {
  const auto* p = forza::GuestPtr(base, address);
  const uint32_t bits = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
void StoreFloat(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  auto* p = forza::GuestPtr(base, address);
  p[0] = uint8_t(bits >> 24);
  p[1] = uint8_t(bits >> 16);
  p[2] = uint8_t(bits >> 8);
  p[3] = uint8_t(bits);
}
}  // namespace

// Render thread, the local cockpit's driver hands: f1 = steering wheel angle in radians
// (plus the car's SteeringWheelAngleOverride).
extern "C" REX_FUNC(GriplineHandsAnim) {
  auto& settings = forza::gripline::Get();
  const bool on = settings.wheel_1to1;
  const int rotation = std::clamp(settings.rotation.load(), 180, 1080);
  const float clamp = on ? float(rotation) * 0.5f * 0.0174532925f : kHalfPi;
  const float stored = LoadFloat(base, kClampAddress);
  if (stored != clamp) {
    StoreFloat(base, kClampAddress, clamp);
    REXLOG_INFO("Gripline: cockpit wheel clamp {:.4f} -> {:.4f} rad ({} degrees lock to lock)",
                stored, clamp, on ? rotation : 180);
  }
  const float scale_now = LoadFloat(base, kAngleScaleAddress);
  const float scale_want = on ? float(std::clamp(REXCVAR_GET(gripline_wheel_angle_scale), 0.1, 4.0))
                              : (stock_scale > 0 ? stock_scale : scale_now);
  if (scale_now > 0 && scale_now < 10 && scale_now != scale_want) {
    if (stock_scale == 0) stock_scale = scale_now;
    StoreFloat(base, kAngleScaleAddress, scale_want);
    REXLOG_INFO("Gripline: SteeringWheelAngleScale {:.3f} -> {:.3f}", scale_now, scale_want);
  }
  const float angle = float(ctx.f1.f64);
  const uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (n < 5 || n % 3600 == 0)
    REXLOG_INFO("Gripline: wheel angle {:.4f} rad ({:.1f} degrees)", angle, angle * 57.2957795f);
  if (on && std::isfinite(angle))
    ctx.f1.f64 = double(std::clamp(angle, -kHalfPi, kHalfPi));
  GriplineHandsAnimOriginal(ctx, base);
}
#endif

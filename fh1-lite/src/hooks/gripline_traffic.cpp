// Gripline Lite for Forza Horizon 1: ReImagined - adjustable traffic and Drivatars.
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// The open world's AI spawner reads its limits from game tunables every time it decides
// (TU4, each a float at tunable +8): AI/Spawning/Density/MaxTraffic (16, by day) and MinTraffic
// (8, at night) cap the civilian cars (sub_82C0B8xx lerps between them by the time of day),
// MaxFestival (7) and MinFestival (2) the Drivatars. AI/Spawning/Interval/TrafficMin/Med/Max
// (0.5, 4, 20 s) space the spawns. The Gripline menu's levels scale these live; the game's own
// values are kept from the first read and put back at "Normal".
#include "gripline_settings.h"

#include "../platform/guest_addr.h"
#include "../platform/guest_memory.h"
#include <rex/logging.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if FORZA_TU4
namespace {
struct Tunable {
  const char* name;
  uint32_t address;  // the tunable; its value is the float at +8
  bool count;        // a car count (scaled up) or a spawn interval (scaled down)
  bool festival;     // Drivatars, else civilian traffic
  float stock = -1;
};
Tunable tunables[] = {
    {"MaxTraffic", 0x8342C53C, true, false},       {"MinTraffic", 0x8342C594, true, false},
    {"MaxFestival", 0x8342C5C4, true, true},       {"MinFestival", 0x8342C47C, true, true},
    {"Interval/TrafficMin", 0x8342CE2C, false, false},
    {"Interval/TrafficMed", 0x8342CB38, false, false},
    {"Interval/TrafficMax", 0x8342CBEC, false, false},
    {"Interval/FestivalMin", 0x8342C700, false, true},
    {"Interval/FestivalMax", 0x8342C6E8, false, true},
};

// Level 0 off, 1 less, 2 normal, 3 more, 4 lots, 5 maximum.
float Factor(int level) {
  static constexpr float kFactors[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
  return kFactors[std::clamp(level, 0, 5)];
}

float Load(uint8_t* base, uint32_t address) {
  const uint8_t* p = forza::GuestPtr(base, address);
  const uint32_t bits = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  float v;
  std::memcpy(&v, &bits, 4);
  return v;
}
void Store(uint8_t* base, uint32_t address, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  uint8_t* p = forza::GuestPtr(base, address);
  p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
}
}  // namespace

namespace forza::gripline {
// Main loop, once a frame (the camera director's step).
void ApplyTraffic(uint8_t* base) {
  auto& g = Get();
  const int traffic = g.traffic_level, drivatars = g.drivatar_level;
  static int applied_traffic = -1, applied_drivatars = -1;
  for (auto& t : tunables) {
    const uint32_t value = t.address + 8;
    const float now = Load(base, value);
    if (t.stock < 0) {
      if (!std::isfinite(now) || now < 0 || now > 1000) continue;  // not loaded yet
      t.stock = now;
    }
    const int level = t.festival ? drivatars : traffic;
    const float f = Factor(level);
    float want;
    if (t.count) {
      want = std::round(t.stock * f);
    } else {
      // More cars spawn sooner; less or none keep the game's spacing.
      want = f > 1.0f ? std::max(0.25f, t.stock / f) : t.stock;
    }
    if (now != want) Store(base, value, want);
  }
  if (traffic != applied_traffic || drivatars != applied_drivatars) {
    applied_traffic = traffic, applied_drivatars = drivatars;
    REXLOG_INFO("Gripline: traffic level {} (max {:.0f} cars by day), Drivatar level {} (max {:.0f})",
                traffic, Load(base, tunables[0].address + 8), drivatars,
                Load(base, tunables[2].address + 8));
  }
}
}  // namespace forza::gripline
#else
namespace forza::gripline {
void ApplyTraffic(uint8_t*) {}
}  // namespace forza::gripline
#endif

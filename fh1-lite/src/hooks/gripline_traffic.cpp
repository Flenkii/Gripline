// Gripline Lite for Forza Horizon 1: ReImagined - adjustable traffic and Drivatars.
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// The open world's AI spawner reads its limits from game tunables every time it decides
// (TU4, each a float at tunable +8): AI/Spawning/Density/MaxTraffic (16, by day) and MinTraffic
// (8, at night) cap the civilian cars (sub_82C0B8xx lerps between them by the time of day),
// MaxFestival (7) and MinFestival (2) the Drivatars. AI/Spawning/Interval/TrafficMin/Med/Max
// (0.5, 4, 20 s) space the spawns. The Gripline menu's levels scale these live; the game's own
// values are kept from the first read and put back at "Normal".
//
// Those are only caps: how many cars the spawner wants comes from the road densities in
// gametunablesettings AIOpenWorld.xml (<Density min max> per road type, per cartype traffic or
// festival). sub_82C14C80 appends each parsed density (0x28 bytes, min at +0, max at +4) to its
// list (a vector: begin, end, capacity); the parser keeps the cartype in r20 (2 traffic,
// 1 festival). The hook remembers the lists and the game's values, and the levels scale them
// live, so "More" and above really bring more cars.
#include "gripline_settings.h"

#include "../platform/guest_addr.h"
#include "../platform/guest_memory.h"
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <algorithm>
#include <mutex>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>

#if FORZA_TU4
#define GriplineDensityAppend sub_82C14C80
#define GriplineDensityAppendOriginal __imp__sub_82C14C80
extern "C" REX_FUNC(GriplineDensityAppendOriginal);

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
    // AI/Traffic/Density/* (sub_82C0D0xx lerps them by the time of day, cartype 2 traffic):
    // the caps the traffic system itself uses, apart from the spawner's above.
    {"Traffic/MaxTraffic", 0x8342C394, true, false},  {"Traffic/MinTraffic", 0x8342C3D0, true, false},
    {"Traffic/MaxFestival", 0x8342C430, true, true},  {"Traffic/MinFestival", 0x8342C4AC, true, true},
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
uint32_t LoadU32(uint8_t* base, uint32_t address) {
  const uint8_t* p = forza::GuestPtr(base, address);
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// One parsed density list and the game's min/max of each of its entries.
struct DensityList {
  uint32_t vector;  // guest: begin, end, capacity
  bool festival;
  std::vector<float> stock;  // min, max, min, max, ...
  float applied = 1.0f;      // the factor its entries carry now
};
constexpr uint32_t kDensitySize = 0x28;
std::mutex density_mutex;
std::vector<DensityList> density_lists;

float DensityFactor(bool festival) {
  auto& g = forza::gripline::Get();
  return Factor(festival ? g.drivatar_level : g.traffic_level);
}

bool Near(float a, float b) { return std::fabs(a - b) <= 1e-4f + std::fabs(b) * 1e-4f; }

// Puts the factor on one list; drops the list if its memory no longer holds what we wrote.
bool ScaleList(uint8_t* base, DensityList& list, float f) {
  const uint32_t begin = LoadU32(base, list.vector), end = LoadU32(base, list.vector + 4);
  if (!begin || end < begin || (end - begin) / kDensitySize != list.stock.size() / 2) return false;
  for (size_t i = 0; i < list.stock.size() / 2; ++i) {
    const uint32_t e = begin + uint32_t(i) * kDensitySize;
    if (!Near(Load(base, e), list.stock[2 * i] * list.applied) ||
        !Near(Load(base, e + 4), list.stock[2 * i + 1] * list.applied))
      return false;
  }
  for (size_t i = 0; i < list.stock.size() / 2; ++i) {
    const uint32_t e = begin + uint32_t(i) * kDensitySize;
    Store(base, e, list.stock[2 * i] * f);
    Store(base, e + 4, list.stock[2 * i + 1] * f);
  }
  list.applied = f;
  return true;
}

void ApplyDensities(uint8_t* base) {
  std::lock_guard lock(density_mutex);
  for (size_t i = 0; i < density_lists.size();) {
    auto& list = density_lists[i];
    const float f = DensityFactor(list.festival);
    if (f == list.applied || ScaleList(base, list, f)) {
      ++i;
    } else {
      density_lists.erase(density_lists.begin() + i);
    }
  }
}
}  // namespace

// The AIOpenWorld.xml parser appends one <Density> (r4) to a cartype's list (r3).
extern "C" REX_FUNC(GriplineDensityAppend) {
  const uint32_t vector = ctx.r3.u32, density = ctx.r4.u32;
  const uint32_t cartype = ctx.r20.u32;  // the parser's: 2 traffic, 1 festival
  if ((cartype == 1 || cartype == 2) && vector && density) {
    const bool festival = cartype == 1;
    const float min = Load(base, density), max = Load(base, density + 4);
    std::lock_guard lock(density_mutex);
    DensityList* list = nullptr;
    for (auto& l : density_lists)
      if (l.vector == vector) list = &l;
    const uint32_t begin = LoadU32(base, vector), end = LoadU32(base, vector + 4);
    const size_t count = end >= begin ? (end - begin) / kDensitySize : 0;
    if (!list || list->stock.size() / 2 != count) {
      // A new list (or one the game rebuilt): start over with what it holds now.
      std::erase_if(density_lists, [&](const DensityList& l) { return l.vector == vector; });
      if (count == 0) {
        density_lists.push_back({vector, festival, {}, 1.0f});
        list = &density_lists.back();
      }
    }
    if (list) {
      const float f = DensityFactor(festival);
      if (list->stock.empty()) list->applied = f;
      if (Near(f, list->applied)) {
        list->stock.push_back(min);
        list->stock.push_back(max);
        Store(base, density, min * f);
        Store(base, density + 4, max * f);
      } else {
        std::erase_if(density_lists, [&](const DensityList& l) { return l.vector == vector; });
      }
    }
  }
  GriplineDensityAppendOriginal(ctx, base);
}

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
  ApplyDensities(base);
  if (traffic != applied_traffic || drivatars != applied_drivatars) {
    applied_traffic = traffic, applied_drivatars = drivatars;
    {
      std::lock_guard lock(density_mutex);
      REXLOG_INFO("Gripline: {} road density lists scaled", density_lists.size());
    }
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

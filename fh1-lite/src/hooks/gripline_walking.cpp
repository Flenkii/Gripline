// Gripline Lite for Forza Horizon 1: ReImagined - Walking (test stage 1).
// Copyright (c) 2026 Gripline contributors. SPDX-License-Identifier: GPL-3.0-or-later
//
// As in Gripline for Forza Horizon 2: F (keyboard) or the left stick press (pad) gets the
// driver out of the stopped car. The picture fades to black, the door opens, a moment later it
// shuts, and the picture comes back with the camera at the walker's eye next to the car. WASD or
// the left stick walk, the mouse, the arrow keys or the right stick look, Shift / RT run, Ctrl /
// B crouch, Space / A jump. F / the left stick press near the car gets back in, the same way.
// The car stays where it was parked (the game gets the handbrake and nothing else meanwhile).
//
// The camera: the camera director's step (hooks/gripline_dashcam.cpp) builds the active
// camera's view; while walking, WalkFrame below replaces the view matrix (director +240, world
// to camera, Direct3D row-vector convention, camera space x right, y up, z forward) and the
// camera position (+176) with the walker's eye. In the black the camera button is pressed until
// a follow camera is active (the car is drawn whole from outside), and back in the car until the
// camera from before is active again.
//
// Collision (test 2, 12.11.2026): the game's own collision world, as the car asks it (see "the
// world's collision" below): the walker stands on the highest ground under the feet that is not
// more than a step up, falls down to lower ground, and is pushed out of steep triangles (walls,
// trees, rocks) at knee and chest height. Where no collision is loaded the height stays. At most
// kMaxDistance from the car.
//
// The game's HUD is moved off the screen while walking and Gripline's walking HUD (as FH2's
// "step pulse" gauge) is drawn instead, both by the overlay with the fade.
//
// Offline only: the get-out request is refused in the online session types (6, 13, 18), and an
// online session, a loading screen, a menu or a teleported car end walking at once.
#include "gripline_settings.h"
#include "gripline_walk_fade.h"
#include "gripline_door_sounds.h"

#include "../platform/driving_gate.h"
#include "../platform/guest_addr.h"
#include "../platform/guest_memory.h"
#include "../platform/hud_overlay.h"
#include "../platform/telemetry.h"
#include "../platform/xr.h"

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

// virtual keys (winuser.h)
constexpr int kVkShift = 0x10, kVkControl = 0x11, kVkSpace = 0x20, kVkLeft = 0x25, kVkUp = 0x26,
              kVkRight = 0x27, kVkDown = 0x28;
constexpr uint16_t kPadStart = 0x0010, kPadLS = 0x0040, kPadA = 0x1000, kPadB = 0x2000;

// Getting out and in (as FH2, Marvin 06.11.2026): dark, door opens (the switch), door shuts,
// 2 s dark in all, light again.
constexpr double kFadeDownMs = 300, kFadeUpMs = 450, kHoldMs = 2000, kShutMs = 800;
constexpr double kCameraWaitMs = 3000;  // at most this much longer dark while the camera changes

// Walking, the FH2 values (V7.1 walking polish).
constexpr float kWalkSpeed = 2.0f, kRunSpeed = 6.0f, kCrouchSpeed = 1.1f;  // m/s
constexpr float kJump = 4.2f, kGravity = 9.81f;
constexpr float kLookRate = 1.6f;  // rad/s at full stick
constexpr float kEaseStart = 0.17f, kEaseStop = 0.11f, kEaseAir = 0.9f, kEaseLook = 0.07f;
constexpr float kEaseCrouch = 0.13f, kCrouchDrop = 0.65f, kEaseBob = 0.3f;
constexpr float kPitchMax = 1.4f;
constexpr float kEnterDistance = 4.0f;  // metres from the car to get back in
constexpr float kMaxDistance = 300.0f;  // keep near the car (the world's collision streams around it)
constexpr float kOutSide = 1.6f;        // metres left of the car's centre at getting out
constexpr float kMaxOutSpeed = 2.0f;    // m/s: the car must (almost) stand
constexpr float kTeleported = 25.0f;    // the parked car moved this far: the game moved it

enum class Phase { kOff, kOutDark, kOutHold, kOutLight, kWalking, kInDark, kInHold, kInLight };

struct Input {
  float ahead = 0, side = 0, yaw = 0, pitch = 0;
  bool run = false, crouch = false, jump = false;
};

struct Walker {
  float pos[3] = {};    // the feet (y: the ground)
  float dir[2] = {0, 1};  // heading (world x, z), unit
  float right2[2] = {1, 0};  // the heading's right (world x, z), unit
  float pitch = 0, yaw_rate = 0, pitch_rate = 0;
  float vel[2] = {};
  float jump_y = 0, fall = 0;
  float crouch = 0, bob_amount = 0, bob_phase = 0;
  float eye[3] = {}, look[3] = {0, 0, 1};
  float drop_v = 0;  // m/s down, stepping or falling down to lower ground
};

std::mutex mutex;  // everything below
Phase phase = Phase::kOff;
Clock::time_point phase_at;
bool door_shut = false;
bool request_out = false;  // pad -> director: the player asked to get out
bool walking_on = false;   // the walker's eye is the picture
Input input;
Walker w;
float car_pos[3] = {}, car_fwd[3] = {0, 0, 1};
float handed = 1.0f;  // r . (u x f) of the game's view basis
uint32_t camera_before = 0;    // the camera's vtable when getting out
bool dash_before = false;      // the Dashboard Cam was on
uint32_t camera_target = 0;    // 0 none, 1 any follow camera, else this vtable
int presses = 0;
Clock::time_point press_at;
bool pressing = false;
Clock::time_point last_toggle;
Clock::time_point last_step;
Clock::time_point far_hint_at;  // "too far from the car" was shown (a second F gets in anyway)
bool far_hint = false;
int bad_frames = 0;  // frames in a loading screen / menu while walking
bool logged_view = false;

std::atomic<float> fade_alpha{0.0f};

// The walking HUD (as Gripline's V7.1 "step pulse" gauge in FH2), drawn by the overlay below.
enum { kModeStand, kModeWalk, kModeRun, kModeCrouch };
struct Gauge {
  float speed = 0;       // m/s
  int mode = kModeStand;
  float distance = -1;   // metres to the car (horizontal)
  float ahead = 1, right = 0;  // the car's direction from the walker's heading, length 1
  float step = 0;        // 0..1, a footfall now
};
std::mutex gauge_mutex;
Gauge gauge;
std::atomic<bool> gauge_on{false};  // the walking HUD is drawn

bool Valid(uint32_t a) { return a >= 0x00100000 && a < 0xF0000000; }
uint32_t Load32(uint8_t* base, uint32_t a) {
  const auto* p = forza::GuestPtr(base, a);
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
float LoadFloat(uint8_t* base, uint32_t a) {
  const uint32_t bits = Load32(base, a);
  float v;
  std::memcpy(&v, &bits, 4);
  return v;
}

uint32_t FirstViewDirector(uint8_t* base) {  // as hooks/vr_head_pose.cpp finds it
  const uint32_t root = Load32(base, FORZA_ADDR(0x832DF024));
  if (!Valid(root)) return 0;
  const uint32_t holder = Load32(base, root + 4);
  if (!Valid(holder)) return 0;
  const uint32_t manager = Load32(base, holder + 4);
  if (!Valid(manager)) return 0;
  return Load32(base, manager + 244 + 8);
}
uint32_t CameraVtable(uint8_t* base) {
  const uint32_t director = FirstViewDirector(base);
  if (!Valid(director)) return 0;
  const uint32_t camera = Load32(base, director + 20);
  return Valid(camera) ? Load32(base, camera) : 0;
}
bool FollowCamera(uint32_t vtable) {
  return vtable == FORZA_ADDR(0x8207552C) || vtable == FORZA_ADDR(0x820755AC) ||
         vtable == FORZA_ADDR(0x8207562C);  // CCamFollow, CCamFollowLow, CCamFollowHigh
}
bool BlendCamera(uint32_t vtable) { return vtable == FORZA_ADDR(0x82075444); }

bool German() {
#ifdef _WIN32
  return (GetUserDefaultUILanguage() & 0x3FF) == 0x07;
#else
  return false;
#endif
}
void Card(const char* de, const char* en, double seconds) {
  forza::HudShow(German() ? "GRIPLINE WALKING" : "GRIPLINE WALKING", German() ? de : en, seconds);
}

bool GameHasFocus() {
#ifdef _WIN32
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
#else
  return false;
#endif
}
bool Key(int vk) {
#ifdef _WIN32
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
  (void)vk;
  return false;
#endif
}

void DoorSound(bool shut) {
#ifdef _WIN32
  using PlaySoundWFn = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
  static PlaySoundWFn play = [] {
    HMODULE winmm = LoadLibraryW(L"winmm.dll");
    return winmm ? reinterpret_cast<PlaySoundWFn>(GetProcAddress(winmm, "PlaySoundW")) : nullptr;
  }();
  if (!play) return;
  constexpr DWORD kAsync = 0x0001, kNoDefault = 0x0002, kMemory = 0x0004;
  play(reinterpret_cast<LPCWSTR>(shut ? door_close_wav : door_open_wav), nullptr, kAsync | kNoDefault | kMemory);
#else
  (void)shut;
#endif
}

float Ease(float dt, float tau) {  // share of the way to a target after dt
  if (!(dt > 0.0f)) return 0.0f;
  return 1.0f - std::exp(-dt / tau);
}

double Ms(Clock::time_point since, Clock::time_point now) {
  return std::chrono::duration<double, std::milli>(now - since).count();
}

void SetPhase(Phase p, Clock::time_point now) {
  phase = p;
  phase_at = now;
}

// The heading's right in the world: the game's camera basis (r = handed * (u x f)) for u = up.
void Right2(const float dir[2], float out[2]) {
  // u x f with u = (0,1,0), f = (dx, 0, dz): (dz, 0, -dx)
  out[0] = handed * dir[1];
  out[1] = handed * -dir[0];
}

// The walker steps out at the car's left (the driver's door in most cars).
void Spawn() {
  const float l = std::sqrt(car_fwd[0] * car_fwd[0] + car_fwd[2] * car_fwd[2]);
  w = Walker{};
  if (l > 0.1f) w.dir[0] = car_fwd[0] / l, w.dir[1] = car_fwd[2] / l;
  Right2(w.dir, w.right2);
  w.pos[0] = car_pos[0] - w.right2[0] * kOutSide;
  w.pos[1] = car_pos[1];
  w.pos[2] = car_pos[2] - w.right2[1] * kOutSide;
  last_step = Clock::now();
}

void Turn(float a) {
  const float c = std::cos(a), s = std::sin(a);
  const float x = w.dir[0] * c + w.right2[0] * s, z = w.dir[1] * c + w.right2[1] * s;
  const float l = std::sqrt(x * x + z * z);
  if (l > 0.5f) w.dir[0] = x / l, w.dir[1] = z / l;
  Right2(w.dir, w.right2);
}

// ---------------------------------------------------------------- the world's collision
// The game's collision world, as the car's own contact query finds it (TU4 sub_82DFB420): the
// car object (telemetry's LocalCar, its position at -0xA20) holds at -0x960 an object whose
// vtable entry 41 (+164) returns the collision world. The world's triangle query
// (sub_82DD7310: r3 the world, r4 a box = min vec4 + max vec4, r5 the most triangles,
// r6 the list, r7 the flag mask; returns the count) fills the list with triangle records:
// +0 u8 flags (the mask's bits), +2 u16, +4 / +8 / +12 pointers to the corners (3 floats).
// The car asks with mask 8 and limit 10000 (a 40000 byte list from the game's allocator).
// Only the cell around the box's centre is asked, and only loaded cells answer.
struct Tri {
  float a[3], b[3], c[3], n[3];  // corners, the unit normal (n.y >= 0)
};
std::vector<Tri> tris;   // around the walker, this frame
bool world_ok = false;   // the query answered with triangles this frame
constexpr float kRadius = 0.3f;      // the walker's body
constexpr float kStepUp = 0.55f;     // the highest step walked onto
constexpr float kWallNy = 0.6f;      // flatter than this (normal.y) is ground, else a wall
constexpr uint32_t kQueryLimit = 10000, kQueryMask = 8;
uint32_t guest_buffer = 0;           // the box and the list, in guest memory
int empty_frames = 0;

void StoreFloat(uint8_t* base, uint32_t a, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  uint8_t* p = forza::GuestPtr(base, a);
  p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
}

bool Code(uint32_t a) { return a >= 0x82000000 && a < 0x83400000; }  // the executable's code (TU4 ends near 0x832BA000)

uint32_t LocalCarObject(uint8_t* base) {  // as platform/telemetry.cpp LocalCar finds it
  const uint32_t root = Load32(base, FORZA_ADDR(0x832DF024));
  if (!Valid(root)) return 0;
  const uint32_t holder = Load32(base, root + 4);
  if (!Valid(holder)) return 0;
  const uint32_t list = Load32(base, holder + 4);
  if (!Valid(list)) return 0;
  const uint32_t first = Load32(base, list + 0x80), end = Load32(base, list + 0x84);
  if (!Valid(first) || first > end || end - first > 256 || (end - first) % 4) return 0;
  for (uint32_t slot = first; slot < end; slot += 4) {
    const uint32_t vehicle = Load32(base, slot);
    if (!Valid(vehicle) || vehicle > 0xEFFF0000 || Load32(base, vehicle + 0x2DD0) != 0) continue;
    const uint32_t car = Load32(base, vehicle + 0x2D90);
    if (car >= 0x100A50 && Valid(car)) return car;
  }
  return 0;
}

// The triangles in the box lo..hi into tris. False: no world, no loaded cell or nothing there.
bool QueryWorld(PPCContext& ctx, uint8_t* base, const float lo[3], const float hi[3]) {
  tris.clear();
#if !FORZA_TU4
  (void)ctx, (void)base, (void)lo, (void)hi;
  return false;  // the addresses below are Title Update 4's
#else
  auto call = [&](uint32_t function, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0,
                  uint32_t r7 = 0) {
    PPCContext c = ctx;
    c.r1.u64 = ctx.r1.u32 - 0x400;  // below the hooked function's frame
    c.r3.u64 = r3, c.r4.u64 = r4, c.r5.u64 = r5, c.r6.u64 = r6, c.r7.u64 = r7;
    rex::runtime::ResolveIndirectFunction(function)(c, base);
    return c.r3.u32;
  };
  if (!guest_buffer) {
    guest_buffer = call(0x824265C8, kQueryLimit * 4 + 64);  // the game's allocator
    if (!Valid(guest_buffer)) {
      guest_buffer = 0;
      return false;
    }
    REXLOG_INFO("Gripline: walking collision buffer {:08X}", guest_buffer);
  }
  // diagnostics: every step logged for the first queries, then every 30 s
  static int diag_count = 0;
  static Clock::time_point diag_at;
  const auto now = Clock::now();
  const bool diag = diag_count < 4 || Ms(diag_at, now) > 30000;
  auto fail = [&](const char* why, uint32_t a, uint32_t b) {
    if (diag) {
      ++diag_count, diag_at = now;
      REXLOG_INFO("Gripline: walking collision: {} ({:08X} {:08X})", why, a, b);
    }
    return false;
  };
  const uint32_t car = LocalCarObject(base);
  if (!car) return fail("no car object", 0, 0);
  const uint32_t holder = Load32(base, car - 0x960);
  if (!Valid(holder) || !Valid(Load32(base, holder))) return fail("no holder at car-0x960", car, holder);
  const uint32_t get_world = Load32(base, Load32(base, holder) + 164);
  if (!Code(get_world)) return fail("holder vtable+164 not code", holder, get_world);
  const uint32_t world = call(get_world, holder);
  if (!Valid(world)) return fail("no world", get_world, world);
  const uint32_t world_vt = Load32(base, world);
  const uint32_t query = Valid(world_vt) ? Load32(base, world_vt + 4) : 0;
  const uint32_t box = (guest_buffer + 15) & ~15u, list = box + 32;
  for (int i = 0; i < 3; ++i) StoreFloat(base, box + 4 * i, lo[i]), StoreFloat(base, box + 16 + 4 * i, hi[i]);
  StoreFloat(base, box + 12, 1.0f), StoreFloat(base, box + 28, 1.0f);
  // as the car asks (its world's vtable entry 1, sub_82DD59F0), then the cell query directly,
  // then with every flag
  uint32_t count = 0, how = 0;
  const uint32_t tries[3][2] = {{query, kQueryMask}, {0x82DD7310, kQueryMask}, {query, 0xFF}};
  uint32_t got[3] = {};
  for (int i = 0; i < 3 && !count; ++i) {
    if (!Code(tries[i][0])) continue;
    got[i] = call(tries[i][0], world, box, kQueryLimit, list, tries[i][1]);
    count = std::min(got[i], kQueryLimit), how = i;
  }
  if (diag) {
    ++diag_count, diag_at = now;
    const uint32_t cached = Load32(base, world + 100);
    REXLOG_INFO("Gripline: walking collision: car {:08X} holder {:08X} world {:08X} vt {:08X} query {:08X} "
                "cell {:08X}; counts {} {} {} (used {})", car, holder, world, world_vt, query, cached,
                got[0], got[1], got[2], how);
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t rec = Load32(base, list + 4 * i);
    if (!Valid(rec)) continue;
    Tri t;
    float* corner[3] = {t.a, t.b, t.c};
    bool ok = true;
    for (int k = 0; k < 3 && ok; ++k) {
      const uint32_t v = Load32(base, rec + 4 + 4 * k);
      if (!Valid(v)) ok = false;
      else
        for (int j = 0; j < 3; ++j) corner[k][j] = LoadFloat(base, v + 4 * j);
    }
    if (!ok) continue;
    const float e1[3] = {t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
    const float e2[3] = {t.c[0] - t.a[0], t.c[1] - t.a[1], t.c[2] - t.a[2]};
    float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(l > 1e-6f) || !std::isfinite(l)) continue;
    const float sgn = n[1] < 0 ? -1.0f : 1.0f;
    for (int j = 0; j < 3; ++j) t.n[j] = sgn * n[j] / l;
    tris.push_back(t);
  }
  return !tris.empty();
#endif
}

// The height of a ground triangle at x, z (false: x, z not over it).
bool HeightAt(const Tri& t, float x, float z, float& h) {
  const float d = (t.b[2] - t.c[2]) * (t.a[0] - t.c[0]) + (t.c[0] - t.b[0]) * (t.a[2] - t.c[2]);
  if (std::fabs(d) < 1e-8f) return false;
  const float l1 = ((t.b[2] - t.c[2]) * (x - t.c[0]) + (t.c[0] - t.b[0]) * (z - t.c[2])) / d;
  const float l2 = ((t.c[2] - t.a[2]) * (x - t.c[0]) + (t.a[0] - t.c[0]) * (z - t.c[2])) / d;
  const float l3 = 1.0f - l1 - l2;
  if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) return false;
  h = l1 * t.a[1] + l2 * t.b[1] + l3 * t.c[1];
  return true;
}

// The highest ground at x, z no higher than top (false: none).
bool GroundAt(float x, float z, float top, float& ground) {
  bool found = false;
  for (const Tri& t : tris) {
    float h;
    if (t.n[1] >= kWallNy && HeightAt(t, x, z, h) && h <= top && (!found || h > ground)) ground = h, found = true;
  }
  return found;
}

// Pushes the body (a circle at x, z) out of the walls it overlaps at height y.
void PushOut(float& x, float& z, float y) {
  for (const Tri& t : tris) {
    if (t.n[1] >= kWallNy) continue;
    // the wall's cut at height y: a segment
    const float* v[3] = {t.a, t.b, t.c};
    float px[2], pz[2];
    int n = 0;
    for (int i = 0; i < 3 && n < 2; ++i) {
      const float* p = v[i];
      const float* q = v[(i + 1) % 3];
      const float dp = p[1] - y, dq = q[1] - y;
      if ((dp < 0) == (dq < 0) || dp == dq) continue;
      const float k = dp / (dp - dq);
      px[n] = p[0] + (q[0] - p[0]) * k, pz[n] = p[2] + (q[2] - p[2]) * k, ++n;
    }
    if (n < 2) continue;
    const float sx = px[1] - px[0], sz = pz[1] - pz[0], sl = sx * sx + sz * sz;
    float k = sl > 1e-8f ? ((x - px[0]) * sx + (z - pz[0]) * sz) / sl : 0.0f;
    k = std::clamp(k, 0.0f, 1.0f);
    const float cx = px[0] + sx * k, cz = pz[0] + sz * k;
    float dx = x - cx, dz = z - cz;
    float d = std::sqrt(dx * dx + dz * dz);
    if (d >= kRadius) continue;
    if (d < 1e-4f) {  // on the wall: out along its horizontal normal
      const float hl = std::sqrt(t.n[0] * t.n[0] + t.n[2] * t.n[2]);
      if (hl < 1e-4f) continue;
      dx = t.n[0] / hl, dz = t.n[2] / hl, d = 0.0f;
    } else {
      dx /= d, dz /= d;
    }
    x += dx * (kRadius - d), z += dz * (kRadius - d);
  }
}

// One step of the walker (director step, main loop). dt 0: paused, the eye is only placed.
void Step(float dt, const Input& in) {
  auto& g = forza::gripline::Get();
  // looking: the rate eases to the stick's
  const float kl = Ease(dt, kEaseLook);
  w.yaw_rate += (in.yaw * kLookRate - w.yaw_rate) * kl;
  w.pitch_rate += (in.pitch * kLookRate - w.pitch_rate) * kl;
  if (dt > 0) Turn(w.yaw_rate * dt);
  w.pitch = std::clamp(w.pitch + w.pitch_rate * dt, -kPitchMax, kPitchMax);
  // crouching
  w.crouch += ((in.crouch ? 1.0f : 0.0f) - w.crouch) * Ease(dt, kEaseCrouch);
  const bool crouching = in.crouch || w.crouch > 0.5f;
  const bool grounded = w.jump_y <= 0.0f && w.fall <= 0.0f;
  // moving: the velocity eases to the wanted one
  const float speed = crouching ? kCrouchSpeed : in.run ? kRunSpeed : kWalkSpeed;
  float ia = in.ahead, is = in.side;
  const float il = std::sqrt(ia * ia + is * is);
  if (il > 1.0f) ia /= il, is /= il;
  const float want[2] = {(w.dir[0] * ia + w.right2[0] * is) * speed, (w.dir[1] * ia + w.right2[1] * is) * speed};
  const float v2 = w.vel[0] * w.vel[0] + w.vel[1] * w.vel[1], w2 = want[0] * want[0] + want[1] * want[1];
  const float kv = Ease(dt, !grounded ? kEaseAir : w2 >= v2 ? kEaseStart : kEaseStop);
  w.vel[0] += (want[0] - w.vel[0]) * kv, w.vel[1] += (want[1] - w.vel[1]) * kv;
  if (w2 == 0.0f && w.vel[0] * w.vel[0] + w.vel[1] * w.vel[1] < 0.0004f) w.vel[0] = w.vel[1] = 0.0f;
  float nx = w.pos[0] + w.vel[0] * dt, nz = w.pos[2] + w.vel[1] * dt;
  if (world_ok && dt > 0.0f) {
    // walls: the body at knee and chest height, twice for corners
    const float body = w.pos[1] + w.jump_y;
    for (int pass = 0; pass < 2; ++pass) {
      PushOut(nx, nz, body + kStepUp + 0.1f);
      PushOut(nx, nz, body + 1.3f);
    }
    // the ground: the highest under the feet that can be stepped onto
    float ground;
    if (GroundAt(nx, nz, w.pos[1] + w.jump_y + kStepUp, ground)) {
      if (ground >= w.pos[1]) {
        // up a step: the jump's height above the old ground is kept above the new one
        w.jump_y = std::max(0.0f, w.jump_y - (ground - w.pos[1]));
        w.pos[1] = ground, w.drop_v = 0.0f;
      } else if (w.drop_v == 0.0f && w.pos[1] - ground <= 0.3f) {
        w.pos[1] = ground;  // down a slope or a small step: follows it
      } else {  // further down: falls (gravity)
        w.drop_v += kGravity * dt;
        w.pos[1] = std::max(ground, w.pos[1] - w.drop_v * dt);
        if (w.pos[1] <= ground) w.drop_v = 0.0f;
      }
    } else {
      nx = w.pos[0], nz = w.pos[2];  // no ground there (an edge, or not loaded): stay
      w.vel[0] = w.vel[1] = 0.0f;
    }
  }  // no collision here (not loaded): the height stays
  const float dx = nx - car_pos[0], dz = nz - car_pos[2];
  if (dx * dx + dz * dz <= kMaxDistance * kMaxDistance) w.pos[0] = nx, w.pos[2] = nz;
  else w.vel[0] = w.vel[1] = 0.0f;
  // jumping
  if (in.jump && grounded && !crouching) w.fall = kJump;
  if (w.jump_y > 0.0f || w.fall > 0.0f) {
    w.fall -= kGravity * dt;
    w.jump_y += w.fall * dt;
    if (w.jump_y <= 0.0f) w.jump_y = 0.0f, w.fall = 0.0f;
  }
  // head bob: one stride = two steps, the eye dips at each step and sways to its side
  const float hs = std::sqrt(w.vel[0] * w.vel[0] + w.vel[1] * w.vel[1]);
  const float bob_want = grounded ? std::clamp(hs / kWalkSpeed, 0.0f, 1.0f) : 0.0f;
  w.bob_amount += (bob_want - w.bob_amount) * Ease(dt, kEaseBob);
  float bob = 0, sway = 0, look_sway = 0;
  if (w.bob_amount > 0.002f) {
    w.bob_phase += dt * 6.2831853f * (0.55f + 0.16f * hs);
    if (w.bob_phase > 6.2831853f) w.bob_phase -= 6.2831853f;
    const float run_share = std::clamp((hs - kWalkSpeed) / (kRunSpeed - kWalkSpeed), 0.0f, 1.0f);
    bob = -(0.026f + 0.02f * run_share) * (1.0f - 0.5f * w.crouch) * std::cos(2 * w.bob_phase) * w.bob_amount;
    sway = (0.022f + 0.012f * run_share) * std::sin(w.bob_phase) * w.bob_amount;
    look_sway = 0.0035f * (1.0f + run_share) * std::sin(w.bob_phase) * w.bob_amount;
  }
  const float eye_up = g.walk_eye.load() - kCrouchDrop * w.crouch;
  w.eye[0] = w.pos[0] + w.right2[0] * sway;
  w.eye[1] = w.pos[1] + eye_up + w.jump_y + bob;
  w.eye[2] = w.pos[2] + w.right2[1] * sway;
  const float cs = std::cos(look_sway), ss = std::sin(look_sway);
  const float lx = w.dir[0] * cs + w.right2[0] * ss, lz = w.dir[1] * cs + w.right2[1] * ss;
  const float pc = std::cos(w.pitch), ps = std::sin(w.pitch);
  w.look[0] = pc * lx, w.look[1] = ps, w.look[2] = pc * lz;

  // the walking HUD's data
  Gauge gd;
  gd.speed = hs;
  gd.mode = crouching ? kModeCrouch : hs < 0.25f ? kModeStand : in.run ? kModeRun : kModeWalk;
  const float cx = car_pos[0] - w.pos[0], cz = car_pos[2] - w.pos[2];
  gd.distance = std::sqrt(cx * cx + cz * cz);
  const float ahead = cx * w.dir[0] + cz * w.dir[1], side = cx * w.right2[0] + cz * w.right2[1];
  const float al = std::sqrt(ahead * ahead + side * side);
  gd.ahead = al > 0.01f ? ahead / al : 1.0f, gd.right = al > 0.01f ? side / al : 0.0f;
  if (w.bob_amount > 0.002f) {  // the eye is lowest (a footfall) where cos(2 * phase) = 1
    const float c = std::max(0.0f, std::cos(2.0f * w.bob_phase)), c4 = c * c * c * c;
    const float run_share = std::clamp((hs - kWalkSpeed) / (kRunSpeed - kWalkSpeed), 0.0f, 1.0f);
    gd.step = c4 * c4 * w.bob_amount * (0.55f + 0.45f * run_share) * (1.0f - 0.6f * w.crouch);
  }
  std::lock_guard glock(gauge_mutex);
  gauge = gd;
}

// The view matrix for the eye looking along look (row-vector, world to camera).
void BuildView(float view[16]) {
  const float* f = w.look;
  float u[3] = {-f[1] * f[0], 1.0f - f[1] * f[1], -f[1] * f[2]};  // up minus its part along f
  const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
  for (float& c : u) c /= ul;
  // r = handed * (u x f)
  float r[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
  for (float& c : r) c *= handed;
  const float* e = w.eye;
  for (int i = 0; i < 3; ++i) {
    view[i * 4 + 0] = r[i];
    view[i * 4 + 1] = u[i];
    view[i * 4 + 2] = f[i];
    view[i * 4 + 3] = 0.0f;
  }
  view[12] = -(r[0] * e[0] + r[1] * e[1] + r[2] * e[2]);
  view[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
  view[14] = -(f[0] * e[0] + f[1] * e[1] + f[2] * e[2]);
  view[15] = 1.0f;
}

// The game's online session type (as hooks/online_photo.cpp asks it), 0 without a session.
uint32_t SessionType(const PPCContext& ctx, uint8_t* base) {
#if !FORZA_TU4
  (void)ctx, (void)base;
  return 0;  // the addresses below are Title Update 4's
#else
  auto call = [&](uint32_t function, uint32_t r3) {
    PPCContext c = ctx;
    c.r3.u64 = r3;
    rex::runtime::ResolveIndirectFunction(function)(c, base);
    return c.r3.u32;
  };
  const uint32_t services = Load32(base, 0x833CAAE4);
  if (!Valid(services)) return 0;
  const uint32_t session = call(0x82F12410, services);
  if (!Valid(session)) return 0;
  const uint32_t holder = call(0x82BB4240, session);
  if (!Valid(holder) || !Valid(Load32(base, holder))) return 0;
  return call(0x824B75E8, Load32(base, holder));
#endif
}
bool OnlineType(uint32_t t) { return t == 6 || t == 13 || t == 18; }

// Ends walking at once (no fade): online, a loading screen, the car moved by the game.
void Abort(const char* why) {
  REXLOG_INFO("Gripline: walking ended at once ({})", why);
  walking_on = false;
  phase = Phase::kOff;
  camera_target = 0;
  pressing = false;
  fade_alpha.store(0.0f);
  gauge_on.store(false);
  forza::gripline::FadeUpdate();
  forza::gripline::HudHide(false);
}

// ---------------------------------------------------------------- the overlay: the walking HUD and the fade
// The walking HUD, bottom right, as FH2's "step pulse" look: getting-around mode, the speed, a line
// that ticks at every footfall, and how far the car is with an arrow to it (near the car: get in).
constexpr ImU32 Rgba(int r, int g, int b, float a) {
  return IM_COL32(r, g, b, int(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
}
constexpr int kLight[3] = {110, 185, 255}, kAccent[3] = {38, 120, 225};
constexpr int kPulseSamples = 140;

class Overlay final : public rex::ui::ImGuiDialog {
 public:
  Overlay(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& context)
      : ImGuiDialog(drawer), drawer_(drawer), context_(context) {
    drawer_->RemoveDialog(this);  // registered only while there is something to draw
  }
  void Update() {
    const bool visible = fade_alpha.load(std::memory_order_relaxed) > 0.0f || gauge_on.load();
    if (visible_.exchange(visible) == visible) return;
    context_.CallInUIThread([this] {
      if (visible_.load()) drawer_->AddDialog(this);
      else drawer_->RemoveDialog(this);
    });
  }
  rex::ui::WindowedAppContext& context() { return context_; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (gauge_on.load()) DrawGauge(io);
    const float a = std::clamp(fade_alpha.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (a <= 0.0f) return;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                                                  IM_COL32(0, 0, 0, int(a * 255.0f + 0.5f)));
  }

 private:
  // the larger font the overlay loaded (Arial Bold, platform/hud_overlay.cpp), else ImGui's own
  static ImFont* Font() {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    return atlas && atlas->Fonts.Size > 1 ? atlas->Fonts[atlas->Fonts.Size - 1] : ImGui::GetFont();
  }
  static float Width(ImFont* f, float size, const char* t) { return f->CalcTextSizeA(size, FLT_MAX, 0, t).x; }
  // text with its right end at x and its top at y, with a soft shadow
  static void Text(ImDrawList* dl, ImFont* f, float size, float x, float y, ImU32 col, const char* t, float sh) {
    const float tw = Width(f, size, t);
    if (sh > 0) dl->AddText(f, size, ImVec2(x - tw + sh * 0.6f, y + sh), IM_COL32(0, 0, 0, 140), t);
    dl->AddText(f, size, ImVec2(x - tw, y), col, t);
  }

  void Feed(float step) {  // one pulse sample every 16 ms, the newest at the right
    const auto now = Clock::now();
    if (pulse_at_ == Clock::time_point{} || Ms(pulse_at_, now) > 2000 || now < pulse_at_) pulse_at_ = now;
    while (Ms(pulse_at_, now) >= 16) {
      std::memmove(pulse_, pulse_ + 1, sizeof(float) * (kPulseSamples - 1));
      pulse_[kPulseSamples - 1] = step;
      pulse_at_ += std::chrono::milliseconds(16);
    }
  }

  void DrawGauge(ImGuiIO& io) {
    Gauge d;
    {
      std::lock_guard lock(gauge_mutex);
      d = gauge;
    }
    Feed(d.mode == kModeStand ? 0.0f : d.step);
    const bool de = German();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* f = Font();
    const float s = std::clamp(io.DisplaySize.y / 1080.0f, 0.6f, 2.5f), sh = 3.0f * s;
    const float right = io.DisplaySize.x - 40.0f * s, base = io.DisplaySize.y - 30.0f * s - 100.0f * s;
    const ImU32 light = Rgba(kLight[0], kLight[1], kLight[2], 1.0f), ink = IM_COL32(255, 255, 255, 255),
                dim = IM_COL32(255, 255, 255, 115);

    // the speed: a big number, km/h
    char number[16];
    std::snprintf(number, sizeof(number), "%d", int(d.speed * 3.6f + 0.5f));
    const float unit_size = 20.0f * s, big = 96.0f * s;
    Text(dl, f, unit_size, right, base - unit_size - 6.0f * s, light, "km/h", sh);
    const float uw = Width(f, unit_size, "km/h");
    Text(dl, f, big, right - uw - 12.0f * s, base - big * 0.92f, ink, number, sh * 1.3f);

    // the three ways of getting around, the one now lit
    static const char* const german[3] = {"RENNEN", "GEHEN", "DUCKEN"};
    static const char* const english[3] = {"RUN", "WALK", "CROUCH"};
    static const int order[3] = {kModeRun, kModeWalk, kModeCrouch};
    const float mode_size = 15.0f * s, my = base - big * 0.92f - 26.0f * s;
    float mx = right;
    for (int i = 0; i < 3; ++i) {
      const char* name = de ? german[i] : english[i];
      const float tw = Width(f, mode_size, name);
      const bool on = d.mode == order[i];
      Text(dl, f, mode_size, mx, my, on ? ink : dim, name, sh);
      if (on) dl->AddLine(ImVec2(mx - tw + 1.5f * s, my + mode_size + 3.0f * s), ImVec2(mx - 1.5f * s, my + mode_size + 3.0f * s), light, 3.0f * s);
      mx -= tw + 18.0f * s;
    }

    // the pulse line
    const float pw = 300.0f * s, px = right - pw, py = base + 44.0f * s, ph = 30.0f * s;
    for (int pass = 0; pass < 2; ++pass)
      for (int i = 1; i < kPulseSamples; ++i) {
        const float x0 = px + pw * float(i - 1) / (kPulseSamples - 1), x1 = px + pw * float(i) / (kPulseSamples - 1);
        const float y0 = py - pulse_[i - 1] * ph, y1 = py - pulse_[i] * ph;
        const float t = float(i) / (kPulseSamples - 1);
        const float a = t < 0.35f ? t / 0.35f * 0.7f : 0.7f + (t - 0.35f) / 0.65f * 0.3f;
        if (pass == 0) dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), Rgba(kAccent[0], kAccent[1], kAccent[2], 0.3f * a), 7.0f * s);
        else dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), Rgba(kLight[0] + int((255 - kLight[0]) * t), kLight[1] + int((255 - kLight[1]) * t), 255, a), 2.4f * s);
      }

    // the car: get in when near, else an arrow and the distance
    const float info = 17.0f * s, iy = py + 14.0f * s;
    if (d.distance >= 0.0f && d.distance <= kEnterDistance) {
      Text(dl, f, info, right, iy, light, de ? "F / LS  EINSTEIGEN" : "F / LS  GET IN", sh);
    } else if (d.distance >= 0.0f) {
      char t[48];
      std::snprintf(t, sizeof(t), "%s  %d m", de ? "AUTO" : "CAR", int(d.distance + 0.5f));
      Text(dl, f, info, right, iy, IM_COL32(255, 255, 255, 235), t, sh);
      // the arrow: up on the screen = ahead, right = right
      const float tw = Width(f, info, t), cx = right - tw - 16.0f * s, cy = iy + info * 0.5f, r = 8.5f * s;
      const float ax = d.right, ay = -d.ahead;  // screen direction to the car
      const ImVec2 tip(cx + ax * r, cy + ay * r), l(cx - ax * r * 0.7f - ay * r * 0.75f, cy - ay * r * 0.7f + ax * r * 0.75f),
          rr(cx - ax * r * 0.7f + ay * r * 0.75f, cy - ay * r * 0.7f - ax * r * 0.75f), back(cx - ax * r * 0.3f, cy - ay * r * 0.3f);
      dl->AddTriangleFilled(ImVec2(tip.x, tip.y + 1.5f * s), ImVec2(l.x, l.y + 1.5f * s), ImVec2(back.x, back.y + 1.5f * s), IM_COL32(0, 0, 0, 140));
      dl->AddTriangleFilled(ImVec2(tip.x, tip.y + 1.5f * s), ImVec2(back.x, back.y + 1.5f * s), ImVec2(rr.x, rr.y + 1.5f * s), IM_COL32(0, 0, 0, 140));
      dl->AddTriangleFilled(tip, l, back, light);
      dl->AddTriangleFilled(tip, back, rr, light);
    }
  }

  rex::ui::ImGuiDrawer* drawer_;
  rex::ui::WindowedAppContext& context_;
  std::atomic<bool> visible_{false};
  float pulse_[kPulseSamples] = {};
  Clock::time_point pulse_at_{};
};
Overlay* overlay = nullptr;

// The game's HUD off the screen while walking: the SDK's HUD placement (patch 0063, the same cvar
// the HUD layout setting moves the HUD with) gets a vertical offset far past the screen's edge.
// Only the game's HUD/UI draws take it (the 3D scene and this ImGui overlay do not). Pause menu,
// map and loading screens draw through it too, so it is only set while walking and driving.
constexpr const char* kHudCvar = "forza_ultrawide_hud_offset_y";
constexpr double kHudAway = 9.0;  // normalized device units: the screen is -1..1
std::atomic<bool> hud_want{false};
bool hud_applied = false;  // UI thread
std::string hud_saved;     // UI thread: the offset before walking

void ApplyHud() {  // UI thread
  const std::string now = rex::cvar::GetFlagByName(kHudCvar);
  if (now.empty()) {
    static bool told = false;
    if (!told) {
      told = true;
      REXLOG_WARN("Gripline: walking - no {} in this build, the game's HUD stays", kHudCvar);
    }
    return;
  }
  const bool away = std::strtod(now.c_str(), nullptr) > kHudAway * 0.5;
  if (hud_want.load()) {
    if (!hud_applied) hud_saved = away ? "0.0" : now;
    hud_applied = true;
    if (!away) rex::cvar::SetFlagByName(kHudCvar, std::to_string(kHudAway));
  } else if (hud_applied) {
    hud_applied = false;
    if (away) rex::cvar::SetFlagByName(kHudCvar, hud_saved);
  }
}
}  // namespace

void forza::gripline::HudHide(bool hide) {
  const bool was = hud_want.exchange(hide);
  if (!overlay) return;
  static Clock::time_point last;
  const auto now = Clock::now();
  // again once a second while hidden: the HUD layout setting may have set its own offset
  if (was == hide && !(hide && Ms(last, now) > 1000)) return;
  last = now;
  overlay->context().CallInUIThread([] { ApplyHud(); });
}

void forza::gripline::InstallFade(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& context) {
  if (!drawer || overlay) return;
  overlay = new Overlay(drawer, context);
  REXLOG_INFO("Gripline: walking overlay ready (fade, walking HUD)");
}
void forza::gripline::FadeUpdate() {
  if (overlay) overlay->Update();
}

bool forza::gripline::WalkingBusy() {
  std::lock_guard lock(mutex);
  return phase != Phase::kOff || request_out;
}

// Player 1's pad, every XamInputGetState (kernel/input_autopress.cpp), after the Dashboard Cam.
void forza::gripline::WalkPad(uint8_t* base, uint16_t& buttons, uint8_t triggers[2], int16_t sticks[4]) {
  static uint16_t previous = 0;
  static bool f_was = false;
  const uint16_t pad = buttons;
  const bool focus = GameHasFocus();
  const bool f_down = focus && Key('F');
  const bool toggle_edge = ((pad & kPadLS) && !(previous & kPadLS)) || (f_down && !f_was);
  previous = pad, f_was = f_down;
  if (MenuOpen()) return;
  const auto now = Clock::now();
  std::lock_guard lock(mutex);
  auto& g = Get();

  // getting out / in
  if (toggle_edge && Ms(last_toggle, now) > 400) {
    last_toggle = now;
    if (phase == Phase::kOff && !request_out) {
      if (!g.walking) {
        // walking off in the menu: F / LS stay the game's
      } else {
        request_out = true;  // the director's step checks and starts it
      }
    } else if (phase == Phase::kWalking) {
      const float dx = w.pos[0] - car_pos[0], dz = w.pos[2] - car_pos[2];
      const float d = std::sqrt(dx * dx + dz * dz);
      if (d <= kEnterDistance || (far_hint && Ms(far_hint_at, now) < 3000)) {
        far_hint = false;
        SetPhase(Phase::kInDark, now);
      } else {
        far_hint = true, far_hint_at = now;
        char de[160], en[160];
        std::snprintf(de, sizeof(de), "Noch %.0f m bis zum Auto.\nNochmal F / linken Stick drücken = sofort einsteigen.", d);
        std::snprintf(en, sizeof(en), "%.0f m to your car.\nPress F / left stick again to get in right away.", d);
        Card(de, en, 3.0);
      }
    }
  }

  // the fade and the door
  const double t = Ms(phase_at, now);
  float alpha = 0.0f;
  switch (phase) {
    case Phase::kOff:
    case Phase::kWalking: alpha = 0.0f; break;
    case Phase::kOutDark:
    case Phase::kInDark: {
      alpha = float(std::min(1.0, t / kFadeDownMs));
      if (t >= kFadeDownMs) {
        const bool out = phase == Phase::kOutDark;
        DoorSound(false);
        door_shut = false;
        if (out) {
          camera_before = CameraVtable(base);
          dash_before = DashCamDrawn();
          Spawn();
          walking_on = true;
          camera_target = FollowCamera(camera_before) ? 0 : 1;
          REXLOG_INFO("Gripline: walking on (camera {:08X}, eye {:.2f} m)", camera_before, g.walk_eye.load());
        } else {
          walking_on = false;
          camera_target = camera_before && CameraVtable(base) != camera_before ? camera_before : 0;
          REXLOG_INFO("Gripline: walking off, back in the car");
        }
        presses = 0, pressing = false, press_at = now;
        SetPhase(out ? Phase::kOutHold : Phase::kInHold, now);
        alpha = 1.0f;
      }
      break;
    }
    case Phase::kOutHold:
    case Phase::kInHold: {
      alpha = 1.0f;
      if (!door_shut && t >= kShutMs) door_shut = true, DoorSound(true);
      if (t >= kHoldMs && (camera_target == 0 || t >= kHoldMs + kCameraWaitMs)) {
        if (camera_target) REXLOG_WARN("Gripline: walking - the camera did not change in time");
        camera_target = 0;
        const bool out = phase == Phase::kOutHold;
        if (!out && dash_before) DashCamRestore(true);
        SetPhase(out ? Phase::kOutLight : Phase::kInLight, now);
        if (out)
          Card("Ausgestiegen - Walking an\nWASD / linker Stick laufen, Maus / Pfeile / rechter Stick schauen,\n"
               "Shift / RT rennen, Strg / B ducken, Leertaste / A springen,\nF / linken Stick drücken = einsteigen (am Auto)\n"
               "Testversion 2: Kollision neu (Boden, Wände)",
               "Out of the car - walking on\nWASD / left stick walk, mouse / arrows / right stick look,\n"
               "Shift / RT run, Ctrl / B crouch, Space / A jump,\nF / left stick press = get in (at the car)\n"
               "Test build 2: new collision (ground, walls)", 9.0);
      }
      break;
    }
    case Phase::kOutLight:
    case Phase::kInLight:
      alpha = float(std::max(0.0, 1.0 - t / kFadeUpMs));
      if (t >= kFadeUpMs) SetPhase(phase == Phase::kOutLight ? Phase::kWalking : Phase::kOff, now), alpha = 0.0f;
      break;
  }
  fade_alpha.store(alpha, std::memory_order_relaxed);
  // the walking HUD and the game's HUD: only while walking in the running game (pause menu,
  // map and loading screens get the game's HUD back)
  const bool driving = forza::telemetry::Player().State() == forza::telemetry::Drive::kDriving;
  gauge_on.store(walking_on && driving && (phase == Phase::kWalking || phase == Phase::kOutLight));
  FadeUpdate();
  HudHide(walking_on && driving);

  if (phase == Phase::kOff) return;  // the game's pad as it is

  // the walker's input: keys (game window in focus) and sticks, whichever is stronger
  auto stick = [](int16_t v) {
    const float f = float(v) / 32767.0f;
    return std::fabs(f) < 0.18f ? 0.0f : std::clamp((f - std::copysign(0.18f, f)) / 0.82f, -1.0f, 1.0f);
  };
  auto pick = [](float key, float pad) { return std::fabs(key) > std::fabs(pad) ? key : pad; };
  Input in;
  const float kw = focus ? float(Key('W')) - float(Key('S')) : 0.0f;
  const float kd = focus ? float(Key('D')) - float(Key('A')) : 0.0f;
  const float kr = focus ? float(Key(kVkRight)) - float(Key(kVkLeft)) : 0.0f;
  const float ku = focus ? float(Key(kVkUp)) - float(Key(kVkDown)) : 0.0f;
  in.ahead = pick(kw, stick(sticks[1]));
  in.side = pick(kd, stick(sticks[0]));
  in.yaw = pick(kr, stick(sticks[2]));
  in.pitch = pick(ku, stick(sticks[3]));
  in.run = (focus && Key(kVkShift)) || triggers[1] > 64;
  in.crouch = (focus && Key(kVkControl)) || (pad & kPadB);
  static bool jump_was = false;
  const bool jump_down = (focus && Key(kVkSpace)) || (pad & kPadA);
  in.jump = jump_down && !jump_was;
  jump_was = jump_down;
  if (phase != Phase::kWalking && phase != Phase::kOutLight) in = Input{};
  // a jump is kept until the director's step has used it
  const bool jump = input.jump || in.jump;
  input = in;
  input.jump = jump;

  // the game: the handbrake (the parked car stays), START for the pause menu, nothing else
  buttons = uint16_t((pad & kPadStart) | kPadA);
  triggers[0] = triggers[1] = 0;
  sticks[0] = sticks[1] = sticks[2] = sticks[3] = 0;

  // the camera button, pressed in the black until the wanted camera is active
  if (camera_target) {
    const uint32_t vt = CameraVtable(base);
    const bool there = camera_target == 1 ? FollowCamera(vt) : vt == camera_target;
    if (there) {
      REXLOG_INFO("Gripline: walking - camera {:08X} after {} presses", vt, presses);
      camera_target = 0, pressing = false;
    } else if (pressing) {
      if (Ms(press_at, now) > 60) pressing = false, press_at = now;  // released
    } else if (Ms(press_at, now) > 350 && !BlendCamera(vt)) {
      if (presses >= 12) {
        REXLOG_WARN("Gripline: walking - camera {:08X} not reached, left as it is", camera_target);
        camera_target = 0;
      } else {
        ++presses, pressing = true, press_at = now;
      }
    }
    if (pressing) buttons |= CameraButtonBit();
  }
}

// The camera director's step (hooks/gripline_dashcam.cpp), main loop, after the game built the
// view. True: view and position are the walker's (to be written in place of the game's).
bool forza::gripline::WalkFrame(PPCContext& ctx, uint8_t* base, uint32_t director, float view[16], float position[3]) {
  if (!Valid(director) || director != FirstViewDirector(base)) return false;
  const auto now = Clock::now();
  std::lock_guard lock(mutex);
  const auto state = forza::telemetry::Player().State();

  if (request_out) {
    request_out = false;
    float pos[3], fwd[3];
    forza::telemetry::CarPhysicsView physics{};
    const bool have_physics = forza::telemetry::LocalCarPhysics(base, physics);
    if (forza::xr::CockpitActive()) {
      REXLOG_INFO("Gripline: walking not in the VR cockpit");
    } else if (state != forza::telemetry::Drive::kDriving || !forza::telemetry::LocalCarPose(base, pos, fwd)) {
      REXLOG_INFO("Gripline: walking - not driving now ({})", forza::telemetry::DriveName(state));
    } else if (OnlineType(SessionType(ctx, base))) {
      Card("Walking geht nur offline.", "Walking is offline only.", 3.0);
    } else if (have_physics && std::fabs(physics.speed) > kMaxOutSpeed) {
      Card("Erst anhalten, dann aussteigen.", "Stop the car first, then get out.", 3.0);
    } else {
      std::memcpy(car_pos, pos, sizeof(car_pos));
      std::memcpy(car_fwd, fwd, sizeof(car_fwd));
      // the handedness of the game's view basis: r . (u x f)
      float m[9];
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i * 3 + j] = LoadFloat(base, director + 240 + 4 * (i * 4 + j));
      const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[3] * (m[1] * m[8] - m[2] * m[7]) +
                        m[6] * (m[1] * m[5] - m[2] * m[4]);
      handed = det < 0.0f ? -1.0f : 1.0f;
      bad_frames = 0;
      SetPhase(Phase::kOutDark, now);
      REXLOG_INFO("Gripline: getting out (car {:.1f} {:.1f} {:.1f}, view basis {})", pos[0], pos[1], pos[2],
                  handed > 0 ? "+" : "-");
    }
  }
  if (!walking_on) return false;

  // the end at once: the session went online (checked every 2 s), loading, the car moved
  static Clock::time_point online_at;
  if (Ms(online_at, now) > 2000) {
    online_at = now;
    if (OnlineType(SessionType(ctx, base))) {
      Abort("online session");
      return false;
    }
  }
  const bool bad = state == forza::telemetry::Drive::kLoading || state == forza::telemetry::Drive::kMenu;
  bad_frames = bad ? bad_frames + 1 : 0;
  float pos[3], fwd[3];
  if (bad_frames > 90) {
    Abort("loading or menu");
    return false;
  }
  if (forza::telemetry::LocalCarPose(base, pos, fwd)) {
    const float dx = pos[0] - car_pos[0], dy = pos[1] - car_pos[1], dz = pos[2] - car_pos[2];
    if (dx * dx + dy * dy + dz * dz > kTeleported * kTeleported) {
      Abort("the game moved the car");
      return false;
    }
  }

  float dt = std::chrono::duration<float>(now - last_step).count();
  last_step = now;
  if (!(dt > 0.0f) || dt > 0.1f) dt = 1.0f / 60.0f;
  if (state != forza::telemetry::Drive::kDriving) dt = 0.0f;  // paused: the walker stands
  Input in = input;
  input.jump = false;
  {
    const float lo[3] = {w.pos[0] - 2.0f, w.pos[1] - 8.0f, w.pos[2] - 2.0f};
    const float hi[3] = {w.pos[0] + 2.0f, w.pos[1] + 2.5f, w.pos[2] + 2.0f};
    world_ok = QueryWorld(ctx, base, lo, hi);
    static int logged = 0;
    if (logged < 3 || (!world_ok && empty_frames == 0)) {
      ++logged;
      REXLOG_INFO("Gripline: walking collision {} triangles at {:.1f} {:.1f} {:.1f}", tris.size(), w.pos[0],
                  w.pos[1], w.pos[2]);
    }
    empty_frames = world_ok ? 0 : empty_frames + 1;
  }
  Step(dt, in);
  BuildView(view);
  position[0] = w.eye[0], position[1] = w.eye[1], position[2] = w.eye[2];
  if (!logged_view) {
    logged_view = true;
    // the game's translation against -(axis . position): tells whether the convention matches
    float game_t[3], game_p[3], game_r[3];
    for (int i = 0; i < 3; ++i) {
      game_t[i] = LoadFloat(base, director + 240 + 4 * (12 + i));
      game_p[i] = LoadFloat(base, director + 176 + 4 * i);
    }
    for (int i = 0; i < 3; ++i) game_r[i] = LoadFloat(base, director + 240 + 4 * (i * 4));
    const float own = -(game_r[0] * game_p[0] + game_r[1] * game_p[1] + game_r[2] * game_p[2]);
    REXLOG_INFO("Gripline: walking view check: game t.x {:.3f} vs -(r.p) {:.3f}; eye {:.1f} {:.1f} {:.1f}",
                game_t[0], own, w.eye[0], w.eye[1], w.eye[2]);
  }
  return true;
}

/* Second cockpit view. FH2 places the driver camera at the steering wheel plus an offset that the
   camera keeps at +0x190 (x right, y up, z forward, metres; one vector per car, the default is the
   tunable Driver\DefaultOffsetFromSteeringWheel, the camera's set-up at game+0x103b690 writes it).
   The camera reads it every frame, so the second view only adds forward/up to it. Anything the game
   writes there itself (another car, the camera set up again) is taken as the new base. */
#include "cockpit_view.h"
#include "logging.h"
#include "support.h"

#define OFFSET_FIELD 0x190

static volatile LONG second_view, forward_mm = 350, up_mm = -50;
static volatile LONG held_by[2];
static void *last_camera;
static float base[3], written[3];
static int have_written;

void cockpit_view_set_offset(int forward_cm, int up_cm) {
    InterlockedExchange(&forward_mm, forward_cm * 10);
    InterlockedExchange(&up_mm, up_cm * 10);
}

void cockpit_view_input(unsigned source, int held) {
    if (source > 1) return;
    LONG before = InterlockedExchange(&held_by[source], held ? 1 : 0);
    if (held && !before) {
        LONG on = !second_view;
        InterlockedExchange(&second_view, on);
        log_append(on ? "COCKPIT VIEW: second view (at the gauges)\r\n" : "COCKPIT VIEW: normal view\r\n");
    }
}

void cockpit_view_frame(void *camera) {
    float *offset = (float *)((unsigned char *)camera + OFFSET_FIELD);
    float now[3];
    if (!memory_read(offset, now, sizeof(now))) return;
    if (camera != last_camera || !have_written || !memory_equal(now, written, sizeof(now))) {
        /* a new camera, or the game set the offset again: that is the car's own position */
        last_camera = camera;
        memory_copy(base, now, sizeof(base));
    }
    float want[3] = {base[0], base[1], base[2]};
    if (second_view) {
        want[1] += (float)up_mm / 1000.0f;
        want[2] += (float)forward_mm / 1000.0f;
    }
    memory_copy(written, want, sizeof(written));
    have_written = 1;
    if (!memory_equal(now, want, sizeof(want)) && memory_is_writable(offset, sizeof(want)))
        memory_copy(offset, want, sizeof(want));
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestCockpitView(void *camera, int toggle, int forward_cm, int up_cm) {
    cockpit_view_set_offset(forward_cm, up_cm);
    if (toggle) {
        cockpit_view_input(COCKPIT_VIEW_KEYBOARD, 1);
        cockpit_view_input(COCKPIT_VIEW_KEYBOARD, 0);
    }
    if (camera) cockpit_view_frame(camera);
}
#endif

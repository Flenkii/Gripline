/* Conflict check: looks for other programs that feed Azimuth a controller of their own.
   The Azimuth Mapper is a separate program (not a file in fh2-mods): it installs the ViGEmBus
   driver and shows Azimuth a virtual Xbox controller built from keys, pads and wheels. With
   Gripline's keyboard or wheel on top, every input would arrive twice.
   The Mapper opens one named event, "AzimuthMapper.ShowWindow" (it lets a second start bring
   the running window to the front), so its presence is one OpenEventW away - no process list. */
#include "extra_options.h"
#include "mod_scan.h"
#include "logging.h"
#include "notify.h"

int mod_scan_mapper_running(void) {
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, L"AzimuthMapper.ShowWindow");
    if (!event) return 0;
    CloseHandle(event);
    return 1;
}

void mod_scan_announce_mapper(void) {
    static volatile LONG announced;
    if (InterlockedCompareExchange(&announced, 1, 0) != 0) return;
    log_append("GRIPLINE: Azimuth Mapper is running; Gripline keyboard and wheel stay off.\r\n");
    notify_show_deferred("Azimuth Mapper detected\nGripline keyboard and wheel off - the Mapper's controller is used");
}

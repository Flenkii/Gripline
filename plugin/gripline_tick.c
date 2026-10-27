/* mods\Gripline\plugins\GriplineTick.dll - Extra Options plugin (FH2PluginVehicleUpdate).

   Gripline itself is fh2-mods\Gripline.dll (FH2 Mod Platform). Weather and time of day need a tick on
   the game thread while the local car updates; Extra Options calls this plugin there, and it passes
   the call on to Gripline's export GriplineVehicleUpdate. Nothing else. */
#include <windows.h>
#include <stdint.h>

typedef struct FH2PluginContext {
    uint32_t size;
    uint32_t api_version;
    uint32_t adapter;
    uint32_t game_image_size;
    void *game_image;
    HMODULE plugin_module;
    const wchar_t *plugins_directory;
    const wchar_t *plugin_path;
    const char *extra_options_version;
    void (*log_utf8)(const char *message);
} FH2PluginContext;

typedef struct FH2PluginVehicleFrame {
    uint32_t size, input_enabled;
    void *car;
    uint32_t buttons;
    float delta_seconds;
    int(WINAPI *flash)(void);
} FH2PluginVehicleFrame;

typedef void (*GriplineUpdate)(void *car, unsigned input_enabled);
static GriplineUpdate update;

__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    HMODULE gripline = GetModuleHandleW(L"Gripline.dll");
    update = gripline ? (GriplineUpdate)(void *)GetProcAddress(gripline, "GriplineVehicleUpdate") : NULL;
    if (context && context->log_utf8)
        context->log_utf8(update ? "GriplineTick: linked to Gripline (weather and time of day tick)."
                                 : "GriplineTick: fh2-mods\\Gripline.dll not loaded - nothing to tick.");
    return update != NULL;
}

__declspec(dllexport) void WINAPI FH2PluginVehicleUpdate(const FH2PluginVehicleFrame *frame) {
    if (update && frame) update(frame->car, frame->input_enabled);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}

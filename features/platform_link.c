/* FH2 Mod Platform link - see platform_link.h. */
#include "extra_options.h"
#include "../third_party/fh2mod/fh2mod.h"
#include "logging.h"
#include "support.h"
#include "notify.h"
#include "platform_link.h"
#include "video_options_xaml.h"

#define PAGE_KEY "Settings"
#define TILE_BACKGROUND 0xFF1D4F91u /* Gripline blue */
#define TILE_TEXT 0xFFFFFFFFu
#define TILE_TITLE_TABLE "PauseMenu.str"
#define TILE_TITLE_KEY 0xF655 /* IDS for the Achievements tile title ("ACHIEVEMENTS" / "ERFOLGE"), all languages */

static const FH2ModApi *api;
static FH2Mod *mod;
static volatile LONG page_active;
static char page_resources[48 * 1024], page_content[64 * 1024];

int platform_page_active(void) {
    return (int)page_active;
}

/* The page is written each time it opens: rebuild it then, so the texts follow the game
   language (known only once the game runs). */
static void page_open(void *user, const char *page_key) {
    (void)user;
    unsigned char *game = api->game_base ? (unsigned char *)api->game_base() : NULL;
    if (video_options_gripline_page(game, page_resources, sizeof(page_resources), page_content,
                                    sizeof(page_content))) {
        api->set_page_xaml(mod, page_key, page_resources, page_content, NULL);
    }
}

__declspec(dllexport) int GriplinePlatformAttach(const FH2ModApi *platform) {
    if (!platform || mod || platform->size < offsetof(FH2ModApi, wait_for_game) + sizeof(void *) ||
        !platform->register_mod)
        return 1;
    api = platform;
    static FH2ModInfo info;
    info.size = sizeof(info);
    info.name = "Gripline";
    info.version = GRIPLINE_VERSION;
    info.github_repo = "Flenkii/Gripline";
    info.platform_version = "2.7.1";
    info.check_updates = 0; /* release tags (v0.2.98-forkN) are no plain version numbers */
    info.description = "Wheel (Logitech G920), keyboard, traffic & weather for Forza Horizon 2. "
                       "Settings: the GRIPLINE tile in the pause menu.";
    mod = api->register_mod(&info);
    if (!mod) return 1;
    api->log(mod, "Gripline " GRIPLINE_VERSION " linked (winmm.dll next to azimuth.exe)");
    if (api->api_version < 8 || api->size < offsetof(FH2ModApi, set_page_tile_colors) + sizeof(void *)) {
        api->log(mod, "platform older than 2.7.0: no tile page, settings stay at Options > HUD");
        return 0;
    }
    static FH2Page page;
    page.size = sizeof(page);
    page.key = PAGE_KEY;
    page.title = "Gripline";
    page.label = "Gripline settings";
    page.description = "Wheel, keyboard, traffic, weather, time of day, cockpit and notifications.";
    page.tile = "Achievements";
    page.on_open = page_open;
    if (!api->add_page(mod, &page)) {
        api->log(mod, "no tile page (see above); settings stay at Options > HUD");
        return 0;
    }
    /* first content right away (English until the game runs; on_open rebuilds it) */
    if (video_options_gripline_page(NULL, page_resources, sizeof(page_resources), page_content,
                                    sizeof(page_content)) &&
        api->set_page_xaml(mod, PAGE_KEY, page_resources, page_content, NULL)) {
        api->set_page_tile_colors(mod, PAGE_KEY, TILE_BACKGROUND, TILE_TEXT);
        api->set_page_tile_text(mod, PAGE_KEY, "SETTINGS");
        if (api->set_string) { /* the tile's big title: ERFOLGE -> GRIPLINE V6 (from fh2mod_init: in time) */
            static wchar_t title[32];
            const char *text = "GRIPLINE " GRIPLINE_VERSION;
            unsigned i = 0;
            for (; text[i] && i + 1 < ARRAY_COUNT(title); ++i) title[i] = (wchar_t)(unsigned char)text[i];
            title[i] = 0;
            api->set_string(mod, TILE_TITLE_TABLE, TILE_TITLE_KEY, title);
        }
        InterlockedExchange(&page_active, 1);
        api->log(mod, "settings on the pause menu tile (Achievements -> GRIPLINE)");
        log_append("PLATFORM: FH2 Mod Platform found - Gripline settings are on the pause menu "
                   "tile GRIPLINE.\r\n");
    } else {
        api->log(mod, "tile page XAML refused; settings stay at Options > HUD");
    }
    return 0;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestGriplinePage(char *resources, size_t rcap, char *content, size_t ccap) {
    return video_options_gripline_page(NULL, resources, rcap, content, ccap);
}
#endif

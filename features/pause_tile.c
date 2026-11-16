/* V7.2: Gripline tile in the pause menu, CARS tab (see pause_tile.h). */
#include "pause_tile.h"

#ifndef PAUSE_TILE_PATCH_ONLY
#include <windows.h>
#include "../gripline.h"
#include "../logging.h"
#include "../support.h"
#endif
#include "key_menu.h"

/* ------------------------------------------------------------------ the text */

static const char *find_in(const char *from, const char *end, const char *needle) {
    size_t k = 0;
    while (needle[k]) ++k;
    if (!k || from > end || (size_t)(end - from) < k) return NULL;
    for (const char *p = from; p + k <= end; ++p) {
        size_t i = 0;
        while (i < k && p[i] == needle[i]) ++i;
        if (i == k) return p;
    }
    return NULL;
}

static const char tile_item[] =
    "\r\n            <local:TileMenuDataItem Heading=\"GRIPLINE\"\r\n"
    "                                    Icon=\"/ForzaUI;component/Textures/Anthem/Icons/PauseMenu/MyProfile.png\"\r\n"
    "                                    x:Key=\"Gripline_MenuDataItem_Tile\" />";

static const char tile_button[] =
    "                        <Button x:Name=\"PART_Button_Gripline\"\r\n"
    "                                Canvas.Left=\"{StaticResource A_Double_TileCoord_X_5}\"\r\n"
    "                                Canvas.Top=\"{StaticResource A_Double_TileCoord_Y_0}\"\r\n"
    "                                DataContext=\"{StaticResource Gripline_MenuDataItem_Tile}\"\r\n"
    "                                Style=\"{StaticResource A_Style_Button_Tile_1x1}\"\r\n"
    "                                Template=\"{StaticResource A_ControlTemplate_IconTile}\"\r\n"
    "                                local:TileMenuNavigationManager.TileState=\"Enabled\"\r\n"
    "                                local:TileMenuNavigationManager.GridX=\"5\"\r\n"
    "                                local:TileMenuNavigationManager.GridY=\"0\"\r\n"
    "                                local:UIPageTransitionManager.TransitionIndex=\"5\" />\r\n";

static const char wide_style[] = "A_Style_Button_Tile_2x1";
static const char small_style[] = "A_Style_Button_Tile_1x1";
static const char wide_template[] = "A_ControlTemplate_HorizontalIconTile";
static const char small_template[] = "A_ControlTemplate_IconTile";

/* is an element between from and end already at GridX 5 / GridY 0? */
static int cell_taken(const char *from, const char *end) {
    for (const char *p = from; (p = find_in(p, end, "GridX=\"5\"")) != NULL; ++p) {
        const char *start = p;
        while (start > from && *start != '<') --start;
        const char *stop = find_in(p, end, ">");
        if (!stop) stop = end;
        if (find_in(start, stop, "GridY=\"0\"")) return 1;
    }
    return 0;
}

/* Pause menu, CARS tab (PART_SCOPE_Autoshow): TUNE CAR 2x1 -> 1x1, Gripline 1x1 beside it. */
size_t pause_tile_patch(const char *src, size_t n, char *out, size_t capacity) {
    const char *end = src + n;
    if (find_in(src, end, "PART_Button_Gripline")) return 0; /* already there */
    const char *key = find_in(src, end, "x:Key=\"A_MenuDataItem_TuneCar\"");
    const char *item_end = key ? find_in(key, end, "/>") : NULL;
    const char *scope = find_in(src, end, "x:Name=\"PART_SCOPE_Autoshow\"");
    const char *scope_end = scope ? find_in(scope, end, "</Canvas>") : NULL;
    const char *tune = scope ? find_in(scope, scope_end ? scope_end : end, "x:Name=\"PART_Button_TuneCar\"") : NULL;
    const char *tune_end = tune ? find_in(tune, scope_end, "/>") : NULL;
    const char *style = tune_end ? find_in(tune, tune_end, wide_style) : NULL;
    const char *templ = tune_end ? find_in(tune, tune_end, wide_template) : NULL;
    if (!item_end || !scope_end || !style || !templ || item_end > scope || cell_taken(scope, scope_end)) return 0;
    item_end += 2;
    const char *line = scope_end;
    while (line > tune_end && line[-1] != '\n') --line; /* insert before the scope's closing line */
    const char *first = style < templ ? style : templ, *second = style < templ ? templ : style;
    const char *first_new = style < templ ? small_style : small_template;
    size_t first_old = style < templ ? sizeof(wide_style) - 1 : sizeof(wide_template) - 1;
    const char *second_new = style < templ ? small_template : small_style;
    size_t second_old = style < templ ? sizeof(wide_template) - 1 : sizeof(wide_style) - 1;

    size_t need = n + (sizeof(tile_item) - 1) + (sizeof(tile_button) - 1) + 16;
    if (need > capacity) return 0;
    size_t o = 0;
#define COPY(from, to)                                                                                          \
    do {                                                                                                        \
        for (const char *q_ = (from); q_ < (to); ++q_) out[o++] = *q_;                                          \
    } while (0)
#define COPYZ(text)                                                                                             \
    do {                                                                                                        \
        for (const char *q_ = (text); *q_; ++q_) out[o++] = *q_;                                                \
    } while (0)
    COPY(src, item_end);
    COPYZ(tile_item);
    COPY(item_end, first);
    COPYZ(first_new);
    COPY(first + first_old, second);
    COPYZ(second_new);
    COPY(second + second_old, line);
    COPYZ(tile_button);
    COPY(line, end);
#undef COPY
#undef COPYZ
    return o;
}

/* stateflow\pause.xml: the pause menu's "expansion_packs" exit (the Marketplace tile that the FH2 Mod
   Platform turns into its UPDATES tile and no longer fires) leads to Gripline's page instead, a
   show_hud_options state like the platform's own pages. */
static const char flow_node[] =
    "    <node id=\"_gripline_page\" state=\"ui_screens.show_hud_options\">\r\n"
    "      <exit id=\"back\" target=\"pause_menu.enterback\" />\r\n"
    "    </node>\r\n\r\n";

size_t pause_tile_patch_flow(const char *src, size_t n, char *out, size_t capacity) {
    const char *end = src + n;
    if (find_in(src, end, "_gripline_page")) return 0;
    static const char exit_head[] = "<exit id=\"expansion_packs\" target=\"";
    const char *exit_at = find_in(src, end, exit_head);
    const char *target = exit_at ? exit_at + sizeof(exit_head) - 1 : NULL;
    const char *target_end = target ? find_in(target, end, "\"") : NULL;
    const char *node = find_in(src, end, "<node id=\"_marketplace_expansion_pack\"");
    if (!target_end || !node || target_end - target > 64 || find_in(target_end + 1, end, exit_head)) return 0;
    const char *line = node;
    while (line > src && line[-1] != '\n') --line;
    if (line < target_end) return 0;
    static const char new_target[] = "_gripline_page";
    size_t need = n + sizeof(flow_node) + sizeof(new_target) + 16;
    if (need > capacity) return 0;
    size_t o = 0;
    for (const char *q = src; q < target; ++q) out[o++] = *q;
    for (const char *q = new_target; *q; ++q) out[o++] = *q;
    for (const char *q = target_end; q < line; ++q) out[o++] = *q;
    for (const char *q = flow_node; *q; ++q) out[o++] = *q;
    for (const char *q = line; q < end; ++q) out[o++] = *q;
    return o;
}

/* ------------------------------------------------------------------ the page */

/* Gripline's own page: a show_hud_options scene (like the FH2 Mod Platform's pages and FH2 Mouse's)
   whose XAML is Gripline's file in its media folder (Extra Options mounts it over game:\media). */
static char page_text[256u * 1024u];
static size_t page_len;

static void put(const char *text) {
    while (*text && page_len + 1 < sizeof(page_text)) page_text[page_len++] = *text++;
}

/* text inside a XAML attribute */
static void put_escaped(const char *text) {
    for (; *text && page_len + 8 < sizeof(page_text); ++text) {
        switch (*text) {
        case '&': put("&amp;"); break;
        case '<': put("&lt;"); break;
        case '>': put("&gt;"); break;
        case '"': put("&quot;"); break;
        case '{': put("("); break; /* a leading { would be read as a markup extension */
        case '}': put(")"); break;
        default: page_text[page_len++] = *text;
        }
    }
}

static void put_number(unsigned value) {
    char digits[12];
    int n = 0;
    do digits[n++] = (char)('0' + value % 10);
    while ((value /= 10) && n < 11);
    while (n) {
        char c[2] = {digits[--n], 0};
        put(c);
    }
}

static const char page_head[] =
    "<Page xmlns:local=\"clr-namespace:ForzaUI\"\r\n"
    "      xmlns:System=\"clr-namespace:System;assembly=mscorlib\"\r\n"
    "      xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"\r\n"
    "      xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"\r\n"
    "      x:Class =\"ForzaUI.A_HudOptions\"\r\n"
    "      Grid.IsSharedSizeScope=\"True\"\r\n"
    "      Style=\"{DynamicResource A_Style_Page_Generic}\"\r\n"
    "      DataContext=\"{Binding RelativeSource={RelativeSource Self}}\"\r\n"
    "      FocusManager.FocusedElement=\"{Binding ElementName=OptionsList}\">\r\n"
    "    <local:UIPageTransitionManager.TransitionManager>\r\n"
    "        <local:UIPageTransitionManager DurationSpread=\"{StaticResource A_Duration_Transition_Spread}\" "
    "TransitionDelay=\"{StaticResource A_Duration_Transition_Delay}\"/>\r\n"
    "    </local:UIPageTransitionManager.TransitionManager>\r\n"
    "    <Page.Resources>\r\n"
    "        <ResourceDictionary>\r\n"
    "            <Style x:Key=\"Gripline_Text_Heading\" TargetType=\"local:FZTextBlock\" BasedOn=\"{StaticResource A_Style_SliderControlText}\">\r\n"
    "                <Setter Property=\"Color\" Value=\"{StaticResource A_Color_TileSelected_Background}\" />\r\n"
    "            </Style>\r\n"
    "            <ControlTemplate x:Key=\"Gripline_Row_Heading\" TargetType=\"{x:Type Button}\">\r\n"
    "                <Grid VerticalAlignment=\"Center\">\r\n"
    "                    <local:FZTextBlock Text=\"{Binding (local:BindingProxy.A), RelativeSource={RelativeSource TemplatedParent}}\" "
    "Style=\"{StaticResource Gripline_Text_Heading}\" HorizontalAlignment=\"Left\" />\r\n"
    "                </Grid>\r\n"
    "            </ControlTemplate>\r\n";

static const char page_body[] =
    "        </ResourceDictionary>\r\n"
    "    </Page.Resources>\r\n"
    "    <Grid>\r\n"
    "        <local:FZMaterial Style=\"{StaticResource A_Style_PauseMenuBlur}\" />\r\n"
    "        <ContentControl x:Name=\"TopBar\" Style=\"{StaticResource A_Style_PlayerBar}\" />\r\n"
    "        <ContentControl x:Name=\"BottomBar\" Style=\"{StaticResource A_Style_ButtonBar}\">\r\n"
    "            <ContentControl x:Name=\"DefaultHelpPanel\" local:ControllerButtonPanel.ADescription=\"{local:Loc HelpButtons.IDS_Select}\" "
    "local:ControllerButtonPanel.BDescription=\"{local:Loc HelpButtons.IDS_Back}\" Style=\"{StaticResource A_Style_ContentControl_DefaultButtonBar}\" />\r\n"
    "        </ContentControl>\r\n"
    "        <Border Style=\"{StaticResource A_Style_Border_ScenePadding}\" SnapsToDevicePixels=\"True\" UseLayoutRounding=\"True\">\r\n"
    "        <Grid x:Name=\"LayoutRoot\">\r\n"
    "            <ContentControl Style=\"{StaticResource A_Style_ContentControl_ScreenTilt}\" Margin=\"0,-60,0,0\" Focusable=\"False\">\r\n"
    "                <StackPanel Name=\"BasicOptions\" Orientation=\"Vertical\" VerticalAlignment=\"Center\" HorizontalAlignment=\"Center\">\r\n"
    "                    <StackPanel Orientation=\"Horizontal\" Margin=\"0,0,0,6\" Style=\"{StaticResource A_Style_FrameworkElement_DefaultTransition_0}\">\r\n"
    "                        <ContentControl Content=\"GRIPLINE\" Style=\"{StaticResource A_Style_ContentControl_ScreenTitleBlock}\" Focusable=\"False\" />\r\n"
    "                    </StackPanel>\r\n"
    "                    <StackPanel Orientation=\"Horizontal\" Style=\"{StaticResource A_Style_FrameworkElement_DefaultTransition_0}\">\r\n"
    "                        <Grid Margin=\"0, 0, 6, 0\" local:UIPageTransitionManager.TransitionIndex=\"0\">\r\n"
    "                            <local:FZMaterial Color=\"{StaticResource A_Color_TileSelected_Text}\">\r\n"
    "                                <local:CommonListBox x:Name=\"OptionsList\" KeyboardNavigation.DirectionalNavigation=\"Contained\" "
    "Style=\"{StaticResource A_Style_ListBox_List_Default}\" ItemContainerStyle=\"{StaticResource A_Style_ListBoxItem_List_Default}\" "
    "Width=\"688\" Height=\"550\" HorizontalAlignment=\"Stretch\" VerticalAlignment=\"Stretch\">\r\n";

static const char page_tail[] =
    "                                </local:CommonListBox>\r\n"
    "                            </local:FZMaterial>\r\n"
    "                        </Grid>\r\n"
    "                        <Grid Width=\"380\" HorizontalAlignment=\"Left\">\r\n"
    "                            <ContentControl Content=\"{Binding Tag.Description}\" DataContext=\"{Binding SelectedItem, ElementName=OptionsList}\" "
    "Style=\"{StaticResource A_Style_DescriptionPanel}\" local:UIPageTransitionManager.TransitionIndex=\"1\" />\r\n"
    "                        </Grid>\r\n"
    "                    </StackPanel>\r\n"
    "                </StackPanel>\r\n"
    "            </ContentControl>\r\n"
    "        </Grid>\r\n"
    "        </Border>\r\n"
    "    </Grid>\r\n"
    "</Page>\r\n";

/* The page (Marvin 14.11.2026): compact, one row per setting, each a left/right choice like the game's
   own options (SpinnerControl) that names what is chosen; the logbook stays in F8. Rows: */
enum { ITEM_HEADING, ITEM_LIST, ITEM_SWITCH };
#define ITEM_CHOICES 24 /* the dashboard cam forward has 21 */
typedef struct {
    int kind;
    unsigned part;                         /* F8 page it comes from (its description) */
    char label[128];
    unsigned count;                        /* choices */
    int current;                           /* the one chosen now */
    int stepper;                           /* 1: A in F8 steps through the values (one key) */
    char keys[ITEM_CHOICES][KEY_MENU_MIRROR_KEY];
    char values[ITEM_CHOICES][KEY_MENU_MIRROR_VALUE];
} PageItem;
#define PAGE_ITEMS 96
static PageItem items[PAGE_ITEMS];
static unsigned item_count;

static int same_text(const char *a, const char *b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

static int switch_value(const char *v) {
    static const char *const on[] = {"On", "on", "An", "an"};
    static const char *const off[] = {"Off", "off", "Aus", "aus"};
    for (unsigned i = 0; i < 4; ++i) {
        if (same_text(v, on[i])) return 1;
        if (same_text(v, off[i])) return 0;
    }
    return -1;
}

static void copy_text(char *out, unsigned capacity, const char *text) {
    unsigned i = 0;
    for (; text[i] && i + 1 < capacity; ++i) out[i] = text[i];
    out[i] = 0;
}

static PageItem *new_item(int kind, unsigned part, const char *label) {
    if (item_count >= PAGE_ITEMS) return NULL;
    PageItem *it = &items[item_count++];
    it->kind = kind, it->part = part, it->count = 0, it->current = 0, it->stepper = 0;
    copy_text(it->label, sizeof(it->label), label);
    return it;
}

/* The F8 pages as they are now -> items[] (also used to see what changed after a choice). */
static void collect_items(void) {
    static KeyMenuMirrorRow rows[64];
    item_count = 0;
    for (unsigned t = 0; t < key_menu_tile_page_count(); ++t) { /* the logbook stays in F8 */
        unsigned p = key_menu_tile_page(t);
        unsigned n = key_menu_mirror_tile_rows(p, rows, 64);
        new_item(ITEM_HEADING, p, key_menu_mirror_label_shown(p));
        unsigned page_start = item_count; /* the page's name stays */
        for (unsigned i = 0; i < n; ++i) {
            const KeyMenuMirrorRow *r = &rows[i];
            if (r->style == KEY_MENU_MIRROR_DIM) continue; /* notes stay in F8 */
            if (r->style == KEY_MENU_MIRROR_HEADING) {
                /* a list under it: the heading names the row ("Card design  < Overdrive >") */
                unsigned j = i + 1, active = 0, has_active = 0;
                while (j < n && rows[j].style == KEY_MENU_MIRROR_NORMAL && rows[j].key[0] &&
                       (rows[j].flags & KEY_MENU_MIRROR_FLAG_CHOICE) && j - i - 1 < ITEM_CHOICES) {
                    if (rows[j].flags & KEY_MENU_MIRROR_FLAG_ACTIVE) active = j - i - 1, has_active = 1;
                    ++j;
                }
                if (has_active && j - i - 1 >= 2) {
                    PageItem *it = new_item(ITEM_LIST, p, r->label);
                    if (!it) return;
                    for (unsigned k = i + 1; k < j; ++k) {
                        copy_text(it->keys[it->count], KEY_MENU_MIRROR_KEY, rows[k].key);
                        copy_text(it->values[it->count], KEY_MENU_MIRROR_VALUE, rows[k].label);
                        ++it->count;
                    }
                    it->current = (int)active;
                    i = j - 1;
                    continue;
                }
                if (same_text(r->label, "Settings") || same_text(r->label, "Einstellungen")) continue;
                if (item_count > page_start && items[item_count - 1].kind == ITEM_HEADING) --item_count; /* nothing under it */
                new_item(ITEM_HEADING, p, r->label);
                continue;
            }
            if (!r->key[0]) continue; /* nothing to change (a value another mod shows) */
            int current = -1;
            PageItem *it = new_item(ITEM_LIST, p, r->label);
            if (!it) return;
            it->count = key_menu_mirror_values(r->key, it->values, ITEM_CHOICES, &current);
            if (it->count >= 2) { /* A in F8 steps through these */
                copy_text(it->keys[0], KEY_MENU_MIRROR_KEY, r->key);
                it->stepper = 1;
                it->current = current < 0 ? 0 : current;
                continue;
            }
            --item_count;
            int on = switch_value(r->value);
            if (on < 0 && (r->flags & KEY_MENU_MIRROR_FLAG_ON)) on = 1;
            if (on >= 0) {
                PageItem *it = new_item(ITEM_SWITCH, p, r->label);
                if (!it) return;
                copy_text(it->keys[0], KEY_MENU_MIRROR_KEY, r->key);
                it->count = 2, it->current = on;
            }
            /* else an action without values ("both to default"): stays in F8 */
        }
        /* a heading with nothing under it goes */
        if (item_count && items[item_count - 1].kind == ITEM_HEADING) --item_count;
    }
}

/* One sentence per row for the description panel (Marvin 14.11.2026), by the row's F8 key; the panel adds
   the choices. A row without one shows its F8 page's description. */
typedef struct {
    const char *key, *en, *de;
} RowText;
static const RowText row_texts[] = {
    {"LI0", "Turns the telemetry panel on or off: speed, grip, g-forces and your best values while you drive.", "Schaltet die Telemetrie-Tafel an oder aus: Tempo, Grip, g-Kr\xc3\xa4""fte und deine Bestwerte beim Fahren."},
    {"LO0", "The colour of all Gripline cards and the key list. You see the change right away in the top right corner.", "Die Farbe aller Gripline-Karten und der Tastenliste. Die \xc3\x84""nderung siehst du sofort oben rechts."},
    {"LN1", "In the cockpit the hands are hidden and the steering wheel turns as far as your real one. Takes effect after a restart of the game.", "Im Cockpit sind die H\xc3\xa4""nde ausgeblendet und das Lenkrad dreht so weit wie dein echtes. Wirkt nach einem Neustart des Spiels."},
    {"LP0", "How far forward the Dashboard Cam sits, in cm. With a known car it is saved for this car only.", "Wie weit vorne die Dashboard Cam sitzt, in cm. Bei einem bekannten Auto wird es nur f\xc3\xbc""r dieses Auto gespeichert."},
    {"LQ0", "How high the Dashboard Cam sits, in cm (minus is lower). With a known car it is saved for this car only.", "Wie hoch die Dashboard Cam sitzt, in cm (minus ist tiefer). Bei einem bekannten Auto wird es nur f\xc3\xbc""r dieses Auto gespeichert."},
    {"LR1", "How much traffic drives in the open world. Takes effect after a restart of the game.", "Wie viel Verkehr in der offenen Welt f\xc3\xa4""hrt. Wirkt nach einem Neustart des Spiels."},
    {"LJ0", "Off: no speed cameras and speed zones in the open world. Takes effect after a restart of the game.", "Aus: keine Blitzer und Geschwindigkeitszonen in der offenen Welt. Wirkt nach einem Neustart des Spiels."},
    {"LJ1", "Off: no names above Drivatars and other cars when you are close. Takes effect after a restart of the game.", "Aus: keine Namen \xc3\xbc""ber Drivataren und anderen Autos, wenn du nah dran bist. Wirkt nach einem Neustart des Spiels."},
    {"LJ2", "Off: no race and event symbols on the road in the open world; events still start from the map. After a restart of the game.", "Aus: keine Rennen- und Event-Symbole auf der Stra\xc3\x9f""e; Events starten weiter \xc3\xbc""ber die Karte. Nach einem Neustart des Spiels."},
    {"LJ3", "On: Drivatars in the open world drive calmly and do not speed up when you get close. Races stay as they are. After a restart.", "An: Drivatare in der offenen Welt fahren gem\xc3\xbc""tlich und geben nicht Gas, wenn du nah kommst. Rennen bleiben gleich. Nach einem Neustart."},
    {"LB117", "Shows a mouse pointer in the game's menus, so you can click tiles and buttons. With 'Only when moved' it hides until you move the mouse.", "Zeigt einen Mauszeiger in den Spielmen\xc3\xbc""s, damit du Kacheln und Kn\xc3\xb6""pfe anklicken kannst. Bei 'Nur bei Bewegung' erscheint er erst, wenn du die Maus bewegst."},
    {"LH0", "Lets you steer the car with the mouse, like in truck simulators. A higher value turns the wheels faster for the same mouse move.", "Damit lenkst du das Auto mit der Maus, wie in LKW-Simulatoren. Ein h\xc3\xb6""herer Wert lenkt bei gleicher Mausbewegung st\xc3\xa4""rker."},
    {"LH20", "A small area around the centre where mouse moves do nothing, so the car keeps going straight when your hand shakes a little.", "Ein kleiner Bereich um die Mitte, in dem Mausbewegungen nichts tun. So f\xc3\xa4""hrt das Auto geradeaus, auch wenn die Hand leicht zittert."},
    {"LH10", "How long after you stop moving the mouse the steering slowly goes back to straight ahead.", "Wie lange nach dem Stillhalten der Maus die Lenkung langsam wieder auf geradeaus geht."},
    {"LV70", "How fast the camera turns when you look around with the mouse while walking around your car.", "Wie schnell sich die Kamera dreht, wenn du dich beim Laufen ums Auto mit der Maus umschaust."},
    {"LV71", "How fast the camera turns when you look around with the mouse while driving. Let go and it slowly turns back to the road.", "Wie schnell sich die Kamera beim Fahren mit der Maus dreht. L\xc3\xa4""sst du los, dreht sie langsam zur\xc3\xbc""ck auf die Stra\xc3\x9f""e."},
    {"LN11", "In the driver and cockpit view your look turns a little into the bend, like a real driver looks where the road goes. Only while the tyres grip, not in a drift.", "In der Fahrer- und Cockpitansicht schaut dein Blick etwas in die Kurve, wie ein echter Fahrer dorthin schaut, wo die Stra\xc3\x9f""e hinf\xc3\xbc""hrt. Nur bei Grip, nicht im Drift."},
    {"LX80", "How strongly your look turns into the bend: off, light, medium, strong, or Custom with the values below.", "Wie stark dein Blick in die Kurve schaut: aus, leicht, mittel, stark oder Eigene mit den Werten darunter."},
    {"LX81", "How far the look turns to the left or right at most, in degrees. Changing it makes the strength Custom.", "Wie weit der Blick h\xc3\xb6""chstens nach links oder rechts schaut, in Grad. Eine \xc3\x84""nderung stellt die St\xc3\xa4""rke auf Eigene."},
    {"LX82", "How strongly the look reacts to the bend: higher values look further into gentle bends too.", "Wie stark der Blick auf die Kurve reagiert: H\xc3\xb6""here Werte schauen auch bei sanften Kurven weiter hinein."},
    {"LX83", "How fast the look turns into the bend. More percent is faster, less is calmer.", "Wie schnell der Blick in die Kurve schaut. Mehr Prozent ist schneller, weniger ist ruhiger."},
    {"LX84", "How fast the look comes back to straight ahead after the bend. More percent is faster.", "Wie schnell der Blick nach der Kurve wieder geradeaus schaut. Mehr Prozent ist schneller."},
    {"LX85", "From this speed the look starts to turn into bends; it is at full strength a little faster.", "Ab dieser Geschwindigkeit schaut der Blick in die Kurve; etwas schneller wirkt er ganz."},
    {"LN9", "How you leave the car: the door opens and the camera glides out, or the picture fades to black like before.", "Wie du aussteigst: Die T\xc3\xbc""r geht auf und die Kamera gleitet hinaus, oder das Bild blendet wie fr\xc3\xbc""her kurz schwarz ab."},
    {"LU0", "Makes the camera sway gently while walking, like someone filming with a phone or a handheld camera.", "L\xc3\xa4""sst die Kamera beim Laufen leicht schwanken, als w\xc3\xbc""rde jemand mit dem Handy oder einer Handkamera filmen."},
    {"LW0", "The small speedometer you see while walking: how it looks and what it shows.", "Der kleine Tacho, den du beim Laufen siehst: wie er aussieht und was er anzeigt."},
    {"LT0", "Cruise control switches itself off when you hit something, so the car does not keep pushing after a crash.", "Der Tempomat schaltet sich bei einem Aufprall selbst aus, damit das Auto nach einem Unfall nicht weiter Gas gibt."},
    {"LT1", "Cruise control switches itself off when the car gets very slow, for example when you brake to a stop.", "Der Tempomat schaltet sich aus, wenn das Auto sehr langsam wird, zum Beispiel wenn du bis zum Stillstand bremst."},
    {"LT10", "Cruise control can only be switched on above this speed.", "Erst ab dieser Geschwindigkeit l\xc3\xa4""sst sich der Tempomat einschalten."},
    {"LM0", "Which cars show no damage: none (as in the game), the other cars (Drivatars and traffic), or every car including yours. Offline only, visible damage only.", "Welche Autos keinen Schaden zeigen: keins (wie im Spiel), die anderen Autos (Drivatare und Verkehr) oder alle Autos, auch deins. Nur offline, nur sichtbarer Schaden."},
    {"LI50", "Writes the telemetry values of your drive into a CSV file, which you can open in Excel or a similar program.", "Schreibt die Telemetrie-Werte deiner Fahrt in eine CSV-Datei, die du in Excel oder \xc3\xa4""hnlichen Programmen \xc3\xb6""ffnen kannst."},
    {"LI1", "Shows or hides this value in the telemetry panel while you drive.", "Zeigt diesen Wert im Telemetrie-Fenster beim Fahren an oder blendet ihn aus."},
    {"LI60", "Where the telemetry panel sits on the screen.", "Wo das Telemetrie-Fenster auf dem Bildschirm sitzt."},
    {"LI61", "How solid the panel's background is. Lower values let you see more of the game behind it.", "Wie deckend der Hintergrund des Fensters ist. Niedrige Werte lassen mehr vom Spiel dahinter durchscheinen."},
    {"LI62", "The size of the telemetry panel and its text.", "Die Gr\xc3\xb6""\xc3\x9f""e des Telemetrie-Fensters und seiner Schrift."},
    {"LN4", "At the start Gripline checks GitHub and shows a card when a newer version is out. Nothing else is sent.", "Beim Start schaut Gripline auf GitHub nach und zeigt eine Karte, wenn es eine neuere Version gibt. Sonst wird nichts gesendet."},
    {"LN5", "How your summoned car drives to you: calm and careful, normal, or sporty and in a hurry.", "Wie dein gerufenes Auto zu dir f\xc3\xa4""hrt: ruhig und vorsichtig, normal oder sportlich und eilig."},
    {"LN6", "Engine and tyre sounds while the summoned car drives to you on its own.", "Motor- und Reifenger\xc3\xa4""usche, w\xc3\xa4""hrend das gerufene Auto allein zu dir f\xc3\xa4""hrt."},
    {"LN7", "Lets Nitro work in online sessions too. Turn it off if your lobby does not want it.", "L\xc3\xa4""sst Nitro auch in Online-Sitzungen funktionieren. Schalte es aus, wenn deine Lobby das nicht m\xc3\xb6""chte."},
    {"LN8", "When another mod already has a feature (for example keyboard and mouse), Gripline turns its own version off so nothing happens twice.", "Hat eine andere Mod eine Funktion schon (zum Beispiel Tastatur und Maus), schaltet Gripline seine eigene ab, damit nichts doppelt passiert."},
    {"LS0", "Choose which kinds of Gripline cards appear on screen.", "W\xc3\xa4""hle, welche Arten von Gripline-Karten auf dem Bildschirm erscheinen."},
    {"LS10", "The short cards when the game starts: Gripline version and the menu key.", "Die kurzen Karten beim Spielstart: Gripline-Version und die Men\xc3\xbc""taste."},
    {"LS11", "A card when you set a new best value, for example a new top speed.", "Eine Karte, wenn du einen neuen Bestwert schaffst, zum Beispiel eine neue H\xc3\xb6""chstgeschwindigkeit."},
    {"LS12", "Cards when you reach driving milestones, like 100 or 1000 km in one car.", "Karten, wenn du Fahr-Meilensteine erreichst, wie 100 oder 1000 km mit einem Auto."},
    {"LS13", "A summary of your trip (distance, time, top speed) when you pause the game.", "Eine Zusammenfassung deiner Fahrt (Strecke, Zeit, H\xc3\xb6""chsttempo), wenn du das Spiel pausierst."},
    {"LS14", "Live info cards while in noclip and while using mouse steering.", "Live-Infokarten im Noclip und w\xc3\xa4""hrend der Maus-Lenkung."},
    {"LS15", "Cards about your steering wheel and about Nitro.", "Karten zu deinem Lenkrad und zu Nitro."},
    {"LS16", "A short confirmation card whenever you change a setting here or on the Mods page.", "Eine kurze Best\xc3\xa4""tigungskarte, wenn du hier oder auf der Mods-Seite etwas \xc3\xa4""nderst."},
    {"LG0", "The look of all Gripline cards. You see the change right away in the top right corner.", "Das Aussehen aller Gripline-Karten. Die \xc3\x84""nderung siehst du sofort oben rechts."},
};

static const RowText *row_text(const char *key) {
    for (unsigned k = 0; k < sizeof(row_texts) / sizeof(row_texts[0]); ++k)
        if (same_text(row_texts[k].key, key)) return &row_texts[k];
    if (key[0] == 'L' && key[1] == 'I' && key[2] >= '1' && key[2] <= '9' && (!key[3] || (key[3] >= '0' && key[3] <= '9' && !key[4])))
        return row_text("LI1"); /* LI1..LI99 below 50: the telemetry values shown (by key: a fixed index went
                                   wrong once rows were added above it) */
    return NULL;
}

/* the panel text of row i: its sentence and its choices ("Classic - Enhanced - Overdrive") */
static void put_row_description(unsigned i, int german) {
    const PageItem *it = &items[i];
    const RowText *t = row_text(it->keys[0]);
    if (!t) {
        put_escaped(key_menu_mirror_description_shown(it->part));
        return;
    }
    put_escaped(german ? t->de : t->en);
    if (it->kind != ITEM_LIST || it->count < 2) return;
    put(german ? " Auswahl: " : " Choices: ");
    if (it->count <= 6) {
        for (unsigned c = 0; c < it->count; ++c) {
            if (c) put(" - ");
            put_escaped(it->values[c]);
        }
    } else { /* a long scale: its ends */
        put_number(it->count);
        put(german ? " Stufen, " : " steps, ");
        put_escaped(it->values[0]);
        put(german ? " bis " : " to ");
        put_escaped(it->values[it->count - 1]);
    }
    put(".");
}

size_t pause_tile_build_page(void) {
    collect_items();
    page_len = 0;
    put(page_head);
    for (unsigned t = 0; t < key_menu_tile_page_count(); ++t) {
        unsigned p = key_menu_tile_page(t);
        put("            <local:TileMenuDataItem x:Key=\"Gripline_Part_");
        put_number(p);
        put("\" Heading=\"\" Description=\"");
        put_escaped(key_menu_mirror_description_shown(p));
        put("\" Icon=\"\" />\r\n");
    }
    int german = key_menu_mirror_german();
    for (unsigned i = 0; i < item_count; ++i) {
        if (items[i].kind == ITEM_HEADING) continue;
        put("            <local:TileMenuDataItem x:Key=\"Gripline_Row_");
        put_number(i);
        put("\" Heading=\"\" Description=\"");
        put_row_description(i, german);
        put("\" Icon=\"\" />\r\n");
    }
    put(page_body);
    int first = 1;
    for (unsigned i = 0; i < item_count; ++i) {
        const PageItem *it = &items[i];
        if (it->kind == ITEM_HEADING) {
            put("                                    <Button Tag=\"{StaticResource Gripline_Part_");
            put_number(it->part);
            put("}\" local:BindingProxy.A=\"");
            put_escaped(it->label);
            put("\" Template=\"{StaticResource Gripline_Row_Heading}\"");
        } else {
            put("                                    <local:SpinnerControl x:Name=\"Gripline_S");
            put_number(i);
            put("\" Label=\"");
            put_escaped(it->label);
            put("\" Tag=\"{StaticResource Gripline_Row_");
            put_number(i);
            put("}\" SelectedIndex=\"");
            put_number(it->kind == ITEM_SWITCH ? (unsigned)it->current : 0u);
            /* the game's choice rows only show items of their own kind: lists get the On/Off items as a
               carrier, Gripline writes their text itself and steps them itself (hook_step) */
            put("\" ItemsSource=\"{Binding OnOffOptions}\"");
            put(" Style=\"{StaticResource A_Style_SpinnerControl_Default}\"");
        }
        if (first) put(" x:Name=\"FirstItem\"");
        first = 0;
        put(" />\r\n");
    }
    put(page_tail);
    return page_len + 1 >= sizeof(page_text) ? 0 : page_len;
}
/* what row i shows now (UTF-8): a list's chosen name, a switch's is the game's own On/Off */
static const char *item_text(unsigned i, int value) {
    if (i >= item_count || items[i].kind != ITEM_LIST || value < 0 || (unsigned)value >= items[i].count) return "";
    return items[i].values[value];
}
const char *pause_tile_page_text(void) {
    return page_text;
}

#ifndef PAUSE_TILE_PATCH_ONLY
/* ------------------------------------------------------------------ the files */

typedef HANDLE(WINAPI *CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef size_t (*PatchFn)(const char *, size_t, char *, size_t);
typedef struct {
    const wchar_t *tail;      /* lower case, backslashes */
    const wchar_t *copy_name; /* in Mount\gripline_ui */
    PatchFn patch;
    wchar_t copy_path[PATH_CAPACITY];
    volatile LONG state;  /* 0 not made yet, 1 made, -1 not as expected / failed */
    volatile LONG seen;   /* opens by the game */
    volatile LONG copied; /* ... that got Gripline's copy */
} Redirect;
static Redirect redirects[2] = {
    {L"\\ui\\scenes\\anthem\\a_pausemenu.xaml", L"A_PauseMenu.xaml", pause_tile_patch, {0}, 0, 0, 0},
    {L"\\stateflow\\pause.xml", L"pause.xml", pause_tile_patch_flow, {0}, 0, 0, 0},
};
static wchar_t copy_dir[PATH_CAPACITY];
static SRWLOCK copy_lock = SRWLOCK_INIT;
#define PAGE_CAPACITY (512u * 1024u)
static char page_in[PAGE_CAPACITY], page_out[PAGE_CAPACITY + 8192u];

static wchar_t lower_w(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
    return c == L'/' ? L'\\' : c;
}

static int ends_with(const wchar_t *name, const wchar_t *tail) {
    size_t n = 0, k = 0;
    while (name[n]) ++n;
    while (tail[k]) ++k;
    if (n < k) return 0;
    for (size_t i = 0; i < k; ++i)
        if (lower_w(name[n - k + i]) != tail[i]) return 0;
    return 1;
}

static int write_file(const wchar_t *path, const char *data, size_t n) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD put = 0;
    BOOL ok = WriteFile(f, data, (DWORD)n, &put, NULL) && put == (DWORD)n;
    CloseHandle(f);
    return ok;
}

static int make_copy(Redirect *r, const wchar_t *source) {
    HANDLE f = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD got = 0;
    BOOL ok = ReadFile(f, page_in, PAGE_CAPACITY, &got, NULL);
    CloseHandle(f);
    if (!ok || !got || got >= PAGE_CAPACITY) return 0;
    size_t n = r->patch(page_in, got, page_out, sizeof(page_out));
    if (!n) return 0;
    CreateDirectoryW(copy_dir, NULL);
    return write_file(r->copy_path, page_out, n);
}

static HANDLE create_file(CreateFileWFn original, LPCWSTR name, DWORD access, DWORD share,
                          LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE template_file) {
    if (name && copy_dir[0] && disposition == OPEN_EXISTING &&
        !(access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE))) {
        for (unsigned i = 0; i < 2; ++i) {
            Redirect *r = &redirects[i];
            if (!r->copy_path[0] || !ends_with(name, r->tail)) continue;
            InterlockedIncrement(&r->seen);
            AcquireSRWLockExclusive(&copy_lock);
            if (!r->state) r->state = make_copy(r, name) ? 1 : -1;
            LONG state = r->state;
            ReleaseSRWLockExclusive(&copy_lock);
            if (state > 0) {
                HANDLE h = original(r->copy_path, access, share, security, disposition, flags, template_file);
                if (h != INVALID_HANDLE_VALUE) {
                    InterlockedIncrement(&r->copied);
                    return h;
                }
            }
            break;
        }
    }
    return original(name, access, share, security, disposition, flags, template_file);
}

/* Azimuth opens the game's files; Extra Options (winmm.dll) may open its mediaoverride files itself */
static CreateFileWFn original_host, original_eo;
static HANDLE WINAPI hook_create_host(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                      DWORD disposition, DWORD flags, HANDLE template_file) {
    return create_file(original_host, name, access, share, security, disposition, flags, template_file);
}
static HANDLE WINAPI hook_create_eo(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                    DWORD disposition, DWORD flags, HANDLE template_file) {
    return create_file(original_eo, name, access, share, security, disposition, flags, template_file);
}

/* IAT slot of `function` imported by name from `dll` (case-insensitive dll name) */
static int same_ascii_ci(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static void **import_slot(unsigned char *module, const char *dll, const char *function) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(module + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(module + dir.VirtualAddress); d->Name; ++d) {
        if (!same_ascii_ci((const char *)(module + d->Name), dll) || !d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(module + d->OriginalFirstThunk);
        void **slots = (void **)(module + d->FirstThunk);
        for (unsigned i = 0; names[i].u1.AddressOfData; ++i) {
            if (names[i].u1.AddressOfData & IMAGE_ORDINAL_FLAG64) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(module + (DWORD)names[i].u1.AddressOfData);
            if (same_ascii_ci((const char *)n->Name, function)) return &slots[i];
        }
    }
    return NULL;
}

static int swap_import(unsigned char *module, CreateFileWFn *original, void *detour) {
    void **slot = import_slot(module, "kernel32.dll", "CreateFileW");
    DWORD old = 0;
    if (!slot || !*slot || !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    *original = (CreateFileWFn)*slot;
    *slot = detour;
    VirtualProtect(slot, sizeof(void *), old, &old);
    return 1;
}

static volatile LONG armed_host, armed_eo;

int pause_tile_arm(unsigned char *host, const wchar_t *mount_dir) {
    if (!host || !mount_dir || !mount_dir[0] || original_host) return 0;
    if (!path_join(copy_dir, mount_dir, L"gripline_ui")) return 0;
    for (unsigned i = 0; i < 2; ++i)
        if (!path_join(redirects[i].copy_path, copy_dir, redirects[i].copy_name)) {
            copy_dir[0] = 0;
            return 0;
        }
    armed_host = swap_import(host, &original_host, (void *)hook_create_host);
    unsigned char *eo = (unsigned char *)GetModuleHandleW(L"winmm.dll");
    armed_eo = eo && eo != host ? swap_import(eo, &original_eo, (void *)hook_create_eo) : 0;
    if (!armed_host && !armed_eo) copy_dir[0] = 0;
    return armed_host || armed_eo;
}

/* ------------------------------------------------------------------ the button */

#define PAGE_SCENE_NAME L"Anthem\\A_GriplinePage"
static wchar_t page_path[PATH_CAPACITY];

static int write_page(void) {
    size_t n = page_path[0] ? pause_tile_build_page() : 0;
    return n && write_file(page_path, pause_tile_page_text(), n);
}

#define PAUSE_WIRE_RVA 0xDC6E30u /* PauseMenu: wires its tiles (this) - the platform's site, not hooked (V7.2, platform 2.9.0) */
#define PAUSE_BIND_RVA 0xD9F9F0u /* binds a named button to a member function (this, name, &pmf) */
#define PAUSE_NAME_RVA 0x1F5FC48u /* "PART_Button_TuneCar", used by the wiring (identity check) */
#define PACKS_TILE_RVA 0xDBDD00u  /* PauseMenu: Expansion Packs tile (fires "expansion_packs") */
#define HUD_SCENE_RVA 0xB3A3E0u   /* scene record of the HUD options page (record*) */
#define WSTRING_ASSIGN_RVA 0x22C570u /* std::wstring::assign(this, text, length) */
#define BLOCKED_RVA 0x2959A0u     /* returns the object whose +0x2610 blocks the tiles */
#define TILE_PREP_RVA 0xDD7640u   /* PauseMenu: called by every tile before its event */
#define FIRE_RVA 0x1422F10u       /* page: fire an event through its own state flow */
/* the page's choices (SpinnerControl) */
#define HUD_WIRE_RVA 0xC7AD40u    /* HudOptions page: after its XAML is loaded (this) - vtable slot 145 */
#define SPIN_STEP_RVA 0x1416830u  /* SpinnerControl: left/right (this, -1 / +1), stops at the ends (its OnKeyDown 0x1418710) */
#define SPIN_STATES_RVA 0x142DA50u /* SpinnerControl: arrow states after its index changed (this) */
#define FIND_NAME_RVA 0x1624C20u  /* element: find a named element in its name scope (this, String*, scratch*) */
#define STRING_MAKE_RVA 0x1663220u /* AVUI::String constructor (this, text, length or -1) */
#define STRING_FREE_RVA 0x16650A0u /* AVUI::String destructor (this) */
#define SET_VALUE_RVA 0x15F8A70u  /* DependencyObject::SetValue (this, property, boxed value) */
#define BOX_INT_RVA 0x336A10u     /* boxes an int (ObjectPtr *out, const int *value) */
#define RELEASE_RVA 0x257730u     /* frees an object whose count reached 0 */
#define SPIN_INDEX_RVA 0x367FE68u /* SpinnerControl.SelectedIndex (DependencyProperty *) - changing it updates the row */
#define SPIN_ITEM_RVA 0x367EEA8u  /* SpinnerControl.SelectedItem: what the row shows (an item not in its list keeps the index) */
#define GO_TO_STATE_RVA 0x1549BF0u /* VisualStateManager::GoToState (control, template root, name, transitions, 0) */
#define STRING_NEW_RVA 0x16A0200u /* makes a String object (ObjectPtr *out, const char *ascii) */
#define TEMPLATE_ROOT_SLOT (0x408u / 8u) /* control vtable: its template root (this, ObjectPtr *out) */
#define ALLOC_RVA 0x14424E0u      /* allocates a UI object (size, type) */
#define STRING_TYPE_RVA 0x16A4B30u /* the String type (0) */
#define STRING_WIDE_RVA 0x1663340u /* AVUI::String constructor (this, wide text, length or -1) */

typedef struct {
    void *function;
    int adjust;
    int unused;
} MemberFunction;
typedef void *(*WireFn)(void *self);
typedef void *(*BindFn)(void *self, const char *name, MemberFunction *handler);
typedef void *(*SceneFn)(void *record);
typedef void *(*AssignFn)(void *string, const wchar_t *text, size_t length);
typedef unsigned char *(*BlockedFn)(void);
typedef void (*SelfFn)(void *self);
typedef void (*FireFn)(void *self, void *event);
typedef void (*EventFn)(void *event, void *self);
typedef void (*StepFn)(void *self, int delta);
typedef void *(*FindNameFn)(void *self, void *name, void *scratch);
typedef void (*StringMakeFn)(void *self, const char *text, int length);
typedef void (*StringFreeFn)(void *self);
typedef void (*SetValueFn)(void *self, void *property, void *value);
typedef void **(*BoxIntFn)(void **out, const int *value);
typedef void (*ReleaseFn)(void *object);
typedef void *(*AllocFn)(size_t size, void *type);
typedef void *(*TypeFn)(void *unused);
typedef void (*WideMakeFn)(void *self, const wchar_t *text, int length);
typedef void **(*StringNewFn)(void **out, const char *ascii);
typedef void **(*RootFn)(void *self, void **out);
typedef char (*GoToStateFn)(void *control, void *root, void *name, int transitions, int unused);
static unsigned char *game_base;
static SceneFn original_scene;
static BindFn bind_button;
static volatile LONG wired_count, click_count, scene_count;
static volatile LONG64 page_pending; /* tick when the tile asked for Gripline's page */
static WireFn original_hud_wire;
static StepFn original_step;
static int shown[PAGE_ITEMS];      /* what each one shows */
static volatile LONG step_count, refresh_count, foreign_steps, text_count;
static WireFn original_states;

/* The page's choices as the game made them, held (UI thread only); every copy of the page the game builds
   is kept (it may build one more than it shows). */
#define PAGE_COPIES 4
static void *spinners[PAGE_COPIES][PAGE_ITEMS];
static unsigned spinner_items; /* item_count when they were found */
static unsigned spinner_copies;

static void release(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0)
        ((ReleaseFn)(void *)(game_base + RELEASE_RVA))(object);
}

/* another options page opens: let go of the old page's choices (held until now, so a freed one is never
   mistaken for one of ours) */
static void forget_spinners(void) {
    for (unsigned c = 0; c < PAGE_COPIES; ++c)
        for (unsigned i = 0; i < PAGE_ITEMS; ++i) {
            if (spinners[c][i]) release(spinners[c][i]);
            spinners[c][i] = NULL;
        }
    spinner_items = 0, spinner_copies = 0;
}

/* a list row shows its value: as the row's selected item (a text, not one of its list's items, so the
   game keeps the index and only shows it) */
static int set_item(void *spinner, const char *utf8) {
    unsigned char *g = game_base;
    if (!spinner) return 0;
    wchar_t wide[KEY_MENU_MIRROR_VALUE];
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, KEY_MENU_MIRROR_VALUE)) wide[0] = 0;
    void *text = ((AllocFn)(void *)(g + ALLOC_RVA))(0x40, ((TypeFn)(void *)(g + STRING_TYPE_RVA))(NULL));
    if (!text) return 0;
    ((WideMakeFn)(void *)(g + STRING_WIDE_RVA))(text, wide, -1);
    ((SetValueFn)(void *)(g + SET_VALUE_RVA))(spinner, *(void **)(g + SPIN_ITEM_RVA), text);
    release(text);
    return 1;
}

/* its arrows: lit where it can still go (the game does this from the index, which a list row does not use) */
static void set_arrows(void *spinner, int value, int count) {
    unsigned char *g = game_base;
    const char *names[2] = {value > 0 ? "LeftEnabled" : "LeftDisabled", value < count - 1 ? "RightEnabled" : "RightDisabled"};
    void *root = NULL;
    void **table = *(void ***)spinner;
    ((RootFn)table[TEMPLATE_ROOT_SLOT])(spinner, &root);
    if (!root) return; /* no template yet: the game's own call after it sets them (hook_states) */
    for (unsigned k = 0; k < 2; ++k) {
        void *name = NULL;
        ((StringNewFn)(void *)(g + STRING_NEW_RVA))(&name, names[k]);
        if (!name) continue;
        ((GoToStateFn)(void *)(g + GO_TO_STATE_RVA))(spinner, root, name, 1, 0);
        release(name);
    }
    release(root);
}

/* a switch row shows `value` (the game updates the row when its index changes) */
static void set_shown(void *spinner, int value) {
    void *box = NULL;
    ((BoxIntFn)(void *)(game_base + BOX_INT_RVA))(&box, &value);
    if (!box) return;
    ((SetValueFn)(void *)(game_base + SET_VALUE_RVA))(spinner, *(void **)(game_base + SPIN_INDEX_RVA), box);
    release(box);
}

static void show_row(void *spinner, unsigned i, int value) {
    if (!spinner) return;
    if (items[i].kind == ITEM_LIST) {
        set_item(spinner, item_text(i, value));
        set_arrows(spinner, value, (int)items[i].count);
    } else {
        set_shown(spinner, value);
    }
}

static int spinner_row(void *self) {
    if (self)
        for (unsigned c = 0; c < spinner_copies; ++c)
            for (unsigned k = 0; k < spinner_items; ++k)
                if (spinners[c][k] == self) return (int)k;
    return -1;
}

static void tile_clicked(void *self, void *sender, void *args) {
    (void)sender;
    (void)args;
    unsigned char *g = game_base;
    if (!g || !self) return;
    unsigned char *blocked = ((BlockedFn)(void *)(g + BLOCKED_RVA))();
    if (blocked && blocked[0x2610]) return;
    int written = write_page();
    if (InterlockedIncrement(&click_count) <= 3)
        log_append(written ? "PAUSETILE: Gripline tile chosen - Gripline page opens.\r\n"
                           : "PAUSETILE: Gripline tile chosen - page file could not be written.\r\n");
    if (!written) return;
    InterlockedExchange64(&page_pending, (LONG64)GetTickCount64());
    ((SelfFn)(void *)(g + TILE_PREP_RVA))(self);
    if (*(void **)((unsigned char *)self + 0x310)) {
        ((FireFn)(void *)(g + FIRE_RVA))(self, g + 0x352FFE8u);
    } else {
        void *event = g + 0x352FFD8u;
        void **table = *(void ***)event;
        ((EventFn)table[2])(event, self);
    }
}

static void *hook_scene(void *record) {
    void *result = original_scene(record);
    LONG64 asked = page_pending;
    if (record && asked && (LONG64)GetTickCount64() - asked < 3000) {
        ((AssignFn)(void *)(game_base + WSTRING_ASSIGN_RVA))((unsigned char *)record + 0x28, PAGE_SCENE_NAME,
                                                             sizeof(PAGE_SCENE_NAME) / sizeof(wchar_t) - 1);
        if (InterlockedIncrement(&scene_count) <= 3) log_append("PAUSETILE: HUD page record -> Gripline's page.\r\n");
    }
    return result;
}

/* V7.2 (FH2 Mod Platform 2.9.0): the tile is bound where the pause menu binds TUNE CAR, in the function that
   binds a named button (game+0xD9F9F0), not by hooking the pause menu's wiring (game+0xDC6E30): that one is the
   platform's own site, and 2.9.0 leaves its MODS tab and EXIT GAME tile out when another hook is there first. */
static int is_tune_car(const char *name) {
    static const char tune[] = "PART_Button_TuneCar";
    if (!name) return 0;
    if (name == (const char *)(game_base + PAUSE_NAME_RVA)) return 1;
    char text[sizeof(tune)];
    if (!memory_read(name, text, sizeof(text))) return 0;
    return memory_equal(text, tune, sizeof(tune));
}

static void *hook_bind(void *self, const char *name, MemberFunction *handler) {
    void *result = bind_button(self, name, handler);
    if (self && is_tune_car(name)) {
        MemberFunction mine = {(void *)tile_clicked, 0, 0};
        bind_button(self, "PART_Button_Gripline", &mine);
        if (InterlockedIncrement(&wired_count) == 1) {
            log_append_value("PAUSETILE: pause menu wired; menu file opened (times): ", (int)redirects[0].seen);
            log_append_value("PAUSETILE: ... with Gripline's tile: ", (int)redirects[0].copied);
            log_append_value("PAUSETILE: state flow opened (times): ", (int)redirects[1].seen);
            log_append_value("PAUSETILE: ... with Gripline's page: ", (int)redirects[1].copied);
            if (redirects[0].state < 0) log_append("PAUSETILE: the pause menu did not look as expected - tile left out.\r\n");
            if (redirects[1].state < 0) log_append("PAUSETILE: the state flow did not look as expected - no page.\r\n");
        }
    }
    return result;
}

static void *find_named(void *page, const char *name) {
    unsigned char g_string[0x60] __attribute__((aligned(16))) = {0};
    void *scratch = NULL;
    ((StringMakeFn)(void *)(game_base + STRING_MAKE_RVA))(g_string, name, -1);
    void *found = ((FindNameFn)(void *)(game_base + FIND_NAME_RVA))(page, g_string, &scratch);
    ((StringFreeFn)(void *)(game_base + STRING_FREE_RVA))(g_string);
    return found;
}

static void spinner_name(unsigned i, char out[24]) {
    static const char head[] = "Gripline_S";
    unsigned n = 0;
    for (; head[n]; ++n) out[n] = head[n];
    char d[8];
    unsigned k = 0;
    do d[k++] = (char)('0' + i % 10); while ((i /= 10) && k < 8);
    while (k) out[n++] = d[--k];
    out[n] = 0;
}

/* An options page is loaded: Gripline's page if its choices are there (no other page has these names).
   Another options page means Gripline's page is gone: its choices are let go. */
static void *hook_hud_wire(void *self) {
    void *result = original_hud_wire(self);
    if (!self) return result;
    unsigned first = 0;
    while (first < item_count && items[first].kind == ITEM_HEADING) ++first;
    char name[24];
    spinner_name(first, name);
    if (first >= item_count || !find_named(self, name)) {
        forget_spinners();
        return result;
    }
    if (spinner_copies >= PAGE_COPIES || (spinner_copies && spinner_items != item_count)) forget_spinners();
    unsigned c = spinner_copies++, found = 0, wanted = 0, texts = 0;
    spinner_items = item_count;
    for (unsigned i = 0; i < item_count; ++i) {
        shown[i] = items[i].current;
        if (items[i].kind == ITEM_HEADING) continue;
        spinner_name(i, name);
        ++wanted;
        void *spinner = find_named(self, name);
        if (!spinner) continue;
        InterlockedIncrement((volatile LONG *)((unsigned char *)spinner + 0x10)); /* held until forget_spinners */
        spinners[c][i] = spinner;
        ++found;
        if (items[i].kind == ITEM_LIST) show_row(spinner, i, shown[i]), ++texts;
    }
    log_append_value("PAUSETILE: Gripline page built, copy ", (int)c + 1);
    log_append_value("PAUSETILE: ... choices found: ", (int)found);
    if (found != wanted) log_append_value("PAUSETILE: ... of ", (int)wanted);
    log_append_value("PAUSETILE: ... list values written at once: ", (int)texts);
    return result;
}

/* the game set a row's arrows after its index changed (and showed its list item): a list row shows its own value */
static void *hook_states(void *self) {
    void *result = original_states(self);
    int i = spinner_row(self);
    if (i >= 0 && items[i].kind == ITEM_LIST) {
        show_row(self, (unsigned)i, shown[i]);
        if (InterlockedIncrement(&text_count) <= 3) log_append_value("PAUSETILE: list row shows its value, row ", i);
    }
    return result;
}

/* row i one to the left (-1) or right (+1), stopping at the ends like the game's own rows: what F8 does
   for it, at once. Returns 0 at an end (nothing changes). */
static int apply_choice(int i, int delta) {
    PageItem *it = &items[i];
    int count = (int)it->count, now = shown[i];
    int next = now + delta;
    if (next < 0) next = 0;
    if (next > count - 1) next = count - 1;
    if (next == now) return 0;
    if (it->kind == ITEM_SWITCH) key_menu_mirror_choose(it->keys[0]);
    else if (!it->stepper) key_menu_mirror_choose(it->keys[next]);
    else if (key_menu_mirror_step(it->keys[0], delta)) { /* ◄ ► like F8: one step, one save */
    } else { /* A steps on (in its own order): until it is the one wanted */
        static char values[ITEM_CHOICES][KEY_MENU_MIRROR_VALUE];
        for (int s = 0; s < count; ++s) {
            int at = -1;
            key_menu_mirror_values(it->keys[0], values, ITEM_CHOICES, &at);
            if (at == next) break;
            key_menu_mirror_choose(it->keys[0]);
        }
    }
    shown[i] = next;
    return 1;
}

/* left / right on one of the page's choices: what F8 does for that row, at once */
static void hook_step(void *self, int delta) {
    int i = spinner_row(self);
    if (i < 0 || (delta != 1 && delta != -1)) {
        if (spinner_items && delta && InterlockedIncrement(&foreign_steps) <= 3)
            log_append_value("PAUSETILE: a choice not on Gripline's page stepped, rows held: ", (int)spinner_items);
        original_step(self, delta);
        return;
    }
    if (!apply_choice(i, delta)) { /* at an end */
        if (items[i].kind == ITEM_SWITCH) original_step(self, delta);
        return;
    }
    if (InterlockedIncrement(&step_count) <= 8) log_append_value("PAUSETILE: page choice changed, row ", i);
    if (items[i].kind == ITEM_LIST) show_row(self, (unsigned)i, shown[i]); /* the game's items are only a carrier */
    else original_step(self, delta);
    for (unsigned c = 0; c < spinner_copies; ++c) /* the same row on the page's other copies */
        if (spinners[c][i] != self) show_row(spinners[c][i], (unsigned)i, shown[i]);
    /* rows another row changes (a card preset, say) show their new values */
    unsigned before = item_count;
    int kinds[PAGE_ITEMS];
    for (unsigned k = 0; k < before; ++k) kinds[k] = items[k].kind;
    collect_items();
    if (item_count != before) return;
    for (unsigned k = 0; k < before; ++k)
        if (items[k].kind != kinds[k]) return;
    for (unsigned k = 0; k < spinner_items; ++k) {
        if (items[k].current == shown[k]) continue;
        shown[k] = items[k].current;
        for (unsigned c = 0; c < spinner_copies; ++c) show_row(spinners[c][k], k, shown[k]);
        if (InterlockedIncrement(&refresh_count) <= 5) log_append_value("PAUSETILE: page row shows its new value, row ", (int)k);
    }
}

static void make_dirs_for(const wchar_t *file) {
    static wchar_t partial[PATH_CAPACITY];
    lstrcpynW(partial, file, PATH_CAPACITY);
    for (wchar_t *p = partial + 3; *p; ++p) {
        if (*p != L'\\') continue;
        *p = 0;
        CreateDirectoryW(partial, NULL);
        *p = L'\\';
    }
}

int pause_tile_start(unsigned char *game, const wchar_t *media_dir, PauseTileHookFn hook) {
    if (!copy_dir[0]) {
        log_append("PAUSETILE: off (no game folder or Azimuth's file import not found).\r\n");
        return 0;
    }
    log_append(armed_host && armed_eo ? "PAUSETILE: pause menu files are watched (Azimuth + Extra Options; Gripline's copies in Mount\\gripline_ui).\r\n"
               : armed_host            ? "PAUSETILE: pause menu files are watched (Azimuth only; Gripline's copies in Mount\\gripline_ui).\r\n"
                                       : "PAUSETILE: pause menu files are watched (Extra Options only; Gripline's copies in Mount\\gripline_ui).\r\n");
    if (!game || !hook) return 0;
    static const unsigned char bind_head[] = {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00};
    /* Expansion Packs tile after its first instruction (the platform hooks its start): the checks and
       the event it fires, rip-relative so the same in every run */
    static const unsigned char packs_body[] = {
        0x48, 0x8b, 0xd9, 0xe8, 0x92, 0x7c, 0x4d, 0xff, 0x80, 0xb8, 0x10, 0x26, 0x00, 0x00, 0x00, 0x75, 0x40, 0x48,
        0x8b, 0xcb, 0xe8, 0x21, 0x99, 0x01, 0x00, 0x48, 0x83, 0xbb, 0x10, 0x03, 0x00, 0x00, 0x00, 0x74, 0x14, 0x48,
        0x8d, 0x15, 0xb8, 0x22, 0x77, 0x02, 0x48, 0x8b, 0xcb, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0xd3, 0x51, 0x66, 0x00};
    static const unsigned char assign_head[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57};
    char name[20];
    if (!memory_read(game + PAUSE_NAME_RVA, name, sizeof(name)) || !memory_equal(name, "PART_Button_TuneCar", 20) ||
        !memory_equal(game + PAUSE_BIND_RVA, bind_head, sizeof(bind_head)) ||
        !memory_equal(game + PACKS_TILE_RVA + 6, packs_body, sizeof(packs_body)) ||
        !memory_equal(game + WSTRING_ASSIGN_RVA, assign_head, sizeof(assign_head))) {
        log_append("PAUSETILE: the pause menu's code is not as expected - tile not wired.\r\n");
        return 0;
    }
    game_base = game;
    if (media_dir && media_dir[0] && path_join(page_path, media_dir, L"UI\\Scenes\\Anthem\\A_GriplinePage.xaml")) {
        make_dirs_for(page_path);
        if (!write_page()) log_append("PAUSETILE: could not write Gripline's page file.\r\n");
    }
    void *original = NULL;
    if (!hook("HUD page scene record (Gripline page)", game + HUD_SCENE_RVA, (void *)hook_scene, &original) ||
        !original) {
        log_append("PAUSETILE: could not hook the HUD page record - no page.\r\n");
        return 0;
    }
    original_scene = (SceneFn)original;
    original = NULL;
    if (!hook("PauseMenu: bind a tile (Gripline tile beside TUNE CAR)", game + PAUSE_BIND_RVA, (void *)hook_bind, &original) ||
        !original) {
        log_append("PAUSETILE: could not hook the tile binding - tile not wired.\r\n");
        return 0;
    }
    bind_button = (BindFn)original;
    log_append("PAUSETILE: tile wiring ready (opens Gripline's page).\r\n");
    /* the page's left/right choices (without them the page only shows the values) */
    static const unsigned char hud_wire_head[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x40, 0xe8};
    static const unsigned char step_head[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xc9};
    static const unsigned char states_head[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xc1};
    static const unsigned char key_head[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x42, 0x64, 0x48, 0x8b, 0xda, 0x83, 0xf8, 0x17};
    if (!memory_equal(game + HUD_WIRE_RVA, hud_wire_head, sizeof(hud_wire_head)) ||
        !memory_equal(game + SPIN_STEP_RVA, step_head, sizeof(step_head)) ||
        !memory_equal(game + SPIN_STATES_RVA, states_head, sizeof(states_head)) ||
        !memory_equal(game + 0x1418710u, key_head, sizeof(key_head))) {
        log_append("PAUSETILE: the options page code is not as expected - page shows values only.\r\n");
        return 1;
    }
    original = NULL;
    if (!hook("HUD options page loaded (Gripline page)", game + HUD_WIRE_RVA, (void *)hook_hud_wire, &original) || !original) {
        log_append("PAUSETILE: could not hook the options page - page shows values only.\r\n");
        return 1;
    }
    original_hud_wire = (WireFn)original;
    original = NULL;
    if (!hook("Choice left/right (Gripline page)", game + SPIN_STEP_RVA, (void *)hook_step, &original) || !original) {
        log_append("PAUSETILE: could not hook the choices - page shows values only.\r\n");
        return 1;
    }
    original_step = (StepFn)original;
    original = NULL;
    if (hook("Choice arrows (Gripline page)", game + SPIN_STATES_RVA, (void *)hook_states, &original) && original)
        original_states = (WireFn)original;
    else
        log_append("PAUSETILE: could not hook the choice arrows - list values may show On/Off until changed.\r\n");
    log_append("PAUSETILE: page choices ready (left/right).\r\n");
    return 1;
}
#endif

#if defined(FH2_TWEAKS_TEST) && !defined(PAUSE_TILE_PATCH_ONLY)
/* left/right on row `item` of the page as built last (no game objects involved) */
__declspec(dllexport) int Fh2TestPauseTileChoose(int item, int delta) {
    if (item < 0 || (unsigned)item >= item_count || items[item].kind == ITEM_HEADING) return -1;
    for (unsigned k = 0; k < item_count; ++k) shown[k] = items[k].current;
    if (!apply_choice(item, delta)) return -2; /* at an end */
    return shown[item];
}
/* what row `item` holds now (its index) and shows (UTF-8 into out) */
__declspec(dllexport) int Fh2TestPauseTileShown(int item, char *out, unsigned size) {
    if (item < 0 || (unsigned)item >= item_count || items[item].kind == ITEM_HEADING) return -1;
    const char *t = item_text((unsigned)item, items[item].current);
    unsigned k = 0;
    for (; t[k] && k + 1 < size; ++k) out[k] = t[k];
    if (size) out[k] = 0;
    return items[item].current;
}
/* row `item`: "kind|label|key|value;value;..." (UTF-8) */
__declspec(dllexport) int Fh2TestPauseTileItem(int item, char *out, unsigned size) {
    if (item < 0 || (unsigned)item >= item_count || !size) return -1;
    const PageItem *it = &items[item];
    unsigned n = 0;
#define ADD(t) for (const char *q = (t); *q && n + 1 < size; ++q) out[n++] = *q
    ADD(it->kind == ITEM_HEADING ? "H" : it->kind == ITEM_LIST ? "L" : "S");
    ADD("|"); ADD(it->label); ADD("|"); ADD(it->keys[0]); ADD("|");
    for (unsigned c = 0; c < it->count && it->kind == ITEM_LIST; ++c) { ADD(it->values[c]); ADD(";"); }
#undef ADD
    out[n] = 0;
    return (int)item_count;
}
__declspec(dllexport) int Fh2TestPauseTilePage(char *out, unsigned size) {
    size_t n = pause_tile_build_page();
    if (!size) return (int)n;
    unsigned k = 0;
    for (; k < n && k + 1 < size; ++k) out[k] = page_text[k];
    out[k] = 0;
    return (int)n;
}
#endif

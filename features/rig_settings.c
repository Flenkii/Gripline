/* compat5: In-game page "Lenkrad & Verkehr" - unofficial add-on.
 *
 * Holds the menu description of the wheel-bridge and More-Traffic settings, forwards
 * changes to the live modules and persists them in the launcher's INI files:
 *   FH2_Wheel.ini         [Wheel]     Enabled, ForceFeedback, FFBStrength, ...
 *   FH2_Keyboard.ini      [Keyboard]  Enabled, SteerTime, ReturnTime, PedalTime
 *   FH2_ExtraOptions.ini  [Traffic]   TrafficLevel, RushHourAllDay, Weather, TimeOfDay
 * Saving happens on a background thread shortly after the last change, by editing only the
 * affected lines (comments and every other key stay untouched). When the launcher changes
 * one of the files while the game runs, the new values are picked up within a second. */
#include "weather.h"
#include "time_of_day.h"
#include "rig_settings.h"
#include "ini_edit.h"
#include "report.h"
#include "cockpit_view.h"
#include "ini_migrate.h"
#include "logging.h"
#include "support.h"
#include "video_options_text.h"
#include "wheel_buttons.h"
#include "keyboard_pad.h"
#include "notify.h"

#define WHEEL_SECTION "Wheel"
#define TRAFFIC_SECTION "Traffic"
#define KEYBOARD_SECTION "Keyboard"
#define GRIPLINE_SECTION "Gripline"
#define EO_INI_LIMIT 8192u
#define FILE_LIMIT 16384u
#define SAVE_DELAY_MS 600u

static const RigSettingInfo infos[RIG_SETTING_COUNT] = {
    [RIG_WHEEL_ENABLED] = {"EoRigWheelEnabled", "Enabled", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_WHEEL_FFB] = {"EoRigForceFeedback", "ForceFeedback", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_WHEEL_STRENGTH] = {"EoRigStrength", "FFBStrength", 0, 200, RIG_KIND_PERCENT, 0, 0.01f,
                            "EO_RigPercent200"},
    [RIG_WHEEL_RUMBLE] = {"EoRigRumble", "Rumble", 0, 200, RIG_KIND_PERCENT, 0, 0.01f,
                          "EO_RigPercent200"},
    [RIG_WHEEL_DAMPING] = {"EoRigDamping", "Damping", 0, 100, RIG_KIND_PERCENT, 0, 0.01f,
                           "A_Style_Slider_Options"},
    [RIG_WHEEL_FRICTION] = {"EoRigFriction", "Friction", 0, 100, RIG_KIND_PERCENT, 0, 0.01f,
                            "A_Style_Slider_Options"},
    [RIG_WHEEL_SMOOTHING] = {"EoRigSmoothing", "Smoothing", 0, 200, RIG_KIND_NUMBER, 0, 1,
                             "EO_RigMilliseconds"},
    [RIG_WHEEL_MINFORCE] = {"EoRigMinimumForce", "MinimumForce", 0, 50, RIG_KIND_PERCENT, 0,
                            0.01f, "EO_RigPercent50"},
    [RIG_WHEEL_OUTWARD] = {"EoRigOutward", "OutwardForce", 0, 100, RIG_KIND_PERCENT, 0, 0.01f,
                           "EO_RigPercent100"},
    [RIG_WHEEL_CENTERING] = {"EoRigCentering", "Centering", 0, 100, RIG_KIND_PERCENT, 0, 0.01f,
                             "EO_RigPercent100"},
    [RIG_WHEEL_LOCK] = {"EoRigLock", "Lock", 30, 450, RIG_KIND_NUMBER, 0, 1, "EO_RigLock"},
    [RIG_WHEEL_CURVE] = {"EoRigCurve", "Curve", 10, 60, RIG_KIND_NUMBER, 1, 0.1f, "EO_RigCurve"},
    [RIG_WHEEL_ROTATION] = {"EoRigRotation", "Rotation", 180, 2520, RIG_KIND_NUMBER, 0, 1,
                            "EO_RigRotation"},
    [RIG_WHEEL_SHIFTER] = {"EoRigShifter", "Shifter", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_WHEEL_BUTTON1] = {"EoRigButton1", "ExtraFunction1", 0, WHEEL_FUNCTION_COUNT - 1,
                           RIG_KIND_FUNCTION, 0, 1, NULL},
    [RIG_WHEEL_BUTTON2] = {"EoRigButton2", "ExtraFunction2", 0, WHEEL_FUNCTION_COUNT - 1,
                           RIG_KIND_FUNCTION, 0, 1, NULL},
    [RIG_WHEEL_BUTTON3] = {"EoRigButton3", "ExtraFunction3", 0, WHEEL_FUNCTION_COUNT - 1,
                           RIG_KIND_FUNCTION, 0, 1, NULL},
    [RIG_TRAFFIC_LEVEL] = {"EoRigTrafficLevel", "TrafficLevel", 0, 5, RIG_KIND_TRAFFIC, 0, 1,
                           NULL},
    [RIG_TRAFFIC_ALL_DAY] = {"EoRigRushHour", "RushHourAllDay", 0, 1, RIG_KIND_TOGGLE, 0, 1,
                             NULL},
    [RIG_WHEEL_PROFILE] = {"EoRigProfile", "", 0, RIG_PROFILE_COUNT - 1, RIG_KIND_PROFILE, 0, 1,
                           NULL},
    [RIG_TRAFFIC_PRESET] = {"EoRigDensity", "", 0, RIG_DENSITY_COUNT - 1, RIG_KIND_DENSITY, 0, 1,
                            NULL},
    [RIG_WEATHER] = {"EoRigWeather", "Weather", 0, RIG_WEATHER_COUNT - 1, RIG_KIND_WEATHER, 0, 1,
                     NULL},
    [RIG_TIME_OF_DAY] = {"EoRigTimeOfDay", "TimeOfDay", 0, RIG_TIME_COUNT - 1, RIG_KIND_TIME, 0, 1,
                         NULL},
    [RIG_WHEEL_FFB_MODE] = {"EoRigFfbMode", "FFBMode", 0, RIG_FFB_MODE_COUNT - 1, RIG_KIND_FFB_MODE,
                            0, 1, NULL},
    [RIG_WHEEL_CLUTCH_BRAKE] = {"EoRigClutchBrake", "ClutchAsBrake", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_KEY_ENABLED] = {"EoKeyEnabled", "Enabled", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_KEY_STEER] = {"EoKeySteer", "SteerTime", 0, 1000, RIG_KIND_NUMBER, 0, 1, "EO_KeyTime"},
    [RIG_KEY_RETURN] = {"EoKeyReturn", "ReturnTime", 0, 1000, RIG_KIND_NUMBER, 0, 1,
                        "EO_KeyTime"},
    [RIG_KEY_PEDAL] = {"EoKeyPedal", "PedalTime", 0, 1000, RIG_KIND_NUMBER, 0, 1, "EO_KeyTime"},
    [RIG_GRIP_NOTIFICATIONS] = {"EoGripNotify", "Notifications", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_GRIP_STEERING] = {"EoGripSteering", "SteeringAnimation", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_GRIP_REPORT] = {"EoGripReport", "", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_GRIP_VIEW_FORWARD] = {"EoGripViewForward", "CockpitViewForward", -20, 80, RIG_KIND_NUMBER, 0, 1, "EO_GripViewForward"},
    [RIG_GRIP_VIEW_UP] = {"EoGripViewUp", "CockpitViewUp", -20, 30, RIG_KIND_NUMBER, 0, 1, "EO_GripViewUp"},
};

/* Rows of the menu. The force feedback is the game's own (its wheel programs), so the old
   tuning values of the mod's own FFB (profile, damping, friction, smoothing, minimum force,
   outward force, centring, lock, curve, FFB type) are no longer shown; they stay readable
   from FH2_Wheel.ini for the fallback when the game's programs are not available.
   The H-pattern shifter is hidden too: the game reads the gear from the wheel but never
   uses it (FH2 only knows shift up / shift down). */
static const unsigned char order[] = {
    RIG_WHEEL_PROFILE,
    RIG_WHEEL_ENABLED,   RIG_WHEEL_FFB,     RIG_WHEEL_STRENGTH, RIG_WHEEL_RUMBLE,
    RIG_WHEEL_ROTATION,  RIG_WHEEL_CLUTCH_BRAKE, RIG_WHEEL_BUTTON1, RIG_WHEEL_BUTTON2,  RIG_WHEEL_BUTTON3,
    RIG_KEY_ENABLED,     RIG_KEY_STEER,     RIG_KEY_RETURN,     RIG_KEY_PEDAL,
    RIG_TRAFFIC_PRESET,  RIG_TRAFFIC_LEVEL, RIG_TRAFFIC_ALL_DAY, RIG_WEATHER, RIG_TIME_OF_DAY,
    RIG_GRIP_STEERING,   RIG_GRIP_VIEW_FORWARD, RIG_GRIP_VIEW_UP, RIG_GRIP_NOTIFICATIONS, RIG_GRIP_REPORT,
};
_Static_assert(sizeof(order) <= RIG_SETTING_COUNT, "menu rows");

int rig_kind_is_choice(unsigned kind) {
    return kind == RIG_KIND_TOGGLE || kind == RIG_KIND_TRAFFIC || kind == RIG_KIND_PROFILE ||
           kind == RIG_KIND_FUNCTION || kind == RIG_KIND_DENSITY || kind == RIG_KIND_WEATHER ||
           kind == RIG_KIND_FFB_MODE || kind == RIG_KIND_TIME;
}

const char *rig_kind_collection(unsigned kind) {
    switch (kind) {
        case RIG_KIND_TRAFFIC: return "TrafficChoices";
        case RIG_KIND_PROFILE: return "ProfileChoices";
        case RIG_KIND_DENSITY: return "DensityChoices";
        case RIG_KIND_WEATHER: return "WeatherChoices";
        case RIG_KIND_FUNCTION: return "FunctionChoices";
        case RIG_KIND_FFB_MODE: return "FfbModeChoices";
        case RIG_KIND_TIME: return "TimeChoices";
    }
    return "ToggleChoices";
}

unsigned rig_setting_menu_count(void) {
    return (unsigned)sizeof(order);
}

unsigned rig_setting_order(unsigned position) {
    return position < sizeof(order) ? order[position] : 0;
}

/* ------------------------------------------------------------------ input profiles */

/* The profile is not stored anywhere: it is whichever preset the current values match
   ("Eigene Werte" otherwise). Choosing a preset sets the wheel / keyboard switches (and the
   clutch pedal as brake); these are the ready-made profiles of the release ZIP, without copying
   files. Wheel and keyboard on/off apply after a restart. */
static const signed char profile_presets[RIG_PROFILE_COUNT][3] = {
    {0, 0, 0},   /* 0 = own values (not a preset) */
    {0, 0, -1},  /* 1 = Controller: no wheel, no keyboard (clutch as brake untouched) */
    {0, 1, -1},  /* 2 = Keyboard */
    {1, 1, 0},   /* 3 = Wheel */
    {1, 1, 1},   /* 4 = Wheel, clutch pedal as brake */
    {0, 0, 0},   /* 5 = Set up wheel: starts the assistant, changes no switches (never "current") */
};
#define PROFILE_SETUP 5
static const unsigned char profile_ids[3] = {RIG_WHEEL_ENABLED, RIG_KEY_ENABLED, RIG_WHEEL_CLUTCH_BRAKE};

/* Traffic presets: whichever one matches TrafficLevel + RushHourAllDay is shown. */
static const unsigned char density_presets[RIG_DENSITY_COUNT][2] = {
    {0, 0}, /* 0 = own values (not a preset) */
    {0, 0}, /* 1 = Original */
    {1, 0}, /* 2 = Ruhig */
    {3, 0}, /* 3 = Lebhaft */
    {4, 1}, /* 4 = Rushhour */
    {5, 1}, /* 5 = Chaos */
};

static int density_current(void) {
    for (unsigned p = 1; p < RIG_DENSITY_COUNT; ++p) {
        if (traffic_setting(RIG_TRAFFIC_LEVEL) == density_presets[p][0] &&
            traffic_setting(RIG_TRAFFIC_ALL_DAY) == density_presets[p][1]) {
            return (int)p;
        }
    }
    return 0;
}

static int profile_current(void) {
    for (unsigned p = 1; p < PROFILE_SETUP; ++p) {
        unsigned i = 0;
        while (i < 3 && (profile_presets[p][i] < 0 || rig_setting_get(profile_ids[i]) == profile_presets[p][i])) {
            ++i;
        }
        if (i == 3) {
            return (int)p;
        }
    }
    return 0;
}

const RigSettingInfo *rig_setting_info(unsigned id) {
    return id < RIG_SETTING_COUNT ? &infos[id] : NULL;
}

/* ------------------------------------------------------------------ texts */

typedef struct {
    const char *heading, *description;
} RigText;

static const RigText german[RIG_SETTING_COUNT] = {
    [RIG_WHEEL_ENABLED] = {"Lenkrad: Lenkrad im Spiel",
                           "Reicht dein Lenkrad (G920 & Co.) ans Spiel durch. Ist das Lenkrad "
                           "eingesteckt, fährt das Spiel nur damit. Aus = mit Tastatur oder "
                           "Controller fahren, obwohl das Lenkrad eingesteckt ist. Wirkt erst nach "
                           "einem Neustart des Spiels."},
    [RIG_WHEEL_FFB] = {"Lenkrad: Force Feedback",
                       "Kräfte am Lenkrad an oder aus. Aus = gar keine Kraft am Lenkrad. Wirkt "
                       "sofort."},
    [RIG_WHEEL_STRENGTH] = {"Lenkrad: FFB-Stärke",
                            "Verstärkung der originalen Spielkräfte. 100 % = genau die Kraft wie "
                            "auf der Xbox, bis 200 % = doppelt so stark. Wirkt sofort."},
    [RIG_WHEEL_RUMBLE] = {"Lenkrad: Vibration",
                          "Stärke der Vibrationen, z. B. auf Rüttelstreifen und im Gelände. 100 % = "
                          "wie im Spiel. Wirkt sofort."},
    [RIG_WHEEL_DAMPING] = {"Lenkrad: Dämpfung",
                           "Grunddämpfung gegen schnelles Drehen des Lenkrads. Nur bei FFB-Art "
                           "Eigenes. Wirkt sofort."},
    [RIG_WHEEL_FRICTION] = {"Lenkrad: Reibung",
                            "Leichte Reibung wie bei einer echten Lenkung. Nur bei FFB-Art Eigenes. "
                            "Wirkt sofort."},
    [RIG_WHEEL_SMOOTHING] = {"Lenkrad: Glättung (ms)",
                             "Glättet die Kraftstufen des Spiels in Millisekunden. Schläge kommen "
                             "weiter sofort durch. 0 = aus. Nur bei FFB-Art Eigenes. Wirkt sofort."},
    [RIG_WHEEL_MINFORCE] = {"Lenkrad: Mindestkraft",
                            "Hebt sehr kleine Kräfte an, damit das Getriebe des Lenkrads sie nicht "
                            "verschluckt. Wirkt sofort."},
    [RIG_WHEEL_OUTWARD] = {"Lenkrad: Kraft nach außen",
                           "Anteil der Spielkraft, der das Lenkrad von der Mitte weg zieht. Nur bei "
                           "FFB-Art Eigenes. Wirkt sofort."},
    [RIG_WHEEL_CENTERING] = {"Lenkrad: Zentrierung (Menüs)",
                             "Zieht das Lenkrad zur Mitte, solange du nicht fährst (Menüs, "
                             "Ladebildschirme). Nur bei FFB-Art Eigenes, bei Spiel macht das Spiel "
                             "die Zentrierung selbst. Wirkt sofort."},
    [RIG_WHEEL_LOCK] = {"Lenkrad: Anschlag (Grad)",
                        "Grad je Seite, ab denen das Lenkrad weich blockiert. Wirkt sofort."},
    [RIG_WHEEL_CURVE] = {"Lenkrad: Lenkkurve",
                         "1,0 = linear. Höher = Mitte direkter, außen ruhiger. Wirkt sofort."},
    [RIG_WHEEL_ROTATION] = {"Lenkrad: Drehwinkel (Grad)",
                            "Lenkwinkel, den das Spiel für dein Lenkrad annimmt. In G HUB 900 Grad "
                            "lassen. Wirkt sofort."},
    [RIG_WHEEL_SHIFTER] = {"Lenkrad: H-Schaltung",
                           "Schaltknauf (Driving Force Shifter) benutzen. Wirkt sofort."},
    [RIG_WHEEL_BUTTON1] = {"Lenkrad: Taste RSB",
                           "Spielfunktion für die rechte Stick-Taste am Lenkrad (G920: RSB, in "
                           "FH2_Wheel.ini ExtraButton1). Wirkt sofort."},
    [RIG_WHEEL_BUTTON2] = {"Lenkrad: Taste LSB",
                           "Spielfunktion für die linke Stick-Taste am Lenkrad (G920: LSB, in "
                           "FH2_Wheel.ini ExtraButton2). Wirkt sofort."},
    [RIG_WHEEL_BUTTON3] = {"Lenkrad: Xbox-Taste",
                           "Spielfunktion für die Xbox-Taste am Lenkrad (in FH2_Wheel.ini "
                           "ExtraButton3). Achtung: Windows kann damit die Xbox Game Bar öffnen. "
                           "Wirkt sofort."},
    [RIG_TRAFFIC_LEVEL] = {"Verkehr: Menge",
                           "Höchstzahl der Verkehrsautos. Wird sofort gespeichert, wirkt aber erst "
                           "nach einem Neustart von Azimuth (Spiel ganz schließen und neu starten)."},
    [RIG_TRAFFIC_ALL_DAY] = {"Verkehr: Rushhour rund um die Uhr",
                             "Volle Verkehrsdichte zu jeder Uhrzeit statt nur von 7 bis 19 Uhr. "
                             "Wirkt sofort."},
    [RIG_WEATHER] = {"Wetter",
                     "Festes Wetter in der freien Fahrt: Sonnig, Bewölkt, Gewitterwolken, Regen "
                     "oder Nebel (Nebel nur auf der Sturm-Insel, sonst sonnig). Original = das "
                     "Wetter wechselt wie im Spiel. Rennen und Treffen mit eigenem Wetter behalten "
                     "es, danach kommt deine Wahl zurück. Wirkt nach dem Schließen des Menüs."},
    [RIG_TIME_OF_DAY] = {"Tageszeit",
                         "Feste Tageszeit in der freien Fahrt: Morgen, Mittag, Sonnenuntergang "
                         "oder Nacht. Langsamer Tag = die Uhr läuft weiter, aber viermal "
                         "langsamer. Original = Tag und Nacht wie im Spiel. Rennen und Events "
                         "behalten ihre Zeit, danach kommt deine Wahl zurück. Online (FH2MP) "
                         "kommt die Zeit aus der Session. Wirkt nach dem Schließen des Menüs."},
    [RIG_TRAFFIC_PRESET] = {"Verkehr: Voreinstellung",
                            "Stellt Menge und Rushhour mit einem Klick ein: Original, Ruhig (24), "
                            "Lebhaft (48), Rushhour (64, rund um die Uhr) oder Chaos (100, rund "
                            "um die Uhr). Änderst du danach Menge oder Rushhour, steht hier Eigene "
                            "Werte. Wird sofort gespeichert, die Menge wirkt aber erst nach einem "
                            "Neustart von Azimuth."},
    [RIG_WHEEL_PROFILE] = {"Eingabe: Profil",
                           "Stellt Lenkrad im Spiel, Tastatur und Kupplung als Bremse mit einem "
                           "Klick ein: Controller, Tastatur, Lenkrad oder Lenkrad mit Kupplung als "
                           "Bremse. Änderst du danach einen der Schalter, steht hier Eigene Werte. "
                           "Lenkrad und Tastatur wirken erst nach einem Neustart des Spiels. "
                           "Lenkrad einrichten: Karten fragen Lenkung, Pedale und Tasten ab - für "
                           "jedes Lenkrad."},
    [RIG_WHEEL_FFB_MODE] = {"Lenkrad: FFB-Art",
                            "Spiel = die originalen Lenkrad-Programme von FH2, genau wie auf der "
                            "Xbox: Rückstellkraft, Lenkgewicht, Dämpfung, Vibration und die "
                            "Zentrierung in Menüs berechnet das Spiel. Eigenes = das alte FFB der "
                            "Mod. Wirkt sofort."},
    [RIG_WHEEL_CLUTCH_BRAKE] = {"Lenkrad: Kupplung als Bremse",
                                "Das Kupplungspedal bremst, die Kupplung ist dann aus. Praktisch, weil "
                                "das Bremspedal des G920 sehr hart ist. Idee: Lancaster. Wirkt sofort."},
    [RIG_KEY_ENABLED] = {"Tastatur: Tastatur",
                         "Die Tastatur steuert das Spiel wie ein Gamepad: W/S Gas und Bremse, A/D "
                         "lenken, Leertaste Handbremse, E/Q schalten, Pfeiltasten = Kamera beim "
                         "Fahren und Blättern in Menüs, Enter/Rücktaste/Esc in Menüs. Belegung "
                         "in FH2_Keyboard.ini. Controller funktionieren weiter. Mit eingestecktem "
                         "Lenkrad fährt das Spiel nur mit dem Lenkrad. Wirkt nach einem Neustart "
                         "des Spiels."},
    [RIG_KEY_STEER] = {"Tastatur: Lenkzeit (ms)",
                       "So lange dauert es bis zum vollen Lenkeinschlag, wenn du A oder D hältst. "
                       "Höher = sanfter. Wirkt sofort."},
    [RIG_KEY_RETURN] = {"Tastatur: Rückstellzeit (ms)",
                        "So lange dauert es, bis die Lenkung nach dem Loslassen wieder gerade steht. "
                        "Wirkt sofort."},
    [RIG_KEY_PEDAL] = {"Tastatur: Pedalzeit (ms)",
                       "So lange dauert es bis Vollgas oder Vollbremsung mit W oder S. 0 = sofort. "
                       "Wirkt sofort."},
    [RIG_GRIP_STEERING] = {"Cockpit: Volle Lenkrad-Animation",
                           "Im Cockpit sind die Hände ausgeblendet und das Lenkrad dreht so weit "
                           "wie dein echtes (Drehwinkel). Aus = Original-Hände und -Lenkrad. Wirkt "
                           "nach einem Neustart des Spiels."},
    [RIG_GRIP_NOTIFICATIONS] = {"Gripline: Benachrichtigungen",
                                "Kleine Gripline-Karten oben rechts im Bild, z. B. wenn etwas "
                                "gespeichert wurde oder Nitro online aus ist. Auch im Vollbild. "
                                "Wirkt sofort."},
    [RIG_GRIP_VIEW_FORWARD] = {"Cockpit-Ansicht 2: vor (cm)",
                            "Zweite Cockpit-Ansicht wie in FH5: so viele Zentimeter weiter vorne am "
                            "Tacho. Kamera-Taste: Cockpit, Cockpit 2, nächste Kamera (auch V oder eine freie "
                            "Lenkradtaste). Das Lenkrad verschwindet dort. "
                            "Wirkt sofort."},
    [RIG_GRIP_VIEW_UP] = {"Cockpit-Ansicht 2: hoch (cm)",
                          "So viele Zentimeter sitzt du in der zweiten Cockpit-Ansicht höher "
                          "(negativ = tiefer). Wirkt sofort."},
    [RIG_GRIP_REPORT] = {"Gripline: Fehlerbericht erstellen",
                         "Auf An stellen: packt Logs und Einstellungen in Gripline_Report.zip "
                         "neben azimuth.exe. Die Datei bei einem Problem auf GitHub unter Issues "
                         "anhängen. Es wird nichts verschickt."},
};

static const RigText english[RIG_SETTING_COUNT] = {
    [RIG_WHEEL_ENABLED] = {"Wheel: Use Wheel In Game",
                           "Passes your wheel (G920 and similar) to the game. While the wheel is "
                           "plugged in, the game drives with the wheel only. Off = drive with "
                           "keyboard or controller although the wheel is plugged in. Restart the "
                           "game to apply."},
    [RIG_WHEEL_FFB] = {"Wheel: Force Feedback",
                       "Forces on the wheel on or off. Off = no force on the wheel at all. Applies "
                       "immediately."},
    [RIG_WHEEL_STRENGTH] = {"Wheel: Force Feedback Strength",
                            "Gain of the game's original forces. 100% = exactly the force of the "
                            "Xbox, up to 200% = twice as strong. Applies immediately."},
    [RIG_WHEEL_RUMBLE] = {"Wheel: Rumble",
                          "Strength of road and kerb rumble. 100% = as in the game. Applies "
                          "immediately."},
    [RIG_WHEEL_DAMPING] = {"Wheel: Damping",
                           "Base damping against fast wheel movement. Type Own only. Applies "
                           "immediately."},
    [RIG_WHEEL_FRICTION] = {"Wheel: Friction",
                            "Light friction like a real steering rack. Type Own only. Applies "
                            "immediately."},
    [RIG_WHEEL_SMOOTHING] = {"Wheel: Smoothing (ms)",
                             "Smooths the game's force steps in milliseconds. Impacts still pass "
                             "through. 0 = off. Type Own only. Applies immediately."},
    [RIG_WHEEL_MINFORCE] = {"Wheel: Minimum Force",
                            "Lifts very small forces so the wheel's gears do not swallow them. "
                            "Applies immediately."},
    [RIG_WHEEL_OUTWARD] = {"Wheel: Outward Force",
                           "Share of the game force that pulls the wheel away from the centre. Type "
                           "Own only. Applies immediately."},
    [RIG_WHEEL_CENTERING] = {"Wheel: Centring (Menus)",
                             "Pulls the wheel to the centre while you are not driving. Type Own "
                             "only; with type Game the game centres the wheel itself. Applies "
                             "immediately."},
    [RIG_WHEEL_LOCK] = {"Wheel: Lock (Degrees)",
                        "Degrees per side after which the wheel is softly blocked. Applies "
                        "immediately."},
    [RIG_WHEEL_CURVE] = {"Wheel: Steering Curve",
                         "1.0 = linear. Higher = more direct centre, calmer outside. Applies "
                         "immediately."},
    [RIG_WHEEL_ROTATION] = {"Wheel: Rotation (Degrees)",
                            "Rotation the game assumes for your wheel. Keep 900 degrees in G HUB. "
                            "Applies immediately."},
    [RIG_WHEEL_SHIFTER] = {"Wheel: H-Pattern Shifter",
                           "Use the Driving Force Shifter. Applies immediately."},
    [RIG_WHEEL_BUTTON1] = {"Wheel: RSB Button",
                           "Game function for the wheel's right stick button (G920: RSB, "
                           "ExtraButton1 in FH2_Wheel.ini). Applies immediately."},
    [RIG_WHEEL_BUTTON2] = {"Wheel: LSB Button",
                           "Game function for the wheel's left stick button (G920: LSB, "
                           "ExtraButton2 in FH2_Wheel.ini). Applies immediately."},
    [RIG_WHEEL_BUTTON3] = {"Wheel: Xbox Button",
                           "Game function for the wheel's Xbox button (ExtraButton3 in "
                           "FH2_Wheel.ini). Windows may open the Xbox Game Bar with it. "
                           "Applies immediately."},
    [RIG_TRAFFIC_LEVEL] = {"Traffic: Amount",
                           "Maximum number of traffic cars. Saved immediately, but only applies after "
                           "restarting Azimuth (close the game completely and start it again)."},
    [RIG_TRAFFIC_ALL_DAY] = {"Traffic: Rush Hour All Day",
                             "Full traffic density at any time of day instead of 7:00-19:00 only. "
                             "Applies immediately."},
    [RIG_WEATHER] = {"Weather",
                     "Fixed weather in free roam: Sunny, Cloudy, Storm Clouds, Rain or Fog (fog "
                     "only on Storm Island, sunny elsewhere). Original = the game's normal "
                     "weather changes. Races and meets with their own weather keep it; your "
                     "choice returns afterwards. Applies after closing the menu."},
    [RIG_TIME_OF_DAY] = {"Time of Day",
                         "Fixed time of day in free roam: Morning, Midday, Sunset or Night. Slow "
                         "Day = the clock keeps running, four times slower. Original = the game's "
                         "normal day and night. Races and events keep their own time; your "
                         "choice returns afterwards. Online (FH2MP) the time comes from the "
                         "session. Applies after closing the menu."},
    [RIG_TRAFFIC_PRESET] = {"Traffic: Preset",
                            "Sets amount and rush hour in one go: Original, Calm (24), Busy (48), "
                            "Rush Hour (64, all day) or Chaos (100, all day). Changing amount or "
                            "rush hour afterwards shows Custom. Saved immediately, the amount only "
                            "applies after restarting Azimuth."},
    [RIG_WHEEL_PROFILE] = {"Input: Profile",
                           "Sets wheel in game, keyboard and clutch pedal as brake in one go: "
                           "Controller, Keyboard, Wheel or Wheel with the clutch pedal as brake. "
                           "Changing one of the switches afterwards shows Custom. Wheel and "
                           "keyboard only apply after restarting the game. Set up wheel: cards "
                           "ask for steering, pedals and buttons - for any wheel."},
    [RIG_WHEEL_FFB_MODE] = {"Wheel: Force Feedback Type",
                            "Game = FH2's original wheel programs, exactly like on the Xbox: "
                            "aligning torque, steering weight, damping, rumble and the centring in "
                            "menus come from the game. Own = the mod's old force feedback. Applies "
                            "immediately."},
    [RIG_WHEEL_CLUTCH_BRAKE] = {"Wheel: Clutch Pedal as Brake",
                                "The clutch pedal brakes, the clutch is off then. Handy because the "
                                "G920 brake pedal is very stiff. Idea: Lancaster. Applies immediately."},
    [RIG_KEY_ENABLED] = {"Keyboard: Keyboard",
                         "The keyboard controls the game like a gamepad: W/S throttle and brake, "
                         "A/D steer, Space handbrake, E/Q shift, arrow keys = camera while driving "
                         "and scrolling in menus, Enter/Backspace/Esc in menus. Bindings in "
                         "FH2_Keyboard.ini. Controllers keep working. While a wheel is plugged "
                         "in, the game drives with the wheel only. Restart the game to apply."},
    [RIG_KEY_STEER] = {"Keyboard: Steering Time (ms)",
                       "Time to full lock while you hold A or D. Higher = smoother. Applies "
                       "immediately."},
    [RIG_KEY_RETURN] = {"Keyboard: Return Time (ms)",
                        "Time until the steering is straight again after letting go. Applies "
                        "immediately."},
    [RIG_KEY_PEDAL] = {"Keyboard: Pedal Time (ms)",
                       "Time to full throttle or full brake with W or S. 0 = instant. Applies "
                       "immediately."},
    [RIG_GRIP_STEERING] = {"Cockpit: Full Steering Animation",
                           "In the cockpit the hands are hidden and the steering wheel turns as "
                           "far as your real one (rotation). Off = original hands and wheel. "
                           "Restart the game to apply."},
    [RIG_GRIP_NOTIFICATIONS] = {"Gripline: Notifications",
                                "Small Gripline cards at the top right of the picture, e.g. when "
                                "something was saved or nitro is off online. Also in fullscreen. "
                                "Applies immediately."},
    [RIG_GRIP_VIEW_FORWARD] = {"Cockpit View 2: Forward (cm)",
                            "Second cockpit view like in FH5: this many centimetres closer to the "
                            "gauges. Camera button: cockpit, cockpit 2, next camera (also V or a free wheel button). "
                            "The steering wheel disappears there. "
                            "Applies immediately."},
    [RIG_GRIP_VIEW_UP] = {"Cockpit View 2: Up (cm)",
                          "You sit this many centimetres higher in the second cockpit view "
                          "(negative = lower). Applies immediately."},
    [RIG_GRIP_REPORT] = {"Gripline: Create Problem Report",
                         "Set to On: packs logs and settings into Gripline_Report.zip next to "
                         "azimuth.exe. Attach that file to a GitHub issue when something goes "
                         "wrong. Nothing is sent anywhere."},
};

static const wchar_t *const profile_german[RIG_PROFILE_COUNT] = {
    L"Eigene Werte", L"Controller", L"Tastatur", L"Lenkrad", L"Lenkrad (Kupplung = Bremse)", L"Lenkrad einrichten (Pedale, Tasten)"};
static const wchar_t *const profile_english[RIG_PROFILE_COUNT] = {
    L"Custom", L"Controller", L"Keyboard", L"Wheel", L"Wheel (Clutch as Brake)", L"Set up wheel (pedals, buttons)"};

static const wchar_t *const density_german[RIG_DENSITY_COUNT] = {
    L"Eigene Werte", L"Original", L"Ruhig", L"Lebhaft", L"Rushhour", L"Chaos"};
static const wchar_t *const density_english[RIG_DENSITY_COUNT] = {
    L"Custom", L"Original", L"Calm", L"Busy", L"Rush Hour", L"Chaos"};

static const wchar_t *const weather_german[RIG_WEATHER_COUNT] = {
    L"Original", L"Sonnig", L"Bewölkt", L"Gewitterwolken", L"Regen", L"Nebel"};
static const wchar_t *const weather_english[RIG_WEATHER_COUNT] = {
    L"Original", L"Sunny", L"Cloudy", L"Storm Clouds", L"Rain", L"Fog"};

static const wchar_t *const time_german[RIG_TIME_COUNT] = {
    L"Original", L"Morgen", L"Mittag", L"Sonnenuntergang", L"Nacht", L"Langsamer Tag"};
static const wchar_t *const time_english[RIG_TIME_COUNT] = {
    L"Original", L"Morning", L"Midday", L"Sunset", L"Night", L"Slow Day"};

static const wchar_t *const ffb_mode_german[RIG_FFB_MODE_COUNT] = {L"Spiel (original)", L"Eigenes (alt)"};
static const wchar_t *const ffb_mode_english[RIG_FFB_MODE_COUNT] = {L"Game (original)", L"Own (old)"};

const wchar_t *rig_ffb_mode_choice(unsigned char *game_image, unsigned index) {
    if (index >= RIG_FFB_MODE_COUNT) {
        return L"";
    }
    return video_options_text_is_german(game_image) ? ffb_mode_german[index] : ffb_mode_english[index];
}

const wchar_t *rig_weather_choice(unsigned char *game_image, unsigned index) {
    if (index >= RIG_WEATHER_COUNT) {
        return L"";
    }
    return video_options_text_is_german(game_image) ? weather_german[index]
                                                     : weather_english[index];
}

const wchar_t *rig_time_choice(unsigned char *game_image, unsigned index) {
    if (index >= RIG_TIME_COUNT) {
        return L"";
    }
    return video_options_text_is_german(game_image) ? time_german[index] : time_english[index];
}

const wchar_t *rig_density_choice(unsigned char *game_image, unsigned index) {
    if (index >= RIG_DENSITY_COUNT) {
        return L"";
    }
    return video_options_text_is_german(game_image) ? density_german[index]
                                                     : density_english[index];
}

const wchar_t *rig_profile_choice(unsigned char *game_image, unsigned index) {
    if (index >= RIG_PROFILE_COUNT) {
        return L"";
    }
    return video_options_text_is_german(game_image) ? profile_german[index]
                                                     : profile_english[index];
}

const wchar_t *rig_function_choice(unsigned char *game_image, unsigned index) {
    return wheel_function_name(game_image, index);
}

static const wchar_t *const traffic_german[6] = {L"Original (~14)", L"24 Autos", L"36 Autos",
                                                  L"48 Autos",       L"64 Autos", L"150 Autos"};
static const wchar_t *const traffic_english[6] = {L"Original (~14)", L"24 Cars", L"36 Cars",
                                                   L"48 Cars",        L"64 Cars", L"150 Cars"};

static const RigText *texts(unsigned char *game_image) {
    return video_options_text_is_german(game_image) ? german : english;
}

const char *rig_setting_heading(unsigned char *game_image, unsigned id) {
    return id < RIG_SETTING_COUNT ? texts(game_image)[id].heading : "";
}

const char *rig_setting_description(unsigned char *game_image, unsigned id) {
    return id < RIG_SETTING_COUNT ? texts(game_image)[id].description : "";
}

const wchar_t *rig_traffic_choice(unsigned char *game_image, unsigned index) {
    if (index >= 6) {
        return L"";
    }
    return video_options_text_is_german(game_image) ? traffic_german[index]
                                                     : traffic_english[index];
}

/* ------------------------------------------------------------------ values */

static wchar_t traffic_ini[PATH_CAPACITY];
static volatile LONG64 dirty_wheel, dirty_traffic, dirty_keyboard, dirty_gripline;
static volatile ULONGLONG save_after;
static SRWLOCK file_lock = SRWLOCK_INIT;
static int started;

static int is_keyboard(unsigned id) {
    return id >= RIG_KEY_FIRST && id < RIG_KEY_END;
}

static int is_gripline(unsigned id) {
    return id >= RIG_GRIP_FIRST && id < RIG_GRIP_END;
}

static volatile LONG gripline_values[RIG_GRIP_END - RIG_GRIP_FIRST] = {1, 0, 35, -5};

void rig_settings_gripline(int notifications, int steering_animation, int view_forward, int view_up) {
    gripline_values[RIG_GRIP_NOTIFICATIONS - RIG_GRIP_FIRST] = notifications ? 1 : 0;
    gripline_values[RIG_GRIP_STEERING - RIG_GRIP_FIRST] = steering_animation ? 1 : 0;
    gripline_values[RIG_GRIP_VIEW_FORWARD - RIG_GRIP_FIRST] = view_forward;
    gripline_values[RIG_GRIP_VIEW_UP - RIG_GRIP_FIRST] = view_up;
    cockpit_view_set_offset(view_forward, view_up);
}

static int stored_value(unsigned id) {
    if (id < RIG_WHEEL_COUNT) {
        return wheel_bridge_setting(id);
    }
    if (is_keyboard(id)) {
        return keyboard_setting(id - RIG_KEY_FIRST);
    }
    if (is_gripline(id)) {
        return (int)gripline_values[id - RIG_GRIP_FIRST];
    }
    if (id == RIG_TIME_OF_DAY) {
        return time_of_day_setting();
    }
    return id == RIG_WEATHER ? weather_setting() : traffic_setting(id);
}

static int is_wheel(unsigned id) {
    return id < RIG_WHEEL_COUNT;
}

static volatile LONG64 *dirty_mask(unsigned id) {
    return is_wheel(id) ? &dirty_wheel : is_keyboard(id) ? &dirty_keyboard
           : is_gripline(id) ? &dirty_gripline : &dirty_traffic;
}

int rig_setting_get(unsigned id) {
    if (id >= RIG_SETTING_COUNT) {
        return 0;
    }
    if (id == RIG_WHEEL_PROFILE) {
        return profile_current();
    }
    if (id == RIG_TRAFFIC_PRESET) {
        return density_current();
    }
    if (id == RIG_GRIP_REPORT) {
        return 0;
    }
    int value = stored_value(id);
    /* the INI may hold values outside the menu range; show them clamped */
    if (value < infos[id].minimum) {
        value = infos[id].minimum;
    }
    if (value > infos[id].maximum) {
        value = infos[id].maximum;
    }
    return value;
}

static void forward(unsigned id, int value) {
    if (is_wheel(id)) {
        wheel_bridge_set_setting(id, value);
    } else if (is_keyboard(id)) {
        keyboard_set_setting(id - RIG_KEY_FIRST, value);
    } else if (is_gripline(id)) {
        LONG before = InterlockedExchange(&gripline_values[id - RIG_GRIP_FIRST], value);
        if (id == RIG_GRIP_NOTIFICATIONS) {
            notify_set_enabled(value);
            if (value && !before) notify_show("Notifications on");
        } else if (id == RIG_GRIP_VIEW_FORWARD || id == RIG_GRIP_VIEW_UP) {
            cockpit_view_set_offset((int)gripline_values[RIG_GRIP_VIEW_FORWARD - RIG_GRIP_FIRST],
                                    (int)gripline_values[RIG_GRIP_VIEW_UP - RIG_GRIP_FIRST]);
        } else if (before != value) {
            notify_show(value ? "Steering animation on\nApplies after a restart"
                              : "Steering animation off\nApplies after a restart");
        }
    } else {
        if (id == RIG_WEATHER) {
            weather_set_setting(value);
        } else if (id == RIG_TIME_OF_DAY) {
            time_of_day_set_setting(value);
        } else {
            traffic_set_setting(id, value);
        }
    }
}

int rig_setting_set(unsigned id, int value) {
    if (id >= RIG_SETTING_COUNT || value < infos[id].minimum || value > infos[id].maximum) {
        return 0;
    }
    if (id == RIG_WHEEL_PROFILE) {
        /* "Eigene Werte" is only a label: picking it keeps the values as they are */
        if (value == 0 || value == profile_current()) {
            return 0;
        }
        if (value == PROFILE_SETUP) { /* nothing is stored; the spinner shows the current profile again */
            if (!wheel_bridge_start_setup())
                notify_show("Wheel setup: no wheel active\nChoose the profile Wheel and restart first");
            return 0;
        }
        int changed = 0;
        for (unsigned i = 0; i < 3; ++i) {
            if (profile_presets[value][i] >= 0) changed |= rig_setting_set(profile_ids[i], profile_presets[value][i]);
        }
        notify_show(value == 1   ? "Profile: Controller\nApplies after a restart"
                    : value == 2 ? "Profile: Keyboard\nApplies after a restart"
                    : value == 3 ? "Profile: Wheel\nApplies after a restart"
                                 : "Profile: Wheel, clutch as brake\nApplies after a restart");
        return changed;
    }
    if (id == RIG_GRIP_REPORT) { /* an action, not a setting: the row goes back to Off */
        if (value == 1) report_create();
        return 0;
    }
    if (id == RIG_TRAFFIC_PRESET) {
        if (value == 0 || value == density_current()) {
            return 0;
        }
        int changed = rig_setting_set(RIG_TRAFFIC_LEVEL, density_presets[value][0]);
        changed |= rig_setting_set(RIG_TRAFFIC_ALL_DAY, density_presets[value][1]);
        return changed;
    }
    int current = stored_value(id);
    if (current == value) {
        return 0;
    }
    forward(id, value);
    save_after = GetTickCount64() + SAVE_DELAY_MS;
    InterlockedOr64(dirty_mask(id), (LONG64)(1ull << id));
    return 1;
}

/* ------------------------------------------------------------------ files */

static char file_buffer[FILE_LIMIT + 1], edit_buffer[2][FILE_LIMIT + 1];
static wchar_t temp_path[PATH_CAPACITY];

static int read_file(const wchar_t *path, char *buffer, size_t capacity, size_t *length,
                     int *missing) {
    *length = 0;
    *missing = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *missing = GetLastError() == ERROR_FILE_NOT_FOUND;
        return *missing;
    }
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
             (ULONGLONG)size.QuadPart < capacity &&
             ReadFile(file, buffer, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart;
    CloseHandle(file);
    if (ok) {
        *length = got;
        buffer[got] = 0;
    }
    return ok;
}

static int modified_time(const wchar_t *path, ULONGLONG *time) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!path || !path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        *time = 0;
        return 0;
    }
    *time = ((ULONGLONG)data.ftLastWriteTime.dwHighDateTime << 32) |
            data.ftLastWriteTime.dwLowDateTime;
    return 1;
}

static int write_file(const wchar_t *path, const char *data, size_t length) {
    size_t n = 0;
    while (path[n] && n < PATH_CAPACITY - 8) {
        temp_path[n] = path[n];
        ++n;
    }
    static const wchar_t suffix[] = L".eotmp";
    for (unsigned i = 0; i < ARRAY_COUNT(suffix); ++i) {
        temp_path[n + i] = suffix[i];
    }
    HANDLE file = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length &&
             FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) {
        ok = MoveFileExW(temp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!ok) {
        DeleteFileW(temp_path);
    }
    return ok;
}

typedef struct {
    const wchar_t *path;
    const char *section;
    unsigned first, end; /* setting ids stored in this file */
    size_t limit;
    int create_missing;
    ULONGLONG known_time;
    const char *name;
    int error_logged;
} RigFile;

static RigFile files[4] = {
    {NULL, WHEEL_SECTION, 0, RIG_WHEEL_COUNT, FILE_LIMIT, 1, 0, "FH2_Wheel.ini", 0},
    /* never create the Extra Options INI: its absence means "all defaults" */
    {traffic_ini, TRAFFIC_SECTION, RIG_TRAFFIC_LEVEL, RIG_TIME_OF_DAY + 1, EO_INI_LIMIT, 0, 0,
     "FH2_ExtraOptions.ini", 0},
    {NULL, KEYBOARD_SECTION, RIG_KEY_FIRST, RIG_KEY_END, FILE_LIMIT, 1, 0, "FH2_Keyboard.ini", 0},
    {traffic_ini, GRIPLINE_SECTION, RIG_GRIP_FIRST, RIG_GRIP_END, EO_INI_LIMIT, 0, 0,
     "FH2_ExtraOptions.ini", 0},
};

static void log_line(const char *first, const char *second);

/* A failed save is retried a few seconds later; it is only logged once. */
static int save_failed(RigFile *f, const char *message) {
    if (!f->error_logged) {
        f->error_logged = 1;
        log_line(message, f->name);
    }
    return 0;
}

static void log_line(const char *first, const char *second) {
    char line[160];
    size_t n = 0;
    for (const char *p = first; *p && n < sizeof(line) - 4; ++p) line[n++] = *p;
    for (const char *p = second; *p && n < sizeof(line) - 4; ++p) line[n++] = *p;
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    log_append(line);
}

/* Writes the current values of the dirty settings into the file. */
static int save_file(RigFile *f, LONG64 dirty) {
    if (!f->path || !f->path[0] || !dirty) {
        return 1;
    }
    size_t length;
    int missing;
    if (!read_file(f->path, file_buffer, f->limit + 1, &length, &missing)) {
        return save_failed(f, "MENU: could not read (retrying) ");
    }
    if (missing && !f->create_missing) {
        return 1;
    }
    const char *source = file_buffer;
    size_t source_length = length;
    unsigned slot = 0;
    for (unsigned id = f->first; id < f->end; ++id) {
        if (!(dirty & (LONG64)(1ull << id))) {
            continue;
        }
        char number[12];
        int value = stored_value(id);
        size_t written = ini_set_value(source, source_length, f->section, infos[id].key,
                                       ini_format_int(value, number), edit_buffer[slot],
                                       f->limit + 1);
        if (!written || written > f->limit) {
            log_line("MENU: not saved, the file would become too large: ", f->name);
            return 1; /* permanent: do not retry */
        }
        source = edit_buffer[slot];
        source_length = written;
        slot ^= 1;
    }
    if (source == file_buffer) {
        return 1;
    }
    if (!write_file(f->path, source, source_length)) {
        return save_failed(f, "MENU: could not save (retrying) ");
    }
    modified_time(f->path, &f->known_time);
    if (f->error_logged) {
        f->error_logged = 0;
        log_line("MENU: saved again: ", f->name);
    }
    return 1;
}

/* The launcher (or an editor) changed the file: take over its values, except for settings
   that were just changed in the menu and still wait to be saved. */
static void reload_file(RigFile *f, LONG64 pending) {
    size_t length;
    int missing;
    if (!f->path || !read_file(f->path, file_buffer, FILE_LIMIT + 1, &length, &missing) ||
        missing) {
        return;
    }
    int changed = 0;
    for (unsigned id = f->first; id < f->end; ++id) {
        int value;
        if ((pending & (LONG64)(1ull << id)) ||
            !ini_find_int(file_buffer, length, f->section, infos[id].key, &value) ||
            value < infos[id].minimum || value > infos[id].maximum) {
            continue;
        }
        int current = stored_value(id);
        if (current != value) {
            forward(id, value);
            changed = 1;
        }
    }
    if (changed) {
        log_line("MENU: values reloaded from ", f->name);
    }
}

static ULONGLONG last_check;

static void saver_step(ULONGLONG now) {
    AcquireSRWLockExclusive(&file_lock);
    volatile LONG64 *masks[4] = {&dirty_wheel, &dirty_traffic, &dirty_keyboard, &dirty_gripline};
    _Static_assert(ARRAY_COUNT(files) == 4, "one dirty mask per file");
    if ((dirty_wheel || dirty_traffic || dirty_keyboard || dirty_gripline) && now >= save_after) {
        for (unsigned i = 0; i < ARRAY_COUNT(files); ++i) {
            LONG64 dirty = InterlockedExchange64(masks[i], 0);
            if (!save_file(&files[i], dirty)) {
                InterlockedOr64(masks[i], dirty);
                save_after = now + 5000; /* retry later, e.g. while the file is locked */
            }
        }
    }
    if (now - last_check >= 1000) {
        last_check = now;
        for (unsigned i = 0; i < ARRAY_COUNT(files); ++i) {
            ULONGLONG time;
            LONG64 pending = *masks[i];
            if (modified_time(files[i].path, &time) && time != files[i].known_time) {
                files[i].known_time = time;
                reload_file(&files[i], pending);
            }
        }
    }
    ReleaseSRWLockExclusive(&file_lock);
}

static DWORD WINAPI saver_thread(LPVOID unused) {
    (void)unused;
    for (;;) {
        Sleep(100);
        saver_step(GetTickCount64());
    }
    return 0;
}

/* Process exit: save changes from the last moments (the saver thread is gone already). */
void rig_settings_flush(void) {
    if (!started) {
        return;
    }
    save_file(&files[0], InterlockedExchange64(&dirty_wheel, 0));
    save_file(&files[1], InterlockedExchange64(&dirty_traffic, 0));
    save_file(&files[2], InterlockedExchange64(&dirty_keyboard, 0));
    save_file(&files[3], InterlockedExchange64(&dirty_gripline, 0));
}

void rig_settings_start(const wchar_t *settings_directory) {
    if (started || !settings_directory ||
        !path_join(traffic_ini, settings_directory, MOD_INI_NAME)) {
        return;
    }
    files[0].path = wheel_bridge_ini_path();
    files[2].path = keyboard_ini_path();
    for (unsigned i = 0; i < ARRAY_COUNT(files); ++i) {
        modified_time(files[i].path, &files[i].known_time);
    }
    HANDLE thread = CreateThread(NULL, 0, saver_thread, NULL, 0, NULL);
    if (!thread) {
        log_append("MENU: Gripline - could not start the save thread; menu changes stay "
                   "until the game closes.\r\n");
        return;
    }
    CloseHandle(thread);
    started = 1;
    log_append("MENU: Gripline page ready (HUD options); changes apply live and are "
               "saved to FH2_Wheel.ini / FH2_Keyboard.ini / FH2_ExtraOptions.ini.\r\n");
}

#ifdef FH2_TWEAKS_TEST
#include "features.h"
#include "settings.h"
__declspec(dllexport) int Fh2TestRigStart(unsigned char *image, const wchar_t *executable,
                                          const wchar_t *directory) {
    ModOptions options;
    ini_migrate(directory);
    int ok = settings_read(directory, &options);
    log_set_directory(directory);
    traffic_tunables_start(image, &options);
    weather_start(image, &options);
    time_of_day_start(image, &options);
    wheel_bridge_start(image, executable, directory);
    keyboard_pad_game_start(image, directory);
    rig_settings_start(directory);
    return ok ? (started ? 1 : 2) : 0;
}
__declspec(dllexport) void Fh2TestRigStep(ULONGLONG now) {
    saver_step(now);
}
__declspec(dllexport) void Fh2TestRigFlush(void) {
    rig_settings_flush();
}
__declspec(dllexport) int Fh2TestRigGet(unsigned id) {
    return rig_setting_get(id);
}
__declspec(dllexport) int Fh2TestRigSet(unsigned id, int value) {
    return rig_setting_set(id, value);
}
#endif

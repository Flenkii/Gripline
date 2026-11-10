#ifndef GRIPLINE_GAME_ADDRESSES_H
#define GRIPLINE_GAME_ADDRESSES_H
/* Offsets (RVAs) into forza_durango_release_final.exe 1.0.0.64 that Gripline's own code uses.
   Found by reading the game's code; add the image base to get the live address. */

/* global pointers (read, never written) */
#define FH2_RVA_TEXT_TABLES 0x3662990u   /* game_language.c: language / text tables object */
#define FH2_RVA_PLAYER_MANAGER 0x34aba60u /* keyboard_pad.c, noclip.c, summon.c: player manager */

/* vtables, used to recognise an object by its type */
#define FH2_RVA_PLAYER_CAR_VTABLE 0x2c5cb80u
#define FH2_RVA_ROAD_BINDINGS_VTABLE 0x2f5d850u

/* functions we call */
#define FH2_RVA_GET_LOCAL_PLAYER 0x21e0c0u
#define FH2_RVA_GET_PLAYER_CAR 0x2da060u
#define FH2_RVA_NEAREST_ROAD 0xf5db40u /* summon.c: snap a point to the nearest road */

#endif

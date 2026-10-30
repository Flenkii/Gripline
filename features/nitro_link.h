#ifndef FH2_NITRO_LINK_H
#define FH2_NITRO_LINK_H

/* Nitro key for NitroSystem (qiangqiang101, separate mod, MIT).

   Gripline does not contain any nitro code. NitroSystem (fh2-mods\FnFNitro.dll, loaded by the
   FH2 Mod Platform) polls XInputGetState for its NitroButton. Wheels and the keyboard are no
   XInput devices, so they cannot press it. While the Gripline nitro key (FH2_Keyboard.ini
   Nitro=) or a wheel button with the function "Nitro" is held, Gripline reports NitroSystem's
   NitroButton as pressed - only in FnFNitro.dll's own XInput import, the game sees nothing. */

enum { NITRO_SOURCE_KEYBOARD = 1, NITRO_SOURCE_WHEEL = 2 };

/* Called by the keyboard and the wheel poll: source held or released. Also looks for
   FnFNitro.dll (at most once per second) and links to it. */
void nitro_link_set(unsigned source, int held);

/* Game mapped: enables the online check (nitro is switched off in an FH2MP session). */
void nitro_link_start(unsigned char *game_image);

/* 1 while a network session (FH2MP) is up. Also used by noclip.c. Safe from any thread. */
int game_session_online(void);

#endif

#ifndef FH2_AUTODRIVE_UI_H
#define FH2_AUTODRIVE_UI_H
#include "extra_options.h"

void autodrive_ui_prepare(unsigned char *image);
void autodrive_ui_set_hidden(int hidden);
/* Creator mode: hides the UI while driving; menus and loading screens stay visible. */
void autodrive_ui_set_creator(int on);
void autodrive_ui_draw(void *application, void *screen, void *transform);
void autodrive_ui_present(void *application, void *transform);
void autodrive_ui_wait_render(void *renderer, void *surface, void *parameters);
#endif

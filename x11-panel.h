#ifndef QINGYIN_X11_PANEL_H
#define QINGYIN_X11_PANEL_H
#include <stdbool.h>

void x11_panel_show(int x, int y, int caret_height, const char *const *items, int count, int selected, bool dark);
void x11_panel_move(int x, int y, int caret_height);
void x11_panel_hide(void);
void x11_panel_close(void);
#endif

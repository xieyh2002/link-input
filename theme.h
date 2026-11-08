#ifndef QINGYIN_THEME_H
#define QINGYIN_THEME_H
#include <gio/gio.h>

typedef struct {
    GDBusConnection *bus;
    guint subscription;
    gboolean dark;
    void (*repaint)(gpointer);
    gpointer data;
} Theme;

void theme_start(Theme *theme, void (*repaint)(gpointer), gpointer data);
void theme_stop(Theme *theme);
#endif

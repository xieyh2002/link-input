#include "x11-panel.h"
#include "panel.h"
#include "settings.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <cairo/cairo-xlib.h>
#include <glib.h>

static Display *display;
static Window window;
static int screen;
static gboolean mapped;

static gboolean open_panel(void)
{
    if (display) return TRUE;
    display = XOpenDisplay(NULL);
    if (!display) return FALSE;
    screen = DefaultScreen(display);
    XSetWindowAttributes attrs = {.override_redirect = True, .save_under = True};
    window = XCreateWindow(display, RootWindow(display, screen), 0, 0, 1, 1, 0,
                           CopyFromParent, InputOutput, CopyFromParent,
                           CWOverrideRedirect | CWSaveUnder, &attrs);
    XWMHints hints = {.flags = InputHint, .input = False};
    XSetWMHints(display, window, &hints);
    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom tooltip = XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    XChangeProperty(display, window, type, XA_ATOM, 32, PropModeReplace,
                    (const unsigned char *)&tooltip, 1);
    return TRUE;
}

void x11_panel_show(int x, int y, int caret_height, const char *const *items, int count, int selected, bool dark)
{
    if (g_getenv("QINGYIN_NO_COMPAT_PANEL") || !count || !open_panel()) { x11_panel_hide(); return; }
    int w, h;
    panel_size(items, count, selected == PANEL_STATUS, &w, &h);
    cairo_surface_t *image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    unsigned char *pixels = cairo_image_surface_get_data(image);
    int stride = cairo_image_surface_get_stride(image);
    panel_draw(pixels, w, h, stride, 1, items, count, selected, dark);
    cairo_surface_mark_dirty(image);

    Pixmap pixmap = XCreatePixmap(display, window, w, h, DefaultDepth(display, screen));
    cairo_surface_t *target = cairo_xlib_surface_create(display, pixmap,
                                                        DefaultVisual(display, screen), w, h);
    cairo_t *cr = cairo_create(target);
    link_color(cr, link_palette(dark)->background);
    cairo_paint(cr);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(target);

    int bit_stride = (w + 7) / 8;
    g_autofree unsigned char *mask = g_malloc0((gsize)bit_stride * h);
    for (int row = 0; row < h; ++row) {
        const guint32 *p = (const guint32 *)(pixels + row * stride);
        for (int col = 0; col < w; ++col)
            if ((p[col] >> 24) >= 128)
                mask[row * bit_stride + col / 8] |= 1u << (col % 8);
    }
    cairo_surface_destroy(image);
    Pixmap shape = XCreateBitmapFromData(display, window, (const char *)mask, w, h);
    int screen_w = DisplayWidth(display, screen), screen_h = DisplayHeight(display, screen);
    int top = y + CLAMP(caret_height, 0, 40) + 3;
    if (top + h > screen_h) top = y - h - 3;
    XMoveResizeWindow(display, window, CLAMP(x, 0, MAX(0, screen_w - w)),
                       CLAMP(top, 0, MAX(0, screen_h - h)), w, h);
    XShapeCombineMask(display, window, ShapeBounding, 0, 0, shape, ShapeSet);
    XShapeCombineRectangles(display, window, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XFreePixmap(display, shape);
    XSetWindowBackgroundPixmap(display, window, pixmap);
    XFreePixmap(display, pixmap);
    XClearWindow(display, window);
    if (!mapped) {
        XMapRaised(display, window);
        mapped = TRUE;
    }
    XFlush(display);
}

void x11_panel_hide(void)
{
    if (!display || !mapped) return;
    XUnmapWindow(display, window);
    mapped = FALSE;
    XFlush(display);
}

void x11_panel_close(void)
{
    if (!display) return;
    XCloseDisplay(display);
    display = NULL;
    mapped = FALSE;
}

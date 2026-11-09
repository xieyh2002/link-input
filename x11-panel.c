#include "x11-panel.h"
#include "panel.h"
#include "settings.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>
#include <cairo/cairo-xlib.h>
#include <glib.h>
#include <math.h>

static Display *display;
static Window window;
static int screen;
static Visual *visual;
static int depth;
static gboolean transparent;
static double panel_scale = 1;
static Atom active_window;
static gboolean mapped;
static PanelContent content;
static int width, height, left, top;

static gboolean open_panel(void)
{
    if (display) return TRUE;
    display = XOpenDisplay(NULL);
    if (!display) return FALSE;
    screen = DefaultScreen(display);
    visual = DefaultVisual(display, screen);
    depth = DefaultDepth(display, screen);
    XSetWindowAttributes attrs = {.override_redirect = True, .save_under = True};
    unsigned long mask = CWOverrideRedirect | CWSaveUnder;
    int opcode, event, error;
    char selection[32];
    g_snprintf(selection, sizeof selection, "_NET_WM_CM_S%d", screen);
    gboolean xwayland = XQueryExtension(display, "XWAYLAND", &opcode, &event, &error);
    const char *resources = xwayland ? XResourceManagerString(display) : NULL;
    if (resources) {
        XrmInitialize();
        XrmDatabase db = XrmGetStringDatabase(resources);
        char *type;
        XrmValue value;
        if (db && XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value)) {
            double dpi = g_ascii_strtod(value.addr, NULL);
            if (dpi >= 48 && dpi <= 768) panel_scale = dpi / 96;
        }
        if (db) XrmDestroyDatabase(db);
    }
    if (XRenderQueryExtension(display, &event, &error) && (xwayland ||
        XGetSelectionOwner(display, XInternAtom(display, selection, False)))) {
        XVisualInfo query = {.screen = screen, .depth = 32, .class = TrueColor};
        int count = 0;
        XVisualInfo *visuals = XGetVisualInfo(display, VisualScreenMask | VisualDepthMask |
                                               VisualClassMask, &query, &count);
        for (int i = 0; i < count; ++i) {
            XRenderPictFormat *format = XRenderFindVisualFormat(display, visuals[i].visual);
            if (!format || format->type != PictTypeDirect || !format->direct.alphaMask) continue;
            visual = visuals[i].visual;
            depth = visuals[i].depth;
            attrs.colormap = XCreateColormap(display, RootWindow(display, screen), visual, AllocNone);
            mask |= CWColormap | CWBorderPixel;
            transparent = TRUE;
            break;
        }
        if (visuals) XFree(visuals);
    }
    window = XCreateWindow(display, RootWindow(display, screen), 0, 0, 1, 1, 0,
                           depth, InputOutput, visual, mask, &attrs);
    active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    XWMHints hints = {.flags = InputHint, .input = False};
    XSetWMHints(display, window, &hints);
    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom tooltip = XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    XChangeProperty(display, window, type, XA_ATOM, 32, PropModeReplace,
                    (const unsigned char *)&tooltip, 1);
    XShapeCombineRectangles(display, window, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    return TRUE;
}

static int ignore_stale_window(Display *dpy, XErrorEvent *event)
{
    return 0;
}

static int caret_bottom_limit(int x, int y, int screen_h)
{
    Window root = RootWindow(display, screen), client = None;
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *value = NULL;
    if (XGetWindowProperty(display, root, active_window, 0, 1, False, XA_WINDOW,
                           &type, &format, &count, &remaining, &value) == Success &&
        type == XA_WINDOW && format == 32 && count == 1)
        client = *(Window *)value;
    if (value) XFree(value);
    if (!client) return screen_h;

    XWindowAttributes attrs;
    Window child;
    int client_x, client_y;
    // The focused window can close before we query its geometry.
    XErrorHandler previous = XSetErrorHandler(ignore_stale_window);
    gboolean found = XGetWindowAttributes(display, client, &attrs) &&
        XTranslateCoordinates(display, client, root, 0, 0, &client_x, &client_y, &child);
    XSetErrorHandler(previous);
    if (found && x >= client_x && x < client_x + attrs.width &&
        y >= client_y && y < client_y + attrs.height)
        return MIN(screen_h, client_y + attrs.height);
    return screen_h;
}

static bool position_panel(int x, int y, int caret_height, int w, int h)
{
    int screen_w = DisplayWidth(display, screen), screen_h = DisplayHeight(display, screen);
    int next_left = CLAMP(x, 0, MAX(0, screen_w - w));
    int next_top = y + CLAMP(caret_height, 0, 40) + 3;
    int above = y - h - 3;
    // Leave room for controls below a composer near the bottom of its window.
    if (next_top + h > screen_h ||
        (above >= 0 && next_top + 2 * h > caret_bottom_limit(x, y, screen_h)))
        next_top = above;
    next_top = CLAMP(next_top, 0, MAX(0, screen_h - h));
    if (w != width || h != height)
        XMoveResizeWindow(display, window, next_left, next_top, w, h);
    else if (left != next_left || top != next_top)
        XMoveWindow(display, window, next_left, next_top);
    else return false;
    width = w;
    height = h;
    left = next_left;
    top = next_top;
    return true;
}

void x11_panel_move(int x, int y, int caret_height)
{
    if (mapped && position_panel(x, y, caret_height, width, height))
        XFlush(display);
}

void x11_panel_show(int x, int y, int caret_height, const char *const *items, int count, int selected, bool dark)
{
    if (g_getenv("QINGYIN_NO_COMPAT_PANEL") || !count || !open_panel()) { x11_panel_hide(); return; }
    if (!panel_content_update(&content, items, count, selected, 1, dark)) {
        position_panel(x, y, caret_height, width, height);
        if (!mapped) {
            XMapRaised(display, window);
            mapped = TRUE;
        }
        XFlush(display);
        return;
    }
    int logical_w, logical_h;
    panel_size(items, count, selected == PANEL_STATUS, &logical_w, &logical_h);
    int w = ceil(logical_w * panel_scale), h = ceil(logical_h * panel_scale);
    cairo_surface_t *image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    unsigned char *pixels = cairo_image_surface_get_data(image);
    int stride = cairo_image_surface_get_stride(image);
    panel_draw(pixels, logical_w, logical_h, stride, panel_scale, items, count, selected, dark);
    cairo_surface_mark_dirty(image);

    Pixmap pixmap = XCreatePixmap(display, window, w, h, depth);
    cairo_surface_t *target = cairo_xlib_surface_create(display, pixmap, visual, w, h);
    cairo_t *cr = cairo_create(target);
    if (transparent) {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    } else {
        link_color(cr, link_palette(dark)->background);
        cairo_paint(cr);
    }
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(target);

    position_panel(x, y, caret_height, w, h);
    if (!transparent) {
        int bit_stride = (w + 7) / 8;
        g_autofree unsigned char *mask = g_malloc0((gsize)bit_stride * h);
        for (int row = 0; row < h; ++row) {
            const guint32 *p = (const guint32 *)(pixels + row * stride);
            for (int col = 0; col < w; ++col)
                if ((p[col] >> 24) >= 128)
                    mask[row * bit_stride + col / 8] |= 1u << (col % 8);
        }
        Pixmap shape = XCreateBitmapFromData(display, window, (const char *)mask, w, h);
        XShapeCombineMask(display, window, ShapeBounding, 0, 0, shape, ShapeSet);
        XFreePixmap(display, shape);
    }
    cairo_surface_destroy(image);
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
    transparent = FALSE;
    panel_scale = 1;
    panel_content_clear(&content);
    width = height = left = top = 0;
}

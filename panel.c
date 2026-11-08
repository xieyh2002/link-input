#include "panel.h"
#include "settings.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <string.h>

enum {
    MARGIN = 2, INSET = 4, ITEM_HEIGHT = 31, ITEM_GAP = 2,
    ITEM_PAD = 8, END_PAD = 8, NUMBER_GAP = 3,
};

bool panel_content_update(PanelContent *content, const char *const *items,
                          int count, int selected, int scale, bool dark)
{
    bool changed = content->count != count || content->selected != selected ||
                   content->scale != scale || content->dark != dark;
    for (int i = 0; i < MAX(count, content->count); i++) {
        const char *item = i < count ? items[i] : NULL;
        if (!g_strcmp0(content->items[i], item)) continue;
        g_free(content->items[i]);
        content->items[i] = g_strdup(item);
        changed = true;
    }
    content->count = count;
    content->selected = selected;
    content->scale = scale;
    content->dark = dark;
    return changed;
}

void panel_content_clear(PanelContent *content)
{
    panel_content_update(content, NULL, 0, 0, 0, false);
}

static PangoLayout *new_layout(cairo_t *cr)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string("Noto Sans CJK SC, Noto Color Emoji, HanaMinB, BabelStone Han");
    pango_font_description_set_size(font, link_settings.font_size * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    return layout;
}

static int item_height(void)
{
    return ITEM_HEIGHT + 2 * (link_settings.font_size - LINK_DEFAULT_FONT_SIZE);
}

static int measure(PangoLayout *layout, const char *text)
{
    int width;
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_size(layout, &width, NULL);
    return width;
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 1.5 * G_PI);
    cairo_close_path(cr);
}

void panel_size(const char *const *candidates, int count, bool status, int *width, int *height)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(surface);
    PangoLayout *layout = new_layout(cr);
    *width = 2 * (MARGIN + INSET) + (status ? 0 : END_PAD);
    *height = item_height() + 2 * (MARGIN + INSET);
    for (int i = 0; i < count; ++i) {
        char number[16];
        g_snprintf(number, sizeof number, "%d.", i + 1);
        *width += (i ? ITEM_GAP : 0) + 2 * ITEM_PAD + (status ? 0 : measure(layout, number) + NUMBER_GAP)
                  + measure(layout, candidates[i]);
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

static void label(cairo_t *cr, PangoLayout *layout, const char *text, int x, int y)
{
    int height;
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_size(layout, NULL, &height);
    cairo_move_to(cr, x, y + (item_height() - height) / 2.0);
    pango_cairo_show_layout(cr, layout);
}

void panel_draw(unsigned char *pixels, int width, int height, int stride,
                int scale, const char *const *candidates, int count, int selected, bool dark)
{
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        pixels, CAIRO_FORMAT_ARGB32, width * scale, height * scale, stride);
    cairo_t *cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_scale(cr, scale, scale);

    rounded(cr, MARGIN, MARGIN, width - 2 * MARGIN, height - 2 * MARGIN, 11);
    const LinkPalette *palette = link_palette(dark);
    link_color(cr, palette->background);
    cairo_fill(cr);

    PangoLayout *layout = new_layout(cr);
    int x = MARGIN + INSET, y = MARGIN + INSET;
    for (int i = 0; i < count; ++i) {
        int text_width = measure(layout, candidates[i]);
        char number[16];
        g_snprintf(number, sizeof number, "%d.", i + 1);
        int number_width = selected == PANEL_STATUS ? 0 : measure(layout, number) + NUMBER_GAP;
        int item_width = 2 * ITEM_PAD + number_width + text_width;
        if (i == selected) {
            int left = i == 0 ? MARGIN : x - ITEM_GAP / 2;
            int right = i == count - 1 ? width - MARGIN : x + item_width + ITEM_GAP / 2;
            cairo_save(cr);
            rounded(cr, MARGIN, MARGIN, width - 2 * MARGIN, height - 2 * MARGIN, 11);
            cairo_clip(cr);
            cairo_rectangle(cr, left, MARGIN, right - left, height - 2 * MARGIN);
            link_color(cr, palette->selected);
            cairo_fill(cr);
            cairo_restore(cr);
        }

        uint32_t selection = palette->selected;
        unsigned brightness = 299 * ((selection >> 16) & 255) + 587 * ((selection >> 8) & 255) +
                              114 * (selection & 255);
        uint32_t highlight_text = brightness > 165000 ? 0x151923 : 0xffffff;
        link_color(cr, i == selected ? highlight_text : palette->text);
        if (selected != PANEL_STATUS) label(cr, layout, number, x + ITEM_PAD, y);
        label(cr, layout, candidates[i], x + ITEM_PAD + number_width, y);
        x += item_width + ITEM_GAP;
    }

    rounded(cr, MARGIN, MARGIN, width - 2 * MARGIN, height - 2 * MARGIN, 11);
    link_color(cr, palette->border);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

#ifdef PANEL_PREVIEW
int main(int argc, char **argv)
{
    link_settings_load();
    bool dark = argc > 1 && g_str_equal(argv[1], "--dark");
    const char *candidates[] = {"开心", "😄", "😃", "开新", "开", "𫔭", "凯"};
    const int scale = 2, count = G_N_ELEMENTS(candidates);
    int width, height;
    panel_size(candidates, count, false, &width, &height);
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width * scale, height * scale);
    panel_draw(cairo_image_surface_get_data(surface), width, height,
               cairo_image_surface_get_stride(surface), scale, candidates, count, 0, dark);
    cairo_surface_mark_dirty(surface);
    cairo_status_t status = cairo_surface_write_to_png(surface, dark ? "panel-preview-dark.png" : "panel-preview.png");
    cairo_surface_destroy(surface);
    return status != CAIRO_STATUS_SUCCESS;
}
#endif

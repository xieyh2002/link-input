#ifndef LINK_SETTINGS_H
#define LINK_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <cairo.h>

#define LINK_MAX_CANDIDATES 9
#define LINK_DEFAULT_FONT_SIZE 14

typedef enum { LINK_ZH, LINK_EN, LINK_JA } LinkLanguage;
typedef enum { LINK_SYSTEM, LINK_LIGHT, LINK_DARK } LinkTheme;
typedef struct {
    uint32_t background, text, selected, border;
} LinkPalette;
typedef struct {
    LinkLanguage language;
    bool double_pinyin, japanese;
    int candidates, font_size;
    LinkTheme theme;
    LinkPalette light, dark;
} LinkSettings;

extern LinkSettings link_settings;
void link_settings_load(void);
static inline const LinkPalette *link_palette(bool dark)
{
    return dark ? &link_settings.dark : &link_settings.light;
}
static inline void link_color(cairo_t *cr, uint32_t rgb)
{
    cairo_set_source_rgb(cr, ((rgb >> 16) & 255) / 255.0,
                             ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
}
#endif

#include "settings.h"
#include <glib.h>
#include <string.h>

#define DEFAULT_SETTINGS { .language = LINK_ZH, .japanese = true, .candidates = 7, .font_size = LINK_DEFAULT_FONT_SIZE, .theme = LINK_SYSTEM, \
    .light = {0xffffff, 0x1c293b, 0x007aff, 0xd1d1d1}, .dark = {0x1f2126, 0xf0f2f7, 0x007aff, 0x4d525e} }

LinkSettings link_settings = DEFAULT_SETTINGS;

static uint32_t read_color(GKeyFile *file, const char *group, const char *key, uint32_t fallback)
{
    g_autofree char *s = g_key_file_get_string(file, group, key, NULL);
    if (!s || s[0] != '#' || strlen(s) != 7) return fallback;
    uint32_t value = 0;
    for (int i = 1; i < 7; i++) {
        int digit = g_ascii_xdigit_value(s[i]);
        if (digit < 0) return fallback;
        value = value * 16 + digit;
    }
    return value;
}

static void read_palette(GKeyFile *file, const char *group, LinkPalette *p)
{
    p->background = read_color(file, group, "background", p->background);
    p->text = read_color(file, group, "text", p->text);
    p->selected = read_color(file, group, "selected", p->selected);
    p->border = read_color(file, group, "border", p->border);
}

void link_settings_load(void)
{
    link_settings = (LinkSettings)DEFAULT_SETTINGS;
    g_autofree char *path = g_build_filename(g_get_user_config_dir(), "link-input", "settings.ini", NULL);
    g_autoptr(GKeyFile) file = g_key_file_new();
    if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) return;
    g_autofree char *language = g_key_file_get_string(file, "Input", "language", NULL);
    g_autofree char *chinese = g_key_file_get_string(file, "Input", "chinese", NULL);
    g_autofree char *theme = g_key_file_get_string(file, "Appearance", "theme", NULL);
    g_autoptr(GError) error = NULL;
    gboolean jp = g_key_file_get_boolean(file, "Input", "japanese", &error);
    if (!error) link_settings.japanese = jp;
    int count = g_key_file_get_integer(file, "Input", "candidates", NULL);
    if (count >= 1 && count <= LINK_MAX_CANDIDATES) link_settings.candidates = count;
    int font_size = g_key_file_get_integer(file, "Appearance", "font_size", NULL);
    if (font_size >= 10 && font_size <= 28) link_settings.font_size = font_size;
    if (!g_strcmp0(language, "en")) link_settings.language = LINK_EN;
    if (!g_strcmp0(language, "ja") && link_settings.japanese) link_settings.language = LINK_JA;
    link_settings.double_pinyin = !g_strcmp0(chinese, "double");
    if (!g_strcmp0(theme, "light")) link_settings.theme = LINK_LIGHT;
    if (!g_strcmp0(theme, "dark")) link_settings.theme = LINK_DARK;
    read_palette(file, "Light", &link_settings.light);
    read_palette(file, "Dark", &link_settings.dark);
}

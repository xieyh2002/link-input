#ifndef LINK_PINYIN_FUZZY_H
#define LINK_PINYIN_FUZZY_H
#include <glib.h>
#include <string.h>

static inline void pinyin_append_fuzzy(GString *algebra, const char *groups)
{
    static const struct {
        const char *group;
        const char *forward;
        const char *backward;
    } rules[] = {
        {"zh/z", "fuzz/^zh/z/", "fuzz/^z(?!h)/zh/"},
        {"ch/c", "fuzz/^ch/c/", "fuzz/^c(?!h)/ch/"},
        {"sh/s", "fuzz/^sh/s/", "fuzz/^s(?!h)/sh/"},
        {"n/l", "fuzz/^n/l/", "fuzz/^l/n/"},
        {"en/eng", "fuzz/en$/eng/", "fuzz/eng$/en/"},
        {"in/ing", "fuzz/in$/ing/", "fuzz/ing$/in/"},
    };
    gboolean enabled[G_N_ELEMENTS(rules)] = {FALSE};

    if (!groups) return;
    while (*groups) {
        while (g_ascii_isspace(*groups)) groups++;
        const char *start = groups;
        while (*groups && *groups != ',') groups++;
        const char *end = groups;
        while (end > start && g_ascii_isspace(end[-1])) end--;
        for (guint i = 0; i < G_N_ELEMENTS(rules); i++)
            if (strlen(rules[i].group) == (gsize)(end - start) &&
                !strncmp(rules[i].group, start, end - start))
                enabled[i] = TRUE;
        if (*groups == ',') groups++;
    }

    for (guint i = 0; i < G_N_ELEMENTS(rules); i++) {
        if (!enabled[i]) continue;
        g_string_append_printf(algebra, "    - %s\n", rules[i].forward);
        g_string_append_printf(algebra, "    - %s\n", rules[i].backward);
    }
}
#endif

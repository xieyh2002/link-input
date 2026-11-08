#ifndef LINK_PINYIN_PHRASES_H
#define LINK_PINYIN_PHRASES_H
#include <glib.h>
#include <string.h>

static inline void pinyin_append_phrases(GString *patch, const char *schema)
{
    static const char settings[] =
        "  custom_phrase/dictionary: ''\n"
        "  custom_phrase/user_dict: custom_phrase\n"
        "  custom_phrase/db_class: stabledb\n"
        "  custom_phrase/enable_completion: false\n"
        "  custom_phrase/enable_sentence: false\n"
        "  custom_phrase/initial_quality: 1000\n";

    if (!patch || !schema) return;
    if (!strcmp(schema, "double_pinyin_flypy"))
        g_string_append(patch,
            "  'engine/translators/@before 1': table_translator@custom_phrase\n");
    else if (strcmp(schema, "luna_pinyin_simp"))
        return;
    g_string_append(patch, settings);
}
#endif

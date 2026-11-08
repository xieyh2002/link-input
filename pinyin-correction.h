#ifndef QINGYIN_PINYIN_CORRECTION_H
#define QINGYIN_PINYIN_CORRECTION_H
#include <glib.h>

static inline void pinyin_append_correction(GString *algebra, gboolean enabled)
{
    if (!enabled) return;
    g_string_append(algebra,
        "    - derive/([aeiou])ng$/$1gn/correction\n"
        "    - derive/ao$/oa/correction\n"
        "    - derive/([iu])a(o|ng?)$/a$1$2/correction\n");
}
#endif

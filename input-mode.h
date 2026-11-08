#ifndef QINGYIN_INPUT_MODE_H
#define QINGYIN_INPUT_MODE_H
#include <rime_api.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "japanese.h"
#include "settings.h"

#define FULL_PINYIN "luna_pinyin_simp"
#define DOUBLE_PINYIN "double_pinyin_flypy"

typedef enum { INPUT_FULL, INPUT_DOUBLE, INPUT_JAPANESE } InputMode;
typedef struct {
    InputMode mode, chinese_mode;
    bool english;
    Japanese *jp;
} InputState;

static inline bool input_prepare_defaults(const char *data, gboolean *changed)
{
    g_autofree char *path = g_build_filename(data, "default.custom.yaml", NULL);
    g_autofree char *existing = NULL;
    const char *old = "patch:\n  menu/page_size: 7\n";
    const char *format =
        "patch:\n"
        "  menu/page_size: %d\n"
        "  punctuator/half_shape/+:\n"
        "    '/': '/'\n"
        "    '\\': '\\'\n"
        "  punctuator/full_shape/+:\n"
        "    '/': '/'\n"
        "    '\\': '\\'\n";
    g_autofree char *defaults = g_strdup_printf(format, link_settings.candidates);
    if (g_file_get_contents(path, &existing, NULL, NULL)) {
        if (!strcmp(existing, defaults)) return true;
        bool managed = !strcmp(existing, old);
        for (int n = 1; !managed && n <= LINK_MAX_CANDIDATES; n++) {
            g_autofree char *candidate = g_strdup_printf(format, n);
            managed = !strcmp(existing, candidate);
        }
        if (!managed) return true;
    } else if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return false;
    }
    if (!g_file_set_contents(path, defaults, -1, NULL)) return false;
    *changed = TRUE;
    return true;
}

static inline const char *input_full_schema(void)
{
    const char *configured = g_getenv("QINGYIN_SCHEMA");
    return configured && *configured ? configured : FULL_PINYIN;
}

static inline bool input_select_rime(RimeApi *rime, RimeSessionId session, const char *schema)
{
    if (!rime->select_schema(session, schema)) return false;
    rime->set_option(session, "ascii_mode", False);
    rime->set_option(session, "qingyin_emoji", True);
    if (!strcmp(schema, DOUBLE_PINYIN))
        rime->set_option(session, "simplification", True);
    return true;
}

static inline void input_deploy_double(RimeApi *rime, const char *shared,
                                       const char *user_dir, bool changed)
{
    g_autofree char *built = g_build_filename(user_dir, "build", DOUBLE_PINYIN ".schema.yaml", NULL);
    g_autofree char *source = g_build_filename(shared, DOUBLE_PINYIN ".schema.yaml", NULL);
    if ((changed || !g_file_test(built, G_FILE_TEST_IS_REGULAR)) &&
        g_file_test(source, G_FILE_TEST_IS_REGULAR) && !rime->deploy_schema(source))
        g_warning("小鹤双拼方案部署失败");
}

static inline void input_clear(InputState *input, RimeApi *rime, RimeSessionId session)
{
    if (session) rime->clear_composition(session);
    if (input->jp) japanese_clear(input->jp);
}

static inline bool input_select(InputState *input, RimeApi *rime, RimeSessionId session,
                                InputMode mode)
{
    if (mode == INPUT_JAPANESE) {
        if (!link_settings.japanese) return false;
        if (!input->jp) input->jp = japanese_new();
        if (!input->jp) {
            g_warning("无法启动日文转换引擎 Anthy");
            return false;
        }
    } else if (!input_select_rime(rime, session, mode == INPUT_DOUBLE ? DOUBLE_PINYIN : input_full_schema())) {
        g_warning("无法加载请求的拼音方案");
        return false;
    }
    input_clear(input, rime, session);
    input->mode = mode;
    if (mode != INPUT_JAPANESE) input->chinese_mode = mode;
    input->english = false;
    return true;
}

static inline void input_apply_initial(InputState *input, RimeApi *rime, RimeSessionId session)
{
    if (link_settings.double_pinyin) input_select(input, rime, session, INPUT_DOUBLE);
    if (link_settings.language == LINK_JA) input_select(input, rime, session, INPUT_JAPANESE);
    if (link_settings.language == LINK_EN) input->english = true;
}

static inline const char *input_cycle(InputState *input, RimeApi *rime, RimeSessionId session)
{
    if (input->english) {
        if (input_select(input, rime, session, INPUT_JAPANESE)) return "日";
        return input_select(input, rime, session, input->chinese_mode) ? "中" : NULL;
    }
    if (input->mode == INPUT_JAPANESE)
        return input_select(input, rime, session, input->chinese_mode) ? "中" : NULL;
    input_clear(input, rime, session);
    input->english = true;
    return "英";
}

static inline const char *input_mode_label(InputMode mode)
{
    return mode == INPUT_FULL ? "全拼" : mode == INPUT_DOUBLE ? "双拼" : "日";
}

static inline bool input_key(InputState *input, RimeApi *rime, RimeSessionId session,
                             uint32_t sym, bool shift)
{
    if (input->english) return false;
    if (input->mode == INPUT_JAPANESE)
        return input->jp && japanese_key(input->jp, sym, shift);
    return rime->process_key(session, (int)sym, shift ? 1 : 0);
}
#endif

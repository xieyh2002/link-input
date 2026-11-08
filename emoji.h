#ifndef QINGYIN_EMOJI_H
#define QINGYIN_EMOJI_H
#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>
#include "pinyin-phrases.h"
#include "pinyin-correction.h"
#include "pinyin-fuzzy.h"

static inline gboolean emoji_write(const char *path, const char *contents,
                                   gboolean *changed, GError **error)
{
    g_autofree char *old = NULL;
    if (g_file_get_contents(path, &old, NULL, NULL) && !strcmp(old, contents))
        return TRUE;
    if (!g_file_set_contents(path, contents, -1, error)) return FALSE;
    *changed = TRUE;
    return TRUE;
}

static inline gboolean schema_invalidate(const char *data, const char *schema, GError **error)
{
    /* Rime compares source timestamps at one-second resolution. */
    g_autofree char *name = g_strconcat(schema, ".schema.yaml", NULL);
    g_autofree char *path = g_build_filename(data, "build", name, NULL);
    if (g_unlink(path) == 0 || errno == ENOENT) return TRUE;
    int saved = errno;
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved),
                "无法刷新 Rime 方案缓存：%s", g_strerror(saved));
    return FALSE;
}

static inline gboolean pinyin_prepare_algebra(const char *data, gboolean *changed, GError **error)
{
    g_autofree char *settings = g_build_filename(g_get_user_config_dir(), "link-input", "pinyin.ini", NULL);
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_key_file_load_from_file(file, settings, G_KEY_FILE_NONE, NULL);
    gboolean correction = g_key_file_get_boolean(file, "Pinyin", "correction", NULL);
    g_autofree char *fuzzy = g_key_file_get_string(file, "Pinyin", "fuzzy", NULL);
    /* Keep the stock Luna abbreviations and alternate spellings. */
    g_autoptr(GString) algebra = g_string_new(
        "algebra:\n"
        "    - erase/^xx$/\n"
        "    - abbrev/^([a-z]).+$/$1/\n"
        "    - abbrev/^([zcs]h).+$/$1/\n"
        "    - derive/^([nl])ve$/$1ue/\n"
        "    - derive/^([jqxy])u/$1v/\n"
        "    - derive/un$/uen/\n"
        "    - derive/ui$/uei/\n"
        "    - derive/iu$/iou/\n");
    pinyin_append_fuzzy(algebra, fuzzy);
    pinyin_append_correction(algebra, correction);
    g_autofree char *path = g_build_filename(data, "link_pinyin_algebra.yaml", NULL);
    return emoji_write(path, algebra->str, changed, error);
}

static inline gboolean emoji_prepare(const char *data, const char *schema,
                                     gboolean *changed, GError **error)
{
    if (strcmp(schema, "luna_pinyin_simp") && strcmp(schema, "double_pinyin_flypy")) return TRUE;
    g_autofree char *filename = g_strconcat(schema, ".custom.yaml", NULL);
    g_autofree char *custom = g_build_filename(data, filename, NULL);
    const char *patch =
        "# Qingyin emoji suggestions\n"
        "patch:\n"
        "  'engine/filters/@before 1': simplifier@qingyin_emoji\n"
        "  qingyin_emoji:\n"
        "    option_name: qingyin_emoji\n"
        "    opencc_config: qingyin-emoji.json\n";
    g_autofree char *ice_dict = g_build_filename(data, "link_ice.dict.yaml", NULL);
    const char *ice =
        "  translator/dictionary: link_ice\n"
        "  translator/user_dict: luna_pinyin\n";
    g_autofree char *ice_patch = g_strconcat(patch, ice, NULL);
    gboolean full = !strcmp(schema, "luna_pinyin_simp");
    g_autoptr(GString) pinyin_patch = g_string_new(patch);
    pinyin_append_phrases(pinyin_patch, schema);
    if (full)
        g_string_append(pinyin_patch,
            "  speller/algebra:\n"
            "    __include: link_pinyin_algebra:/algebra\n");
    g_autofree char *pinyin_ice_patch = g_strconcat(pinyin_patch->str, ice, NULL);
    gboolean schema_changed = FALSE;
    if (full && !pinyin_prepare_algebra(data, &schema_changed, error)) return FALSE;
    if (schema_changed) {
        *changed = TRUE;
        if (!schema_invalidate(data, schema, error)) return FALSE;
    }
    g_autofree char *existing = NULL;
    if (g_file_get_contents(custom, &existing, NULL, NULL) &&
        strcmp(existing, patch) && strcmp(existing, ice_patch) &&
        strcmp(existing, pinyin_patch->str) && strcmp(existing, pinyin_ice_patch))
        return TRUE; /* Preserve user-managed Rime schema overrides. */

    g_autofree char *dir = g_build_filename(data, "opencc", NULL);
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "无法创建 OpenCC 目录");
        return FALSE;
    }
    g_autofree char *config = g_build_filename(dir, "qingyin-emoji.json", NULL);
    g_autofree char *dict = g_build_filename(dir, "qingyin-emoji.txt", NULL);
    const char *json = "{\"name\":\"Qingyin emoji\","
        "\"segmentation\":{\"type\":\"mmseg\",\"dict\":{\"type\":\"text\",\"file\":\"qingyin-emoji.txt\"}},"
        "\"conversion_chain\":[{\"dict\":{\"type\":\"text\",\"file\":\"qingyin-emoji.txt\"}}]}\n";
    const char *entries =
        "开心\t开心 😄 😃\n"
        "高兴\t高兴 😊\n"
        "微笑\t微笑 🙂 😊\n"
        "大笑\t大笑 😂 😆\n"
        "笑哭\t笑哭 😂 🤣\n"
        "流泪\t流泪 😢\n"
        "哭泣\t哭泣 😭\n"
        "生气\t生气 😠 😡\n"
        "害羞\t害羞 😊 😳\n"
        "惊讶\t惊讶 😮 😲\n"
        "思考\t思考 🤔\n"
        "无语\t无语 😅 🙃\n"
        "谢谢\t谢谢 🙏\n"
        "点赞\t点赞 👍\n"
        "鼓掌\t鼓掌 👏\n"
        "比心\t比心 🫰 🫶\n"
        "爱心\t爱心 ❤️ 💖\n"
        "玫瑰\t玫瑰 🌹\n"
        "庆祝\t庆祝 🎉 🥳\n"
        "生日\t生日 🎂\n"
        "礼物\t礼物 🎁\n"
        "加油\t加油 💪\n"
        "再见\t再见 👋\n"
        "猫咪\t猫咪 🐱\n"
        "狗狗\t狗狗 🐶\n"
        "熊猫\t熊猫 🐼\n"
        "咖啡\t咖啡 ☕\n"
        "太阳\t太阳 ☀️\n"
        "月亮\t月亮 🌙\n"
        "星星\t星星 ⭐\n"
        "彩虹\t彩虹 🌈\n"
        "闪电\t闪电 ⚡\n"
        "火焰\t火焰 🔥\n";
    gboolean patch_changed = FALSE;
    if (!emoji_write(dict, entries, changed, error) ||
        !emoji_write(config, json, changed, error) ||
        !emoji_write(custom, g_file_test(ice_dict, G_FILE_TEST_IS_REGULAR) ? pinyin_ice_patch : pinyin_patch->str,
                     &patch_changed, error))
        return FALSE;
    if (patch_changed) {
        *changed = TRUE;
        if (!schema_changed && !schema_invalidate(data, schema, error)) return FALSE;
    }
    return TRUE;
}
#endif

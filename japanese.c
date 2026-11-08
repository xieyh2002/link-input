#include "japanese.h"
#include "settings.h"

#include <dlfcn.h>
#include <glib.h>
#include <xkbcommon/xkbcommon-keysyms.h>

/* Public layouts and constants from anthy/input.h.
 * Load it on demand so the other modes work without the Japanese library. */
typedef struct anthy_input_config AnthyConfig;
typedef struct anthy_input_context AnthyContext;
typedef struct anthy_input_segment {
    char *str;
    int cand_no, noconv_len, nr_cand, flag;
    struct anthy_input_segment *next;
} AnthySegment;
typedef struct {
    int state;
    char *commit, *cut_buf;
    AnthySegment *segment, *cur_segment;
} AnthyPreedit;

enum { ANTHY_CONVERTING = 3, ANTHY_HIRAGANA = 2, PAGE_SIZE = LINK_MAX_CANDIDATES };

static struct {
    void *lib;
    bool legacy_encoding;
    int (*init)(void);
    AnthyConfig *(*create_config)(void);
    void (*free_config)(AnthyConfig *);
    AnthyContext *(*create_context)(AnthyConfig *);
    void (*free_context)(AnthyContext *);
    int (*map_select)(AnthyContext *, int);
    void (*key)(AnthyContext *, int);
    void (*space)(AnthyContext *);
    void (*quit)(AnthyContext *);
    void (*erase_prev)(AnthyContext *);
    void (*commit)(AnthyContext *);
    void (*move)(AnthyContext *, int);
    void (*next_candidate)(AnthyContext *);
    void (*prev_candidate)(AnthyContext *);
    AnthyPreedit *(*get_preedit)(AnthyContext *);
    void (*free_preedit)(AnthyPreedit *);
    AnthySegment *(*get_candidate)(AnthyContext *, int);
    void (*free_segment)(AnthySegment *);
    int (*select_candidate)(AnthyContext *, int);
} anthy;

struct Japanese {
    AnthyConfig *config;
    AnthyContext *ctx, *preview;
    char *text, *commit;
    char *choices[PAGE_SIZE];
    int first, count, selected;
    GArray *edits;
};

static bool anthy_load(void)
{
    if (anthy.lib) return true;
    anthy.lib = dlopen("libanthyinput-unicode.so.0", RTLD_NOW | RTLD_LOCAL);
    anthy.legacy_encoding = !anthy.lib;
    if (!anthy.lib) anthy.lib = dlopen("libanthyinput.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!anthy.lib) return false;
#define LOAD(name) do { \
    anthy.name = (__typeof__(anthy.name))dlsym(anthy.lib, "anthy_input_" #name); \
    if (!anthy.name) goto error; \
} while (0)
    LOAD(init); LOAD(create_config); LOAD(free_config); LOAD(create_context);
    LOAD(free_context); LOAD(map_select); LOAD(key); LOAD(space); LOAD(quit);
    LOAD(erase_prev); LOAD(commit); LOAD(move); LOAD(next_candidate);
    LOAD(prev_candidate); LOAD(get_preedit); LOAD(free_preedit);
    LOAD(get_candidate); LOAD(free_segment); LOAD(select_candidate);
#undef LOAD
    if (anthy.init() == 0) return true;
error:
    dlclose(anthy.lib);
    anthy.lib = NULL;
    return false;
}

static char *copy_text(const char *text)
{
    if (!anthy.legacy_encoding) return g_strdup(text);
    char *utf8 = g_convert(text, -1, "UTF-8", "EUC-JP", NULL, NULL, NULL);
    return utf8 ? utf8 : g_utf8_make_valid(text, -1);
}

static char *take_text(GString *text)
{
    if (!anthy.legacy_encoding) return g_string_free(text, FALSE);
    char *utf8 = copy_text(text->str);
    g_string_free(text, TRUE);
    return utf8;
}

static void collect_choices(Japanese *jp, AnthyContext *ctx, AnthyPreedit *v)
{
    for (int i = 0; i < PAGE_SIZE; i++) g_clear_pointer(&jp->choices[i], g_free);
    jp->count = jp->first = jp->selected = 0;
    if (!v || v->state != ANTHY_CONVERTING || !v->cur_segment ||
        v->cur_segment->nr_cand <= 0) return;
    AnthySegment *active = v->cur_segment;
    int index = CLAMP(active->cand_no, 0, active->nr_cand - 1);
    int page_size = link_settings.candidates;
    jp->first = (index / page_size) * page_size;
    jp->selected = index - jp->first;
    for (int i = 0; i < page_size && jp->first + i < active->nr_cand; i++) {
        AnthySegment *candidate = anthy.get_candidate(ctx, jp->first + i);
        if (!candidate) break;
        if (candidate->str) {
            GString *phrase = g_string_new(NULL);
            for (AnthySegment *s = v->segment; s; s = s->next) {
                const char *part = s == active ? candidate->str : s->str;
                if (part) g_string_append(phrase, part);
            }
            jp->choices[jp->count++] = take_text(phrase);
        }
        anthy.free_segment(candidate);
    }
}

static void reset_context(AnthyContext *ctx)
{
    anthy.quit(ctx); /* Conversion -> editing -> idle. */
    anthy.quit(ctx);
}

static void refresh(Japanese *jp)
{
    g_clear_pointer(&jp->text, g_free);
    AnthyPreedit *v = anthy.get_preedit(jp->ctx);
    reset_context(jp->preview);
    if (!v) {
        collect_choices(jp, jp->preview, NULL);
        return;
    }
    if (v->commit && *v->commit) {
        g_clear_pointer(&jp->commit, g_free);
        jp->commit = copy_text(v->commit);
    }
    GString *text = g_string_new(NULL);
    for (AnthySegment *s = v->segment; s; s = s->next)
        if (s->str) g_string_append(text, s->str);
    jp->text = take_text(text);
    anthy.free_preedit(v);
    /* Conversion flushes pending romaji, so keep it out of the editable context. */
    if (*jp->text) {
        for (guint i = 0; i < jp->edits->len; i++) {
            uint32_t key = g_array_index(jp->edits, uint32_t, i);
            if (key == XKB_KEY_BackSpace) anthy.erase_prev(jp->preview);
            else if (key == XKB_KEY_Left || key == XKB_KEY_Right)
                anthy.move(jp->preview, key == XKB_KEY_Left ? -1 : 1);
            else anthy.key(jp->preview, (int)key);
        }
        anthy.space(jp->preview);
    } else g_array_set_size(jp->edits, 0);
    v = anthy.get_preedit(jp->preview);
    collect_choices(jp, jp->preview, v);
    if (v) anthy.free_preedit(v);
}

Japanese *japanese_new(void)
{
    if (!anthy_load()) return NULL;
    Japanese *jp = g_new0(Japanese, 1);
    jp->edits = g_array_new(FALSE, FALSE, sizeof(uint32_t));
    jp->config = anthy.create_config();
    if (jp->config) {
        jp->ctx = anthy.create_context(jp->config);
        jp->preview = anthy.create_context(jp->config);
    }
    if (!jp->ctx || !jp->preview) { japanese_free(jp); return NULL; }
    anthy.map_select(jp->ctx, ANTHY_HIRAGANA);
    anthy.map_select(jp->preview, ANTHY_HIRAGANA);
    refresh(jp);
    return jp;
}

void japanese_free(Japanese *jp)
{
    if (!jp) return;
    if (jp->preview) anthy.free_context(jp->preview);
    if (jp->ctx) anthy.free_context(jp->ctx);
    if (jp->config) anthy.free_config(jp->config);
    g_array_unref(jp->edits);
    g_free(jp->text);
    g_free(jp->commit);
    for (int i = 0; i < PAGE_SIZE; i++) g_free(jp->choices[i]);
    g_free(jp);
}

void japanese_clear(Japanese *jp)
{
    if (!jp) return;
    reset_context(jp->ctx);
    refresh(jp);
    g_clear_pointer(&jp->commit, g_free);
}

bool japanese_key(Japanese *jp, uint32_t sym, bool shift)
{
    if (!jp || !jp->ctx) return false;
    bool composing = jp->text && *jp->text;
    bool number = !shift && sym >= XKB_KEY_1 && sym <= XKB_KEY_9 &&
        sym - XKB_KEY_1 < (uint32_t)jp->count;
    if (jp->count && (number || sym == XKB_KEY_space)) {
        int index = number ? (int)(sym - XKB_KEY_1) : jp->selected;
        anthy.select_candidate(jp->preview, jp->first + index);
        anthy.commit(jp->preview);
        AnthyPreedit *v = anthy.get_preedit(jp->preview);
        if (v && v->commit && *v->commit) {
            g_free(jp->commit);
            jp->commit = copy_text(v->commit);
        }
        if (v) anthy.free_preedit(v);
        reset_context(jp->ctx);
    } else if (sym == XKB_KEY_space && composing) {
        anthy.commit(jp->ctx);
    } else if ((sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) && composing) {
        anthy.commit(jp->ctx);
    } else if (sym == XKB_KEY_BackSpace && composing) {
        anthy.erase_prev(jp->ctx);
        g_array_append_val(jp->edits, sym);
    } else if (sym == XKB_KEY_Escape && composing) {
        anthy.quit(jp->ctx);
    } else if ((sym == XKB_KEY_Up || sym == XKB_KEY_Down) && jp->count) {
        if (sym == XKB_KEY_Up) anthy.prev_candidate(jp->preview);
        else anthy.next_candidate(jp->preview);
        AnthyPreedit *v = anthy.get_preedit(jp->preview);
        collect_choices(jp, jp->preview, v);
        if (v) anthy.free_preedit(v);
        return true;
    } else if ((sym == XKB_KEY_Left || sym == XKB_KEY_Right) && composing) {
        anthy.move(jp->ctx, sym == XKB_KEY_Left ? -1 : 1);
        g_array_append_val(jp->edits, sym);
    } else if ((sym >= XKB_KEY_a && sym <= XKB_KEY_z) ||
               (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) ||
               sym == XKB_KEY_minus || sym == XKB_KEY_comma || sym == XKB_KEY_period ||
               sym == XKB_KEY_bracketleft || sym == XKB_KEY_bracketright ||
               (sym == XKB_KEY_apostrophe && composing)) {
        sym = (uint32_t)g_ascii_tolower((gchar)sym);
        anthy.key(jp->ctx, (int)sym);
        g_array_append_val(jp->edits, sym);
    } else {
        return false;
    }
    refresh(jp);
    return true;
}

const char *japanese_preedit(Japanese *jp)
{
    return jp && jp->text ? jp->text : "";
}

int japanese_candidates(Japanese *jp, const char **items, int cap, int *selected)
{
    *selected = jp ? jp->selected : 0;
    int count = jp ? MIN(jp->count, MAX(cap, 0)) : 0;
    for (int i = 0; i < count; ++i) items[i] = jp->choices[i];
    return count;
}

char *japanese_take_commit(Japanese *jp)
{
    return jp ? g_steal_pointer(&jp->commit) : NULL;
}

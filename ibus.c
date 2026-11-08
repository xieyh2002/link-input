#include <ibus.h>
#include <rime_api.h>
#include <string.h>
#include "x11-panel.h"
#include "panel.h"
#include "emoji.h"
#include "theme.h"
#include "input-mode.h"

#define PAGE_SIZE LINK_MAX_CANDIDATES
#define ENGINE_NAME "qingyin"

typedef struct {
    IBusEngine parent;
    RimeSessionId session;
    guint shift_key, swallowed_shortcut;
    InputState input;
    gchar *context_id;
    gboolean sensitive;
} QingyinEngine;
typedef IBusEngineClass QingyinEngineClass;

static RimeApi *rime;
static const char *schema;
G_DEFINE_TYPE(QingyinEngine, qingyin_engine, IBUS_TYPE_ENGINE)

static Theme theme;
static QingyinEngine *visible_engine;
static guint status_timer;
static const char *status_label;

static void clear_status(void)
{
    if (status_timer) g_source_remove(status_timer);
    status_timer = 0;
    status_label = NULL;
}

static void hide_panel(void)
{
    clear_status();
    visible_engine = NULL;
    x11_panel_hide();
}

static gboolean expire_status(gpointer unused)
{
    status_timer = 0;
    hide_panel();
    return G_SOURCE_REMOVE;
}

static void paint_status(QingyinEngine *e)
{
    IBusEngine *engine = IBUS_ENGINE(e);
    const char *items[] = {status_label};
    x11_panel_show(engine->cursor_area.x, engine->cursor_area.y,
                   engine->cursor_area.height, items, 1, PANEL_STATUS, theme.dark);
}

static void show_status(QingyinEngine *e, const char *label)
{
    clear_status();
    if (!label) return;
    visible_engine = e;
    status_label = label;
    paint_status(e);
    status_timer = g_timeout_add(850, expire_status, NULL);
}

static void paint_context(QingyinEngine *e, const RimeContext *ctx)
{
    IBusEngine *engine = IBUS_ENGINE(e);
    int count = MIN(ctx->menu.num_candidates, link_settings.candidates);
    const char *choices[PAGE_SIZE] = {0};
    for (int i = 0; i < count; ++i) choices[i] = ctx->menu.candidates[i].text;
    x11_panel_show(engine->cursor_area.x, engine->cursor_area.y,
                   engine->cursor_area.height, choices, count,
                   ctx->menu.highlighted_candidate_index, theme.dark);
    visible_engine = count > 0 ? e : NULL;
}

static void paint_japanese(QingyinEngine *e)
{
    IBusEngine *engine = IBUS_ENGINE(e);
    const char *choices[PAGE_SIZE] = {0};
    int selected = 0;
    int count = japanese_candidates(e->input.jp, choices, link_settings.candidates, &selected);
    x11_panel_show(engine->cursor_area.x, engine->cursor_area.y,
                   engine->cursor_area.height, choices, count, selected, theme.dark);
    visible_engine = count > 0 ? e : NULL;
}

static void repaint(gpointer unused)
{
    QingyinEngine *e = visible_engine;
    if (!e) return;
    if (status_label) { paint_status(e); return; }
    if (e->input.mode == INPUT_JAPANESE) { paint_japanese(e); return; }
    RIME_STRUCT(RimeContext, ctx);
    if (rime->get_context(e->session, &ctx)) {
        paint_context(e, &ctx);
        rime->free_context(&ctx);
    }
}

static void publish(QingyinEngine *e)
{
    clear_status();
    IBusEngine *engine = IBUS_ENGINE(e);
    if (e->input.mode == INPUT_JAPANESE) {
        g_autofree char *commit = japanese_take_commit(e->input.jp);
        if (commit && *commit) ibus_engine_commit_text(engine, ibus_text_new_from_string(commit));
        const char *text = japanese_preedit(e->input.jp);
        ibus_engine_update_preedit_text_with_mode(engine, ibus_text_new_from_string(text),
                                                 (guint)g_utf8_strlen(text, -1), *text != 0,
                                                 IBUS_ENGINE_PREEDIT_CLEAR);
        paint_japanese(e);
        return;
    }
    RIME_STRUCT(RimeCommit, commit);
    if (rime->get_commit(e->session, &commit)) {
        ibus_engine_commit_text(engine, ibus_text_new_from_string(commit.text));
        rime->free_commit(&commit);
    }
    RIME_STRUCT(RimeContext, ctx);
    if (!rime->get_context(e->session, &ctx)) {
        ibus_engine_hide_preedit_text(engine);
        hide_panel();
        return;
    }
    const char *preedit = ctx.composition.preedit ? ctx.composition.preedit : "";
    guint cursor = (guint)g_utf8_pointer_to_offset(preedit, preedit + CLAMP(
        ctx.composition.cursor_pos, 0, (int)strlen(preedit)));
    ibus_engine_update_preedit_text_with_mode(engine, ibus_text_new_from_string(preedit),
                                            cursor, *preedit != 0, IBUS_ENGINE_PREEDIT_CLEAR);
    paint_context(e, &ctx);
    rime->free_context(&ctx);
}

static void reset(IBusEngine *engine)
{
    QingyinEngine *e = (QingyinEngine *)engine;
    e->shift_key = 0;
    e->swallowed_shortcut = 0;
    input_clear(&e->input, rime, e->session);
    hide_panel();
}

static void toggle(QingyinEngine *e)
{
    const char *label = input_cycle(&e->input, rime, e->session);
    if (!label) return;
    publish(e);
    show_status(e, label);
}

static gboolean process(IBusEngine *engine, guint keyval, guint keycode, guint state)
{
    QingyinEngine *e = (QingyinEngine *)engine;
    if (!e->session || e->sensitive) return FALSE;
    gboolean is_shift = keyval == IBUS_Shift_L || keyval == IBUS_Shift_R;
    guint shortcuts = state & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK | IBUS_META_MASK);
    if (state & IBUS_RELEASE_MASK) {
        if (is_shift && e->shift_key == keyval) toggle(e);
        if (keyval == e->swallowed_shortcut) {
            e->swallowed_shortcut = 0;
            return TRUE;
        }
        return FALSE;
    }
    e->shift_key = is_shift && !shortcuts && !(state & IBUS_SHIFT_MASK) ? keyval : 0;
    if (is_shift) return FALSE;
    if (shortcuts == (IBUS_CONTROL_MASK | IBUS_MOD1_MASK) && !(state & IBUS_SHIFT_MASK) &&
        keyval >= IBUS_1 && keyval <= IBUS_3 &&
        input_select(&e->input, rime, e->session, (InputMode)(keyval - IBUS_1))) {
        publish(e);
        show_status(e, input_mode_label(e->input.mode));
        e->swallowed_shortcut = keyval;
        return TRUE;
    }
    if (keyval == IBUS_space && shortcuts == IBUS_CONTROL_MASK && !(state & IBUS_SHIFT_MASK)) {
        toggle(e);
        e->swallowed_shortcut = keyval;
        return TRUE;
    }
    if (!shortcuts && input_key(&e->input, rime, e->session, keyval, state & IBUS_SHIFT_MASK)) {
        publish(e);
        return TRUE;
    }
    return FALSE;
}

static void content_type(IBusEngine *engine, guint purpose, guint hints)
{
    QingyinEngine *e = (QingyinEngine *)engine;
    e->sensitive = purpose == IBUS_INPUT_PURPOSE_PASSWORD || purpose == IBUS_INPUT_PURPOSE_PIN ||
                   (hints & IBUS_INPUT_HINT_PRIVATE);
    if (e->sensitive) reset(engine);
}

static void candidate_clicked(IBusEngine *engine, guint index, guint button, guint state)
{
    QingyinEngine *e = (QingyinEngine *)engine;
    if (e->session && !e->sensitive && !e->input.english && e->input.mode != INPUT_JAPANESE && index < (guint)link_settings.candidates && button == 1 &&
        rime->select_candidate_on_current_page(e->session, index))
        publish(e);
}

static void focus_in_id(IBusEngine *engine, const gchar *id, const gchar *client)
{
    QingyinEngine *e = (QingyinEngine *)engine;
    if (g_strcmp0(client, "fake") == 0) return;
    if (g_strcmp0(e->context_id, id) != 0) {
        input_clear(&e->input, rime, e->session);
        g_free(e->context_id);
        e->context_id = g_strdup(id);
        e->sensitive = FALSE;
    }
}

static void focus_out_id(IBusEngine *engine, const gchar *id)
{
    QingyinEngine *e = (QingyinEngine *)engine;
    if (g_strcmp0(e->context_id, id) != 0) return;
    reset(engine);
}

static void destroy(IBusObject *object)
{
    QingyinEngine *e = (QingyinEngine *)object;
    if (visible_engine == e) hide_panel();
    if (e->input.jp) japanese_free(e->input.jp);
    if (e->session) rime->destroy_session(e->session);
    e->session = 0;
    g_clear_pointer(&e->context_id, g_free);
    IBUS_OBJECT_CLASS(qingyin_engine_parent_class)->destroy(object);
}

static void qingyin_engine_class_init(QingyinEngineClass *klass)
{
    klass->process_key_event = process;
    klass->reset = reset;
    klass->focus_in_id = focus_in_id;
    klass->focus_out_id = focus_out_id;
    klass->set_content_type = content_type;
    klass->candidate_clicked = candidate_clicked;
    IBUS_OBJECT_CLASS(klass)->destroy = destroy;
}

static void qingyin_engine_init(QingyinEngine *e)
{
    e->session = rime->create_session();
    if (e->session && !input_select_rime(rime, e->session, schema)) {
        g_warning("无法加载link输入法方案：%s", schema);
        rime->destroy_session(e->session);
        e->session = 0;
    }
    if (e->session) input_apply_initial(&e->input, rime, e->session);
}

typedef IBusFactory QingyinFactory;
typedef IBusFactoryClass QingyinFactoryClass;
G_DEFINE_TYPE(QingyinFactory, qingyin_factory, IBUS_TYPE_FACTORY)

static IBusEngine *create_engine(IBusFactory *factory, const gchar *name)
{
    static guint next_id;
    if (g_strcmp0(name, ENGINE_NAME) != 0) return NULL;
    g_autofree gchar *path = g_strdup_printf("/org/freedesktop/IBus/Engine/Qingyin/%u", ++next_id);
    return g_object_new(qingyin_engine_get_type(), "engine-name", name, "object-path", path,
        "connection", ibus_service_get_connection(IBUS_SERVICE(factory)), "has-focus-id", TRUE, NULL);
}

static void qingyin_factory_class_init(QingyinFactoryClass *klass) { klass->create_engine = create_engine; }
static void qingyin_factory_init(QingyinFactory *factory) {}

int main(void)
{
    link_settings_load();
    ibus_init();
    IBusBus *bus = ibus_bus_new();
    if (!ibus_bus_is_connected(bus)) {
        g_printerr("无法连接 IBus 传输服务\n");
        return 1;
    }
    g_autofree char *data = g_build_filename(g_get_user_data_dir(), "qingyin", "rime-ibus", NULL);
    if (g_mkdir_with_parents(data, 0700) < 0) return 1;
    gboolean fresh = FALSE;
    if (!input_prepare_defaults(data, &fresh)) return 1;
    gboolean defaults_changed = fresh;
    schema = input_full_schema();
    g_autoptr(GError) emoji_error = NULL;
    if (!emoji_prepare(data, schema, &fresh, &emoji_error))
        g_warning("无法部署表情候选：%s", emoji_error->message);
    gboolean double_changed = FALSE;
    g_clear_error(&emoji_error);
    if (!emoji_prepare(data, DOUBLE_PINYIN, &double_changed, &emoji_error))
        g_warning("无法部署双拼表情候选：%s", emoji_error->message);
    rime = rime_get_api();
    RIME_STRUCT(RimeTraits, traits);
    traits.app_name = "rime.qingyin-ibus";
    traits.shared_data_dir = g_getenv("QINGYIN_RIME_DATA");
    if (!traits.shared_data_dir) traits.shared_data_dir = "/usr/share/rime-data";
    traits.user_data_dir = data;
    traits.min_log_level = 2;
    rime->setup(&traits);
    rime->initialize(&traits);
    if (rime->start_maintenance(fresh)) rime->join_maintenance_thread();
    input_deploy_double(rime, traits.shared_data_dir, data, double_changed || defaults_changed);

    IBusFactory *factory = g_object_new(qingyin_factory_get_type(), "object-path", IBUS_PATH_FACTORY,
                                         "connection", ibus_bus_get_connection(bus), NULL);
    IBusComponent *component = ibus_component_new("org.freedesktop.IBus.Qingyin",
        "link输入法", "0.1", "", "", "", "", "");
    ibus_component_add_engine(component, ibus_engine_desc_new(ENGINE_NAME, "link输入法",
        "link输入法", "zh_CN", "", "", "", "default"));
    if (!ibus_bus_register_component(bus, component)) return 1;
    g_signal_connect(bus, "disconnected", G_CALLBACK(ibus_quit), NULL);
    g_print("link输入法 IBus 接口已注册\n");
    theme_start(&theme, repaint, NULL);
    ibus_main();
    clear_status();
    theme_stop(&theme);
    g_object_unref(component);
    g_object_unref(factory);
    g_object_unref(bus);
    x11_panel_close();
    rime->finalize();
    return 0;
}

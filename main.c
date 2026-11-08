#define _GNU_SOURCE
#include <errno.h>
#include <glib.h>
#include <poll.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <rime_api.h>
#include "input-method-unstable-v2.h"
#include "virtual-keyboard-unstable-v1.h"
#include "panel.h"
#include "emoji.h"
#include "theme.h"
#include "input-mode.h"

#define CANDIDATES LINK_MAX_CANDIDATES
#define KEYS 768
#define HINT_SENSITIVE ((1u << 6) | (1u << 7))
#define PURPOSE_PASSWORD 8
#define PURPOSE_PIN 9

struct app;
struct panel_buffer {
    struct app *app;
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    unsigned char *pixels;
    int capacity, width, height;
    bool busy;
};

struct app {
    Theme theme;
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_surface *surface;
    struct wl_callback *panel_frame;
    struct panel_buffer panel_buffers[3];
    PanelContent panel;
    bool panel_pending;
    struct zwp_input_method_manager_v2 *manager;
    struct zwp_virtual_keyboard_manager_v1 *vk_manager;
    struct zwp_input_method_v2 *im;
    struct zwp_input_method_keyboard_grab_v2 *grab;
    struct zwp_input_popup_surface_v2 *popup;
    struct zwp_virtual_keyboard_v1 *vk;
    struct xkb_context *xkb;
    struct xkb_state *keys;
    RimeApi *rime;
    RimeSessionId session;
    InputState input;
    guint status_timer, panel_wait;
    int32_t caret[4];
    bool caret_known;
    const char *status;
    uint32_t serial, hint, purpose;
    int scale;
    int32_t repeat_rate, repeat_delay, repeat_code, tap_shift;
    gint64 repeat_at;
    bool active, next_active, reset, unavailable, failed, visible;
    bool consumed[KEYS], forwarded[KEYS];
};

static bool engine_start(struct app *a) {
    const char *shared = getenv("QINGYIN_RIME_DATA");
    const char *schema = input_full_schema();
    g_autofree char *path = g_build_filename(g_get_user_data_dir(), "qingyin", "rime", NULL);
    if (g_mkdir_with_parents(path, 0700) < 0) {
        perror("Rime 数据目录");
        return false;
    }
    gboolean needs_deploy = FALSE;
    if (!input_prepare_defaults(path, &needs_deploy)) {
        fprintf(stderr, "无法保存 Rime 默认设置\n");
        return false;
    }

    g_autoptr(GError) emoji_error = NULL;
    gboolean emoji_changed = FALSE;
    if (!emoji_prepare(path, schema, &emoji_changed, &emoji_error))
        g_warning("无法部署表情候选：%s", emoji_error->message);

    gboolean double_changed = FALSE;
    g_clear_error(&emoji_error);
    if (!emoji_prepare(path, DOUBLE_PINYIN, &double_changed, &emoji_error))
        g_warning("无法部署双拼表情候选：%s", emoji_error->message);

    a->rime = rime_get_api();
    RIME_STRUCT(RimeTraits, traits);
    traits.app_name = "rime.qingyin";
    traits.shared_data_dir = shared ? shared : "/usr/share/rime-data";
    traits.user_data_dir = path;
    traits.min_log_level = 2;
    a->rime->setup(&traits);
    a->rime->initialize(&traits);
    if (a->rime->start_maintenance(needs_deploy || emoji_changed))
        a->rime->join_maintenance_thread();
    input_deploy_double(a->rime, traits.shared_data_dir, path, double_changed || needs_deploy || emoji_changed);
    a->session = a->rime->create_session();
    if (!a->session || !input_select_rime(a->rime, a->session, schema)) {
        fprintf(stderr, "无法加载 Rime 方案：%s\n", schema);
        return false;
    }
    input_apply_initial(&a->input, a->rime, a->session);
    return true;
}

static void draw_panel(struct app *a);

static void buffer_release(void *data, struct wl_buffer *buffer) {
    struct panel_buffer *slot = data;
    slot->busy = false;
    draw_panel(slot->app);
}
static const struct wl_buffer_listener buffer_listener = {buffer_release};

static void free_panel_buffer(struct panel_buffer *slot) {
    if (slot->buffer) wl_buffer_destroy(slot->buffer);
    if (slot->pool) wl_shm_pool_destroy(slot->pool);
    if (slot->pixels) munmap(slot->pixels, slot->capacity);
    *slot = (struct panel_buffer){0};
}

static bool prepare_buffer(struct app *a, struct panel_buffer *slot, int w, int h) {
    const int bytes = w * h * 4;
    if (bytes > slot->capacity) {
        int capacity = 4096;
        while (capacity < bytes) capacity *= 2;
        int fd = memfd_create("qingyin-panel", MFD_CLOEXEC);
        if (fd < 0) return false;
        if (ftruncate(fd, capacity) < 0) { close(fd); return false; }
        unsigned char *pixels = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (pixels == MAP_FAILED) { close(fd); return false; }
        struct wl_shm_pool *pool = wl_shm_create_pool(a->shm, fd, capacity);
        close(fd);
        if (!pool) { munmap(pixels, capacity); return false; }
        free_panel_buffer(slot);
        *slot = (struct panel_buffer){.app = a, .pool = pool, .pixels = pixels,
                                      .capacity = capacity};
    }
    if (!slot->buffer || slot->width != w || slot->height != h) {
        if (slot->buffer) wl_buffer_destroy(slot->buffer);
        slot->buffer = wl_shm_pool_create_buffer(slot->pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
        if (!slot->buffer) return false;
        wl_buffer_add_listener(slot->buffer, &buffer_listener, slot);
        slot->width = w;
        slot->height = h;
    }
    return true;
}

static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct app *a = data;
    wl_callback_destroy(callback);
    a->panel_frame = NULL;
    draw_panel(a);
}
static const struct wl_callback_listener frame_listener = {frame_done};

static void clear_status(struct app *a) {
    if (a->status_timer) g_source_remove(a->status_timer);
    a->status_timer = 0;
    a->status = NULL;
}

static void hide(struct app *a) {
    clear_status(a);
    g_clear_handle_id(&a->panel_wait, g_source_remove);
    panel_content_clear(&a->panel);
    a->panel_pending = false;
    if (a->panel_frame) {
        wl_callback_destroy(a->panel_frame);
        a->panel_frame = NULL;
    }
    if (!a->visible) return;
    wl_surface_attach(a->surface, NULL, 0, 0);
    wl_surface_commit(a->surface);
    a->visible = false;
}

static void draw_panel(struct app *a) {
    /* Keep only the latest content while waiting for the compositor. */
    if (!a->panel_pending || a->panel_frame || a->panel_wait || !a->active) return;
    PanelContent *p = &a->panel;
    bool has_free_slot = false;
    for (unsigned i = 0; i < G_N_ELEMENTS(a->panel_buffers); i++)
        has_free_slot |= !a->panel_buffers[i].busy;
    if (!has_free_slot) return;
    const char *const *items = (const char *const *)p->items;
    int width, height;
    panel_size(items, p->count, p->selected == PANEL_STATUS, &width, &height);
    int w = width * p->scale, h = height * p->scale;
    struct panel_buffer *slot = NULL;
    for (unsigned i = 0; i < G_N_ELEMENTS(a->panel_buffers); i++) {
        struct panel_buffer *candidate = &a->panel_buffers[i];
        if (candidate->busy) continue;
        if (!slot || (candidate->width == w && candidate->height == h))
            slot = candidate;
        if (slot->width == w && slot->height == h) break;
    }
    if (!slot || !prepare_buffer(a, slot, w, h)) return;
    panel_draw(slot->pixels, width, height, w * 4, p->scale, items, p->count, p->selected, p->dark);
    a->panel_frame = wl_surface_frame(a->surface);
    if (a->panel_frame) wl_callback_add_listener(a->panel_frame, &frame_listener, a);
    wl_surface_set_buffer_scale(a->surface, p->scale);
    wl_surface_attach(a->surface, slot->buffer, 0, 0);
    wl_surface_damage_buffer(a->surface, 0, 0, w, h);
    wl_surface_commit(a->surface);
    slot->busy = true;
    a->panel_pending = false;
    a->visible = true;
}

static gboolean finish_panel_wait(gpointer data) {
    struct app *a = data;
    a->panel_wait = 0;
    draw_panel(a);
    return G_SOURCE_REMOVE;
}

static void paint(struct app *a, const char *const *choices, int count, int selected) {
    if (!count || !a->active) { hide(a); return; }
    if (selected == PANEL_STATUS) {
        g_clear_handle_id(&a->panel_wait, g_source_remove);
    } else if (!a->visible && !a->panel_pending) {
        /* The client may send its new caret only after it receives the preedit. */
        a->panel_wait = g_timeout_add(24, finish_panel_wait, a);
    }
    if (panel_content_update(&a->panel, choices, count, selected, a->scale, a->theme.dark))
        a->panel_pending = true;
    draw_panel(a);
}

static gboolean expire_status(gpointer data) {
    struct app *a = data;
    a->status_timer = 0;
    hide(a);
    return G_SOURCE_REMOVE;
}

static void paint_status(struct app *a) {
    const char *items[] = {a->status};
    paint(a, items, 1, PANEL_STATUS);
}

static void show_status(struct app *a, const char *label) {
    clear_status(a);
    if (!label || !a->active) return;
    a->status = label;
    paint_status(a);
    a->status_timer = g_timeout_add(850, expire_status, a);
}

static void paint_context(struct app *a, const RimeContext *ctx) {
    const char *choices[CANDIDATES];
    int count = MIN(ctx->menu.num_candidates, link_settings.candidates);
    for (int i = 0; i < count; i++) choices[i] = ctx->menu.candidates[i].text;
    paint(a, choices, count, ctx->menu.highlighted_candidate_index);
}

static void paint_japanese(struct app *a) {
    const char *choices[CANDIDATES] = {0};
    int selected = 0;
    int count = japanese_candidates(a->input.jp, choices, link_settings.candidates, &selected);
    paint(a, choices, count, selected);
}

static void repaint(gpointer data) {
    struct app *a = data;
    if (!a->active || !a->visible) return;
    if (a->status) { paint_status(a); return; }
    if (a->input.mode == INPUT_JAPANESE) { paint_japanese(a); return; }
    RIME_STRUCT(RimeContext, ctx);
    if (a->rime->get_context(a->session, &ctx)) {
        paint_context(a, &ctx);
        a->rime->free_context(&ctx);
    }
}

static void publish(struct app *a) {
    clear_status(a);
    if (a->input.mode == INPUT_JAPANESE) {
        g_autofree char *commit = japanese_take_commit(a->input.jp);
        if (commit && *commit) zwp_input_method_v2_commit_string(a->im, commit);
        const char *text = japanese_preedit(a->input.jp);
        int cursor = (int)strlen(text);
        zwp_input_method_v2_set_preedit_string(a->im, text, cursor, cursor);
        paint_japanese(a);
        zwp_input_method_v2_commit(a->im, a->serial);
        return;
    }
    RIME_STRUCT(RimeCommit, commit);
    if (a->rime->get_commit(a->session, &commit)) {
        zwp_input_method_v2_commit_string(a->im, commit.text);
        a->rime->free_commit(&commit);
    }
    RIME_STRUCT(RimeContext, ctx);
    if (a->rime->get_context(a->session, &ctx)) {
        const char *preedit = ctx.composition.preedit ? ctx.composition.preedit : "";
        int cursor = ctx.composition.cursor_pos;
        zwp_input_method_v2_set_preedit_string(a->im, preedit, cursor, cursor);
        paint_context(a, &ctx);
        a->rime->free_context(&ctx);
    } else {
        zwp_input_method_v2_set_preedit_string(a->im, "", 0, 0);
        hide(a);
    }
    zwp_input_method_v2_commit(a->im, a->serial);
}

static void scale_changed(void *data, struct wl_surface *surface, int32_t scale) {
    struct app *a = data;
    if (scale >= 1 && scale <= 8 && scale != a->scale) {
        a->scale = scale;
        repaint(a);
    }
}
static void surface_output(void *data, struct wl_surface *surface, struct wl_output *output) {}
static void transform_changed(void *data, struct wl_surface *surface, uint32_t transform) {}
static const struct wl_surface_listener surface_listener = {
    .enter = surface_output, .leave = surface_output,
    .preferred_buffer_scale = scale_changed, .preferred_buffer_transform = transform_changed,
};

static void keymap(void *data, struct zwp_input_method_keyboard_grab_v2 *grab,
                   uint32_t format, int32_t fd, uint32_t size) {
    struct app *a = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !size) goto fail;
    const char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) goto fail;
    struct xkb_keymap *parsed = xkb_keymap_new_from_buffer(a->xkb, map,
        map[size - 1] == '\0' ? size - 1 : size, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap((void *)map, size);
    if (!parsed) goto fail;
    struct xkb_state *state = xkb_state_new(parsed);
    xkb_keymap_unref(parsed);
    if (!state) goto fail;
    xkb_state_unref(a->keys);
    a->keys = state;
    zwp_virtual_keyboard_v1_keymap(a->vk, format, fd, size);
    close(fd);
    return;
fail:
    close(fd);
    fprintf(stderr, "无法读取 Wayland 键盘布局\n");
    a->failed = true;
}

static bool mod(struct app *a, const char *name) {
    return xkb_state_mod_name_is_active(a->keys, name, XKB_STATE_MODS_EFFECTIVE) > 0;
}

static bool editable(const struct app *a) {
    return a->active && !(a->hint & HINT_SENSITIVE) &&
           a->purpose != PURPOSE_PASSWORD && a->purpose != PURPOSE_PIN;
}

static void toggle_mode(struct app *a) {
    const char *label = input_cycle(&a->input, a->rime, a->session);
    if (!label) return;
    a->repeat_code = -1;
    publish(a);
    show_status(a, label);
}

static void key(void *data, struct zwp_input_method_keyboard_grab_v2 *grab,
                uint32_t serial, uint32_t time, uint32_t code, uint32_t state) {
    struct app *a = data;
    if (!a->keys) return;
    if (code >= KEYS) { zwp_virtual_keyboard_v1_key(a->vk, time, code, state); return; }
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (a->repeat_code == (int32_t)code) a->repeat_code = -1;
        if (a->tap_shift == (int32_t)code) {
            a->tap_shift = -1;
            if (editable(a)) toggle_mode(a);
        }
        if (a->forwarded[code]) zwp_virtual_keyboard_v1_key(a->vk, time, code, state);
        a->forwarded[code] = a->consumed[code] = false;
        return;
    }
    if (a->forwarded[code]) { zwp_virtual_keyboard_v1_key(a->vk, time, code, state); return; }
    if (a->consumed[code]) return;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(a->keys, code + 8);
    bool ctrl = mod(a, XKB_MOD_NAME_CTRL), shift = mod(a, XKB_MOD_NAME_SHIFT);
    bool alt = mod(a, XKB_MOD_NAME_ALT), super = mod(a, XKB_MOD_NAME_LOGO);
    a->tap_shift = -1;
    a->repeat_code = -1;
    if (sym == XKB_KEY_Shift_L || sym == XKB_KEY_Shift_R) {
        if (editable(a) && !ctrl && !shift && !alt && !super)
            a->tap_shift = (int32_t)code;
        goto forward;
    }
    if (editable(a) && ctrl && alt && !shift && !super && sym >= XKB_KEY_1 && sym <= XKB_KEY_3 &&
        input_select(&a->input, a->rime, a->session, (InputMode)(sym - XKB_KEY_1))) {
        publish(a);
        show_status(a, input_mode_label(a->input.mode));
        a->consumed[code] = true;
        return;
    }
    if (editable(a) && ctrl && !shift && !alt && !super && sym == XKB_KEY_space) {
        toggle_mode(a);
        a->consumed[code] = true;
        return;
    }
    if (editable(a) && !ctrl && !alt && !super &&
        input_key(&a->input, a->rime, a->session, sym, shift)) {
        publish(a);
        if (sym == XKB_KEY_BackSpace && !shift && a->repeat_rate > 0) {
            a->repeat_code = (int32_t)code;
            a->repeat_at = g_get_monotonic_time() + 1000LL * MAX(a->repeat_delay, 1);
        }
        a->consumed[code] = true;
        return;
    }
forward:
    zwp_virtual_keyboard_v1_key(a->vk, time, code, state);
    a->forwarded[code] = true;
}

static void modifiers(void *data, struct zwp_input_method_keyboard_grab_v2 *grab,
                      uint32_t serial, uint32_t depressed, uint32_t latched,
                      uint32_t locked, uint32_t group) {
    struct app *a = data;
    if (!a->keys) return;
    xkb_state_update_mask(a->keys, depressed, latched, locked, 0, 0, group);
    zwp_virtual_keyboard_v1_modifiers(a->vk, depressed, latched, locked, group);
}
static void repeat(void *data, struct zwp_input_method_keyboard_grab_v2 *grab,
                   int32_t rate, int32_t delay) {
    struct app *a = data;
    a->repeat_rate = CLAMP(rate, 0, 1000);
    a->repeat_delay = MAX(delay, 0);
    if (!a->repeat_rate) a->repeat_code = -1;
}
static const struct zwp_input_method_keyboard_grab_v2_listener keyboard_listener = {
    keymap, key, modifiers, repeat
};

static void activate(void *data, struct zwp_input_method_v2 *im) {
    struct app *a = data;
    a->next_active = a->reset = true;
    a->hint = a->purpose = 0;
}
static void deactivate(void *data, struct zwp_input_method_v2 *im) {
    struct app *a = data;
    a->next_active = false;
    a->reset = true;
}
static void surrounding(void *data, struct zwp_input_method_v2 *im,
                        const char *text, uint32_t cursor, uint32_t anchor) {}
static void cause(void *data, struct zwp_input_method_v2 *im, uint32_t value) {}
static void content(void *data, struct zwp_input_method_v2 *im,
                    uint32_t hint, uint32_t purpose) {
    struct app *a = data;
    a->hint = hint;
    a->purpose = purpose;
}
static void sync_grab(struct app *a) {
    if (!a->vk) return;
    if (editable(a) && !a->grab) {
        a->grab = zwp_input_method_v2_grab_keyboard(a->im);
        zwp_input_method_keyboard_grab_v2_add_listener(a->grab, &keyboard_listener, a);
    } else if (!editable(a) && a->grab) {
        zwp_input_method_keyboard_grab_v2_release(a->grab);
        a->grab = NULL;
        memset(a->consumed, 0, sizeof a->consumed);
        memset(a->forwarded, 0, sizeof a->forwarded);
        a->repeat_code = a->tap_shift = -1;
    }
}

static void done(void *data, struct zwp_input_method_v2 *im) {
    struct app *a = data;
    a->serial++;
    a->active = a->next_active;
    if (a->reset || (a->hint & HINT_SENSITIVE) ||
        a->purpose == PURPOSE_PASSWORD || a->purpose == PURPOSE_PIN) {
        a->caret_known = false;
        a->repeat_code = a->tap_shift = -1;
        input_clear(&a->input, a->rime, a->session);
        if (a->visible && a->active) publish(a);
        else hide(a);
        a->reset = false;
    }
    sync_grab(a);
}
static void unavailable(void *data, struct zwp_input_method_v2 *im) {
    struct app *a = data;
    a->unavailable = true;
    fprintf(stderr, "Wayland 输入法席位不可用，可能已被 Fcitx5 或其他输入法占用。\n");
}
static const struct zwp_input_method_v2_listener im_listener = {
    activate, deactivate, surrounding, cause, content, done, unavailable
};

static void rect(void *data, struct zwp_input_popup_surface_v2 *popup,
                 int32_t x, int32_t y, int32_t w, int32_t h) {
    struct app *a = data;
    const int32_t next[] = {x, y, w, h};
    if (a->caret_known && !memcmp(a->caret, next, sizeof next)) return;
    memcpy(a->caret, next, sizeof next);
    a->caret_known = true;
    if (a->panel_wait && h > 0) {
        g_clear_handle_id(&a->panel_wait, g_source_remove);
        draw_panel(a);
    }
}
static const struct zwp_input_popup_surface_v2_listener popup_listener = {rect};

static void global(void *data, struct wl_registry *reg, uint32_t id,
                   const char *interface, uint32_t version) {
    struct app *a = data;
#define BIND(field, iface, ver) \
    if (!a->field && !strcmp(interface, #iface)) \
        a->field = wl_registry_bind(reg, id, &iface##_interface, MIN(version, ver))
    BIND(compositor, wl_compositor, 6);
    BIND(shm, wl_shm, 1);
    BIND(seat, wl_seat, 1);
    BIND(manager, zwp_input_method_manager_v2, 1);
    BIND(vk_manager, zwp_virtual_keyboard_manager_v1, 1);
#undef BIND
}
static void removed(void *data, struct wl_registry *reg, uint32_t id) {}
static const struct wl_registry_listener registry_listener = {global, removed};

static int engine_check(struct app *a) {
    if (!input_select_rime(a->rime, a->session, FULL_PINYIN)) return 1;
    for (const char *p = "nihao"; *p; p++)
        if (!a->rime->process_key(a->session, *p, 0)) return 1;
    RIME_STRUCT(RimeContext, ctx);
    if (!a->rime->get_context(a->session, &ctx)) return 1;
    printf("Rime 每页上限：%d 个候选词\n", ctx.menu.page_size);
    bool correct = ctx.menu.num_candidates > 0 && !strcmp(ctx.menu.candidates[0].text, "你好");
    a->rime->free_context(&ctx);
    if (!correct || !a->rime->process_key(a->session, XKB_KEY_space, 0)) return 1;
    RIME_STRUCT(RimeCommit, commit);
    if (!a->rime->get_commit(a->session, &commit)) return 1;
    correct = !strcmp(commit.text, "你好");
    printf("nihao + 空格 → %s\n", commit.text);
    a->rime->free_commit(&commit);
    return !correct;
}

static int run(struct app *a) {
    a->display = wl_display_connect(NULL);
    if (!a->display) { fprintf(stderr, "无法连接 Wayland\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(a->display);
    wl_registry_add_listener(reg, &registry_listener, a);
    if (wl_display_roundtrip(a->display) < 0 || !a->manager || !a->vk_manager ||
        !a->seat || !a->shm || !a->compositor || wl_compositor_get_version(a->compositor) < 4) {
        fprintf(stderr, "Wayland 会话缺少所需协议\n");
        return 1;
    }
    a->im = zwp_input_method_manager_v2_get_input_method(a->manager, a->seat);
    zwp_input_method_v2_add_listener(a->im, &im_listener, a);
    if (wl_display_roundtrip(a->display) < 0 || a->unavailable) return 1;
    a->xkb = xkb_context_new(0);
    if (!a->xkb) return 1;
    a->vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(a->vk_manager, a->seat);
    a->surface = wl_compositor_create_surface(a->compositor);
    wl_surface_add_listener(a->surface, &surface_listener, a);
    a->popup = zwp_input_method_v2_get_input_popup_surface(a->im, a->surface);
    zwp_input_popup_surface_v2_add_listener(a->popup, &popup_listener, a);
    sync_grab(a);
    if (wl_display_roundtrip(a->display) < 0 || a->unavailable || a->failed) return 1;
    fprintf(stderr, "link输入法已连接 Wayland；单击 Shift 或按 Ctrl+空格循环切换中、英、日。\n");
    theme_start(&a->theme, repaint, a);
    GMainContext *context = g_main_context_default();
    g_autoptr(GArray) fds = g_array_new(FALSE, FALSE, sizeof(GPollFD));
    while (!a->unavailable && !a->failed) {
        if (wl_display_dispatch_pending(a->display) < 0) break;
        if (a->unavailable || a->failed) break;
        GPollFD fd = {.fd = wl_display_get_fd(a->display), .events = G_IO_IN};
        if (wl_display_flush(a->display) < 0) {
            if (errno != EAGAIN) break;
            fd.events |= G_IO_OUT;
        }
        int timeout = -1;
        if (a->repeat_code >= 0) {
            gint64 remaining = a->repeat_at - g_get_monotonic_time();
            timeout = (int)CLAMP((remaining + 999) / 1000, 0, INT_MAX);
        }
        gint priority, glib_timeout, n;
        g_main_context_acquire(context);
        g_main_context_prepare(context, &priority);
        do {
            n = g_main_context_query(context, priority, &glib_timeout,
                                     (GPollFD *)fds->data, (gint)fds->len);
            if (n <= (gint)fds->len) break;
            g_array_set_size(fds, n);
        } while (TRUE);
        g_array_set_size(fds, n);
        g_array_append_val(fds, fd);
        if (glib_timeout >= 0 && (timeout < 0 || glib_timeout < timeout))
            timeout = glib_timeout;
        int result = g_poll((GPollFD *)fds->data, n + 1, timeout);
        int saved_errno = errno;
        fd = g_array_index(fds, GPollFD, n);
        if (result >= 0 && g_main_context_check(context, priority, (GPollFD *)fds->data, n))
            g_main_context_dispatch(context);
        g_main_context_release(context);
        if (result < 0) {
            if (saved_errno == EINTR) continue;
            break;
        }
        if (fd.revents & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) break;
        if ((fd.revents & G_IO_IN) && wl_display_dispatch(a->display) < 0) break;
        if (a->repeat_code >= 0 && g_get_monotonic_time() >= a->repeat_at) {
            if (editable(a) && input_key(&a->input, a->rime, a->session, XKB_KEY_BackSpace, false)) {
                publish(a);
                a->repeat_at = g_get_monotonic_time() + 1000000LL / a->repeat_rate;
            } else {
                a->repeat_code = -1;
            }
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    bool check = argc == 2 && !strcmp(argv[1], "--check-engine");
    if (argc != 1 && !check) { fprintf(stderr, "用法：%s [--check-engine]\n", argv[0]); return 2; }
    link_settings_load();
    struct app a = {.scale = 1, .repeat_code = -1, .tap_shift = -1};
    int result = engine_start(&a) ? (check ? engine_check(&a) : run(&a)) : 1;
    clear_status(&a);
    g_clear_handle_id(&a.panel_wait, g_source_remove);
    theme_stop(&a.theme);
    if (a.input.jp) japanese_free(a.input.jp);
    panel_content_clear(&a.panel);
    if (a.panel_frame) wl_callback_destroy(a.panel_frame);
    for (unsigned i = 0; i < G_N_ELEMENTS(a.panel_buffers); i++)
        free_panel_buffer(&a.panel_buffers[i]);
    if (a.display) wl_display_disconnect(a.display);
    xkb_state_unref(a.keys);
    xkb_context_unref(a.xkb);
    if (a.session) a.rime->destroy_session(a.session);
    if (a.rime) a.rime->finalize();
    return result;
}

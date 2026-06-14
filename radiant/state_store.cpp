#include "state_store.hpp"
#include "state_store_internal.hpp"
#include "animation.h"
#include "dom_range.hpp"
#include "dom_range_resolver.hpp"
#include "source_pos_bridge.hpp"   // R7 step 3c — register path recorder
#include "../lib/log.h"
#include "../lib/mem_factory.h"
#include "../lib/memtrack.h"
#include "../lib/hashmap_helpers.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "event_state_log.hpp"
#include "form_control.hpp"
#include "text_control.hpp"
#include "text_edit.hpp"
#include "editing.hpp"
#include "state_machine.hpp"
#include "state_schema.hpp"
#include "retained_display_list.hpp"
#include "../lambda/ast.hpp"
#include "../lambda/mark_builder.hpp"
// str.h included via view.hpp
#include "view.hpp"
#include "../lib/tagged.hpp"
#include "../lib/arraylist.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <direct.h>
  #include <process.h>
  #define getpid _getpid
#else
  #include <unistd.h>
#endif

// Declared in event.cpp
extern bool is_view_focusable(View* view);

static void focus_sync_text_control_state(DocState* state, View* view);

HASHMAP_DEFINE_FIELD2_KEY(state_key, StateEntry, key.node, key.name)
HASHMAP_DEFINE_FIELD2_KEY(view_state_entry, ViewStateEntry, view_id, kind)

static void view_state_release_payload(ViewState* view_state);
static ViewState* view_state_get_for_kind(DocState* state, View* view, ViewStateKind kind);
static FormControlProp* form_prop_for_view(View* view);
static bool view_element_has_attr(View* view, const char* attr_name);
static int form_default_selected_index_from_tree(View* view);
static float form_default_range_value(View* view, FormControlProp* form);

typedef struct DumpNodeState {
    void* node;
    uint64_t flags;
    bool has_value;
    Item value;
    bool has_sel_start;
    bool has_sel_end;
    int64_t sel_start;
    int64_t sel_end;
} DumpNodeState;

HASHMAP_DEFINE_PTRKEY(dump_node_state, DumpNodeState, node)

typedef struct StateDumpLog {
    FILE* out;
    char path[512];
    char doc_id[96];
    int pid;
    uint64_t seq;
    bool enabled;
} StateDumpLog;

enum {
    DUMP_FLAG_HOVER             = 1ull << 0,
    DUMP_FLAG_ACTIVE            = 1ull << 1,
    DUMP_FLAG_FOCUS             = 1ull << 2,
    DUMP_FLAG_FOCUS_VISIBLE     = 1ull << 3,
    DUMP_FLAG_FOCUS_WITHIN      = 1ull << 4,
    DUMP_FLAG_CHECKED           = 1ull << 5,
    DUMP_FLAG_DISABLED          = 1ull << 6,
    DUMP_FLAG_SELECTED          = 1ull << 7,
    DUMP_FLAG_TARGET            = 1ull << 8,
    DUMP_FLAG_PLACEHOLDER_SHOWN = 1ull << 9,
    DUMP_FLAG_VISITED           = 1ull << 10,
    DUMP_FLAG_INDETERMINATE     = 1ull << 11,
    DUMP_FLAG_ENABLED           = 1ull << 12,
    DUMP_FLAG_READONLY          = 1ull << 13,
    DUMP_FLAG_VALID             = 1ull << 14,
    DUMP_FLAG_INVALID           = 1ull << 15,
    DUMP_FLAG_REQUIRED          = 1ull << 16,
    DUMP_FLAG_OPTIONAL          = 1ull << 17,
    DUMP_FLAG_EMPTY             = 1ull << 18,
};

typedef struct DumpBuildContext {
    DocState* state;
    Input* input;
    MarkBuilder* builder;
    HashMap* by_node;
} DumpBuildContext;

static void view_state_release_all_payloads(HashMap* view_state_map) {
    if (!view_state_map) return;
    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(view_state_map, &iter, &item)) {
        ViewStateEntry* entry = (ViewStateEntry*)item;
        if (entry) view_state_release_payload(entry->state);
    }
}

// ============================================================================
// Interned State Names
// ============================================================================

// Static storage for interned state name strings
// These pointers are used for fast pointer comparison
static const char* s_interned_names[64] = {0};
static int s_interned_count = 0;
static bool s_interned_initialized = false;

static const char* intern_state_name(const char* name) {
    if (!name) return NULL;

    // Check if already interned
    for (int i = 0; i < s_interned_count; i++) {
        if (strcmp(s_interned_names[i], name) == 0) {
            return s_interned_names[i];
        }
    }

    // Intern new name (static storage, never freed)
    if (s_interned_count < 64) {
        char* interned = mem_strdup(name, MEM_CAT_LAYOUT);
        s_interned_names[s_interned_count++] = interned;
        return interned;
    }

    // Fallback: just use the provided pointer
    log_error("state name intern table full, name: %s", name);
    return name;
}

// Initialize common state names at startup
static void init_interned_names(void) {
    if (s_interned_initialized) return;
    s_interned_initialized = true;

    // Pre-intern all common state names
    intern_state_name(STATE_HOVER);
    intern_state_name(STATE_ACTIVE);
    intern_state_name(STATE_FOCUS);
    intern_state_name(STATE_FOCUS_WITHIN);
    intern_state_name(STATE_FOCUS_VISIBLE);
    intern_state_name(STATE_VISITED);
    intern_state_name(STATE_LINK);
    intern_state_name(STATE_CHECKED);
    intern_state_name(STATE_INDETERMINATE);
    intern_state_name(STATE_DISABLED);
    intern_state_name(STATE_ENABLED);
    intern_state_name(STATE_READONLY);
    intern_state_name(STATE_VALID);
    intern_state_name(STATE_INVALID);
    intern_state_name(STATE_REQUIRED);
    intern_state_name(STATE_OPTIONAL);
    intern_state_name(STATE_PLACEHOLDER);
    intern_state_name(STATE_SELECTED);
    intern_state_name(STATE_EMPTY);
    intern_state_name(STATE_TARGET);
    intern_state_name(STATE_VALUE);
    intern_state_name(STATE_SELECTION_START);
    intern_state_name(STATE_SELECTION_END);
    intern_state_name(STATE_CARET_OFFSET);
    intern_state_name(STATE_CARET_LINE);
    intern_state_name(STATE_CARET_COLUMN);
    intern_state_name(STATE_ANCHOR_OFFSET);
    intern_state_name(STATE_ANCHOR_LINE);
    intern_state_name(STATE_FOCUS_OFFSET);
    intern_state_name(STATE_FOCUS_LINE);
    intern_state_name(STATE_SCROLL_X);
    intern_state_name(STATE_SCROLL_Y);
}

void radiant_state_cleanup_interned_names(void) {
    for (int i = 0; i < s_interned_count; i++) {
        if (s_interned_names[i]) {
            mem_free((void*)s_interned_names[i]);
            s_interned_names[i] = NULL;
        }
    }
    s_interned_count = 0;
    s_interned_initialized = false;
}

// ============================================================================
// Mark State Dump
// ============================================================================

static void state_dump_make_temp_dir(void) {
#if defined(_WIN32) && !defined(__MINGW32__)
    _mkdir("temp");
    _mkdir("temp\\state");
#elif defined(_WIN32)
    mkdir("temp");
    mkdir("temp/state");
#else
    mkdir("temp", 0755);
    mkdir("temp/state", 0755);
#endif
}

static void state_dump_sanitize_doc_name(const char* in, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!in || !*in) {
        snprintf(out, out_sz, "doc");
        return;
    }

    const char* base = in;
    for (const char* p = in; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    size_t i = 0;
    for (const char* p = base; *p && i + 1 < out_sz && i < 64; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
            out[i++] = c;
        } else {
            out[i++] = '_';
        }
    }
    if (i == 0) {
        snprintf(out, out_sz, "doc");
    } else {
        out[i] = '\0';
    }
}

StateDumpLog* radiant_state_dump_open(const char* doc_name) {
    state_dump_make_temp_dir();

    StateDumpLog* dump = (StateDumpLog*)mem_calloc(1, sizeof(StateDumpLog), MEM_CAT_SYSTEM);
    if (!dump) return NULL;

    dump->pid = (int)getpid();
    state_dump_sanitize_doc_name(doc_name, dump->doc_id, sizeof(dump->doc_id));
    snprintf(dump->path, sizeof(dump->path), "./temp/state/state_%d_%s.mark",
             dump->pid, dump->doc_id);

    dump->out = fopen(dump->path, "w");
    if (!dump->out) {
        log_error("state_dump: failed to open %s: %s", dump->path, strerror(errno));
        mem_free(dump);
        return NULL;
    }

    dump->enabled = true;
    log_info("state_dump: opened %s (doc_id=%s)", dump->path, dump->doc_id);
    return dump;
}

void radiant_state_dump_close(StateDumpLog* dump) {
    if (!dump) return;
    if (dump->out) {
        fflush(dump->out);
        fclose(dump->out);
    }
    dump->out = NULL;
    dump->enabled = false;
    mem_free(dump);
}

bool radiant_state_dump_enabled(StateDumpLog* dump) {
    return dump && dump->enabled && dump->out;
}

const char* radiant_state_dump_path(StateDumpLog* dump) {
    return radiant_state_dump_enabled(dump) ? dump->path : NULL;
}

void radiant_state_set_dump_log(DocState* state, StateDumpLog* dump) {
    if (state) state->state_dump_log = dump;
}

static int state_dump_child_index(const DomNode* node) {
    if (!node || !node->parent || !node->parent->as_element()) return 0;
    int index = 0;
    const DomNode* scan = node->parent->as_element()->first_child;
    while (scan && scan != node) {
        index++;
        scan = scan->next_sibling;
    }
    return index;
}

static void state_dump_node_ref(const DomNode* node, char* buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) return;
    buf[0] = '\0';
    if (!node) return;

    const DomElement* el = node->as_element();
    if (el && el->id && el->id[0]) {
        snprintf(buf, buf_sz, "#%s", el->id);
        return;
    }

    const DomNode* chain[64];
    int depth = 0;
    const DomNode* cur = node;
    while (cur && depth < 64) {
        chain[depth++] = cur;
        cur = cur->parent;
    }

    size_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        const DomNode* n = chain[i];
        if (!n->is_element()) continue;
        const char* name = n->node_name();
        if (!name || !name[0]) name = "node";
        int written = 0;
        if (n->parent) {
            written = snprintf(buf + pos, pos < buf_sz ? buf_sz - pos : 0,
                               "%s%s.%d", pos == 0 ? "" : ".", name,
                               state_dump_child_index(n));
        } else {
            written = snprintf(buf + pos, pos < buf_sz ? buf_sz - pos : 0,
                               "%s%s", pos == 0 ? "" : ".", name);
        }
        if (written < 0) break;
        if ((size_t)written >= (pos < buf_sz ? buf_sz - pos : 0)) {
            pos = buf_sz - 1;
            break;
        }
        pos += (size_t)written;
    }
    buf[buf_sz - 1] = '\0';
}

static bool state_dump_float_changed(float value) {
    return fabsf(value) > 0.0001f;
}

static bool state_dump_float_differs(float a, float b) {
    return fabsf(a - b) > 0.0001f;
}

static bool state_dump_item_truthy(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_NULL || item.item == ItemNull.item) return false;
    if (type == LMD_TYPE_BOOL) return item.bool_val != 0;
    return true;
}

static bool state_dump_item_int(Item item, int64_t* out_value) {
    if (!out_value) return false;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) {
        *out_value = item.int_val;
        return true;
    }
    if (type == LMD_TYPE_INT64 && item.int64_ptr) {
        *out_value = *(int64_t*)(uintptr_t)item.int64_ptr;
        return true;
    }
    return false;
}

static uint64_t state_dump_flag_for_name(const char* name) {
    if (!name || name[0] != ':') return 0;
    if (strcmp(name, STATE_HOVER) == 0) return DUMP_FLAG_HOVER;
    if (strcmp(name, STATE_ACTIVE) == 0) return DUMP_FLAG_ACTIVE;
    if (strcmp(name, STATE_FOCUS) == 0) return DUMP_FLAG_FOCUS;
    if (strcmp(name, STATE_FOCUS_VISIBLE) == 0) return DUMP_FLAG_FOCUS_VISIBLE;
    if (strcmp(name, STATE_FOCUS_WITHIN) == 0) return DUMP_FLAG_FOCUS_WITHIN;
    if (strcmp(name, STATE_CHECKED) == 0) return DUMP_FLAG_CHECKED;
    if (strcmp(name, STATE_DISABLED) == 0) return DUMP_FLAG_DISABLED;
    if (strcmp(name, STATE_SELECTED) == 0) return DUMP_FLAG_SELECTED;
    if (strcmp(name, STATE_TARGET) == 0) return DUMP_FLAG_TARGET;
    if (strcmp(name, STATE_PLACEHOLDER) == 0) return DUMP_FLAG_PLACEHOLDER_SHOWN;
    if (strcmp(name, STATE_VISITED) == 0) return DUMP_FLAG_VISITED;
    if (strcmp(name, STATE_INDETERMINATE) == 0) return DUMP_FLAG_INDETERMINATE;
    if (strcmp(name, STATE_ENABLED) == 0) return DUMP_FLAG_ENABLED;
    if (strcmp(name, STATE_READONLY) == 0) return DUMP_FLAG_READONLY;
    if (strcmp(name, STATE_VALID) == 0) return DUMP_FLAG_VALID;
    if (strcmp(name, STATE_INVALID) == 0) return DUMP_FLAG_INVALID;
    if (strcmp(name, STATE_REQUIRED) == 0) return DUMP_FLAG_REQUIRED;
    if (strcmp(name, STATE_OPTIONAL) == 0) return DUMP_FLAG_OPTIONAL;
    if (strcmp(name, STATE_EMPTY) == 0) return DUMP_FLAG_EMPTY;
    return 0;
}

static DumpNodeState* state_dump_get_node_state(HashMap* by_node, void* node, bool create) {
    if (!by_node || !node) return NULL;
    DumpNodeState query;
    memset(&query, 0, sizeof(query));
    query.node = node;
    DumpNodeState* found = (DumpNodeState*)hashmap_get(by_node, &query);
    if (found || !create) return found;
    hashmap_set(by_node, &query);
    return (DumpNodeState*)hashmap_get(by_node, &query);
}

static void state_dump_collect_state_map(DocState* state, HashMap* by_node) {
    if (!state || !state->state_map || !by_node) return;

    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(state->state_map, &iter, &item)) {
        StateEntry* entry = (StateEntry*)item;
        if (!entry || !entry->key.node || !entry->key.name) continue;

        DumpNodeState* ns = state_dump_get_node_state(by_node, entry->key.node, true);
        if (!ns) continue;

        uint64_t flag = state_dump_flag_for_name(entry->key.name);
        if (flag != 0) {
            if (state_dump_item_truthy(entry->value)) ns->flags |= flag;
            continue;
        }

        if (strcmp(entry->key.name, STATE_VALUE) == 0 &&
            entry->value.item != ItemNull.item) {
            ns->has_value = true;
            ns->value = entry->value;
            continue;
        }

        int64_t ivalue = 0;
        if (strcmp(entry->key.name, STATE_SELECTION_START) == 0 &&
            state_dump_item_int(entry->value, &ivalue)) {
            ns->has_sel_start = true;
            ns->sel_start = ivalue;
        } else if (strcmp(entry->key.name, STATE_SELECTION_END) == 0 &&
                   state_dump_item_int(entry->value, &ivalue)) {
            ns->has_sel_end = true;
            ns->sel_end = ivalue;
        }
    }
}

static void state_dump_append_flag(ArrayBuilder& arr, MarkBuilder* builder,
                                   uint64_t flags, uint64_t bit,
                                   const char* name) {
    if ((flags & bit) == 0) return;
    arr.append(builder->createSymbolItem(name));
}

static Item state_dump_flags_item(DumpBuildContext* ctx, uint64_t flags) {
    ArrayBuilder arr = ctx->builder->array();
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_HOVER, "hover");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_ACTIVE, "active");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_FOCUS, "focus");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_FOCUS_VISIBLE, "focus-visible");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_FOCUS_WITHIN, "focus-within");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_CHECKED, "checked");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_DISABLED, "disabled");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_SELECTED, "selected");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_TARGET, "target");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_PLACEHOLDER_SHOWN, "placeholder-shown");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_VISITED, "visited");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_INDETERMINATE, "indeterminate");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_ENABLED, "enabled");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_READONLY, "readonly");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_VALID, "valid");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_INVALID, "invalid");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_REQUIRED, "required");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_OPTIONAL, "optional");
    state_dump_append_flag(arr, ctx->builder, flags, DUMP_FLAG_EMPTY, "empty");
    return arr.final();
}

static bool state_dump_string_differs(const char* value, uint32_t value_len,
                                      const char* base) {
    uint32_t base_len = base ? (uint32_t)strlen(base) : 0;
    if (!value) value_len = 0;
    if (value_len != base_len) return true;
    if (value_len == 0) return false;
    return memcmp(value, base, value_len) != 0;
}

static Item state_dump_scroll_item(DumpBuildContext* ctx, const ViewState* vs, bool* out_any) {
    *out_any = false;
    if (!vs || vs->kind != VIEW_STATE_SCROLL) return ItemNull;

    const ViewState* s = vs;
    bool changed = state_dump_float_changed(s->data.scroll.x) ||
        state_dump_float_changed(s->data.scroll.y) ||
        s->data.scroll.h_dragging || s->data.scroll.v_dragging ||
        s->data.scroll.h_hovered || s->data.scroll.v_hovered;
    if (!changed) return ItemNull;

    MapBuilder map = ctx->builder->map();
    if (state_dump_float_changed(s->data.scroll.x)) map.put("x", (double)s->data.scroll.x);
    if (state_dump_float_changed(s->data.scroll.y)) map.put("y", (double)s->data.scroll.y);
    if (state_dump_float_changed(s->data.scroll.max_x)) map.put("max_x", (double)s->data.scroll.max_x);
    if (state_dump_float_changed(s->data.scroll.max_y)) map.put("max_y", (double)s->data.scroll.max_y);
    if (s->data.scroll.h_dragging) map.put("h_dragging", true);
    if (s->data.scroll.v_dragging) map.put("v_dragging", true);
    *out_any = true;
    return map.final();
}

static Item state_dump_form_item(DumpBuildContext* ctx, View* view,
                                 const ViewState* vs, uint64_t* io_flags,
                                 bool* out_any) {
    *out_any = false;
    if (!view || !vs || vs->kind != VIEW_STATE_FORM_CONTROL) return ItemNull;

    FormControlProp* form = form_prop_for_view(view);
    const ViewState* s = vs;
    bool checked_default = view_element_has_attr(view, "checked");
    bool disabled_default = view_element_has_attr(view, "disabled");
    bool readonly_default = view_element_has_attr(view, "readonly");
    bool required_default = view_element_has_attr(view, "required");
    int selected_default = form_default_selected_index_from_tree(view);
    float range_default = form_default_range_value(view, form);

    if ((s->data.form.checked != 0) != checked_default) *io_flags |= DUMP_FLAG_CHECKED;
    if ((s->data.form.disabled != 0) != disabled_default) *io_flags |= DUMP_FLAG_DISABLED;
    if ((s->data.form.readonly != 0) != readonly_default) *io_flags |= DUMP_FLAG_READONLY;
    if ((s->data.form.required != 0) != required_default) *io_flags |= DUMP_FLAG_REQUIRED;

    MapBuilder map = ctx->builder->map();
    bool any = false;
    if (s->data.form.selected_index != selected_default) {
        map.put("selected_index", (int64_t)s->data.form.selected_index);
        any = true;
    }
    if (state_dump_float_differs(s->data.form.range_value, range_default)) {
        map.put("range_value", (double)s->data.form.range_value);
        any = true;
    }
    if (s->data.form.dropdown_open) {
        map.put("dropdown_open", true);
        any = true;
    }
    if (s->data.form.hover_index != -1) {
        map.put("hover_index", (int64_t)s->data.form.hover_index);
        any = true;
    }
    if (s->data.form.has_current_value) {
        const char* base = NULL;
        if (view->is_element()) {
            DomElement* elem = lam::dom_require_element(view);
            base = elem ? elem->get_attribute("value") : NULL;
        }
        if (state_dump_string_differs(s->data.form.current_value,
                                      s->data.form.current_value_len, base)) {
            map.put("value", ctx->builder->createStringItem(
                s->data.form.current_value ? s->data.form.current_value : "",
                s->data.form.current_value_len));
            any = true;
        }
    }
    if (s->data.form.selection_start != 0 || s->data.form.selection_end != 0) {
        ArrayBuilder sel = ctx->builder->array();
        sel.append((int64_t)s->data.form.selection_start);
        sel.append((int64_t)s->data.form.selection_end);
        map.put("sel", sel.final());
        any = true;
    }

    *out_any = any;
    return any ? map.final() : ItemNull;
}

static Item state_dump_pair_item(DumpBuildContext* ctx, int64_t a, int64_t b) {
    ArrayBuilder arr = ctx->builder->array();
    arr.append(a);
    arr.append(b);
    return arr.final();
}

static void state_dump_emit_doc_attrs(DumpBuildContext* ctx, ElementBuilder& doc) {
    DocState* state = ctx->state;
    char ref[512];

    if (state->focus && state->focus->current) {
        MapBuilder map = ctx->builder->map();
        state_dump_node_ref((const DomNode*)state->focus->current, ref, sizeof(ref));
        map.put("target", ref);
        if (state->focus->focus_visible) map.put("visible", true);
        if (state->focus->from_keyboard) map.put("from_keyboard", true);
        if (state->focus->from_mouse) map.put("from_mouse", true);
        doc.attr("focus", map.final());
    }

    if (state->caret && state->caret->view) {
        MapBuilder map = ctx->builder->map();
        state_dump_node_ref((const DomNode*)state->caret->view, ref, sizeof(ref));
        map.put("target", ref);
        map.put("offset", (int64_t)state->caret->char_offset);
        if (state->caret->line != 0) map.put("line", (int64_t)state->caret->line);
        if (state->caret->column != 0) map.put("column", (int64_t)state->caret->column);
        doc.attr("caret", map.final());
    }

    if (state->selection && !state->selection->is_collapsed) {
        MapBuilder map = ctx->builder->map();
        MapBuilder anchor = ctx->builder->map();
        state_dump_node_ref((const DomNode*)state->selection->anchor_view, ref, sizeof(ref));
        anchor.put("node", ref);
        anchor.put("offset", (int64_t)state->selection->anchor_offset);
        map.put("anchor", anchor.final());
        MapBuilder focus = ctx->builder->map();
        state_dump_node_ref((const DomNode*)state->selection->focus_view, ref, sizeof(ref));
        focus.put("node", ref);
        focus.put("offset", (int64_t)state->selection->focus_offset);
        map.put("focus", focus.final());
        doc.attr("selection", map.final());
    }

    if (state->hover_target) {
        state_dump_node_ref((const DomNode*)state->hover_target, ref, sizeof(ref));
        doc.attr("hover", ref);
    }
    if (state->active_target) {
        state_dump_node_ref((const DomNode*)state->active_target, ref, sizeof(ref));
        doc.attr("active", ref);
    }
    if (state->drag_target) {
        state_dump_node_ref((const DomNode*)state->drag_target, ref, sizeof(ref));
        doc.attr("drag", ref);
    }
    if (state->drag_drop && (state->drag_drop->active || state->drag_drop->pending)) {
        MapBuilder map = ctx->builder->map();
        if (state->drag_drop->source_view) {
            state_dump_node_ref((const DomNode*)state->drag_drop->source_view, ref, sizeof(ref));
            map.put("source", ref);
        }
        if (state->drag_drop->drop_target) {
            state_dump_node_ref((const DomNode*)state->drag_drop->drop_target, ref, sizeof(ref));
            map.put("target", ref);
        }
        if (state->drag_drop->active) map.put("active", true);
        if (state->drag_drop->pending) map.put("pending", true);
        doc.attr("drag_drop", map.final());
    }
    if (state->open_dropdown) {
        MapBuilder map = ctx->builder->map();
        state_dump_node_ref((const DomNode*)state->open_dropdown, ref, sizeof(ref));
        map.put("owner", ref);
        map.put("x", (double)state->dropdown_x);
        map.put("y", (double)state->dropdown_y);
        map.put("w", (double)state->dropdown_width);
        map.put("h", (double)state->dropdown_height);
        doc.attr("dropdown", map.final());
    }
    if (state->context_menu_target) {
        MapBuilder map = ctx->builder->map();
        state_dump_node_ref((const DomNode*)state->context_menu_target, ref, sizeof(ref));
        map.put("target", ref);
        if (state->context_menu_hover != -1) map.put("hover", (int64_t)state->context_menu_hover);
        doc.attr("context_menu", map.final());
    }
    if (state_dump_float_changed(state->scroll_x) || state_dump_float_changed(state->scroll_y)) {
        MapBuilder map = ctx->builder->map();
        if (state_dump_float_changed(state->scroll_x)) map.put("x", (double)state->scroll_x);
        if (state_dump_float_changed(state->scroll_y)) map.put("y", (double)state->scroll_y);
        doc.attr("scroll", map.final());
    }
    if (state_dump_float_differs(state->zoom_level, 1.0f)) {
        doc.attr("zoom", (double)state->zoom_level);
    }
    if (state->is_dirty || state->needs_reflow || state->needs_repaint ||
        state->reflow_scheduler.pending || state->dirty_tracker.dirty_list ||
        state->dirty_tracker.full_repaint || state->dirty_tracker.full_reflow) {
        MapBuilder map = ctx->builder->map();
        if (state->is_dirty) map.put("dirty", true);
        if (state->needs_reflow || state->dirty_tracker.full_reflow) map.put("reflow", true);
        if (state->needs_repaint || state->dirty_tracker.full_repaint) map.put("repaint", true);
        int64_t pending = 0;
        for (ReflowRequest* req = state->reflow_scheduler.pending; req; req = req->next) pending++;
        if (pending > 0) map.put("pending", pending);
        int64_t rects = 0;
        for (DirtyRect* rect = state->dirty_tracker.dirty_list; rect; rect = rect->next) rects++;
        if (rects > 0) map.put("rects", rects);
        doc.attr("render", map.final());
    }
    if (state->editing.has_active_surface || state->editing.pointer_selecting ||
        state->editing.composing || state->editing.autoscroll.active) {
        MapBuilder map = ctx->builder->map();
        if (state->editing.has_active_surface) map.put("active_surface", true);
        if (state->editing.pointer_selecting) map.put("pointer_selecting", true);
        if (state->editing.composing) map.put("composing", true);
        if (state->editing.autoscroll.active) map.put("autoscroll", true);
        doc.attr("editing", map.final());
    }
    if (state->visited_links && state->visited_links->url_hash_set &&
        hashmap_count(state->visited_links->url_hash_set) > 0) {
        MapBuilder map = ctx->builder->map();
        map.put("count", (int64_t)hashmap_count(state->visited_links->url_hash_set));
        doc.attr("visited", map.final());
    }
    if (state->has_active_video || state->video_frame_pending ||
        state->video_controls.hover_video || state->video_controls.is_seeking) {
        MapBuilder map = ctx->builder->map();
        if (state->has_active_video) map.put("active", true);
        if (state->video_frame_pending) map.put("frame_pending", true);
        if (state->video_controls.hover_video) map.put("controls_hover", true);
        if (state->video_controls.is_seeking) map.put("seeking", true);
        if (state_dump_float_changed(state->video_controls.seek_fraction)) {
            map.put("seek_fraction", (double)state->video_controls.seek_fraction);
        }
        doc.attr("video", map.final());
    }
}

static Item state_dump_build_element(DumpBuildContext* ctx, DomElement* elem) {
    if (!ctx || !ctx->builder || !elem) return ItemNull;

    ArrayList* children = arraylist_new(4);
    bool has_child = false;
    if (!children) return ItemNull;

    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        Item child_item = state_dump_build_element(ctx, lam::dom_require_element(child));
        if (child_item.item != ItemNull.item) {
            arraylist_append(children, (void*)(uintptr_t)child_item.item);
            has_child = true;
        }
    }

    DumpNodeState* ns = state_dump_get_node_state(ctx->by_node, elem, false);
    uint64_t flags = ns ? ns->flags : 0;

    View* elem_view = (View*)elem;
    ViewState* vs = view_state_get(ctx->state, elem_view);
    if (vs) {
        if (vs->flags.hovered) flags |= DUMP_FLAG_HOVER;
        if (vs->flags.active) flags |= DUMP_FLAG_ACTIVE;
        if (vs->flags.focused) flags |= DUMP_FLAG_FOCUS;
    }

    bool scroll_any = false;
    ViewState* scroll_vs = view_state_get_for_kind(ctx->state, elem_view, VIEW_STATE_SCROLL);
    Item scroll_item = state_dump_scroll_item(ctx, scroll_vs, &scroll_any);
    bool form_any = false;
    ViewState* form_vs = view_state_get_for_kind(ctx->state, elem_view, VIEW_STATE_FORM_CONTROL);
    Item form_item = state_dump_form_item(ctx, elem_view, form_vs, &flags, &form_any);

    bool has_state = flags != 0 || scroll_any || form_any ||
        (ns && (ns->has_value || ns->has_sel_start || ns->has_sel_end));
    if (!has_state && !has_child) {
        arraylist_free(children);
        return ItemNull;
    }

    const char* tag = elem->node_name();
    if (!tag || !tag[0]) tag = "node";
    ElementBuilder eb = ctx->builder->element(tag);
    if (elem->parent) eb.attr("index", (int64_t)state_dump_child_index(elem));
    if (elem->id && elem->id[0]) eb.attr("id", elem->id);

    if (flags != 0) eb.attr("flags", state_dump_flags_item(ctx, flags));
    if (scroll_any) eb.attr("scroll", scroll_item);
    if (form_any) eb.attr("form", form_item);
    if (ns && ns->has_value) eb.attr("value", ctx->builder->deep_copy(ns->value));
    if (ns && (ns->has_sel_start || ns->has_sel_end)) {
        int64_t start = ns->has_sel_start ? ns->sel_start : 0;
        int64_t end = ns->has_sel_end ? ns->sel_end : start;
        eb.attr("sel", state_dump_pair_item(ctx, start, end));
    }

    for (int i = 0; i < children->length; i++) {
        Item child_item;
        child_item.item = (uint64_t)(uintptr_t)children->data[i];
        eb.child(child_item);
    }
    arraylist_free(children);
    return eb.final();
}

static Item state_dump_build_doc_item(DocState* state, Input* input) {
    if (!state || !state->owner_store || !state->owner_store->document ||
        !state->owner_store->document->root || !input) {
        MarkBuilder builder(input);
        return builder.element("doc").final();
    }

    MarkBuilder builder(input);
    HashMap* by_node = dump_node_state_new(64);
    state_dump_collect_state_map(state, by_node);

    DumpBuildContext ctx;
    ctx.state = state;
    ctx.input = input;
    ctx.builder = &builder;
    ctx.by_node = by_node;

    ElementBuilder doc = builder.element("doc");
    state_dump_emit_doc_attrs(&ctx, doc);

    Item root_item = state_dump_build_element(&ctx, state->owner_store->document->root);
    if (root_item.item != ItemNull.item) doc.child(root_item);

    if (by_node) hashmap_free(by_node);
    return doc.final();
}

StrBuf* radiant_state_dump_mark(DocState* state) {
    Pool* scratch_pool = mem_pool_create(NULL, MEM_ROLE_VIEW, "state.dump.scratch");
    if (!scratch_pool) return NULL;

    Input* input = Input::create(scratch_pool, nullptr, nullptr);
    if (!input) {
        mem_pool_destroy(scratch_pool);
        return NULL;
    }

    Item doc_item = state_dump_build_doc_item(state, input);
    StrBuf* out = strbuf_new_cap(1024);
    if (out) print_root_item(out, doc_item);

    if (input->type_list) {
        arraylist_free(input->type_list);
        input->type_list = NULL;
    }
    mem_pool_destroy(scratch_pool);
    return out;
}

bool radiant_state_dump_to_file(DocState* state, const char* path) {
    if (!state || !path) return false;
    StrBuf* mark = radiant_state_dump_mark(state);
    if (!mark) return false;

    FILE* out = fopen(path, "w");
    if (!out) {
        log_error("state_dump: failed to write %s: %s", path, strerror(errno));
        strbuf_free(mark);
        return false;
    }
    if (mark->str && mark->length > 0) fwrite(mark->str, 1, mark->length, out);
    fclose(out);
    strbuf_free(mark);
    return true;
}

void radiant_state_dump_emit_cascade(DocState* state, uint64_t cascade_id) {
    if (!state || !radiant_state_dump_enabled(state->state_dump_log)) return;

    StateDumpLog* dump = state->state_dump_log;
    StrBuf* mark = radiant_state_dump_mark(state);
    if (!mark) return;

    uint64_t seq = ++dump->seq;
    uint64_t out_cascade = cascade_id != 0 ? cascade_id : seq;
    char header[128];
    int header_len = snprintf(header, sizeof(header),
                              "<entry cascade:%" PRIu64 ", seq:%" PRIu64 ";\n",
                              out_cascade, seq);
    if (header_len > 0) {
        size_t write_len = (size_t)header_len;
        if (write_len >= sizeof(header)) write_len = sizeof(header) - 1;
        fwrite(header, 1, write_len, dump->out);
    }
    if (mark->str && mark->length > 0) {
        fwrite(mark->str, 1, mark->length, dump->out);
        if (mark->str[mark->length - 1] != '\n') fputc('\n', dump->out);
    }
    fwrite(">\n", 1, 2, dump->out);
    fflush(dump->out);
    strbuf_free(mark);
}

// ============================================================================
// State Store Creation/Destruction
// ============================================================================

DocState* radiant_state_create(Pool* pool, StateUpdateMode mode) {
    init_interned_names();

    if (!pool) {
        log_error("radiant_state_create: pool is NULL");
        return NULL;
    }

    DocState* state = (DocState*)pool_calloc(pool, sizeof(DocState));
    if (!state) {
        log_error("radiant_state_create: failed to allocate DocState");
        return NULL;
    }

    state->pool = pool;
    state->mode = mode;
    state->lifecycle = DOC_LIFECYCLE_UNINITIALIZED;
    state->version = 1;
    state->zoom_level = 1.0f;

    // Create dedicated arena for state allocations
    state->arena = mem_arena_create(NULL, pool, MEM_ROLE_VIEW, "state.arena");
    if (!state->arena) {
        log_error("radiant_state_create: failed to create arena");
        return NULL;
    }

    // Create state hashmap
    state->state_map = hashmap_new(
        sizeof(StateEntry),
        64,  // initial capacity
        0x12345678, 0x87654321,  // hash seeds
        state_key_hash,
        state_key_cmp,
        NULL,  // no element free function
        NULL   // no user data
    );
    if (!state->state_map) {
        log_error("radiant_state_create: failed to create state_map");
        arena_destroy(state->arena);
        return NULL;
    }

    state->view_state_map = view_state_entry_new(64);
    if (!state->view_state_map) {
        log_error("radiant_state_create: failed to create view_state_map");
        hashmap_free(state->state_map);
        state->state_map = NULL;
        arena_destroy(state->arena);
        state->arena = NULL;
        return NULL;
    }

    state->retained_dl_cache = retained_dl_cache_create(pool);
    if (!state->retained_dl_cache) {
        log_error("radiant_state_create: failed to create retained display-list cache");
        hashmap_free(state->view_state_map);
        state->view_state_map = NULL;
        hashmap_free(state->state_map);
        state->state_map = NULL;
        arena_destroy(state->arena);
        state->arena = NULL;
        return NULL;
    }

    // Initialize dirty tracker arena
    state->dirty_tracker.arena = mem_arena_create(NULL, pool, MEM_ROLE_VIEW, "state.dirty");

    // Initialize reflow scheduler arena
    state->reflow_scheduler.arena = mem_arena_create(NULL, pool, MEM_ROLE_VIEW, "state.reflow");

    // Initialize template reactive state: create the map and inject it into
    // the global template state store so Lambda views use the same map.
    tmpl_state_init();
    state->template_state_map = tmpl_state_get_map();

    // Initialize render map for observer-based reconciliation (Phase 3)
    render_map_init();
    state->render_map = render_map_get_map();

    // R7 step 3c — register the source-path recorder so apply() persists
    // child-index paths into the editor bridge's path side-table.
    render_map_set_path_recorder(&render_map_record_path);

    // Initialize animation scheduler
    state->animation_scheduler = animation_scheduler_create(pool);

    // F8: context-menu starts hidden; -1 indicates "no item highlighted".
    state->context_menu_target = nullptr;
    state->context_menu_hover = -1;

    log_debug("radiant_state_create: created state store with mode %d", mode);
    return state;
}

StateStore* state_store_create(DomDocument* document) {
    if (!document) {
        log_error("state_store_create: document is NULL");
        return NULL;
    }
    if (document->state_store) {
        if (document->state_store->doc_state && !document->state) {
            document->state = document->state_store->doc_state;
        }
        return document->state_store;
    }

    StateStore* store = (StateStore*)pool_calloc(document->pool, sizeof(StateStore));
    if (!store) {
        log_error("state_store_create: failed to allocate StateStore");
        return NULL;
    }

    store->document = document;
    store->pool = document->pool;
    store->arena = mem_arena_create(NULL, document->pool, MEM_ROLE_VIEW, "state.store");
    if (!store->arena) {
        log_error("state_store_create: failed to create StateStore arena");
        return NULL;
    }

    store->doc_state = document->state ? document->state : radiant_state_create(document->pool, STATE_MODE_IN_PLACE);
    if (!store->doc_state) {
        log_error("state_store_create: failed to create DocState");
        arena_destroy(store->arena);
        store->arena = NULL;
        return NULL;
    }

    document->state_store = store;
    document->state = store->doc_state;
    store->doc_state->owner_store = store;
    if (store->doc_state->lifecycle == DOC_LIFECYCLE_UNINITIALIZED) {
        doc_state_set_lifecycle(store->doc_state, DOC_LIFECYCLE_LOADING);
    }
    log_debug("state_store_create: created StateStore for document");
    return store;
}

DocState* state_store_doc_state(StateStore* store) {
    return store ? store->doc_state : NULL;
}

void state_store_destroy(DomDocument* document) {
    if (!document) return;
    StateStore* store = document->state_store;
    DocState* state = store ? store->doc_state : document->state;
    if (state) {
        doc_state_set_lifecycle(state, DOC_LIFECYCLE_UNLOADED);
        state->owner_store = NULL;
        radiant_state_destroy(state);
    }
    if (store && store->arena) {
        arena_destroy(store->arena);
        store->arena = NULL;
    }
    if (store) {
        store->doc_state = NULL;
    }
    document->state_store = NULL;
    document->state = NULL;
}

DocState* radiant_document_ensure_state(DomDocument* document, const char* owner) {
    const char* prefix = owner ? owner : "radiant_document_ensure_state";
    if (!document) {
        log_error("%s: document is NULL", prefix);
        return NULL;
    }
    StateStore* store = state_store_create(document);
    DocState* state = state_store_doc_state(store);
    if (!state) {
        log_error("%s: failed to create StateStore", prefix);
        return NULL;
    }
    return state;
}

void radiant_document_destroy_state(DomDocument* document) {
    state_store_destroy(document);
}

// ----------------------------------------------------------------------------
// Bridge accessors for radiant/dom_range.cpp.
// dom_range.cpp deliberately does NOT include state_store.hpp (which would
// pull in GLFW + the full render stack into every unit test). It declares
// these two functions extern "C" and we provide the production
// implementation here. Unit tests can supply their own implementation
// against a minimal DocState stub.
// ----------------------------------------------------------------------------
struct DomRange;
extern "C" Arena* dom_range_state_arena(DocState* state) {
    return state ? state->arena : NULL;
}
extern "C" DomRange** dom_range_state_live_ranges_slot(DocState* state) {
    return state ? (DomRange**)&state->live_ranges : NULL;
}
extern "C" struct DomSelection* dom_range_state_selection(DocState* state) {
    return state ? state->dom_selection : NULL;
}

// ----------------------------------------------------------------------------
// Selection/caret machine helpers.
// EditingSelection is the canonical StateStore selection surface. DOM ranges
// and text controls publish through separate writers, while CaretState and
// SelectionState remain private render/event projections.
// ----------------------------------------------------------------------------
static bool state_store_ensure_projection_storage(DocState* state);

static DomSelection* sync_ensure_selection(DocState* state) {
    if (!state) return NULL;
    if (!state->dom_selection) {
        state->dom_selection = dom_selection_create(state);
    }
    if (state->dom_selection) {
        state_store_ensure_projection_storage(state);
    }
    return state->dom_selection;
}

static DomSelectionDirection editing_direction_from_text_control(uint32_t start_u16,
                                                                 uint32_t end_u16,
                                                                 uint8_t direction) {
    if (start_u16 == end_u16) return DOM_SEL_DIR_NONE;
    return direction == 2 ? DOM_SEL_DIR_BACKWARD : DOM_SEL_DIR_FORWARD;
}

extern "C" void state_store_refresh_editing_selection_shadow(DocState* state) {
    if (!state) return;

    EditingSelection* shadow = &state->sel;
    memset(shadow, 0, sizeof(*shadow));
    shadow->kind = EDIT_SEL_NONE;
    shadow->direction = DOM_SEL_DIR_NONE;
    shadow->mutation_seq = state->selection_mutation_seq;

    DomSelection* dom_selection = state->dom_selection;
    if (!dom_selection || dom_selection->range_count == 0) return;

    shadow->kind = EDIT_SEL_DOM_RANGE;
    shadow->direction = dom_selection->direction;
    shadow->range = dom_selection->ranges[0];
}

extern "C" bool state_store_editing_selection_shadow_matches(DocState* state) {
    if (!state) return true;

    const EditingSelection* shadow = &state->sel;
    if (shadow->kind == EDIT_SEL_TEXT_CONTROL) {
        if (!shadow->control || shadow->range != NULL ||
            shadow->mutation_seq != state->selection_mutation_seq) {
            return false;
        }
        DomElement* control = shadow->control;
        if (!tc_is_text_control(control)) return false;
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        if (!form) return false;
        DomSelectionDirection direction = editing_direction_from_text_control(
            form->selection_start, form->selection_end, form->selection_direction);
        return shadow->direction == direction &&
               shadow->start_u16 == form->selection_start &&
               shadow->end_u16 == form->selection_end;
    }

    DomSelection* dom_selection = state->dom_selection;
    if (!dom_selection || dom_selection->range_count == 0) {
        return shadow->kind == EDIT_SEL_NONE &&
               shadow->direction == DOM_SEL_DIR_NONE &&
               shadow->range == NULL &&
               shadow->control == NULL &&
               shadow->start_u16 == 0 &&
               shadow->end_u16 == 0 &&
               shadow->mutation_seq == state->selection_mutation_seq;
    }

    return shadow->kind == EDIT_SEL_DOM_RANGE &&
           shadow->direction == dom_selection->direction &&
           shadow->range == dom_selection->ranges[0] &&
           shadow->control == NULL &&
           shadow->start_u16 == 0 &&
           shadow->end_u16 == 0 &&
           shadow->mutation_seq == state->selection_mutation_seq;
}

extern "C" void state_store_note_selection_mutation(DocState* state) {
    if (!state) return;
    state->selection_mutation_seq++;
    state->selection_layout_dirty = true;
}

extern "C" bool state_store_set_selection(DocState* state,
                                           const DomBoundary* anchor,
                                           const DomBoundary* focus,
                                           const char** out_exception) {
    if (!state) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    if (!anchor || !focus || !anchor->node || !focus->node) {
        dom_selection_remove_all_ranges(selection);
        state_store_refresh_caret_projection(state);
        return true;
    }

    bool ok = false;
    if (dom_boundary_compare(anchor, focus) == DOM_BOUNDARY_EQUAL) {
        ok = dom_selection_collapse(selection, anchor->node, anchor->offset, out_exception);
    } else {
        ok = dom_selection_set_base_and_extent(selection,
            anchor->node, anchor->offset, focus->node, focus->offset, out_exception);
    }
    return ok;
}

extern "C" bool state_store_add_selection_range(DocState* state,
                                                 DomRange* range,
                                                 const char** out_exception) {
    if (!state || !range) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    dom_selection_add_range(selection, range);
    return true;
}

extern "C" bool state_store_remove_selection_range(DocState* state,
                                                    DomRange* range,
                                                    const char** out_exception) {
    if (!state || !range) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    bool in_selection = false;
    for (uint32_t i = 0; i < selection->range_count; i++) {
        if (selection->ranges[i] == range) {
            in_selection = true;
            break;
        }
    }
    if (!in_selection) {
        if (out_exception) *out_exception = "NotFoundError";
        return false;
    }

    dom_selection_remove_range(selection, range);
    return true;
}

extern "C" bool state_store_modify_selection(DocState* state,
                                              const char* alter,
                                              const char* direction,
                                              const char* granularity,
                                              const char** out_exception) {
    if (!state) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    return dom_selection_modify(selection, alter, direction, granularity,
                                out_exception);
}

extern "C" bool state_store_delete_selection_from_document(
        DocState* state,
        const char** out_exception) {
    if (!state) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }

    DomSelection* selection = state->dom_selection;
    if (!selection || selection->range_count == 0 || !selection->ranges[0]) {
        return true;
    }
    if (dom_selection_is_collapsed(selection)) {
        return true;
    }

    DomRange* range = selection->ranges[0];
    const char* exc = NULL;
    if (!dom_range_delete_contents(range, &exc) || exc) {
        if (out_exception) *out_exception = exc ? exc : "InvalidStateError";
        return false;
    }

    if (range->start.node) {
        DomBoundary caret = { range->start.node, range->start.offset };
        return state_store_set_selection(state, &caret, &caret, out_exception);
    }
    return state_store_set_selection(state, NULL, NULL, out_exception);
}

// Canonical writer for text-control selection. `state->sel` is the StateStore
// canonical store; `form->selection_*` is the HTML-facing mirror written in
// lockstep here so JS (`selectionStart/End`) observes the same value. Both are
// only ever written together through this one function, and
// state_store_editing_selection_shadow_matches() asserts they agree. Every
// `form->selection_*` writer (tc_set_selection_range, tc_set_value) routes
// through here — do not write `form->selection_*` without publishing `sel`.
extern "C" void state_store_set_text_control_selection(DocState* state,
                                                        DomElement* control,
                                                        uint32_t start_u16,
                                                        uint32_t end_u16,
                                                        uint8_t direction) {
    if (!state || !control || !tc_is_text_control(control)) return;
    tc_ensure_init(control);
    FormControlProp* form = control->form;
    if (!form) return;

    uint32_t max_u16 = form->current_value_u16_len;
    if (start_u16 > max_u16) start_u16 = max_u16;
    if (end_u16 > max_u16) end_u16 = max_u16;
    if (start_u16 > end_u16) start_u16 = end_u16;
    direction &= 3;

    DomSelectionDirection editing_direction =
        editing_direction_from_text_control(start_u16, end_u16, direction);
    EditingSelection* sel = &state->sel;
    bool form_changed = form->selection_start != start_u16 ||
                        form->selection_end != end_u16 ||
                        form->selection_direction != direction;
    bool changed = form_changed ||
                   sel->kind != EDIT_SEL_TEXT_CONTROL ||
                   sel->control != control ||
                   sel->start_u16 != start_u16 ||
                   sel->end_u16 != end_u16 ||
                   sel->direction != editing_direction;
    if (changed) {
        state_store_note_selection_mutation(state);
    }

    form->selection_start = start_u16;
    form->selection_end = end_u16;
    form->selection_direction = direction;
    form->state_ref = state;
    form_control_sync_text_control_state(state, static_cast<View*>(control));

    memset(sel, 0, sizeof(*sel));
    sel->kind = EDIT_SEL_TEXT_CONTROL;
    sel->direction = editing_direction;
    sel->control = control;
    sel->start_u16 = start_u16;
    sel->end_u16 = end_u16;
    sel->mutation_seq = state->selection_mutation_seq;

    state_store_refresh_caret_projection(state);
}

static void selection_write_optional_ref(JsonWriter* w, const char* key, View* view) {
    event_state_log_write_node_ref(w, key, (const DomNode*)view);
}

static void state_transition_write_anchor(JsonWriter* w, DocState* state, View* view);

static const char* selection_state_name(DomSelection* selection) {
    if (!selection || selection->range_count == 0) return "SelectionEmpty";
    if (dom_selection_is_collapsed(selection)) return "CaretCollapsed";
    return selection->direction == DOM_SEL_DIR_BACKWARD ?
        "RangeSelectedBackward" : "RangeSelectedForward";
}

static void selection_log_transition(DocState* state, const char* transition,
                                     View* anchor_view, int anchor_offset,
                                     View* focus_view, int focus_offset) {
    if (!state || !event_state_log_enabled(state->active_event_log)) return;

    char buf[1024];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "doc");
        state_transition_write_anchor(&w, state, NULL);
        jw_kv_str(&w, "name", "selection");
        jw_key(&w, "old");
        jw_obj_begin(&w);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            jw_kv_str(&w, "machine", "selection");
            jw_kv_str(&w, "transition", transition ? transition : "selection_update");
            jw_kv_str(&w, "state", selection_state_name(state->dom_selection));
            jw_key(&w, "anchor");
            jw_obj_begin(&w);
                selection_write_optional_ref(&w, "node", anchor_view);
                jw_kv_int(&w, "offset", anchor_offset);
            jw_obj_end(&w);
            jw_key(&w, "focus");
            jw_obj_begin(&w);
                selection_write_optional_ref(&w, "node", focus_view);
                jw_kv_int(&w, "offset", focus_offset);
            jw_obj_end(&w);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static bool selection_is_text_control_view(View* view) {
    return view && view->is_element() && tc_is_text_control(lam::dom_require_element(view));
}

static bool state_store_ensure_projection_storage(DocState* state) {
    if (!state || !state->arena) return false;
    if (!state->caret) {
        state->caret = (CaretState*)arena_alloc(state->arena, sizeof(CaretState));
        if (state->caret) {
            memset(state->caret, 0, sizeof(CaretState));
            state->caret->prev_abs_x = -1;  // not yet rendered
        }
    }
    if (!state->selection) {
        state->selection = (SelectionState*)arena_alloc(state->arena, sizeof(SelectionState));
        if (state->selection) {
            memset(state->selection, 0, sizeof(SelectionState));
        }
    }
    return state->caret != NULL && state->selection != NULL;
}

// View* in the legacy API IS a DomNode* (typedef in dom_node.hpp).
// Convert (View*, byte_offset) → DomBoundary.
static DomBoundary boundary_from_legacy(View* view, int byte_offset) {
    DomBoundary b = { NULL, 0 };
    if (!view) return b;
    DomNode* n = static_cast<DomNode*>(view);
    if (n->is_text()) {
        DomText* t = lam::dom_require_text(n);
        uint32_t bo = byte_offset < 0 ? 0 : (uint32_t)byte_offset;
        b.node = n;
        b.offset = dom_text_utf8_to_utf16(t, bo);
    } else if (selection_is_text_control_view(view)) {
        DomElement* elem = lam::dom_require_element(view);
        tc_ensure_init(elem);
        FormControlProp* form = elem->form;
        uint32_t value_len = form && form->current_value ? form->current_value_len : 0;
        uint32_t bo = byte_offset < 0 ? 0 : (uint32_t)byte_offset;
        if (bo > value_len) bo = value_len;
        b.node = n;
        b.offset = bo;
    } else {
        // Element view: clamp offset to child count.
        b.node = n;
        uint32_t lim = dom_node_boundary_length(n);
        b.offset = byte_offset < 0 ? 0 :
                   ((uint32_t)byte_offset > lim ? lim : (uint32_t)byte_offset);
    }
    return b;
}

static uint32_t text_control_byte_offset_to_u16(FormControlProp* form,
                                                int byte_offset) {
    if (!form) return 0;
    uint32_t value_len = form->current_value ? form->current_value_len : 0;
    uint32_t byte = byte_offset < 0 ? 0 : (uint32_t)byte_offset;
    if (byte > value_len) byte = value_len;
    return tc_utf8_to_utf16_offset(form->current_value ? form->current_value : "",
        value_len, byte);
}

static uint32_t text_control_u16_offset_to_byte(FormControlProp* form,
                                                uint32_t u16_offset) {
    if (!form) return 0;
    uint32_t value_len = form->current_value ? form->current_value_len : 0;
    uint32_t max_u16 = form->current_value_u16_len;
    if (u16_offset > max_u16) u16_offset = max_u16;
    return tc_utf16_to_utf8_offset(form->current_value ? form->current_value : "",
        value_len, u16_offset);
}

static bool text_control_set_selection_from_byte_offsets(DocState* state,
                                                         View* view,
                                                         int anchor_byte,
                                                         int focus_byte,
                                                         const char* source) {
    if (!state || !selection_is_text_control_view(view)) return false;
    DomElement* elem = lam::dom_require_element(view);
    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    if (!form) return false;
    form->state_ref = state;

    uint32_t anchor_u16 = text_control_byte_offset_to_u16(form, anchor_byte);
    uint32_t focus_u16 = text_control_byte_offset_to_u16(form, focus_byte);
    uint32_t start_u16 = anchor_u16;
    uint32_t end_u16 = focus_u16;
    uint8_t direction = 0;
    if (anchor_u16 < focus_u16) {
        direction = 1;
    } else if (anchor_u16 > focus_u16) {
        start_u16 = focus_u16;
        end_u16 = anchor_u16;
        direction = 2;
    }

    tc_set_selection_range(elem, start_u16, end_u16, direction);
    log_debug("%s: text-control canonical selection anchor=%d focus=%d start=%u end=%u dir=%u",
        source ? source : "text_control_set_selection_from_byte_offsets",
        anchor_byte, focus_byte, start_u16, end_u16, direction);
    return true;
}

static DomNode* selection_sync_root_from_boundary(const DomBoundary* boundary) {
    if (!boundary || !boundary->node) return NULL;
    DomNode* root = boundary->node;
    while (root->parent) root = root->parent;
    return root;
}

static bool selection_sync_boundary_in_root(DomNode* root, const DomBoundary* boundary) {
    if (!root || !boundary || !boundary->node) return false;
    for (DomNode* cur = boundary->node; cur; cur = cur->parent) {
        if (cur == root) return true;
    }
    return false;
}

static bool selection_sync_rebind_boundary(DomNode* root,
                                           const DomBoundary* boundary,
                                           DomBoundary* out) {
    if (!root || !boundary || !boundary->node || !out) return false;
    if (selection_sync_boundary_in_root(root, boundary)) {
        *out = *boundary;
        return true;
    }

    SourcePosC source_pos;
    if (!source_pos_from_dom_boundary(boundary, &source_pos)) return false;
    DomBoundary rebound = {NULL, 0};
    bool ok = dom_boundary_from_source_pos(root, &source_pos, &rebound);
    source_pos_free(&source_pos);
    if (!ok || !rebound.node) return false;
    *out = rebound;
    log_debug("[DOM-SYNC REBIND] rebound legacy endpoint %p to current %p offset=%u",
        (void*)boundary->node, (void*)rebound.node, rebound.offset);
    return true;
}

static bool selection_extend_dom_to_focus(DomSelection* selection,
                                          const DomBoundary* focus,
                                          const char** out_exception) {
    if (!selection || !focus || !focus->node) return false;
    if (selection->range_count == 0) {
        return state_store_set_selection(selection->state, focus, focus, out_exception);
    }

    DomBoundary anchor = dom_selection_anchor_boundary(selection);
    DomNode* current_root = selection_sync_root_from_boundary(focus);
    if (current_root && !selection_sync_boundary_in_root(current_root, &anchor)) {
        DomBoundary rebound_anchor = anchor;
        if (!selection_sync_rebind_boundary(current_root, &anchor, &rebound_anchor)) {
            log_debug("selection_extend_dom_to_focus: skipped extend; anchor cannot resolve in current tree");
            return false;
        }
        DomBoundaryOrder rebound_order = dom_boundary_compare(&rebound_anchor, focus);
        if (rebound_order == DOM_BOUNDARY_DISJOINT) {
            return state_store_set_selection(selection->state, focus, focus, out_exception);
        }
        return state_store_set_selection(selection->state, &rebound_anchor, focus, out_exception);
    }

    DomBoundaryOrder order = dom_boundary_compare(&anchor, focus);
    if (order == DOM_BOUNDARY_DISJOINT) {
        return state_store_set_selection(selection->state, focus, focus, out_exception);
    }
    return state_store_set_selection(selection->state, &anchor, focus, out_exception);
}

extern "C" void dom_selection_sync_from_legacy_selection(DocState* state) {
    if (!state || !state->selection) return;
    SelectionState* sel = state->selection;
    DomBoundary anc = boundary_from_legacy(sel->anchor_view, sel->anchor_offset);
    DomBoundary foc = boundary_from_legacy(sel->focus_view,  sel->focus_offset);
    if (!anc.node || !foc.node) return;

    DomNode* current_root = selection_sync_root_from_boundary(&foc);
    if (!current_root) current_root = selection_sync_root_from_boundary(&anc);
    if (current_root) {
        DomBoundary rebound_anc = anc;
        DomBoundary rebound_foc = foc;
        if (!selection_sync_rebind_boundary(current_root, &anc, &rebound_anc) ||
            !selection_sync_rebind_boundary(current_root, &foc, &rebound_foc)) {
            log_debug("[DOM-SYNC REBIND] skipped legacy selection sync; endpoint cannot resolve in current tree");
            return;
        }
        anc = rebound_anc;
        foc = rebound_foc;
    }

    const char* exc = NULL;
    if (!state_store_set_selection(state, &anc, &foc, &exc)) {
        log_debug("[DOM-SYNC] set_base_and_extent rejected: %s", exc ? exc : "?");
    }
    state->selection_layout_dirty = true;
}

extern "C" void dom_selection_sync_from_legacy_caret(DocState* state) {
    if (!state || !state->caret) return;
    DomSelection* ds = sync_ensure_selection(state);
    if (!ds) return;
    // During an active drag-selection (or any non-collapsed projection
    // whose focus matches the caret), the state_store_legacy_caret_set() call is just
    // moving the focus end of the selection — NOT collapsing it. Mirroring
    // it as `collapse(...)` here would wipe out the selection that
    // `state_store_legacy_selection_extend()` just synced into DomSelection a moment ago.
    // In that case skip the sync; the projection-to-DomSelection
    // mirror in selection_extend already updated the focus boundary.
    SelectionState* sel = state->selection;
    if (sel && !sel->is_collapsed && sel->is_selecting &&
        sel->focus_view == state->caret->view &&
        sel->focus_offset == state->caret->char_offset) {
        return;
    }
    bool preserve_pointer_selection = sel && sel->is_selecting;
    View* preserve_anchor_view = sel ? sel->anchor_view : NULL;
    int preserve_anchor_offset = sel ? sel->anchor_offset : 0;
    DomBoundary b = boundary_from_legacy(state->caret->view, state->caret->char_offset);
    if (!b.node) return;
    const char* exc = NULL;
    if (!state_store_set_selection(state, &b, &b, &exc)) {
        log_debug("[DOM-SYNC] collapse rejected: %s", exc ? exc : "?");
    }
    if (preserve_pointer_selection && state->selection) {
        state->selection->is_selecting = true;
        if (preserve_anchor_view) {
            state->selection->anchor_view = preserve_anchor_view;
            state->selection->anchor_offset = preserve_anchor_offset;
        }
    }
    state->selection_layout_dirty = true;
}

static void caret_local_from_absolute(View* view, float abs_x, float abs_y,
                                      float* out_x, float* out_y) {
    float x = abs_x;
    float y = abs_y;
    View* parent = view ? view->parent : NULL;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK ||
            parent->view_type == RDT_VIEW_INLINE_BLOCK ||
            parent->view_type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = lam::view_require_block(parent);
            x -= block->x;
            y -= block->y;
            if (block->scroller && block->scroller->pane) {
                x += block->scroller->pane->h_scroll_position;
                y += block->scroller->pane->v_scroll_position;
            }
        }
        parent = parent->parent;
    }
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static void dom_boundary_to_legacy_projection(const DomBoundary& boundary,
                                              View** out_view, int* out_offset) {
    DomNode* node = boundary.node;
    if (!node) {
        if (out_view) *out_view = NULL;
        if (out_offset) *out_offset = 0;
        return;
    }
    if (out_view) *out_view = static_cast<View*>(node);
    if (!out_offset) return;
    if (node->is_text()) {
        DomText* text = lam::dom_require_text(node);
        uint32_t byte_offset = dom_text_utf16_to_utf8(text, boundary.offset);
        *out_offset = static_cast<int>(byte_offset);
    } else {
        *out_offset = static_cast<int>(boundary.offset);
    }
}

// ---------------------------------------------------------------------------
// Canonical selection → legacy projection refresh.
//
// The StateStore selection mutation sequence is authoritative. Legacy
// CaretState/SelectionState are cache projections for render/debug/event
// compatibility and are rebuilt when their projection stamp falls behind.
// Layout cache fields (caret x/y/height) are derived via the resolver.
// View* IS DomNode*, so node→view conversion is just a cast; UTF-16→UTF-8 for
// text offsets.
// ---------------------------------------------------------------------------
extern "C" void state_store_refresh_caret_projection(DocState* state) {
    if (!state) return;

    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        if (!state_store_ensure_projection_storage(state)) return;

        EditingSelection* current = &state->sel;
        DomElement* control = current->control;
        if (!control || !tc_is_text_control(control)) return;
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        if (!form) return;

        uint32_t start_u16 = current->start_u16;
        uint32_t end_u16 = current->end_u16;
        if (start_u16 > form->current_value_u16_len) {
            start_u16 = form->current_value_u16_len;
        }
        if (end_u16 > form->current_value_u16_len) {
            end_u16 = form->current_value_u16_len;
        }
        if (start_u16 > end_u16) start_u16 = end_u16;

        uint32_t start_byte = text_control_u16_offset_to_byte(form, start_u16);
        uint32_t end_byte = text_control_u16_offset_to_byte(form, end_u16);
        int start_offset = static_cast<int>(start_byte); // INT_CAST_OK: legacy projection stores text-control byte offsets as int.
        int end_offset = static_cast<int>(end_byte); // INT_CAST_OK: legacy projection stores text-control byte offsets as int.
        bool backward = current->direction == DOM_SEL_DIR_BACKWARD;
        int anchor_offset = backward ? end_offset : start_offset;
        int focus_offset = backward ? start_offset : end_offset;
        View* view = static_cast<View*>(control);

        SelectionState* legacy = state->selection;
        bool collapsed = start_u16 == end_u16;
        bool control_focused = state->focus && state->focus->current == view;
        bool caret_visible = collapsed && control_focused;
        CaretState* caret = state->caret;
        bool projection_changed =
            legacy->anchor_view != view ||
            legacy->focus_view != view ||
            legacy->view != view ||
            legacy->anchor_offset != anchor_offset ||
            legacy->focus_offset != focus_offset ||
            legacy->is_collapsed != collapsed ||
            caret->view != view ||
            caret->char_offset != focus_offset ||
            caret->visible != caret_visible;

        bool was_selecting = legacy->is_selecting;
        legacy->anchor_view = view;
        legacy->focus_view = view;
        legacy->view = view;
        legacy->anchor_offset = anchor_offset;
        legacy->focus_offset = focus_offset;
        legacy->is_collapsed = collapsed;
        legacy->is_selecting = was_selecting;

        caret->view = view;
        caret->char_offset = focus_offset;
        caret->visible = caret_visible;
        caret->blink_time = 0;

        state->selection_layout_dirty = false;
        state->selection_projection_seq = state->selection_mutation_seq;
        if (projection_changed) state->needs_repaint = true;
        log_debug("[SEL-PROJECT] text-control projection view=%p anchor=%d focus=%d collapsed=%d",
            (void*)view, anchor_offset, focus_offset, legacy->is_collapsed);
        return;
    }

    bool seq_current = state->selection_projection_seq == state->selection_mutation_seq;
    if (seq_current && !state->selection_layout_dirty) return;

    state_store_refresh_editing_selection_shadow(state);
    DomSelection* ds = state->dom_selection;
    if (!ds) return;

    // Empty selection: clear legacy state too.
    DomBoundary anchor = dom_selection_anchor_boundary(ds);
    DomBoundary focus = dom_selection_focus_boundary(ds);
    if (ds->range_count == 0 || !anchor.node) {
        if (state->selection) {
            state->selection->anchor_view = NULL;
            state->selection->focus_view = NULL;
            state->selection->view = NULL;
            state->selection->anchor_offset = 0;
            state->selection->focus_offset = 0;
            state->selection->is_collapsed = true;
            state->selection->is_selecting = false;
        }
        if (state->caret) {
            state->caret->view = NULL;
            state->caret->char_offset = 0;
            state->caret->visible = false;
        }
        state->selection_projection_seq = state->selection_mutation_seq;
        state->selection_layout_dirty = false;
        state->needs_repaint = true;
        return;
    }

    // Convert DomBoundary (node, utf16-or-child-offset) → (View*, byte_offset).
    View* anc_view = NULL; int anc_off = 0;
    View* foc_view = NULL; int foc_off = 0;
    dom_boundary_to_legacy_projection(anchor, &anc_view, &anc_off);
    dom_boundary_to_legacy_projection(focus,  &foc_view, &foc_off);

    if (!state_store_ensure_projection_storage(state)) {
        return;
    }
    SelectionState* sel = state->selection;
    sel->anchor_view   = anc_view;
    sel->focus_view    = foc_view;
    sel->view          = foc_view;  // legacy single-view fallback
    sel->anchor_offset = anc_off;
    sel->focus_offset  = foc_off;
    bool dom_collapsed = dom_selection_is_collapsed(ds);
    sel->is_collapsed  = dom_collapsed;

    {
        CaretState* caret = state->caret;
        // A collapsed DOM Selection can project its focus onto a non-editable
        // element (e.g. <body> via Selection.collapse). Such a position has no
        // rendered insertion point, so the legacy caret must not be marked
        // visible there — a visible caret is only valid inside editable content
        // (a text control, or a text node within editable flow). Marking it
        // visible on a non-text-control element violates the DocState
        // interaction invariant ("visible element caret target is not editable").
        bool caret_can_render = !foc_view ||
            !foc_view->is_element() ||
            tc_is_text_control(lam::dom_require_element(foc_view));
        caret->view        = foc_view;
        caret->char_offset = foc_off;
        caret->visible     = dom_collapsed && caret_can_render;
        caret->blink_time  = 0;

        // Resolve layout cache (x/y/height) via the resolver. Best-effort:
        // if no layout has run yet, leave previous cache values in place.
        DomRange* r = ds->ranges[0];
        if (r) {
            // Force re-resolve to reflect possibly-mutated layout.
            r->layout_valid = false;
            if (dom_range_resolve_layout(r)) {
                // Use END boundary (focus) for caret position when range is
                // forward-directed; spec direction tells us which is focus.
                bool focus_at_end = (ds->direction != DOM_SEL_DIR_BACKWARD);
                float abs_x = focus_at_end ? r->end_x : r->start_x;
                float abs_y = focus_at_end ? r->end_y : r->start_y;
                caret_local_from_absolute(foc_view, abs_x, abs_y,
                                          &caret->x, &caret->y);
                caret->height = focus_at_end ? r->end_height : r->start_height;
                // Mirror into selection start/end for legacy renderer.
                sel->start_x = r->start_x;
                sel->start_y = r->start_y;
                sel->end_x   = r->end_x;
                sel->end_y   = r->end_y;
            }
        }
    }

    state->selection_layout_dirty = false;
    state->selection_projection_seq = state->selection_mutation_seq;
    state->needs_repaint = true;
    log_debug("[SEL-PROJECT] state_store_refresh_caret_projection: anc=(%p,%d) foc=(%p,%d) collapsed=%d",
              (void*)anc_view, anc_off, (void*)foc_view, foc_off, dom_collapsed);
}

void radiant_state_destroy(DocState* state) {
    if (!state) return;

    // Detach template state map from global store before destroying
    if (state->template_state_map) {
        tmpl_state_set_map(NULL);
        state->template_state_map = NULL;
    }

    // Detach render map from global store before destroying
    if (state->render_map) {
        render_map_set_map(NULL);
        state->render_map = NULL;
    }

    if (state->state_map) {
        hashmap_free(state->state_map);
        state->state_map = NULL;
    }

    if (state->view_state_map) {
        view_state_release_all_payloads(state->view_state_map);
        hashmap_free(state->view_state_map);
        state->view_state_map = NULL;
    }

    if (state->retained_dl_cache) {
        retained_dl_cache_destroy(state->retained_dl_cache);
        state->retained_dl_cache = NULL;
    }

    if (state->arena) {
        arena_destroy(state->arena);
        state->arena = NULL;
    }

    if (state->dirty_tracker.arena) {
        arena_destroy(state->dirty_tracker.arena);
        state->dirty_tracker.arena = NULL;
    }

    if (state->reflow_scheduler.arena) {
        arena_destroy(state->reflow_scheduler.arena);
        state->reflow_scheduler.arena = NULL;
    }

    if (state->visited_links) {
        visited_links_destroy(state->visited_links);
        state->visited_links = NULL;
    }

    // Destroy animation scheduler
    if (state->animation_scheduler) {
        animation_scheduler_destroy(state->animation_scheduler);
        state->animation_scheduler = NULL;
    }

    log_debug("radiant_state_destroy: destroyed state store");
}

void radiant_state_reset(DocState* state) {
    if (!state) return;

    // Clear the state map
    hashmap_clear(state->state_map, false);
    view_state_release_all_payloads(state->view_state_map);
    hashmap_clear(state->view_state_map, false);

    // Reset arenas
    if (state->arena) {
        arena_reset(state->arena);
    }

    // Reset global state
    state->focus = NULL;
    state->hover_target = NULL;
    state->active_target = NULL;
    state->drag_target = NULL;
    state->caret = NULL;
    state->selection = NULL;
    state->cursor = NULL;
    state->scroll_x = 0;
    state->scroll_y = 0;
    state->editing_autoscroll_active = false;
    state->editing_autoscroll_surface = NULL;
    state->editing_autoscroll_pointer_x = 0.0f;
    state->editing_autoscroll_pointer_y = 0.0f;
    state->editing_tick_last_time = 0.0;
    state->editing_caret_blink_elapsed = 0.0;
    editing_surface_clear(&state->editing.active_surface);
    state->editing.has_active_surface = false;
    state->editing.pointer_selecting = false;
    state->editing.drag_mode = EDITING_DRAG_CHAR;
    state->editing.drag_anchor_view = NULL;
    state->editing.drag_anchor_offset = 0;
    state->editing.composing = false;
    state->editing.composition.active = false;
    editing_surface_clear(&state->editing.composition.surface);
    state->editing.composition.anchor_view = NULL;
    state->editing.composition.anchor_offset = 0;
    state->editing.composition.preedit_len = 0;
    state->editing.composition.dom_preedit_len = 0;
    state->editing.composition.commit_len = 0;
    state->editing.composition.caret = 0;
    state->editing.composition.update_count = 0;
    state->editing.composition.committed = false;
    state->editing.composition.canceled = false;
    state->editing.autoscroll.active = false;
    state->editing.autoscroll.surface = NULL;
    state->editing.autoscroll.pointer_x = 0.0f;
    state->editing.autoscroll.pointer_y = 0.0f;
    state->editing.autoscroll.tick_last_time = 0.0;
    state->editing.autoscroll.caret_blink_elapsed = 0.0;
    state->editing.rich_transaction_phase = EDITING_RICH_TX_IDLE;
    state->editing.rich_transaction_target = NULL;
    state->editing.rich_transaction_target_ranges_active = false;
    state->editing.rich_transaction_target_ranges_required = false;
    state->editing.rich_transaction_target_ranges_valid = true;
    state->editing.rich_transaction_input_type = 0;
    state->editing.rich_transaction_selection_seq = 0;
    state->editing.rich_transaction_target_range_count = 0;
    memset(state->editing.rich_transaction_target_ranges, 0,
           sizeof(state->editing.rich_transaction_target_ranges));

    // Reset dirty state
    state->is_dirty = false;
    state->needs_reflow = false;
    state->needs_repaint = false;
    dirty_clear(&state->dirty_tracker);
    reflow_clear(state);

    // Clear template reactive state
    tmpl_state_reset();

    // Clear render map
    render_map_reset();

    state->version++;

    log_debug("radiant_state_reset: reset state store to version %llu", state->version);
}

void editing_interaction_set_active_surface(DocState* state,
                                            const EditingSurface* surface) {
    if (!state) return;
    if (state->editing.composition.active) {
        if (!surface || surface->kind == EDIT_SURFACE_NONE) return;
        bool same_composition_surface =
            state->editing.composition.surface.kind == surface->kind &&
            state->editing.composition.surface.owner == surface->owner &&
            state->editing.composition.surface.view == surface->view;
        if (!same_composition_surface) return;
    }
    if (surface && surface->kind != EDIT_SURFACE_NONE) {
        bool same_surface = state->editing.has_active_surface &&
            state->editing.active_surface.kind == surface->kind &&
            state->editing.active_surface.owner == surface->owner &&
            state->editing.active_surface.view == surface->view;
        if (!same_surface) {
            state->editing.composing = false;
            state->editing.composition.active = false;
            editing_surface_clear(&state->editing.composition.surface);
        }
        state->editing.active_surface = *surface;
        state->editing.has_active_surface = true;
    } else {
        editing_surface_clear(&state->editing.active_surface);
        state->editing.has_active_surface = false;
        state->editing.composing = false;
        state->editing.composition.active = false;
        editing_surface_clear(&state->editing.composition.surface);
    }
}

void editing_interaction_set_autoscroll(DocState* state,
                                        bool active,
                                        View* surface,
                                        float pointer_x,
                                        float pointer_y) {
    if (!state) return;

    state->editing_autoscroll_active = active;
    state->editing_autoscroll_surface = active ? surface : NULL;
    state->editing_autoscroll_pointer_x = active ? pointer_x : 0.0f;
    state->editing_autoscroll_pointer_y = active ? pointer_y : 0.0f;

    state->editing.autoscroll.active = active;
    state->editing.autoscroll.surface = active ? surface : NULL;
    state->editing.autoscroll.pointer_x = active ? pointer_x : 0.0f;
    state->editing.autoscroll.pointer_y = active ? pointer_y : 0.0f;

    if (active && surface) {
        EditingSurface resolved;
        if (editing_surface_from_target(surface, &resolved)) {
            editing_interaction_set_active_surface(state, &resolved);
        }
    }
}

void editing_interaction_clear_autoscroll(DocState* state) {
    editing_interaction_set_autoscroll(state, false, NULL, 0.0f, 0.0f);
}

void editing_interaction_set_clock(DocState* state,
                                   double tick_last_time,
                                   double caret_blink_elapsed) {
    if (!state) return;
    state->editing_tick_last_time = tick_last_time;
    state->editing_caret_blink_elapsed = caret_blink_elapsed;
    state->editing.autoscroll.tick_last_time = tick_last_time;
    state->editing.autoscroll.caret_blink_elapsed = caret_blink_elapsed;
}

void editing_interaction_set_composing(DocState* state,
                                       const EditingSurface* surface,
                                       bool composing) {
    if (!state) return;
    if (surface && surface->kind != EDIT_SURFACE_NONE) {
        editing_interaction_set_active_surface(state, surface);
    }
    state->editing.composing = composing;
}

static void editing_interaction_begin_composition_raw(DocState* state,
                                                      const EditingSurface* surface,
                                                      View* anchor_view,
                                                      int anchor_offset) {
    if (!state || !surface || surface->kind == EDIT_SURFACE_NONE) return;
    editing_interaction_set_active_surface(state, surface);
    state->editing.composing = true;
    state->editing.composition.active = true;
    state->editing.composition.surface = *surface;
    state->editing.composition.anchor_view = anchor_view;
    state->editing.composition.anchor_offset = anchor_offset;
    state->editing.composition.preedit_len = 0;
    state->editing.composition.dom_preedit_len = 0;
    state->editing.composition.commit_len = 0;
    state->editing.composition.caret = 0;
    state->editing.composition.update_count = 0;
    state->editing.composition.committed = false;
    state->editing.composition.canceled = false;
}

void editing_interaction_begin_composition(DocState* state,
                                           const EditingSurface* surface,
                                           View* anchor_view,
                                           int anchor_offset) {
    if (!state || !surface || surface->kind == EDIT_SURFACE_NONE) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_IME, SM_EV_COMPOSITION_START,
                               surface ? surface->view : NULL);
    editing_interaction_begin_composition_raw(state, surface, anchor_view, anchor_offset);
    sm_guard.commit();
}

static void editing_interaction_update_composition_raw(DocState* state,
                                                       const EditingSurface* surface,
                                                       uint32_t preedit_len,
                                                       uint32_t caret) {
    if (!state || !surface || surface->kind == EDIT_SURFACE_NONE) return;
    if (!state->editing.composition.active) {
        View* anchor_view = surface->view;
        int anchor_offset = 0;
        editing_interaction_begin_composition_raw(state, surface,
                                                  anchor_view, anchor_offset);
    } else {
        editing_interaction_set_active_surface(state, surface);
        state->editing.composition.surface = *surface;
    }
    state->editing.composing = true;
    state->editing.composition.preedit_len = preedit_len;
    state->editing.composition.caret = caret;
    state->editing.composition.update_count++;
    state->editing.composition.committed = false;
    state->editing.composition.canceled = false;
}

void editing_interaction_update_composition(DocState* state,
                                            const EditingSurface* surface,
                                            uint32_t preedit_len,
                                            uint32_t caret) {
    if (!state || !surface || surface->kind == EDIT_SURFACE_NONE) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_IME, SM_EV_COMPOSITION_UPDATE,
                               surface ? surface->view : NULL);
    editing_interaction_update_composition_raw(state, surface, preedit_len, caret);
    sm_guard.commit();
}

static void editing_interaction_end_composition_raw(DocState* state,
                                                    const EditingSurface* surface,
                                                    uint32_t commit_len,
                                                    bool canceled) {
    if (!state) return;
    if (surface && surface->kind != EDIT_SURFACE_NONE) {
        editing_interaction_set_active_surface(state, surface);
        state->editing.composition.surface = *surface;
    }
    state->editing.composing = false;
    state->editing.composition.active = false;
    state->editing.composition.commit_len = commit_len;
    state->editing.composition.preedit_len = 0;
    state->editing.composition.dom_preedit_len = 0;
    state->editing.composition.caret = 0;
    state->editing.composition.committed = !canceled && commit_len > 0;
    state->editing.composition.canceled = canceled;
}

void editing_interaction_end_composition(DocState* state,
                                         const EditingSurface* surface,
                                         uint32_t commit_len,
                                         bool canceled) {
    if (!state) return;
    View* target = surface && surface->kind != EDIT_SURFACE_NONE
        ? surface->view : state->editing.composition.surface.view;
    SmTransitionGuard sm_guard(state, SM_FAMILY_IME,
                               canceled ? SM_EV_COMPOSITION_CANCEL : SM_EV_COMPOSITION_COMMIT,
                               target);
    editing_interaction_end_composition_raw(state, surface, commit_len, canceled);
    sm_guard.commit();
}

void editing_interaction_sync_projection(DocState* state) {
    if (!state) return;

    state->editing.autoscroll.active = state->editing_autoscroll_active;
    state->editing.autoscroll.surface = state->editing_autoscroll_surface;
    state->editing.autoscroll.pointer_x = state->editing_autoscroll_pointer_x;
    state->editing.autoscroll.pointer_y = state->editing_autoscroll_pointer_y;
    state->editing.autoscroll.tick_last_time = state->editing_tick_last_time;
    state->editing.autoscroll.caret_blink_elapsed = state->editing_caret_blink_elapsed;
    bool pointer_selecting = selection_is_pointer_range_active(state);
    state->editing.pointer_selecting = pointer_selecting;
    if (pointer_selecting && !state->editing.drag_anchor_view &&
        state->selection && state->selection->anchor_view) {
        state->editing.drag_anchor_view = state->selection->anchor_view;
        state->editing.drag_anchor_offset = state->selection->anchor_offset;
        state->editing.drag_mode = EDITING_DRAG_CHAR;
    } else if (!pointer_selecting && !state->text_selection_press_in_range) {
        state->editing.drag_anchor_view = NULL;
        state->editing.drag_anchor_offset = 0;
        state->editing.drag_mode = EDITING_DRAG_CHAR;
    }

    if (state->editing.composition.active &&
        state->editing.composition.surface.kind != EDIT_SURFACE_NONE) {
        state->editing.active_surface = state->editing.composition.surface;
        state->editing.has_active_surface = true;
        state->editing.composing = true;
        return;
    }

    EditingSurface surface;
    if (editing_surface_from_focus(state, &surface)) {
        editing_interaction_set_active_surface(state, &surface);
        return;
    }

    View* caret_view = caret_get_view(state);
    if (caret_view && editing_surface_from_target(caret_view, &surface)) {
        editing_interaction_set_active_surface(state, &surface);
        return;
    }

    editing_interaction_set_active_surface(state, NULL);
}

// ============================================================================
// State Get/Set Operations
// ============================================================================

Item state_get(DocState* state, void* node, const char* name) {
    if (!state || !node || !name) return ItemNull;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    const StateEntry* found = (const StateEntry*)hashmap_get(state->state_map, &query);
    if (found) {
        return found->value;
    }
    return ItemNull;
}

static void state_assert_after_mutation(DocState* state, const char* context);

void doc_state_set_lifecycle(DocState* state, DocLifecycleState lifecycle) {
    if (!state || state->lifecycle == lifecycle) return;

    SmEvent event = SM_EV_DOC_LOAD;
    if (lifecycle == DOC_LIFECYCLE_COMMITTED) {
        event = SM_EV_DOC_COMMIT;
    } else if (lifecycle == DOC_LIFECYCLE_UNLOADED) {
        event = SM_EV_DOC_UNLOAD;
    }

    SmTransitionGuard sm_guard(state, SM_FAMILY_DOCUMENT, event, NULL);
    state->lifecycle = lifecycle;
    state->version++;
    sm_guard.commit();
    state_assert_after_mutation(state, "doc_state_set_lifecycle");
}

bool state_get_bool(DocState* state, void* node, const char* name) {
    if (state && node && name) {
        View* view = static_cast<View*>(node);
        if (strcmp(name, STATE_HOVER) == 0) return view_state_get_hovered(state, view);
        if (strcmp(name, STATE_ACTIVE) == 0) return view_state_get_active(state, view);
        if (strcmp(name, STATE_FOCUS) == 0) return view_state_get_focused(state, view);
        if (strcmp(name, STATE_CHECKED) == 0) return form_control_get_checked(state, view);
        if (strcmp(name, STATE_DISABLED) == 0) return form_control_is_disabled(state, view);
        if (strcmp(name, STATE_READONLY) == 0) return form_control_is_readonly(state, view);
        if (strcmp(name, STATE_REQUIRED) == 0) return form_control_is_required(state, view);
    }

    Item value = state_get(state, node, name);
    if (value.item == ItemNull.item) return false;
    // Check bool type and value
    if ((value.item >> 56) == LMD_TYPE_BOOL) {
        return (value.item & 0xFF) != 0;  // bottom byte is the bool value
    }
    // For other types, treat non-null as true
    return true;
}

bool state_get_pseudo_state(DocState* state, View* view, uint32_t pseudo_state) {
    if (!view) return false;

    switch (pseudo_state) {
        case PSEUDO_STATE_HOVER:
            return state_get_bool(state, view, STATE_HOVER);
        case PSEUDO_STATE_ACTIVE:
            return state_get_bool(state, view, STATE_ACTIVE);
        case PSEUDO_STATE_FOCUS:
            return state_get_bool(state, view, STATE_FOCUS);
        case PSEUDO_STATE_FOCUS_WITHIN:
            return state_get_bool(state, view, STATE_FOCUS_WITHIN);
        case PSEUDO_STATE_FOCUS_VISIBLE:
            return state_get_bool(state, view, STATE_FOCUS_VISIBLE);
        case PSEUDO_STATE_VISITED:
            return state_get_bool(state, view, STATE_VISITED);
        case PSEUDO_STATE_LINK:
            return state_get_bool(state, view, STATE_LINK);
        case PSEUDO_STATE_TARGET:
            return state_get_bool(state, view, STATE_TARGET);
        case PSEUDO_STATE_CHECKED:
            return form_control_get_checked(state, view);
        case PSEUDO_STATE_DISABLED:
            return form_control_is_disabled(state, view);
        case PSEUDO_STATE_ENABLED:
            return !form_control_is_disabled(state, view);
        case PSEUDO_STATE_REQUIRED:
            return form_control_is_required(state, view);
        case PSEUDO_STATE_OPTIONAL:
            return !form_control_is_required(state, view);
        case PSEUDO_STATE_READ_ONLY:
            return form_control_is_readonly(state, view);
        case PSEUDO_STATE_READ_WRITE:
            return !form_control_is_readonly(state, view);
        case PSEUDO_STATE_INDETERMINATE:
            return state_get_bool(state, view, STATE_INDETERMINATE);
        case PSEUDO_STATE_VALID:
            return state_get_bool(state, view, STATE_VALID);
        case PSEUDO_STATE_INVALID:
            return state_get_bool(state, view, STATE_INVALID);
        case PSEUDO_STATE_PLACEHOLDER_SHOWN:
            return state_get_bool(state, view, STATE_PLACEHOLDER);
        case PSEUDO_STATE_SELECTED:
            return state_get_bool(state, view, STATE_SELECTED);
        default:
            return false;
    }
}

static bool dom_element_default_pseudo_state(DomElement* element, uint32_t pseudo_state) {
    if (!element) return false;
    switch (pseudo_state) {
        case PSEUDO_STATE_LINK:
            return dom_element_has_attribute(element, "href");
        case PSEUDO_STATE_CHECKED:
            return dom_element_has_attribute(element, "checked");
        case PSEUDO_STATE_DISABLED:
            return dom_element_has_attribute(element, "disabled");
        case PSEUDO_STATE_ENABLED:
            return !dom_element_has_attribute(element, "disabled");
        case PSEUDO_STATE_REQUIRED:
            return dom_element_has_attribute(element, "required");
        case PSEUDO_STATE_OPTIONAL:
            return !dom_element_has_attribute(element, "required");
        case PSEUDO_STATE_READ_ONLY:
            return dom_element_has_attribute(element, "readonly");
        case PSEUDO_STATE_READ_WRITE:
            return !dom_element_has_attribute(element, "readonly");
        case PSEUDO_STATE_SELECTED:
            return dom_element_has_attribute(element, "selected");
        default:
            return false;
    }
}

bool state_resolve_selector_pseudo_state(void* context, DomElement* element, uint32_t pseudo_state) {
    if (!element) return false;
    if (!element->doc || !element->doc->view_tree) {
        return dom_element_default_pseudo_state(element, pseudo_state);
    }
    DocState* state = (DocState*)context;
    if (!state && element->doc) {
        state = (DocState*)element->doc->state;
    }
    if (pseudo_state == PSEUDO_STATE_LINK) {
        return dom_element_default_pseudo_state(element, pseudo_state);
    }
    return state_get_pseudo_state(state, static_cast<View*>(element), pseudo_state);
}

void state_configure_selector_matcher(DocState* state, SelectorMatcher* matcher) {
    if (!matcher) return;
    selector_matcher_set_pseudo_state_resolver(matcher, state_resolve_selector_pseudo_state, state);
}

static DomDocument* view_state_resolve_document(View* view) {
    if (!view) return NULL;
    DomNode* node = static_cast<DomNode*>(view);
    while (node) {
        if (node->is_element()) {
            DomElement* elem = lam::dom_require_element(node);
            if (elem->doc) return elem->doc;
        }
        node = node->parent;
    }
    return NULL;
}

static uint32_t view_state_resolve_id(View* view) {
    if (!view) return 0;
    if (view->id != 0) return view->id;
    DomDocument* doc = view_state_resolve_document(view);
    if (doc) {
        view->id = doc->next_node_id++;
        return view->id;
    }
    return 0;
}

static void doc_state_log_dropdown_owner_transition(DocState* state,
                                                    View* old_view,
                                                    View* new_view);
static void doc_state_log_context_menu_target_transition(DocState* state,
                                                         View* old_view,
                                                         View* new_view);
static void doc_state_log_context_menu_hover_transition(DocState* state,
                                                        int old_hover,
                                                        int new_hover);
static void doc_state_log_view_target_transition(DocState* state,
                                                 const char* name,
                                                 View* old_view,
                                                 View* new_view);
static void doc_state_log_bool_transition(DocState* state, const char* name,
                                          bool old_value, bool new_value);

static void state_transition_write_anchor(JsonWriter* w, DocState* state, View* view) {
    jw_key(w, "anchor");
    jw_obj_begin(w);
        const char* doc_id = event_state_log_doc_id(state ? state->active_event_log : NULL);
        if (doc_id) jw_kv_str(w, "doc_id", doc_id);
        if (view) {
            uint32_t view_id = view_state_resolve_id(view);
            if (view_id != 0) jw_kv_uint(w, "view_id", view_id);
            event_state_log_write_node_ref(w, "view", (const DomNode*)view);
        }
    jw_obj_end(w);
}

static ViewState* view_state_get_for_kind(DocState* state, View* view, ViewStateKind kind) {
    if (!state || !view) return NULL;

    uint32_t view_id = view_state_resolve_id(view);
    if (view_id == 0 || !state->view_state_map) return NULL;

    ViewStateEntry query = { .view_id = view_id, .kind = kind, .state = NULL };
    const ViewStateEntry* found = (const ViewStateEntry*)hashmap_get(state->view_state_map, &query);
    return found ? found->state : NULL;
}

static ViewState* view_state_primary_cache(View* view, ViewState* view_state) {
    if (!view || !view_state) return view_state;
    if (view_state->kind == VIEW_STATE_FORM_CONTROL ||
        !view->view_state_ref ||
        view->view_state_ref->kind == VIEW_STATE_BASE) {
        view->view_state_ref = view_state;
    }
    return view_state;
}

ViewState* view_state_get(DocState* state, View* view) {
    if (!state || !view) return NULL;

    ViewState* form_state = view_state_get_for_kind(state, view, VIEW_STATE_FORM_CONTROL);
    if (form_state) return view_state_primary_cache(view, form_state);

    ViewState* scroll_state = view_state_get_for_kind(state, view, VIEW_STATE_SCROLL);
    if (scroll_state) return view_state_primary_cache(view, scroll_state);

    ViewState* base_state = view_state_get_for_kind(state, view, VIEW_STATE_BASE);
    if (base_state) return view_state_primary_cache(view, base_state);

    ViewState* custom_state = view_state_get_for_kind(state, view, VIEW_STATE_CUSTOM);
    if (custom_state) return view_state_primary_cache(view, custom_state);

    view->view_state_ref = NULL;
    return NULL;
}

static void doc_state_detach_transient_owner(DocState* state, View* view) {
    if (!state || !view) return;

    if (state->hover_target == view) {
        doc_state_log_view_target_transition(state, "hover.target", state->hover_target, NULL);
        state->hover_target = NULL;
    }
    if (state->active_target == view) {
        doc_state_log_view_target_transition(state, "active.target", state->active_target, NULL);
        state->active_target = NULL;
    }
    if (state->drag_target == view) {
        doc_state_log_view_target_transition(state, "drag.target", state->drag_target, NULL);
        state->drag_target = NULL;
        if (state->is_dragging) {
            doc_state_log_bool_transition(state, "drag.active", true, false);
            state->is_dragging = false;
        }
    }
    if (state->open_dropdown == view) {
        doc_state_log_dropdown_owner_transition(state, state->open_dropdown, NULL);
        state->open_dropdown = NULL;
        state->dropdown_width = 0.0f;
        state->dropdown_height = 0.0f;
    }
    if (state->context_menu_target == view) {
        doc_state_log_context_menu_target_transition(state, state->context_menu_target, NULL);
        if (state->context_menu_hover != -1) {
            doc_state_log_context_menu_hover_transition(state, state->context_menu_hover, -1);
        }
        state->context_menu_target = NULL;
        state->context_menu_hover = -1;
        state->context_menu_width = 0.0f;
        state->context_menu_height = 0.0f;
    }
    if (state->drag_drop) {
        if (state->drag_drop->source_view == view || state->drag_drop->drop_target == view) {
            memset(state->drag_drop, 0, sizeof(DragDropState));
        }
    }
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (state->active_text_control == elem) state->active_text_control = NULL;
        if (state->last_focused_text_control == elem) state->last_focused_text_control = NULL;
    }
}

static uint32_t view_state_detach_node(DocState* state, DomNode* node) {
    if (!state || !node) return 0;

    uint32_t removed = 0;
    View* view = static_cast<View*>(node);
    uint32_t view_id = view->id;
    doc_state_detach_transient_owner(state, view);
    if (view_id != 0 && state->view_state_map) {
        for (int kind_int = VIEW_STATE_BASE; kind_int <= VIEW_STATE_CUSTOM; kind_int++) {
            ViewStateKind kind = (ViewStateKind)kind_int;
            ViewStateEntry query = { .view_id = view_id, .kind = kind, .state = NULL };
            const ViewStateEntry* found = (const ViewStateEntry*)hashmap_get(state->view_state_map, &query);
            if (found) view_state_release_payload(found->state);
            if (hashmap_delete(state->view_state_map, &query)) {
                removed++;
            }
        }
    }
    view->view_state_ref = NULL;
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (elem && elem->form) {
            elem->form->form_state_ref = NULL;
            elem->form->scroll_state_ref = NULL;
        }
    }

    if (node->is_element()) {
        DomElement* element = lam::dom_require_element(node);
        DomNode* child = element->first_child;
        while (child) {
            removed += view_state_detach_node(state, child);
            child = child->next_sibling;
        }
    }
    return removed;
}

static View* view_state_find_live_id(DomNode* node, uint32_t view_id) {
    if (!node || view_id == 0) return NULL;
    if (node->id == view_id) return static_cast<View*>(node);
    if (node->is_element()) {
        DomElement* element = lam::dom_require_element(node);
        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            View* found = view_state_find_live_id(child, view_id);
            if (found) return found;
        }
    }
    return NULL;
}

static bool view_state_tree_contains_view(DomNode* node, View* view) {
    if (!node || !view) return false;
    if (static_cast<View*>(node) == view) return true;
    if (node->is_element()) {
        DomElement* element = lam::dom_require_element(node);
        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            if (view_state_tree_contains_view(child, view)) return true;
        }
    }
    return false;
}

static uint32_t view_state_clear_interaction_flag(DocState* state, const char* name) {
    if (!state || !state->view_state_map || !name) return 0;
    uint32_t changed = 0;
    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(state->view_state_map, &iter, &item)) {
        ViewStateEntry* entry = (ViewStateEntry*)item;
        ViewState* view_state = entry ? entry->state : NULL;
        if (!view_state) continue;
        if (strcmp(name, "hover") == 0 && view_state->flags.hovered) {
            view_state->flags.hovered = 0;
            changed++;
        } else if (strcmp(name, "active") == 0 && view_state->flags.active) {
            view_state->flags.active = 0;
            changed++;
        }
    }
    return changed;
}

static uint32_t doc_state_prune_stale_transient_owners(DocState* state, DomNode* root) {
    if (!state || !root) return 0;
    uint32_t changed = 0;

    if (state->hover_target && !view_state_tree_contains_view(root, state->hover_target)) {
        doc_state_log_view_target_transition(state, "hover.target", state->hover_target, NULL);
        state->hover_target = NULL;
        changed++;
    }
    if (!state->hover_target) changed += view_state_clear_interaction_flag(state, "hover");

    if (state->active_target && !view_state_tree_contains_view(root, state->active_target)) {
        doc_state_log_view_target_transition(state, "active.target", state->active_target, NULL);
        state->active_target = NULL;
        changed++;
    }
    if (!state->active_target) changed += view_state_clear_interaction_flag(state, "active");

    if (state->drag_target && !view_state_tree_contains_view(root, state->drag_target)) {
        doc_state_log_view_target_transition(state, "drag.target", state->drag_target, NULL);
        state->drag_target = NULL;
        state->is_dragging = false;
        changed++;
    }
    if (state->open_dropdown && !view_state_tree_contains_view(root, state->open_dropdown)) {
        doc_state_log_dropdown_owner_transition(state, state->open_dropdown, NULL);
        state->open_dropdown = NULL;
        state->dropdown_width = 0.0f;
        state->dropdown_height = 0.0f;
        changed++;
    }
    if (state->context_menu_target && !view_state_tree_contains_view(root, state->context_menu_target)) {
        doc_state_log_context_menu_target_transition(state, state->context_menu_target, NULL);
        state->context_menu_target = NULL;
        state->context_menu_hover = -1;
        state->context_menu_width = 0.0f;
        state->context_menu_height = 0.0f;
        changed++;
    }
    if (state->drag_drop) {
        bool stale_source = state->drag_drop->source_view && !view_state_tree_contains_view(root, state->drag_drop->source_view);
        bool stale_target = state->drag_drop->drop_target && !view_state_tree_contains_view(root, state->drag_drop->drop_target);
        if (stale_source || stale_target) {
            memset(state->drag_drop, 0, sizeof(DragDropState));
            changed++;
        }
    }
    if (state->active_text_control && !view_state_tree_contains_view(root, static_cast<View*>(state->active_text_control))) {
        state->active_text_control = NULL;
        changed++;
    }
    if (state->last_focused_text_control && !view_state_tree_contains_view(root, static_cast<View*>(state->last_focused_text_control))) {
        state->last_focused_text_control = NULL;
        changed++;
    }

    return changed;
}

uint32_t view_state_prune_orphans(DocState* state) {
    if (!state || !state->view_state_map || !state->owner_store || !state->owner_store->document) return 0;

    DomDocument* doc = state->owner_store->document;
    DomNode* root = doc->root ? static_cast<DomNode*>(doc->root) : nullptr;
    uint32_t removed = 0;
    uint32_t owner_changes = doc_state_prune_stale_transient_owners(state, root);
    bool found_orphan = true;
    while (found_orphan) {
        found_orphan = false;
        size_t iter = 0;
        void* item = NULL;
        while (hashmap_iter(state->view_state_map, &iter, &item)) {
            ViewStateEntry* entry = (ViewStateEntry*)item;
            uint32_t state_view_id = entry->state ? entry->state->view_id : 0;
            View* key_live = view_state_find_live_id(root, entry->view_id);
            View* state_live = view_state_find_live_id(root, state_view_id);
            if (!entry->state || !key_live || !state_live) {
                ViewStateEntry query = { .view_id = entry->view_id, .kind = entry->kind, .state = NULL };
                view_state_release_payload(entry->state);
                hashmap_delete(state->view_state_map, &query);
                removed++;
                found_orphan = true;
                break;
            }
        }
    }

    if (removed > 0 || owner_changes > 0) {
        state->is_dirty = true;
        state->needs_repaint = true;
        state->version++;
        log_debug("view_state_prune_orphans: removed %u orphaned ViewState entries, pruned %u transient owners/flags",
            removed, owner_changes);
    }
    return removed + owner_changes;
}

uint32_t view_state_detach_subtree(DocState* state, DomNode* root) {
    if (!state || !root) return 0;
    uint32_t removed = view_state_detach_node(state, root);
    if (removed > 0) {
        state->is_dirty = true;
        state->needs_repaint = true;
        state->version++;
        log_debug("view_state_detach_subtree: removed %u ViewState entries", removed);
        state_assert_after_mutation(state, "view_state_detach_subtree");
    }
    return removed;
}

static ViewState* view_state_get_or_create(DocState* state, View* view, ViewStateKind kind) {
    if (!state || !view || !state->arena || !state->view_state_map) return NULL;
    ViewState* existing = view_state_get_for_kind(state, view, kind);
    if (existing) return view_state_primary_cache(view, existing);
    ViewState* primary = view_state_get(state, view);

    uint32_t view_id = view_state_resolve_id(view);
    if (view_id == 0) return NULL;

    ViewState* created = (ViewState*)arena_alloc(state->arena, sizeof(ViewState));
    if (!created) return NULL;
    memset(created, 0, sizeof(ViewState));
    created->view_id = view_id;
    created->kind = kind;
    if (primary && primary->view_id == view_id) {
        created->flags = primary->flags;
    }
    if (kind == VIEW_STATE_FORM_CONTROL) {
        created->data.form.selected_index = -1;
        created->data.form.hover_index = -1;
        created->data.form.range_value = 0.5f;
    }

    ViewStateEntry entry = { .view_id = view_id, .kind = kind, .state = created };
    hashmap_set(state->view_state_map, &entry);
    return view_state_primary_cache(view, created);
}

static void view_state_log_bool_transition(DocState* state, View* view,
                                           const char* name, bool old_value,
                                           bool new_value) {
    if (!state || old_value == new_value || !event_state_log_enabled(state->active_event_log)) return;

    char buf[512];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "view");
        state_transition_write_anchor(&w, state, view);
        jw_kv_str(&w, "name", name ? name : "view.state");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            jw_kv_bool(&w, "value", old_value);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            jw_kv_bool(&w, "value", new_value);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void view_state_log_int_transition(DocState* state, View* view,
                                          const char* name, int old_value,
                                          int new_value) {
    if (!state || old_value == new_value || !event_state_log_enabled(state->active_event_log)) return;

    char buf[512];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "view");
        state_transition_write_anchor(&w, state, view);
        jw_kv_str(&w, "name", name ? name : "view.state");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            jw_kv_int(&w, "value", old_value);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            jw_kv_int(&w, "value", new_value);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void view_state_log_float_transition(DocState* state, View* view,
                                            const char* name, float old_value,
                                            float new_value) {
    if (!state || old_value == new_value || !event_state_log_enabled(state->active_event_log)) return;

    char buf[512];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "view");
        state_transition_write_anchor(&w, state, view);
        jw_kv_str(&w, "name", name ? name : "view.state");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            jw_kv_double(&w, "value", old_value);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            jw_kv_double(&w, "value", new_value);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void view_state_log_scroll_transition(DocState* state, View* view,
                                             const char* name,
                                             float old_x, float old_y,
                                             float old_max_x, float old_max_y,
                                             float new_x, float new_y,
                                             float new_max_x, float new_max_y) {
    if (!state || !event_state_log_enabled(state->active_event_log)) return;
    if (old_x == new_x && old_y == new_y && old_max_x == new_max_x && old_max_y == new_max_y) return;

    char buf[768];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "view");
        state_transition_write_anchor(&w, state, view);
        jw_kv_str(&w, "name", name ? name : "scroll");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            jw_kv_double(&w, "x", old_x);
            jw_kv_double(&w, "y", old_y);
            jw_kv_double(&w, "max_x", old_max_x);
            jw_kv_double(&w, "max_y", old_max_y);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            jw_kv_double(&w, "x", new_x);
            jw_kv_double(&w, "y", new_y);
            jw_kv_double(&w, "max_x", new_max_x);
            jw_kv_double(&w, "max_y", new_max_y);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void doc_state_log_dropdown_owner_transition(DocState* state,
                                                    View* old_view,
                                                    View* new_view) {
    if (!state || old_view == new_view || !event_state_log_enabled(state->active_event_log)) return;

    char buf[768];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "doc");
        state_transition_write_anchor(&w, state, NULL);
        jw_kv_str(&w, "name", "dropdown.owner");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            event_state_log_write_node_ref(&w, "view", (const DomNode*)old_view);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            event_state_log_write_node_ref(&w, "view", (const DomNode*)new_view);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void doc_state_log_context_menu_target_transition(DocState* state,
                                                         View* old_view,
                                                         View* new_view) {
    if (!state || old_view == new_view || !event_state_log_enabled(state->active_event_log)) return;

    char buf[768];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "doc");
        state_transition_write_anchor(&w, state, NULL);
        jw_kv_str(&w, "name", "context_menu.target");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            event_state_log_write_node_ref(&w, "view", (const DomNode*)old_view);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            event_state_log_write_node_ref(&w, "view", (const DomNode*)new_view);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void doc_state_log_context_menu_hover_transition(DocState* state,
                                                        int old_hover,
                                                        int new_hover) {
    if (!state || old_hover == new_hover || !event_state_log_enabled(state->active_event_log)) return;

    char buf[512];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "doc");
        state_transition_write_anchor(&w, state, NULL);
        jw_kv_str(&w, "name", "context_menu.hover");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            jw_kv_int(&w, "value", old_hover);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            jw_kv_int(&w, "value", new_hover);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void doc_state_log_view_target_transition(DocState* state,
                                                 const char* name,
                                                 View* old_view,
                                                 View* new_view) {
    if (!state || old_view == new_view || !event_state_log_enabled(state->active_event_log)) return;

    char buf[768];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "doc");
        state_transition_write_anchor(&w, state, NULL);
        jw_kv_str(&w, "name", name ? name : "target");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            event_state_log_write_node_ref(&w, "view", (const DomNode*)old_view);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            event_state_log_write_node_ref(&w, "view", (const DomNode*)new_view);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void doc_state_log_bool_transition(DocState* state, const char* name,
                                          bool old_value, bool new_value) {
    if (!state || old_value == new_value || !event_state_log_enabled(state->active_event_log)) return;

    char buf[512];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "scope", "doc");
        state_transition_write_anchor(&w, state, NULL);
        jw_kv_str(&w, "name", name ? name : "doc.state");
        jw_key(&w, "old");
        jw_obj_begin(&w);
            jw_kv_bool(&w, "value", old_value);
        jw_obj_end(&w);
        jw_key(&w, "new");
        jw_obj_begin(&w);
            jw_kv_bool(&w, "value", new_value);
        jw_obj_end(&w);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

bool view_state_get_hovered(DocState* state, View* view) {
    ViewState* view_state = view_state_get(state, view);
    return view_state ? view_state->flags.hovered != 0 : false;
}

bool view_state_get_active(DocState* state, View* view) {
    ViewState* view_state = view_state_get(state, view);
    return view_state ? view_state->flags.active != 0 : false;
}

bool view_state_get_focused(DocState* state, View* view) {
    ViewState* view_state = view_state_get(state, view);
    return view_state ? view_state->flags.focused != 0 : false;
}

static FormControlProp* form_prop_for_view(View* view) {
    if (!view || !view->is_block()) return NULL;
    ViewBlock* block = lam::view_require_block(view);
    return block->form;
}

static bool view_element_has_attr(View* view, const char* attr_name) {
    if (!view || !view->is_element() || !attr_name) return false;
    ViewElement* elem = lam::view_require_element(view);
    return elem->get_attribute(attr_name) != NULL;
}

static int form_default_selected_index_from_tree(View* view) {
    if (!view || !view->is_element()) return -1;
    DomElement* element = lam::dom_require_element(view);
    if (element->tag() != HTM_TAG_SELECT) return -1;

    int option_count = 0;
    int selected_index = -1;
    DomNode* child = element->first_child;
    while (child) {
        if (child->is_element()) {
            DomElement* child_elem = lam::dom_require_element(child);
            if (child_elem->tag() == HTM_TAG_OPTION) {
                if (child_elem->has_attribute("selected") && selected_index < 0) {
                    selected_index = option_count;
                }
                option_count++;
            } else if (child_elem->tag() == HTM_TAG_OPTGROUP) {
                DomNode* opt_child = child_elem->first_child;
                while (opt_child) {
                    if (opt_child->is_element()) {
                        DomElement* opt_elem = lam::dom_require_element(opt_child);
                        if (opt_elem->tag() == HTM_TAG_OPTION) {
                            if (opt_elem->has_attribute("selected") && selected_index < 0) {
                                selected_index = option_count;
                            }
                            option_count++;
                        }
                    }
                    opt_child = opt_child->next_sibling;
                }
            }
        }
        child = child->next_sibling;
    }
    if (selected_index >= 0) return selected_index;
    return option_count > 0 ? 0 : -1;
}

static float form_default_range_value(View* view, FormControlProp* form) {
    if (!form || form->control_type != FORM_CONTROL_RANGE || !view || !view->is_element()) return 0.5f;
    ViewElement* elem = lam::view_require_element(view);
    const char* value_attr = elem->get_attribute("value");
    if (!value_attr) return 0.5f;

    float range_span = form->range_max - form->range_min;
    if (range_span == 0.0f) return 0.5f;
    float value = (float)str_to_double_default(value_attr, strlen(value_attr), form->range_min);
    float normalized = (value - form->range_min) / range_span;
    if (normalized < 0.0f) return 0.0f;
    if (normalized > 1.0f) return 1.0f;
    return normalized;
}

static ViewState* form_view_state_get(DocState* state, View* view) {
    ViewState* view_state = view_state_get_for_kind(state, view, VIEW_STATE_FORM_CONTROL);
    if (view_state && view && view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (elem && elem->form) elem->form->form_state_ref = view_state;
    }
    return view_state;
}

static void view_state_release_form_payload(ViewState* view_state) {
    if (!view_state || view_state->kind != VIEW_STATE_FORM_CONTROL) return;
    if (view_state->data.form.current_value) {
        mem_free(view_state->data.form.current_value);
        view_state->data.form.current_value = NULL;
    }
    view_state->data.form.current_value_len = 0;
    view_state->data.form.current_value_u16_len = 0;
    view_state->data.form.has_current_value = 0;
}

static void view_state_release_payload(ViewState* view_state) {
    if (!view_state) return;
    if (view_state->kind == VIEW_STATE_FORM_CONTROL) {
        view_state_release_form_payload(view_state);
    }
}

static bool form_view_is_text_control(View* view) {
    if (!view || !view->is_element()) return false;
    DomElement* elem = lam::dom_require_element(view);
    return elem && tc_is_text_control(elem);
}

static SmFamily form_control_schema_family_for_view(View* view) {
    FormControlProp* form = form_prop_for_view(view);
    if (!form) return SM_FAMILY__COUNT;
    switch (form->control_type) {
        case FORM_CONTROL_CHECKBOX:
        case FORM_CONTROL_RADIO:
            return SM_FAMILY_FORM_CHECKABLE;
        case FORM_CONTROL_SELECT:
            return SM_FAMILY_FORM_SELECT;
        case FORM_CONTROL_RANGE:
            return SM_FAMILY_FORM_RANGE;
        case FORM_CONTROL_TEXT:
        case FORM_CONTROL_TEXTAREA:
            return SM_FAMILY_FORM_TEXT;
        default:
            return SM_FAMILY__COUNT;
    }
}

static SmFamily form_control_schema_family_for_readonly(View* view) {
    SmFamily family = form_control_schema_family_for_view(view);
    return family == SM_FAMILY_FORM_TEXT ? family : SM_FAMILY__COUNT;
}

static void form_view_state_store_text_value(ViewState* view_state,
                                             FormControlProp* form) {
    if (!view_state || !form || !form->current_value) return;
    char* copy = (char*)mem_alloc((size_t)form->current_value_len + 1,
                                  MEM_CAT_DOM);
    if (!copy) return;
    if (form->current_value_len > 0) {
        memcpy(copy, form->current_value, form->current_value_len);
    }
    copy[form->current_value_len] = '\0';
    if (view_state->data.form.current_value) {
        mem_free(view_state->data.form.current_value);
    }
    view_state->data.form.current_value = copy;
    view_state->data.form.current_value_len = form->current_value_len;
    view_state->data.form.current_value_u16_len = form->current_value_u16_len;
    view_state->data.form.has_current_value = 1;
}

static ViewState* form_view_state_get_or_create(DocState* state, View* view, FormControlProp* form) {
    if (!state || !view || !form) return NULL;
    ViewState* view_state = form_view_state_get(state, view);
    bool should_seed_from_form = false;
    if (view_state) {
        form->form_state_ref = view_state;
        return view_state;
    } else {
        view_state = view_state_get_or_create(state, view, VIEW_STATE_FORM_CONTROL);
        if (!view_state) return NULL;
        should_seed_from_form = true;
        form->form_state_ref = view_state;
    }

    if (should_seed_from_form) {
        view_state->data.form.disabled = view_element_has_attr(view, "disabled") ? 1 : 0;
        view_state->data.form.readonly = view_element_has_attr(view, "readonly") ? 1 : 0;
        view_state->data.form.required = view_element_has_attr(view, "required") ? 1 : 0;
        view_state->data.form.checked = view_element_has_attr(view, "checked") ? 1 : 0;
        view_state->data.form.dropdown_open = 0;
        view_state->data.form.selected_index = form_default_selected_index_from_tree(view);
        view_state->data.form.hover_index = -1;
        view_state->data.form.range_value = form_default_range_value(view, form);
        view_state->data.form.selection_start = form->selection_start;
        view_state->data.form.selection_end = form->selection_end;
        view_state->data.form.selection_direction = form->selection_direction;
        if (form_view_is_text_control(view) && form->current_value) {
            form_view_state_store_text_value(view_state, form);
        }
    }
    return view_state;
}

static void form_state_mark_dirty(DocState* state) {
    if (!state) return;
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "form_view_state_mutation");
}

static void view_state_set_hovered_internal(DocState* state, View* view, bool hovered,
                                            bool assert_after_mutation) {
    ViewState* view_state = view_state_get(state, view);
    if (!view_state && !hovered) return;
    if (!view_state) view_state = view_state_get_or_create(state, view, VIEW_STATE_BASE);
    if (!view_state) return;
    bool old_value = view_state->flags.hovered != 0;
    if (old_value == hovered) return;
    view_state->flags.hovered = hovered ? 1 : 0;
    view_state_log_bool_transition(state, view, "hover", old_value, hovered);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    if (assert_after_mutation) state_assert_after_mutation(state, "view_state_set_hovered");
}

void view_state_set_hovered(DocState* state, View* view, bool hovered) {
    view_state_set_hovered_internal(state, view, hovered, true);
}

static void view_state_set_active_internal(DocState* state, View* view, bool active,
                                           bool assert_after_mutation);

void view_state_set_active(DocState* state, View* view, bool active) {
    view_state_set_active_internal(state, view, active, true);
}

static void view_state_set_active_internal(DocState* state, View* view, bool active,
                                           bool assert_after_mutation) {
    ViewState* view_state = view_state_get(state, view);
    if (!view_state && !active) return;
    if (!view_state) view_state = view_state_get_or_create(state, view, VIEW_STATE_BASE);
    if (!view_state) return;
    bool old_value = view_state->flags.active != 0;
    if (old_value == active) return;
    view_state->flags.active = active ? 1 : 0;
    view_state_log_bool_transition(state, view, "active", old_value, active);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    if (assert_after_mutation) state_assert_after_mutation(state, "view_state_set_active");
}

void view_state_set_focused(DocState* state, View* view, bool focused) {
    ViewState* view_state = view_state_get(state, view);
    if (!view_state && !focused) return;
    if (!view_state) view_state = view_state_get_or_create(state, view, VIEW_STATE_BASE);
    if (!view_state) return;
    bool old_value = view_state->flags.focused != 0;
    if (old_value == focused) return;
    view_state->flags.focused = focused ? 1 : 0;
    view_state_log_bool_transition(state, view, "focus", old_value, focused);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "view_state_set_focused");
}

void doc_state_set_hover_target(DocState* state, View* target) {
    if (!state) return;
    View* old_target = state->hover_target;
    if (old_target == target) return;

    View* node = old_target;
    while (node) {
        view_state_set_hovered_internal(state, node, false, false);
        node = static_cast<View*>(node->parent);
    }

    node = target;
    while (node) {
        view_state_set_hovered_internal(state, node, true, false);
        node = static_cast<View*>(node->parent);
    }

    state->hover_target = target;
    doc_state_log_view_target_transition(state, "hover.target", old_target, target);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "doc_state_set_hover_target");
}

void doc_state_set_active_target(DocState* state, View* target) {
    if (!state) return;
    View* old_target = state->active_target;
    if (old_target == target) return;

    View* node = old_target;
    while (node) {
        view_state_set_active_internal(state, node, false, false);
        node = static_cast<View*>(node->parent);
    }

    node = target;
    while (node) {
        view_state_set_active_internal(state, node, true, false);
        node = static_cast<View*>(node->parent);
    }

    state->active_target = target;
    doc_state_log_view_target_transition(state, "active.target", old_target, target);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "doc_state_set_active_target");
}

void doc_state_set_drag_state(DocState* state, View* target, bool dragging) {
    if (!state) return;
    View* new_target = dragging ? target : NULL;
    View* old_target = state->drag_target;
    bool old_dragging = state->is_dragging;
    if (old_target == new_target && old_dragging == dragging) return;

    state->drag_target = new_target;
    state->is_dragging = dragging;
    doc_state_log_view_target_transition(state, "drag.target", old_target, new_target);
    doc_state_log_bool_transition(state, "drag.active", old_dragging, dragging);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "doc_state_set_drag_state");
}

DragDropState* doc_state_begin_drag_drop(DocState* state, View* source,
                                         float start_x, float start_y,
                                         const char* drag_data) {
    if (!state || !source) return NULL;
    if (!state->drag_drop) {
        state->drag_drop = (DragDropState*)arena_alloc(state->arena, sizeof(DragDropState));
    }
    if (!state->drag_drop) return NULL;

    DragDropState* drag_drop = state->drag_drop;
    memset(drag_drop, 0, sizeof(DragDropState));
    drag_drop->source_view = source;
    drag_drop->start_x = start_x;
    drag_drop->start_y = start_y;
    drag_drop->current_x = start_x;
    drag_drop->current_y = start_y;
    drag_drop->pending = true;
    drag_drop->drag_data = drag_data;

    state->version++;
    state_assert_after_mutation(state, "doc_state_begin_drag_drop");
    return drag_drop;
}

void doc_state_update_drag_drop_motion(DocState* state, float x, float y) {
    if (!state || !state->drag_drop) return;
    DragDropState* drag_drop = state->drag_drop;
    if (drag_drop->current_x == x && drag_drop->current_y == y) return;
    drag_drop->current_x = x;
    drag_drop->current_y = y;
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "doc_state_update_drag_drop_motion");
}

void doc_state_set_drag_drop_active(DocState* state, bool active) {
    if (!state || !state->drag_drop) return;
    DragDropState* drag_drop = state->drag_drop;
    bool old_active = drag_drop->active;
    bool old_pending = drag_drop->pending;
    drag_drop->active = active;
    drag_drop->pending = active ? false : drag_drop->pending;
    if (old_active == drag_drop->active && old_pending == drag_drop->pending) return;
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "doc_state_set_drag_drop_active");
}

void doc_state_set_drag_drop_target(DocState* state, View* drop_target,
                                    const DomBoundary* drop_start,
                                    const DomBoundary* drop_end) {
    if (!state || !state->drag_drop) return;
    DragDropState* drag_drop = state->drag_drop;
    bool has_range = drop_start && drop_end &&
        drop_start->node && drop_end->node;
    bool range_changed = drag_drop->has_drop_range != has_range;
    if (has_range && !range_changed) {
        range_changed =
            drag_drop->drop_start.node != drop_start->node ||
            drag_drop->drop_start.offset != drop_start->offset ||
            drag_drop->drop_end.node != drop_end->node ||
            drag_drop->drop_end.offset != drop_end->offset;
    }
    if (drag_drop->drop_target == drop_target && !range_changed) return;
    drag_drop->drop_target = drop_target;
    drag_drop->has_drop_range = has_range;
    if (has_range) {
        drag_drop->drop_start = *drop_start;
        drag_drop->drop_end = *drop_end;
    } else {
        memset(&drag_drop->drop_start, 0, sizeof(drag_drop->drop_start));
        memset(&drag_drop->drop_end, 0, sizeof(drag_drop->drop_end));
    }
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    state_assert_after_mutation(state, "doc_state_set_drag_drop_target");
}

void doc_state_clear_drag_drop(DocState* state) {
    if (!state || !state->drag_drop) return;
    memset(state->drag_drop, 0, sizeof(DragDropState));
    state->version++;
    state_assert_after_mutation(state, "doc_state_clear_drag_drop");
}

void doc_state_mark_dirty(DocState* state) {
    if (!state) return;
    if (state->is_dirty) return;
    state->is_dirty = true;
    state->version++;
    state_assert_after_mutation(state, "doc_state_mark_dirty");
}

void doc_state_request_repaint(DocState* state) {
    if (!state) return;
    bool changed = !state->needs_repaint || !state->is_dirty;
    state->needs_repaint = true;
    state->is_dirty = true;
    if (changed) state->version++;
    state_assert_after_mutation(state, "doc_state_request_repaint");
}

void doc_state_mark_video_frame_pending(DocState* state) {
    if (!state) return;
    state->video_frame_pending = true;
    state->video_frame_generation++;
    if (state->video_frame_generation == 0) state->video_frame_generation = 1;
}

void doc_state_clear_video_frame_pending(DocState* state) {
    if (!state) return;
    state->video_frame_pending = false;
}

void doc_state_request_reflow(DocState* state) {
    if (!state) return;
    bool changed = !state->needs_reflow || !state->is_dirty;
    state->needs_reflow = true;
    state->is_dirty = true;
    if (changed) state->version++;
    state_assert_after_mutation(state, "doc_state_request_reflow");
}

void doc_state_clear_reflow(DocState* state) {
    if (!state || !state->needs_reflow) return;
    state->needs_reflow = false;
    state->version++;
    state_assert_after_mutation(state, "doc_state_clear_reflow");
}

void doc_state_clear_render_flags(DocState* state) {
    if (!state) return;
    bool changed = state->is_dirty || state->needs_repaint || state->selection_layout_dirty ||
        dirty_has_regions(&state->dirty_tracker) || state->dirty_tracker.full_repaint ||
        state->dirty_tracker.full_reflow;
    state->is_dirty = false;
    state->needs_repaint = false;
    state->selection_layout_dirty = false;
    dirty_clear(&state->dirty_tracker);
    if (changed) state->version++;
    state_assert_after_mutation(state, "doc_state_clear_render_flags");
}

void doc_state_clear_repaint(DocState* state) {
    if (!state || !state->needs_repaint) return;
    state->needs_repaint = false;
    state->version++;
    state_assert_after_mutation(state, "doc_state_clear_repaint");
}

void doc_state_sync_viewport_scroll(DocState* state, DomDocument* doc,
                                    float scroll_x, float scroll_y) {
    if (!state) return;
    if (scroll_x < 0.0f) scroll_x = 0.0f;
    if (scroll_y < 0.0f) scroll_y = 0.0f;

    bool changed = state->scroll_x != scroll_x || state->scroll_y != scroll_y;
    state->scroll_x = scroll_x;
    state->scroll_y = scroll_y;
    if (doc) {
        doc->pending_viewport_scroll_x = scroll_x;
        doc->pending_viewport_scroll_y = scroll_y;
    }
    if (changed) state->version++;
    state_assert_after_mutation(state, "doc_state_sync_viewport_scroll");
}

bool state_has(DocState* state, void* node, const char* name) {
    if (!state || !node || !name) return false;

    if (strcmp(name, STATE_HOVER) == 0 ||
        strcmp(name, STATE_ACTIVE) == 0 ||
        strcmp(name, STATE_FOCUS) == 0 ||
        strcmp(name, STATE_CHECKED) == 0 ||
        strcmp(name, STATE_DISABLED) == 0 ||
        strcmp(name, STATE_READONLY) == 0 ||
        strcmp(name, STATE_REQUIRED) == 0) {
        return view_state_get(state, static_cast<View*>(node)) != NULL;
    }

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    return hashmap_get(state->state_map, &query) != NULL;
}

static bool s_in_batch = false;

static void state_assert_after_mutation(DocState* state, const char* context) {
    if (!state) return;
    if (state->transition_depth > 0 || state->active_cascade_depth > 0 || s_in_batch) return;
    radiant_state_assert_valid(state, context);
}

void state_set(DocState* state, void* node, const char* name, Item value) {
    if (!state || !node || !name) return;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    // Check for existing entry
    const StateEntry* existing = (const StateEntry*)hashmap_get(state->state_map, &query);

    if (existing) {
        // Update existing entry
        Item old_value = existing->value;
        StateChangeCallback on_change = existing->on_change;
        void* callback_udata = existing->callback_udata;

        // Create updated entry (hashmap_set replaces)
        StateEntry updated = *existing;
        updated.value = value;
        updated.last_modified = state->version;
        hashmap_set(state->state_map, &updated);

        // Fire callback if registered
        if (on_change) {
            on_change(node, name, old_value, value, callback_udata);
        }
    } else {
        // Create new entry
        StateEntry entry = {
            .key = { node, interned },
            .value = value,
            .last_modified = state->version,
            .on_change = NULL,
            .callback_udata = NULL
        };
        hashmap_set(state->state_map, &entry);
    }

    state->is_dirty = true;
    state->version++;

    log_debug("state_set: node=%p, name=%s, version=%llu", node, name, state->version);
    state_assert_after_mutation(state, "state_set");
}

void state_set_bool(DocState* state, void* node, const char* name, bool value) {
    if (state && node && name) {
        View* view = static_cast<View*>(node);
        if (strcmp(name, STATE_HOVER) == 0) {
            view_state_set_hovered(state, view, value);
            return;
        }
        if (strcmp(name, STATE_ACTIVE) == 0) {
            view_state_set_active(state, view, value);
            return;
        }
        if (strcmp(name, STATE_FOCUS) == 0) {
            view_state_set_focused(state, view, value);
            return;
        }
        if (strcmp(name, STATE_CHECKED) == 0) {
            form_control_set_checked(state, view, value);
            return;
        }
        if (strcmp(name, STATE_DISABLED) == 0) {
            form_control_set_disabled(state, view, value);
            return;
        }
        if (strcmp(name, STATE_READONLY) == 0) {
            form_control_set_readonly(state, view, value);
            return;
        }
        if (strcmp(name, STATE_REQUIRED) == 0) {
            form_control_set_required(state, view, value);
            return;
        }
    }

    Item item_value = { .item = value ? ITEM_TRUE : ITEM_FALSE };
    state_set(state, node, name, item_value);
}

bool form_control_get_checked(DocState* state, View* view) {
    if (!view || !view->is_element()) return false;
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.checked != 0;
    return view_element_has_attr(view, "checked");
}

static void form_control_set_checked_for_event(DocState* state, View* view, bool checked,
                                               SmEvent event, uint32_t extra_action) {
    if (!view || !view->is_element()) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_CHECKABLE, event, view);
    FormControlProp* form = form_prop_for_view(view);
    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    if (!view_state) return;

    bool old_value = view_state->data.form.checked != 0;
    if (old_value == checked) return;
    view_state->data.form.checked = checked ? 1 : 0;
    sm_observe_action(state, SM_ACT_WRITE_CHECKED);
    if (extra_action != SM_ACT_NONE) sm_observe_action(state, extra_action);

    if (form) form->state_ref = state;

    view_state_log_bool_transition(state, view, "form.checked", old_value, checked);
    sm_guard.commit();
    form_state_mark_dirty(state);
}

void form_control_set_checked(DocState* state, View* view, bool checked) {
    form_control_set_checked_for_event(state, view, checked,
                                       SM_EV_FORM_SET_CHECKED,
                                       SM_ACT_NONE);
}

void form_control_uncheck_radio_group_peer(DocState* state, View* view) {
    FormControlProp* form = form_prop_for_view(view);
    if (!form || form->control_type != FORM_CONTROL_RADIO) return;
    form_control_set_checked_for_event(state, view, false,
                                       SM_EV_FORM_UNCHECK_RADIO_GROUP,
                                       SM_ACT_UNCHECK_RADIO_GROUP);
}

static void scroll_state_attach(DocState* state, void* pane_ptr) {
    if (!state || !pane_ptr) return;
    ScrollPane* pane = (ScrollPane*)pane_ptr;
    pane->state_ref = state;
}

static ViewState* scroll_view_state_get_or_create(DocState* state, View* view, ScrollPane* pane) {
    if (!state || !view || !pane) return NULL;
    ViewState* view_state = view_state_get_for_kind(state, view, VIEW_STATE_SCROLL);
    bool created = false;
    if (!view_state) {
        view_state = view_state_get_or_create(state, view, VIEW_STATE_SCROLL);
        if (!view_state) return NULL;
        created = true;
    }
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (elem && elem->form) elem->form->scroll_state_ref = view_state;
    }

    if (created) {
        view_state->data.scroll.x = pane->h_scroll_position;
        view_state->data.scroll.y = pane->v_scroll_position;
        view_state->data.scroll.max_x = pane->h_max_scroll;
        view_state->data.scroll.max_y = pane->v_max_scroll;
    }
    return view_state;
}

void scroll_state_set_max_for_view(DocState* state, View* view, void* pane_ptr,
                                   float h_max, float v_max) {
    if (!pane_ptr) return;
    ScrollPane* pane = (ScrollPane*)pane_ptr;
    if (state) scroll_state_attach(state, pane_ptr);

    if (h_max < 0.0f) h_max = 0.0f;
    if (v_max < 0.0f) v_max = 0.0f;
    SmTransitionGuard sm_guard(state, SM_FAMILY_SCROLL, SM_EV_SCROLL_SET_MAX, view);

    ViewState* view_state = scroll_view_state_get_or_create(state, view, pane);
    float old_x = pane->h_scroll_position;
    float old_y = pane->v_scroll_position;
    float old_max_x = pane->h_max_scroll;
    float old_max_y = pane->v_max_scroll;
    if (view_state) {
        old_x = view_state->data.scroll.x;
        old_y = view_state->data.scroll.y;
        old_max_x = view_state->data.scroll.max_x;
        old_max_y = view_state->data.scroll.max_y;
        view_state->data.scroll.max_x = h_max;
        view_state->data.scroll.max_y = v_max;
        if (view_state->data.scroll.x > h_max) view_state->data.scroll.x = h_max;
        if (view_state->data.scroll.y > v_max) view_state->data.scroll.y = v_max;
        pane->h_scroll_position = view_state->data.scroll.x;
        pane->v_scroll_position = view_state->data.scroll.y;
    }

    pane->h_max_scroll = h_max;
    pane->v_max_scroll = v_max;

    if (pane->h_scroll_position > pane->h_max_scroll) {
        pane->h_scroll_position = pane->h_max_scroll;
    }
    if (pane->v_scroll_position > pane->v_max_scroll) {
        pane->v_scroll_position = pane->v_max_scroll;
    }

    if (state) {
        view_state_log_scroll_transition(state, view, "scroll.max",
            old_x, old_y, old_max_x, old_max_y,
            pane->h_scroll_position, pane->v_scroll_position,
            pane->h_max_scroll, pane->v_max_scroll);
    }
    if (state) sm_guard.commit();
}

void scroll_state_set_position_for_view(DocState* state, View* view, void* pane_ptr,
                                        float h_pos, float v_pos,
                                        bool is_viewport) {
    if (!pane_ptr) return;
    ScrollPane* pane = (ScrollPane*)pane_ptr;
    if (state) scroll_state_attach(state, pane_ptr);

    if (h_pos < 0.0f) h_pos = 0.0f;
    if (v_pos < 0.0f) v_pos = 0.0f;
    if (h_pos > pane->h_max_scroll) h_pos = pane->h_max_scroll;
    if (v_pos > pane->v_max_scroll) v_pos = pane->v_max_scroll;
    SmTransitionGuard sm_guard(state, SM_FAMILY_SCROLL, SM_EV_SCROLL_SET_POSITION, view);

    float old_x = pane->h_scroll_position;
    float old_y = pane->v_scroll_position;
    float old_max_x = pane->h_max_scroll;
    float old_max_y = pane->v_max_scroll;

    ViewState* view_state = scroll_view_state_get_or_create(state, view, pane);
    if (view_state) {
        old_x = view_state->data.scroll.x;
        old_y = view_state->data.scroll.y;
        old_max_x = view_state->data.scroll.max_x;
        old_max_y = view_state->data.scroll.max_y;
        view_state->data.scroll.x = h_pos;
        view_state->data.scroll.y = v_pos;
        view_state->data.scroll.max_x = pane->h_max_scroll;
        view_state->data.scroll.max_y = pane->v_max_scroll;
    }

    pane->h_scroll_position = h_pos;
    pane->v_scroll_position = v_pos;

    if (state) {
        view_state_log_scroll_transition(state, view, "scroll.position",
            old_x, old_y, old_max_x, old_max_y,
            h_pos, v_pos, pane->h_max_scroll, pane->v_max_scroll);
        state->is_dirty = true;
        state->needs_repaint = true;
        if (is_viewport) {
            state->scroll_x = h_pos;
            state->scroll_y = v_pos;
        }
        state->version++;
        sm_guard.commit();
        state_assert_after_mutation(state, "scroll_state_set_position_for_view");
    }
}

static void scroll_state_get_position(DocState* state, void* pane_ptr,
                                      float* out_h_pos, float* out_v_pos,
                                      float* out_h_max, float* out_v_max) {
    (void)state;
    if (out_h_pos) *out_h_pos = 0.0f;
    if (out_v_pos) *out_v_pos = 0.0f;
    if (out_h_max) *out_h_max = 0.0f;
    if (out_v_max) *out_v_max = 0.0f;

    if (!pane_ptr) return;
    ScrollPane* pane = (ScrollPane*)pane_ptr;
    if (out_h_pos) *out_h_pos = pane->h_scroll_position;
    if (out_v_pos) *out_v_pos = pane->v_scroll_position;
    if (out_h_max) *out_h_max = pane->h_max_scroll;
    if (out_v_max) *out_v_max = pane->v_max_scroll;
}

void scroll_state_get_position_for_view(DocState* state, View* view, void* pane_ptr,
                                        float* out_h_pos, float* out_v_pos,
                                        float* out_h_max, float* out_v_max) {
    if (out_h_pos) *out_h_pos = 0.0f;
    if (out_v_pos) *out_v_pos = 0.0f;
    if (out_h_max) *out_h_max = 0.0f;
    if (out_v_max) *out_v_max = 0.0f;

    ViewState* view_state = state && view ? view_state_get_for_kind(state, view, VIEW_STATE_SCROLL) : NULL;
    if (view_state && view_state->kind == VIEW_STATE_SCROLL) {
        if (out_h_pos) *out_h_pos = view_state->data.scroll.x;
        if (out_v_pos) *out_v_pos = view_state->data.scroll.y;
        if (out_h_max) *out_h_max = view_state->data.scroll.max_x;
        if (out_v_max) *out_v_max = view_state->data.scroll.max_y;
        return;
    }

    scroll_state_get_position(state, pane_ptr, out_h_pos, out_v_pos, out_h_max, out_v_max);
}

static void scroll_interaction_mark_dirty(DocState* state) {
    if (!state) return;
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
}

void scroll_state_set_hover_for_view(DocState* state, View* view, void* pane_ptr,
                                     bool h_hovered, bool v_hovered) {
    if (!state || !view) return;
    ScrollPane* pane = (ScrollPane*)pane_ptr;
    if (pane) scroll_state_attach(state, pane_ptr);
    SmTransitionGuard sm_guard(state, SM_FAMILY_SCROLL, SM_EV_SCROLLBAR_HOVER, view);

    ViewState* view_state = scroll_view_state_get_or_create(state, view, pane);
    if (!view_state) return;

    bool old_h = view_state->data.scroll.h_hovered != 0;
    bool old_v = view_state->data.scroll.v_hovered != 0;
    if (old_h == h_hovered && old_v == v_hovered) return;

    view_state->data.scroll.h_hovered = h_hovered ? 1 : 0;
    view_state->data.scroll.v_hovered = v_hovered ? 1 : 0;
    view_state_log_bool_transition(state, view, "scroll.h_hovered", old_h, h_hovered);
    view_state_log_bool_transition(state, view, "scroll.v_hovered", old_v, v_hovered);
    scroll_interaction_mark_dirty(state);
    sm_guard.commit();
    state_assert_after_mutation(state, "scroll_state_set_hover_for_view");
}

void scroll_state_begin_drag_for_view(DocState* state, View* view, void* pane_ptr,
                                      bool horizontal,
                                      float start_x, float start_y,
                                      float h_start_scroll, float v_start_scroll) {
    if (!state || !view) return;
    ScrollPane* pane = (ScrollPane*)pane_ptr;
    if (pane) scroll_state_attach(state, pane_ptr);
    SmTransitionGuard sm_guard(state, SM_FAMILY_SCROLL, SM_EV_SCROLLBAR_BEGIN_DRAG, view);

    ViewState* view_state = scroll_view_state_get_or_create(state, view, pane);
    if (!view_state) return;

    bool old_h = view_state->data.scroll.h_dragging != 0;
    bool old_v = view_state->data.scroll.v_dragging != 0;
    view_state->data.scroll.h_dragging = horizontal ? 1 : 0;
    view_state->data.scroll.v_dragging = horizontal ? 0 : 1;
    view_state->data.scroll.drag_start_x = start_x;
    view_state->data.scroll.drag_start_y = start_y;
    view_state->data.scroll.h_drag_start_scroll = h_start_scroll;
    view_state->data.scroll.v_drag_start_scroll = v_start_scroll;

    view_state_log_bool_transition(state, view, "scroll.h_dragging", old_h, horizontal);
    view_state_log_bool_transition(state, view, "scroll.v_dragging", old_v, !horizontal);
    scroll_interaction_mark_dirty(state);
    sm_guard.commit();
    state_assert_after_mutation(state, "scroll_state_begin_drag_for_view");
}

void scroll_state_clear_drag_for_view(DocState* state, View* view, void* pane_ptr) {
    if (!state || !view) return;
    ScrollPane* pane = (ScrollPane*)pane_ptr;
    if (pane) scroll_state_attach(state, pane_ptr);
    SmTransitionGuard sm_guard(state, SM_FAMILY_SCROLL, SM_EV_SCROLLBAR_CLEAR_DRAG, view);

    ViewState* view_state = scroll_view_state_get_or_create(state, view, pane);
    if (!view_state) return;

    bool old_h = view_state->data.scroll.h_dragging != 0;
    bool old_v = view_state->data.scroll.v_dragging != 0;
    if (!old_h && !old_v && view_state->data.scroll.drag_start_x == 0.0f &&
        view_state->data.scroll.drag_start_y == 0.0f &&
        view_state->data.scroll.h_drag_start_scroll == 0.0f &&
        view_state->data.scroll.v_drag_start_scroll == 0.0f) {
        return;
    }

    view_state->data.scroll.h_dragging = 0;
    view_state->data.scroll.v_dragging = 0;
    view_state->data.scroll.drag_start_x = 0.0f;
    view_state->data.scroll.drag_start_y = 0.0f;
    view_state->data.scroll.h_drag_start_scroll = 0.0f;
    view_state->data.scroll.v_drag_start_scroll = 0.0f;

    view_state_log_bool_transition(state, view, "scroll.h_dragging", old_h, false);
    view_state_log_bool_transition(state, view, "scroll.v_dragging", old_v, false);
    scroll_interaction_mark_dirty(state);
    sm_guard.commit();
    state_assert_after_mutation(state, "scroll_state_clear_drag_for_view");
}

void scroll_state_get_interaction_for_view(DocState* state, View* view,
                                           ScrollInteractionState* out_state) {
    if (!out_state) return;
    memset(out_state, 0, sizeof(ScrollInteractionState));
    ViewState* view_state = state && view ? view_state_get_for_kind(state, view, VIEW_STATE_SCROLL) : NULL;
    if (!view_state || view_state->kind != VIEW_STATE_SCROLL) return;

    out_state->h_hovered = view_state->data.scroll.h_hovered != 0;
    out_state->v_hovered = view_state->data.scroll.v_hovered != 0;
    out_state->h_dragging = view_state->data.scroll.h_dragging != 0;
    out_state->v_dragging = view_state->data.scroll.v_dragging != 0;
    out_state->drag_start_x = view_state->data.scroll.drag_start_x;
    out_state->drag_start_y = view_state->data.scroll.drag_start_y;
    out_state->h_drag_start_scroll = view_state->data.scroll.h_drag_start_scroll;
    out_state->v_drag_start_scroll = view_state->data.scroll.v_drag_start_scroll;
}

bool scroll_state_is_hovered_for_view(DocState* state, View* view) {
    ScrollInteractionState interaction;
    scroll_state_get_interaction_for_view(state, view, &interaction);
    return interaction.h_hovered || interaction.v_hovered;
}

bool scroll_state_is_dragging_for_view(DocState* state, View* view) {
    ScrollInteractionState interaction;
    scroll_state_get_interaction_for_view(state, view, &interaction);
    return interaction.h_dragging || interaction.v_dragging;
}

const char* form_control_get_value(DocState* state, View* view, uint32_t* out_len) {
    if (out_len) *out_len = 0;
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state && view_state->data.form.has_current_value) {
        if (out_len) *out_len = view_state->data.form.current_value_len;
        return view_state->data.form.current_value;
    }
    if (!view || !view->is_block()) return nullptr;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return nullptr;

    FormControlProp* form = block->form;
    if (out_len) *out_len = form->current_value_len;
    return form->current_value;
}

bool form_control_restore_text_control_state(DocState* state, View* view) {
    if (!state || !view || !view->is_element()) return false;
    ViewState* view_state = form_view_state_get(state, view);
    if (!view_state || !view_state->data.form.has_current_value) return false;

    DomElement* elem = lam::dom_require_element(view);
    if (!elem || !tc_is_text_control(elem)) return false;
    FormControlProp* form = elem->form;
    if (!form) return false;

    char* copy = (char*)mem_alloc((size_t)view_state->data.form.current_value_len + 1,
                                  MEM_CAT_DOM);
    if (!copy) return false;
    if (view_state->data.form.current_value_len > 0) {
        memcpy(copy, view_state->data.form.current_value,
               view_state->data.form.current_value_len);
    }
    copy[view_state->data.form.current_value_len] = '\0';

    if (form->current_value) mem_free(form->current_value);
    form->current_value = copy;
    form->current_value_len = view_state->data.form.current_value_len;
    form->current_value_u16_len = view_state->data.form.current_value_u16_len;
    if (form->current_value_u16_len == 0 && form->current_value_len > 0) {
        form->current_value_u16_len = tc_utf8_to_utf16_length(
            form->current_value, form->current_value_len);
    }
    form->selection_start = view_state->data.form.selection_start;
    form->selection_end = view_state->data.form.selection_end;
    if (form->selection_start > form->current_value_u16_len) {
        form->selection_start = form->current_value_u16_len;
    }
    if (form->selection_end > form->current_value_u16_len) {
        form->selection_end = form->current_value_u16_len;
    }
    if (form->selection_end < form->selection_start) {
        form->selection_end = form->selection_start;
    }
    form->selection_direction = view_state->data.form.selection_direction;
    if (form->selection_direction > 2) form->selection_direction = 0;
    form->tc_initialized = 1;
    form->state_ref = state;
    form->form_state_ref = view_state;
    form->value = form->current_value;
    view->view_state_ref = view_state;
    return true;
}

void form_control_set_value(DocState* state, View* view, const char* value, uint32_t len) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_TEXT, SM_EV_FORM_SET_VALUE, view);

    DomElement* elem = lam::dom_require_element(block);
    block->form->state_ref = state;

    // For text controls (input text, textarea), route through tc_set_value
    // to ensure all side effects (validation, events, history) are handled.
    if (tc_is_text_control(elem)) {
        tc_set_value(elem, value, len);
    } else {
        // For non-text controls, directly update the value field.
        // (Selects, file inputs, etc. don't have the complex mutation semantics
        // of text editing and validation history that tc_set_value provides.)
        FormControlProp* form = block->form;
        if (form->current_value) {
            mem_free(form->current_value);
        }
        form->current_value = (char*)mem_alloc(len + 1, MEM_CAT_DOM);
        memcpy(form->current_value, value, len);
        form->current_value[len] = '\0';
        form->current_value_len = len;
        extern uint32_t tc_utf8_to_utf16_length(const char* utf8, uint32_t byte_len);
        form->current_value_u16_len = tc_utf8_to_utf16_length(form->current_value, len);
        form->selection_start = form->current_value_u16_len;
        form->selection_end = form->current_value_u16_len;
        form->selection_direction = 0;
        form->value = form->current_value;
    }

    ViewState* view_state = form_view_state_get_or_create(state, view, block->form);
    if (view_state) {
        view_state->data.form.selection_start = block->form->selection_start;
        view_state->data.form.selection_end = block->form->selection_end;
        view_state->data.form.selection_direction = block->form->selection_direction;
        if (tc_is_text_control(elem) && block->form->current_value) {
            form_view_state_store_text_value(view_state, block->form);
        }
    }

    sm_guard.commit();
    form_state_mark_dirty(state);
}

void form_control_get_selection(DocState* state, View* view,
                                uint32_t* out_start, uint32_t* out_end, uint8_t* out_direction) {
    if (out_start) *out_start = 0;
    if (out_end) *out_end = 0;
    if (out_direction) *out_direction = 0;

    if (view && view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (elem && tc_is_text_control(elem)) {
            tc_ensure_init(elem);
            FormControlProp* form = elem->form;
            if (!form) return;
            if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
                state->sel.control == elem) {
                if (out_start) *out_start = state->sel.start_u16;
                if (out_end) *out_end = state->sel.end_u16;
                if (out_direction) *out_direction = form->selection_direction;
                return;
            }
            if (out_start) *out_start = form->selection_start;
            if (out_end) *out_end = form->selection_end;
            if (out_direction) *out_direction = form->selection_direction;
            return;
        }
    }

    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) {
        if (out_start) *out_start = view_state->data.form.selection_start;
        if (out_end) *out_end = view_state->data.form.selection_end;
        if (out_direction) *out_direction = view_state->data.form.selection_direction;
        return;
    }

    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;

    FormControlProp* form = block->form;
    if (out_start) *out_start = form->selection_start;
    if (out_end) *out_end = form->selection_end;
    if (out_direction) *out_direction = form->selection_direction;
}

void form_control_set_selection(DocState* state, View* view,
                                uint32_t start, uint32_t end, uint8_t direction) {
    if (!view) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_TEXT, SM_EV_FORM_SET_SELECTION, view);

    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (elem && tc_is_text_control(elem)) {
            tc_ensure_init(elem);
            FormControlProp* form = elem->form;
            if (!form) return;
            form->state_ref = state;
            tc_set_selection_range(elem, start, end, direction);
            ViewState* view_state = form_view_state_get_or_create(state, view, form);
            if (view_state) {
                view_state->data.form.selection_start = form->selection_start;
                view_state->data.form.selection_end = form->selection_end;
                view_state->data.form.selection_direction = form->selection_direction;
            }
            sm_guard.commit();
            form_state_mark_dirty(state);
            return;
        }
    }

    if (!view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;

    DomElement* elem = lam::dom_require_element(block);
    FormControlProp* form = block->form;
    form->state_ref = state;

    // For text controls, route through tc_set_selection_range to ensure
    // selection change events and callbacks are properly triggered.
    if (tc_is_text_control(elem)) {
        tc_set_selection_range(elem, start, end, direction);
        ViewState* view_state = form_view_state_get_or_create(state, view, form);
        if (view_state) {
            view_state->data.form.selection_start = form->selection_start;
            view_state->data.form.selection_end = form->selection_end;
            view_state->data.form.selection_direction = form->selection_direction;
        }
    } else {
        // For non-text controls, directly update selection fields.
        uint32_t max_offset = form->current_value_u16_len;
        if (start > max_offset) start = max_offset;
        if (end > max_offset) end = max_offset;
        form->selection_start = start;
        form->selection_end = end;
        form->selection_direction = direction & 3;
        ViewState* view_state = form_view_state_get_or_create(state, view, form);
        if (view_state) {
            view_state->data.form.selection_start = form->selection_start;
            view_state->data.form.selection_end = form->selection_end;
            view_state->data.form.selection_direction = form->selection_direction;
        }
    }

    sm_guard.commit();
    form_state_mark_dirty(state);
}

void form_control_sync_text_control_state(DocState* state, View* view) {
    if (!state || !view || !view->is_block()) return;
    ViewBlock* block = lam::view_require_block(view);
    FormControlProp* form = block->form;
    if (!form) return;

    form->state_ref = state;
    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    if (!view_state) return;
    view_state->data.form.selection_start = form->selection_start;
    view_state->data.form.selection_end = form->selection_end;
    view_state->data.form.selection_direction = form->selection_direction;
    if (form->current_value) {
        form_view_state_store_text_value(view_state, form);
    }
}

void form_control_sync_text_control_focus_state(DocState* state, View* view) {
    if (!state || !view || !view->is_element()) return;

    DomElement* elem = lam::dom_require_element(view);
    if (!tc_is_text_control(elem)) return;

    bool should_sync = false;
    if (state->focus && state->focus->current == view) {
        should_sync = true;
    }
    if (!should_sync && state->caret && state->caret->view == view) {
        should_sync = true;
    }
    if (!should_sync && state->selection &&
        (state->selection->view == view ||
         state->selection->anchor_view == view ||
         state->selection->focus_view == view)) {
        should_sync = true;
    }
    if (!should_sync && state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
        state->sel.control == elem) {
        should_sync = true;
    }
    if (!should_sync && state->active_text_control == elem) {
        should_sync = true;
    }
    if (!should_sync) return;

    focus_sync_text_control_state(state, view);
}

int form_control_get_selected_index(DocState* state, View* view) {
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.selected_index;

    return form_default_selected_index_from_tree(view);
}

void form_control_set_selected_index(DocState* state, View* view, int index) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_SELECT, SM_EV_FORM_SET_SELECTED_INDEX, view);

    FormControlProp* form = block->form;
    form->state_ref = state;

    // Clamp to valid range: -1 (none) or 0 to option_count-1
    if (index < -1) index = -1;
    if (index >= form->option_count) index = form->option_count - 1;

    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    int old_index = view_state ? view_state->data.form.selected_index : form_default_selected_index_from_tree(view);
    if (view_state) {
        if (old_index == index) return;
        view_state->data.form.selected_index = index;
    }

    view_state_log_int_transition(state, view, "form.selected_index", old_index, index);
    sm_guard.commit();
    form_state_mark_dirty(state);
}

float form_control_get_range_value(DocState* state, View* view) {
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.range_value;

    FormControlProp* form = form_prop_for_view(view);
    return form_default_range_value(view, form);
}

void form_control_set_range_value(DocState* state, View* view, float value) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_RANGE, SM_EV_FORM_SET_RANGE_VALUE, view);

    FormControlProp* form = block->form;
    form->state_ref = state;

    // Clamp to 0.0-1.0
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    float old_value = view_state ? view_state->data.form.range_value : form_default_range_value(view, form);
    if (view_state) {
        if (old_value == value) return;
        view_state->data.form.range_value = value;
    }

    view_state_log_float_transition(state, view, "form.range_value", old_value, value);
    sm_guard.commit();
    form_state_mark_dirty(state);
}

// ============================================================================
// Phase 4A: Constraint Attributes (disabled, readonly, required)
// ============================================================================

bool form_control_is_disabled(DocState* state, View* view) {
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.disabled != 0;

    if (!view) return false;
    return view_element_has_attr(view, "disabled");
}

void form_control_set_disabled(DocState* state, View* view, bool disabled) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmFamily family = form_control_schema_family_for_view(view);
    SmTransitionGuard sm_guard(state, family, SM_EV_FORM_SET_DISABLED, view);

    FormControlProp* form = block->form;
    form->state_ref = state;
    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    bool old_value = view_state ? view_state->data.form.disabled != 0 : view_element_has_attr(view, "disabled");
    if (view_state) {
        if (old_value == disabled) return;
        view_state->data.form.disabled = disabled ? 1 : 0;
    }
    view_state_log_bool_transition(state, view, "form.disabled", old_value, disabled);
    if (family != SM_FAMILY__COUNT) sm_guard.commit();
    form_state_mark_dirty(state);
}

bool form_control_is_readonly(DocState* state, View* view) {
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.readonly != 0;

    if (!view) return false;
    return view_element_has_attr(view, "readonly");
}

void form_control_set_readonly(DocState* state, View* view, bool readonly) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmFamily family = form_control_schema_family_for_readonly(view);
    SmTransitionGuard sm_guard(state, family, SM_EV_FORM_SET_READONLY, view);

    FormControlProp* form = block->form;
    form->state_ref = state;
    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    bool old_value = view_state ? view_state->data.form.readonly != 0 : view_element_has_attr(view, "readonly");
    if (view_state) {
        if (old_value == readonly) return;
        view_state->data.form.readonly = readonly ? 1 : 0;
    }
    view_state_log_bool_transition(state, view, "form.readonly", old_value, readonly);
    if (family != SM_FAMILY__COUNT) sm_guard.commit();
    form_state_mark_dirty(state);
}

bool form_control_is_required(DocState* state, View* view) {
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.required != 0;

    if (!view) return false;
    return view_element_has_attr(view, "required");
}

void form_control_set_required(DocState* state, View* view, bool required) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmFamily family = form_control_schema_family_for_view(view);
    SmTransitionGuard sm_guard(state, family, SM_EV_FORM_SET_REQUIRED, view);

    FormControlProp* form = block->form;
    form->state_ref = state;
    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    bool old_value = view_state ? view_state->data.form.required != 0 : view_element_has_attr(view, "required");
    if (view_state) {
        if (old_value == required) return;
        view_state->data.form.required = required ? 1 : 0;
    }
    view_state_log_bool_transition(state, view, "form.required", old_value, required);
    if (family != SM_FAMILY__COUNT) sm_guard.commit();
    form_state_mark_dirty(state);
}

// ============================================================================
// Phase 4B: Dropdown State Machine (open, close, hover tracking)
// ============================================================================

bool form_control_is_dropdown_open(DocState* state, View* view) {
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.dropdown_open != 0;

    return false;
}

void doc_state_open_dropdown(DocState* state, View* view) {
    if (!state || !view) return;

    View* old_owner = state->open_dropdown;
    if (old_owner == view && form_control_is_dropdown_open(state, view)) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_DROPDOWN, SM_EV_DROPDOWN_OPEN, view);

    if (old_owner && old_owner != view) {
        state->open_dropdown = NULL;
        form_control_close_dropdown(state, old_owner);
    } else if (old_owner == view) {
        state->open_dropdown = NULL;
    }

    form_control_open_dropdown(state, view);
    state->open_dropdown = view;
    doc_state_log_dropdown_owner_transition(state, old_owner, view);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    sm_guard.commit();
    state_assert_after_mutation(state, "doc_state_open_dropdown");
}

void doc_state_close_dropdown(DocState* state, View* view) {
    if (!state) return;

    View* old_owner = state->open_dropdown;
    View* target = view ? view : old_owner;
    if (!target) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_DROPDOWN, SM_EV_DROPDOWN_CLOSE, target);

    bool owner_changed = old_owner == target || !view;
    if (owner_changed) {
        state->open_dropdown = NULL;
    }

    form_control_close_dropdown(state, target);
    if (owner_changed) {
        doc_state_log_dropdown_owner_transition(state, old_owner, NULL);
    }

    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    sm_guard.commit();
    state_assert_after_mutation(state, "doc_state_close_dropdown");
}

void doc_state_set_dropdown_geometry(DocState* state,
                                     float x, float y, float width, float height) {
    if (!state) return;
    if (width < 0.0f) width = 0.0f;
    if (height < 0.0f) height = 0.0f;

    if (state->dropdown_x == x && state->dropdown_y == y &&
        state->dropdown_width == width && state->dropdown_height == height) {
        return;
    }
    SmTransitionGuard sm_guard(state, SM_FAMILY_DROPDOWN, SM_EV_DROPDOWN_SET_GEOMETRY,
                               state->open_dropdown);

    state->dropdown_x = x;
    state->dropdown_y = y;
    state->dropdown_width = width;
    state->dropdown_height = height;
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    sm_guard.commit();
    state_assert_after_mutation(state, "doc_state_set_dropdown_geometry");
}

void doc_state_open_context_menu(DocState* state, View* target,
                                 float x, float y, float width, float height) {
    if (!state || !target) return;
    if (width < 0.0f) width = 0.0f;
    if (height < 0.0f) height = 0.0f;
    SmTransitionGuard sm_guard(state, SM_FAMILY_CONTEXT_MENU, SM_EV_CONTEXT_MENU_OPEN, target);

    View* old_target = state->context_menu_target;
    int old_hover = state->context_menu_hover;

    state->context_menu_target = target;
    state->context_menu_x = x;
    state->context_menu_y = y;
    state->context_menu_width = width;
    state->context_menu_height = height;
    state->context_menu_hover = -1;

    doc_state_log_context_menu_target_transition(state, old_target, target);
    doc_state_log_context_menu_hover_transition(state, old_hover, -1);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    sm_guard.commit();
    state_assert_after_mutation(state, "doc_state_open_context_menu");
}

void doc_state_close_context_menu(DocState* state) {
    if (!state || !state->context_menu_target) return;

    View* old_target = state->context_menu_target;
    int old_hover = state->context_menu_hover;
    SmTransitionGuard sm_guard(state, SM_FAMILY_CONTEXT_MENU, SM_EV_CONTEXT_MENU_CLOSE, old_target);

    state->context_menu_target = NULL;
    state->context_menu_hover = -1;

    doc_state_log_context_menu_target_transition(state, old_target, NULL);
    doc_state_log_context_menu_hover_transition(state, old_hover, -1);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    sm_guard.commit();
    state_assert_after_mutation(state, "doc_state_close_context_menu");
}

void doc_state_set_context_menu_hover(DocState* state, int hover_index) {
    if (!state || !state->context_menu_target) return;
    if (hover_index < -1) hover_index = -1;

    int old_hover = state->context_menu_hover;
    if (old_hover == hover_index) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_CONTEXT_MENU, SM_EV_CONTEXT_MENU_HOVER,
                               state->context_menu_target);

    state->context_menu_hover = hover_index;
    doc_state_log_context_menu_hover_transition(state, old_hover, hover_index);
    state->is_dirty = true;
    state->needs_repaint = true;
    state->version++;
    sm_guard.commit();
    state_assert_after_mutation(state, "doc_state_set_context_menu_hover");
}

void form_control_open_dropdown(DocState* state, View* view) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_SELECT, SM_EV_DROPDOWN_OPEN, view);

    FormControlProp* form = block->form;
    form->state_ref = state;
    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    bool old_open = view_state ? view_state->data.form.dropdown_open != 0 : false;
    int old_hover = view_state ? view_state->data.form.hover_index : -1;
    int selected_index = view_state ? view_state->data.form.selected_index : form_default_selected_index_from_tree(view);
    if (view_state) {
        view_state->data.form.dropdown_open = 1;
        view_state->data.form.hover_index = selected_index;
    }
    view_state_log_bool_transition(state, view, "form.dropdown_open", old_open, true);
    view_state_log_int_transition(state, view, "form.hover_index", old_hover, selected_index);
    sm_guard.commit();
    form_state_mark_dirty(state);
}

void form_control_close_dropdown(DocState* state, View* view) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_SELECT, SM_EV_DROPDOWN_CLOSE, view);

    FormControlProp* form = block->form;
    form->state_ref = state;
    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    bool old_open = view_state ? view_state->data.form.dropdown_open != 0 : false;
    int old_hover = view_state ? view_state->data.form.hover_index : -1;
    if (view_state) {
        view_state->data.form.dropdown_open = 0;
        view_state->data.form.hover_index = -1;
    }
    view_state_log_bool_transition(state, view, "form.dropdown_open", old_open, false);
    view_state_log_int_transition(state, view, "form.hover_index", old_hover, -1);
    sm_guard.commit();
    form_state_mark_dirty(state);
}

void form_control_set_hover_index(DocState* state, View* view, int index) {
    if (!view || !view->is_block()) return;

    ViewBlock* block = lam::view_require_block(view);
    if (!block->form) return;
    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_SELECT, SM_EV_FORM_SET_HOVER_INDEX, view);

    FormControlProp* form = block->form;
    form->state_ref = state;

    // Clamp index to valid range
    if (index < -1) index = -1;
    if (index >= form->option_count) index = form->option_count - 1;

    ViewState* view_state = form_view_state_get_or_create(state, view, form);
    int old_index = view_state ? view_state->data.form.hover_index : -1;
    if (view_state) {
        if (old_index == index) return;
        view_state->data.form.hover_index = index;
    }

    view_state_log_int_transition(state, view, "form.hover_index", old_index, index);
    sm_guard.commit();
    form_state_mark_dirty(state);
}

int form_control_get_hover_index(DocState* state, View* view) {
    ViewState* view_state = form_view_state_get(state, view);
    if (view_state) return view_state->data.form.hover_index;

    return -1;
}

void state_remove(DocState* state, void* node, const char* name) {
    if (!state || !node || !name) return;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    const StateEntry* existing = (const StateEntry*)hashmap_get(state->state_map, &query);
    if (existing) {
        // Fire callback with null new value
        if (existing->on_change) {
            existing->on_change(node, name, existing->value, ItemNull, existing->callback_udata);
        }

        hashmap_delete(state->state_map, &query);
        state->is_dirty = true;
        state->version++;

        log_debug("state_remove: node=%p, name=%s", node, name);
        state_assert_after_mutation(state, "state_remove");
    }
}

// ============================================================================
// Immutable Mode Operations
// ============================================================================

DocState* state_set_immutable(DocState* state, void* node, const char* name, Item value) {
    if (!state || state->mode != STATE_MODE_IMMUTABLE) {
        // Fallback to in-place
        state_set(state, node, name, value);
        return state;
    }

    // Create new state version with shallow copy
    DocState* new_state = (DocState*)arena_alloc(state->arena, sizeof(DocState));
    if (!new_state) {
        log_error("state_set_immutable: failed to allocate new state");
        return state;
    }

    *new_state = *state;  // shallow copy
    new_state->version = state->version + 1;
    new_state->prev_version = state;

    // TODO: implement proper HAMT for structural sharing
    // For now, just create a new hashmap (not truly immutable)
    new_state->state_map = hashmap_new(
        sizeof(StateEntry),
        hashmap_count(state->state_map) + 16,
        0x12345678, 0x87654321,
        state_key_hash,
        state_key_cmp,
        NULL, NULL
    );

    // Copy all entries
    size_t iter = 0;
    void* item;
    while (hashmap_iter(state->state_map, &iter, &item)) {
        hashmap_set(new_state->state_map, item);
    }

    // Set the new value
    const char* interned = intern_state_name(name);
    StateEntry entry = {
        .key = { node, interned },
        .value = value,
        .last_modified = new_state->version,
        .on_change = NULL,
        .callback_udata = NULL
    };
    hashmap_set(new_state->state_map, &entry);

    new_state->is_dirty = true;

    log_debug("state_set_immutable: created version %llu", new_state->version);
    return new_state;
}

DocState* state_remove_immutable(DocState* state, void* node, const char* name) {
    if (!state || state->mode != STATE_MODE_IMMUTABLE) {
        state_remove(state, node, name);
        return state;
    }

    // Similar to set_immutable but deletes instead
    DocState* new_state = (DocState*)arena_alloc(state->arena, sizeof(DocState));
    if (!new_state) {
        log_error("state_remove_immutable: failed to allocate new state");
        return state;
    }

    *new_state = *state;
    new_state->version = state->version + 1;
    new_state->prev_version = state;

    new_state->state_map = hashmap_new(
        sizeof(StateEntry),
        hashmap_count(state->state_map),
        0x12345678, 0x87654321,
        state_key_hash,
        state_key_cmp,
        NULL, NULL
    );

    const char* interned = intern_state_name(name);

    // Copy all entries except the one to remove
    size_t iter = 0;
    void* item;
    while (hashmap_iter(state->state_map, &iter, &item)) {
        StateEntry* entry = (StateEntry*)item;
        if (entry->key.node != node || entry->key.name != interned) {
            hashmap_set(new_state->state_map, entry);
        }
    }

    new_state->is_dirty = true;

    log_debug("state_remove_immutable: created version %llu", new_state->version);
    return new_state;
}

// ============================================================================
// Callback Registration
// ============================================================================

void state_on_change(DocState* state, void* node, const char* name,
    StateChangeCallback callback, void* udata) {
    if (!state || !node || !name) return;

    const char* interned = intern_state_name(name);
    StateEntry query = { .key = { node, interned } };

    const StateEntry* existing = (const StateEntry*)hashmap_get(state->state_map, &query);
    if (existing) {
        // Update callback on existing entry
        StateEntry updated = *existing;
        updated.on_change = callback;
        updated.callback_udata = udata;
        hashmap_set(state->state_map, &updated);
    } else {
        // Create entry with callback but null value
        StateEntry entry = {
            .key = { node, interned },
            .value = ItemNull,
            .last_modified = 0,
            .on_change = callback,
            .callback_udata = udata
        };
        hashmap_set(state->state_map, &entry);
    }
}

// ============================================================================
// Batch Operations
// ============================================================================

void state_begin_batch(DocState* state) {
    (void)state;
    s_in_batch = true;
}

static void state_sync_selection_before_assert(DocState* state) {
    if (!state) return;
    state_store_refresh_caret_projection(state);
}

static void state_sync_dirty_flags_before_assert(DocState* state) {
    if (!state) return;

    // Dirty tracking is authoritative for pending visual work. Keep flags
    // aligned so invariant checks and the render loop observe one source.
    if (state->dirty_tracker.full_reflow) {
        state->needs_reflow = true;
    }
    if (state->selection_layout_dirty || state->dirty_tracker.full_repaint ||
        state->dirty_tracker.full_reflow || dirty_has_regions(&state->dirty_tracker)) {
        state->needs_repaint = true;
    }
}

void state_end_batch(DocState* state) {
    state_sync_selection_before_assert(state);
    state_sync_dirty_flags_before_assert(state);
    s_in_batch = false;
    // TODO: trigger deferred callbacks
    radiant_state_assert_valid(state, "state_end_batch");
}

// ============================================================================
// Dirty Tracking
// ============================================================================

static void dirty_mark_rect_with_source(DirtyTracker* tracker, float x, float y,
                                        float width, float height,
                                        uint32_t source_view_id) {
    if (!tracker) return;

    if (tracker->full_repaint) return;  // already marked for full repaint

    // clip to viewport — skip rects entirely outside visible area
    if (tracker->viewport_height > 0) {
        float vt = tracker->viewport_y;
        float vb = vt + tracker->viewport_height;
        if (y + height < vt || y > vb) return;
        if (y < vt) { height -= (vt - y); y = vt; }
        if (y + height > vb) { height = vb - y; }
    }

    // Check if we should coalesce with existing rects
    DirtyRect* dr = tracker->dirty_list;
    while (dr) {
        // Check for overlap
        bool overlaps = !(x + width < dr->x || dr->x + dr->width < x ||
                         y + height < dr->y || dr->y + dr->height < y);
        if (overlaps) {
            // Expand existing rect to include new rect
            float new_x = (x < dr->x) ? x : dr->x;
            float new_y = (y < dr->y) ? y : dr->y;
            float new_right = (x + width > dr->x + dr->width) ? x + width : dr->x + dr->width;
            float new_bottom = (y + height > dr->y + dr->height) ? y + height : dr->y + dr->height;

            dr->x = new_x;
            dr->y = new_y;
            dr->width = new_right - new_x;
            dr->height = new_bottom - new_y;
            if (dr->source_view_id != source_view_id) {
                dr->source_view_id = 0;
            }
            return;
        }
        dr = dr->next;
    }

    // Add new dirty rect
    if (!tracker->arena) return;

    DirtyRect* new_dr = (DirtyRect*)arena_alloc(tracker->arena, sizeof(DirtyRect));
    if (!new_dr) return;

    new_dr->x = x;
    new_dr->y = y;
    new_dr->width = width;
    new_dr->height = height;
    new_dr->source_view_id = source_view_id;
    new_dr->next = tracker->dirty_list;
    tracker->dirty_list = new_dr;
}

void dirty_mark_rect(DirtyTracker* tracker, float x, float y, float width, float height) {
    dirty_mark_rect_with_source(tracker, x, y, width, height, 0);
}

void dirty_mark_element(DocState* state, void* view_ptr) {
    if (!state || !view_ptr) return;

    View* view = static_cast<View*>(view_ptr);

    // Get element's absolute bounds
    float abs_x = view->x, abs_y = view->y;
    ViewElement* p = view->parent_view();
    while (p) {
        abs_x += p->x;
        abs_y += p->y;
        p = p->parent_view();
    }

    dirty_mark_rect_with_source(&state->dirty_tracker, abs_x, abs_y,
                                view->width, view->height, view->id);
    state->needs_repaint = true;
}

void dirty_clear(DirtyTracker* tracker) {
    if (!tracker) return;

    tracker->dirty_list = NULL;
    tracker->full_repaint = false;
    tracker->full_reflow = false;

    if (tracker->arena) {
        arena_reset(tracker->arena);
    }
}

bool dirty_has_regions(DirtyTracker* tracker) {
    if (!tracker) return false;
    return tracker->dirty_list != NULL || tracker->full_repaint;
}

// ============================================================================
// Reflow Scheduling
// ============================================================================

void reflow_schedule(DocState* state, void* node, ReflowScope scope, uint32_t reason) {
    if (!state || !node) return;

    ReflowScheduler* scheduler = &state->reflow_scheduler;

    // Check if we can coalesce with existing request
    ReflowRequest* req = scheduler->pending;
    while (req) {
        if (req->node == node) {
            // Upgrade scope if needed
            if (scope > req->scope) {
                req->scope = scope;
            }
            req->reason |= reason;
            return;
        }
        req = req->next;
    }

    // Add new request
    if (!scheduler->arena) return;

    ReflowRequest* new_req = (ReflowRequest*)arena_alloc(scheduler->arena, sizeof(ReflowRequest));
    if (!new_req) return;

    new_req->node = node;
    new_req->scope = scope;
    new_req->reason = reason;
    new_req->next = scheduler->pending;
    scheduler->pending = new_req;

    state->needs_reflow = true;

    log_debug("reflow_schedule: node=%p, scope=%d, reason=0x%x", node, scope, reason);
}

/**
 * Determine the highest reflow scope from all pending requests
 * Returns the maximum scope needed (REFLOW_FULL takes precedence)
 */
static ReflowScope get_max_reflow_scope(ReflowScheduler* scheduler) {
    ReflowScope max_scope = REFLOW_NONE;
    ReflowRequest* req = scheduler->pending;
    while (req) {
        if (req->scope > max_scope) {
            max_scope = req->scope;
        }
        if (max_scope == REFLOW_FULL) break;  // can't go higher
        req = req->next;
    }
    return max_scope;
}

/**
 * Mark element and optionally its ancestors/descendants for style recomputation
 */
static void mark_for_style_recompute(View* view, ReflowScope scope) {
    if (!view || !view->is_element()) return;

    DomElement* element = lam::dom_require_element(view);
    element->needs_style_recompute = true;
    element->styles_resolved = false;

    // For REFLOW_SUBTREE, mark all descendants
    if (scope >= REFLOW_SUBTREE) {
        View* child = view->is_block() ? (lam::view_require_block(view))->first_child : nullptr;
        while (child) {
            mark_for_style_recompute(child, REFLOW_SUBTREE);
            child = child->next();
        }
    }

    // For REFLOW_ANCESTORS, mark ancestors up to root
    if (scope == REFLOW_ANCESTORS || scope == REFLOW_FULL) {
        View* parent = view->parent;
        while (parent) {
            if (parent->is_element()) {
                DomElement* pe = lam::dom_require_element(parent);
                pe->needs_style_recompute = true;
                pe->styles_resolved = false;
            }
            parent = parent->parent;
        }
    }
}

void reflow_process_pending(DocState* state) {
    if (!state) return;

    ReflowScheduler* scheduler = &state->reflow_scheduler;
    if (scheduler->is_processing) return;  // prevent re-entry
    if (!scheduler->pending) return;       // nothing to do

    scheduler->is_processing = true;

    // Determine maximum scope
    ReflowScope max_scope = get_max_reflow_scope(scheduler);
    log_debug("reflow_process_pending: max_scope=%d", max_scope);

    // Mark affected elements for style recomputation
    ReflowRequest* req = scheduler->pending;
    while (req) {
        log_debug("reflow_process: node=%p, scope=%d, reason=0x%x", req->node, req->scope, req->reason);

        // Mark element for style recompute
        View* view = static_cast<View*>(req->node);
        mark_for_style_recompute(view, req->scope);

        req = req->next;
    }

    // Clear pending requests
    if (scheduler->arena) {
        arena_reset(scheduler->arena);
    }
    scheduler->pending = NULL;
    scheduler->is_processing = false;

    // Set flag indicating reflow is needed
    // Actual layout will be triggered by the render loop calling layout_html_doc
    state->needs_reflow = (max_scope > REFLOW_NONE);
}

void reflow_clear(DocState* state) {
    if (!state) return;

    ReflowScheduler* scheduler = &state->reflow_scheduler;
    scheduler->pending = NULL;
    scheduler->is_processing = false;

    if (scheduler->arena) {
        arena_reset(scheduler->arena);
    }
}

// ============================================================================
// Visited Links
// ============================================================================

static uint64_t url_hash_func(const void* item, uint64_t seed0, uint64_t seed1) {
    const uint64_t* hash = (const uint64_t*)item;
    return *hash;
}

static int url_hash_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const uint64_t* ha = (const uint64_t*)a;
    const uint64_t* hb = (const uint64_t*)b;
    if (*ha < *hb) return -1;
    if (*ha > *hb) return 1;
    return 0;
}

VisitedLinks* visited_links_create(Pool* pool) {
    VisitedLinks* visited = (VisitedLinks*)pool_calloc(pool, sizeof(VisitedLinks));
    if (!visited) return NULL;

    visited->url_hash_set = hashmap_new(
        sizeof(uint64_t),
        128,
        0xDEADBEEF, 0xCAFEBABE,
        url_hash_func,
        url_hash_compare,
        NULL, NULL
    );

    // Use random seeds for privacy
    visited->seed0 = 0x1234567890ABCDEFULL;
    visited->seed1 = 0xFEDCBA0987654321ULL;

    return visited;
}

void visited_links_destroy(VisitedLinks* visited) {
    if (!visited) return;

    if (visited->url_hash_set) {
        hashmap_free(visited->url_hash_set);
    }
}

void visited_links_add(VisitedLinks* visited, const char* url) {
    if (!visited || !url) return;

    uint64_t hash = hashmap_murmur(url, strlen(url), visited->seed0, visited->seed1);
    hashmap_set(visited->url_hash_set, &hash);

    log_debug("visited_links_add: hash=0x%llx", hash);
}

bool visited_links_check(VisitedLinks* visited, const char* url) {
    if (!visited || !url) return false;

    uint64_t hash = hashmap_murmur(url, strlen(url), visited->seed0, visited->seed1);
    return hashmap_get(visited->url_hash_set, &hash) != NULL;
}

// ============================================================================
// Legacy caret/selection write API
// ============================================================================
//
// These wrappers preserve view + byte-offset callers while routing through the
// canonical selection writer. Keep new rich editing mutations on
// state_store_set_selection() / editing transactions instead.

void state_store_legacy_caret_set(DocState* state, View* view, int char_offset) {
    log_debug("CARET_SET called: state=%p view=%p offset=%d", state, view, char_offset);
    if (!state) return;

    if (state->transition_depth == 0) {
        CaretTransitionArgs args = { .target = view, .offset = char_offset };
        caret_transition(state, CARET_TRANSITION_COLLAPSE_TO_BOUNDARY, &args);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection || !state->caret) {
        log_error("caret_set: failed to ensure DomSelection / projection storage");
        return;
    }

    SelectionState* active_selection = state->selection;
    if (active_selection && active_selection->is_selecting && !active_selection->is_collapsed &&
        active_selection->focus_view == view && active_selection->focus_offset == char_offset) {
        if (selection_is_text_control_view(view)) {
            if (text_control_set_selection_from_byte_offsets(state, view,
                    active_selection->anchor_offset, char_offset, "caret_set_active_text_control") &&
                state->selection) {
                state->selection->is_selecting = true;
            }
            state->needs_repaint = true;
            log_debug("caret_set: projected active text-control selection focus view=%p offset=%d",
                view, char_offset);
            return;
        }
        state->caret->view = view;
        state->caret->char_offset = char_offset;
        state->caret->visible = true;
        state->caret->blink_time = 0;
        state->needs_repaint = true;
        log_debug("caret_set: projected active selection focus view=%p offset=%d", view, char_offset);
        return;
    }
    bool preserve_pointer_selection = active_selection && active_selection->is_selecting;
    View* preserve_anchor_view = active_selection ? active_selection->anchor_view : NULL;
    int preserve_anchor_offset = active_selection ? active_selection->anchor_offset : 0;

    if (selection_is_text_control_view(view)) {
        if (!text_control_set_selection_from_byte_offsets(state, view, char_offset,
                char_offset, "caret_set")) {
            log_debug("caret_set: text-control collapse rejected");
            return;
        }
        if (preserve_pointer_selection && state->selection) {
            state->selection->is_selecting = true;
            if (preserve_anchor_view) {
                state->selection->anchor_view = preserve_anchor_view;
                state->selection->anchor_offset = preserve_anchor_offset;
            }
        } else if (state->selection) {
            state->selection->is_selecting = false;
        }
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, "collapse_to_text_control_boundary",
            view, char_offset, view, char_offset);
        log_debug("caret_set: text-control view=%p, offset=%d", view, char_offset);
        return;
    }

    DomBoundary boundary = boundary_from_legacy(view, char_offset);
    if (!boundary.node) return;

    const char* exc = NULL;
    if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
        log_debug("caret_set: collapse rejected: %s", exc ? exc : "?");
        return;
    }
    if (state->selection) {
        if (preserve_pointer_selection) {
            state->selection->is_selecting = true;
            if (preserve_anchor_view) {
                state->selection->anchor_view = preserve_anchor_view;
                state->selection->anchor_offset = preserve_anchor_offset;
            }
        } else {
            state->selection->is_selecting = false;
        }
    }
    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, "collapse_to_boundary", view, char_offset, view, char_offset);

    log_debug("caret_set: view=%p, offset=%d", view, char_offset);
}

void state_store_legacy_caret_set_position(DocState* state, View* view, int line, int column) {
    if (!state) return;

    if (!sync_ensure_selection(state) || !state->caret) return;

    CaretState* caret = state->caret;
    caret->view = view;
    caret->line = line;
    caret->column = column;
    caret->visible = true;
    caret->blink_time = 0;

    // Convert line/column to char_offset (caller should do this based on text content)
    // For now, this is a placeholder
    state->needs_repaint = true;

    log_debug("caret_set_position: view=%p, line=%d, col=%d", view, line, column);
}

// ============================================================================
// Cross-View Navigation Helpers
// ============================================================================

/**
 * Check if a view is navigable (can hold a caret position)
 * Only text views can hold the caret. Markers (list bullets, etc.) are
 * decorative and not editable — browsers skip over them when arrow-keying
 * the caret vertically. Including markers here would land the caret on a
 * ViewMarker and `update_caret_visual_position` would then read its
 * uninitialized `height` field as the caret height, painting a vertical
 * bar across the entire window.
 */
static bool is_view_navigable(View* view) {
    if (!view) return false;
    switch (view->view_type) {
        case RDT_VIEW_TEXT:
            return true;
        default:
            return false;
    }
}

/**
 * Get the text length of a view (for text views) or 1 (for atomic views like markers)
 */
static int get_view_content_length(View* view) {
    if (!view) return 0;

    if (view->is_text()) {
        ViewText* text = lam::view_require_text(view);
        if (text->text) {
            return strlen(text->text);
        }
        return 0;
    }

    // Atomic elements like markers count as 1 character
    if (view->view_type == RDT_VIEW_MARKER) {
        return 1;
    }

    return 0;
}

/**
 * Find the first navigable view within a subtree (depth-first)
 */
static View* find_first_navigable_in_subtree(View* root) {
    if (!root) return nullptr;

    if (is_view_navigable(root)) return root;

    if (root->is_element()) {
        DomElement* elem = lam::dom_require_element(root);
        View* child = static_cast<View*>(elem->first_child);
        while (child) {
            View* found = find_first_navigable_in_subtree(child);
            if (found) return found;
            child = child->next();
        }
    }

    return nullptr;
}

/**
 * Find the last navigable view within a subtree (depth-first, rightmost)
 */
static View* find_last_navigable_in_subtree(View* root) {
    if (!root) return nullptr;

    // First check children (rightmost first)
    if (root->is_element()) {
        DomElement* elem = lam::dom_require_element(root);
        if (elem->first_child) {
            // Find last child
            View* child = static_cast<View*>(elem->first_child);
            View* last_child = child;
            while (child) {
                last_child = child;
                child = child->next();
            }
            // Search from last to first
            while (last_child) {
                View* found = find_last_navigable_in_subtree(last_child);
                if (found) return found;
                last_child = last_child->prev_placed_view();
            }
        }
    }

    if (is_view_navigable(root)) return root;

    return nullptr;
}

/**
 * Find the next navigable view in document order (depth-first traversal)
 * Returns NULL if there is no next view
 */
static View* find_next_navigable_view(View* current) {
    if (!current) return nullptr;

    // First try next sibling and its subtree
    View* next = current->next();
    while (next) {
        View* found = find_first_navigable_in_subtree(next);
        if (found) return found;
        next = next->next();
    }

    // No more siblings, go up to parent and try its next sibling
    View* parent = current->parent;
    while (parent) {
        View* parent_next = parent->next();
        while (parent_next) {
            View* found = find_first_navigable_in_subtree(parent_next);
            if (found) return found;
            parent_next = parent_next->next();
        }
        parent = parent->parent;
    }

    return nullptr;
}

/**
 * Find the previous navigable view in document order
 * Returns NULL if there is no previous view
 */
static View* find_prev_navigable_view(View* current) {
    if (!current) return nullptr;

    log_debug("find_prev_navigable_view: current=%p type=%d", current, current->view_type);

    // First try previous sibling and its subtree (find last navigable)
    View* prev = current->prev_placed_view();
    while (prev) {
        log_debug("  checking prev sibling=%p type=%d", prev, prev->view_type);
        View* found = find_last_navigable_in_subtree(prev);
        if (found) {
            log_debug("  found=%p type=%d in sibling subtree", found, found->view_type);
            return found;
        }
        prev = prev->prev_placed_view();
    }

    // No more siblings, go up to parent and try its previous sibling
    View* parent = current->parent;
    while (parent) {
        log_debug("  going up to parent=%p type=%d", parent, parent->view_type);
        View* parent_prev = parent->prev_placed_view();
        while (parent_prev) {
            log_debug("  checking parent's prev sibling=%p type=%d", parent_prev, parent_prev->view_type);
            View* found = find_last_navigable_in_subtree(parent_prev);
            if (found) {
                log_debug("  found=%p type=%d in parent sibling subtree", found, found->view_type);
                return found;
            }
            parent_prev = parent_prev->prev_placed_view();
        }
        parent = parent->parent;
    }

    log_debug("  no prev navigable view found");
    return nullptr;
}

int utf8_offset_by_chars(unsigned char* text_data, int current_offset, int delta) {
    if (!text_data || delta == 0) return current_offset;

    if (delta > 0) {
        // Moving forward: skip over UTF-8 characters
        int chars_to_move = delta;
        int new_offset = current_offset;
        unsigned char* p = text_data + current_offset;
        unsigned char* p_end = p + strlen((const char*)p);
        while (chars_to_move > 0 && p < p_end) {
            uint32_t codepoint;
            int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
            if (bytes <= 0) bytes = 1;  // invalid UTF-8, skip one byte
            new_offset += bytes;
            p += bytes;
            chars_to_move--;
        }
        return new_offset;
    } else {
        // Moving backward: find start of previous UTF-8 characters
        int chars_to_move = -delta;
        int new_offset = current_offset;
        while (chars_to_move > 0 && new_offset > 0) {
            // Move back one byte at a time until we find a UTF-8 lead byte
            new_offset--;
            // Skip continuation bytes (10xxxxxx pattern)
            while (new_offset > 0 && (text_data[new_offset] & 0xC0) == 0x80) {
                new_offset--;
            }
            chars_to_move--;
        }
        return new_offset;
    }
}

/**
 * Check if a text view has meaningful content (non-empty, not just whitespace)
 */
static bool has_meaningful_content(View* view) {
    if (!view) return false;

    if (view->is_text()) {
        ViewText* text = lam::view_require_text(view);
        unsigned char* str = text->text_data();
        if (!str || *str == '\0') {
            log_debug("has_meaningful_content: view %p - empty string", view);
            return false;
        }

        // Check if it's only whitespace
        unsigned char* p = str;
        while (*p) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                log_debug("has_meaningful_content: view %p - has content '%c'", view, *p);
                return true;  // has non-whitespace content
            }
            p++;
        }
        log_debug("has_meaningful_content: view %p - whitespace only, len=%d", view, (int)(p - str));
        return false;  // only whitespace
    }

    // Markers always have meaningful content
    if (view->view_type == RDT_VIEW_MARKER) return true;

    return false;
}

/**
 * Check if a character is whitespace that gets collapsed in HTML rendering
 */
static inline bool is_collapsible_whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/**
 * Check if whitespace should be preserved for a view based on CSS white-space property.
 * Returns true if whitespace should NOT be collapsed (pre, pre-wrap, pre-line modes).
 */
static bool should_preserve_whitespace(View* view) {
    if (!view) return false;

    // Walk up the parent chain to find a block with white-space property
    View* parent = view->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = lam::dom_require_element(parent);
            if (elem->blk) {
                CssEnum ws = elem->blk->white_space;
                // CSS_VALUE_PRE, CSS_VALUE_PRE_WRAP, CSS_VALUE_PRE_LINE preserve whitespace
                if (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP || ws == CSS_VALUE_PRE_LINE) {
                    return true;
                }
                // Found a block with white-space set, use its value
                if (ws != 0) {
                    return false;  // normal or nowrap - collapse whitespace
                }
            }
        }
        parent = parent->parent;
    }
    return false;  // default: collapse whitespace
}

/**
 * Skip over collapsed whitespace when moving forward (right).
 * In HTML, consecutive whitespace is collapsed to a single space.
 * Only skip if we PASSED a whitespace char AND there's more whitespace ahead.
 * This preserves word boundaries (stopping after 'd' in "word  with").
 * @param prev_offset The offset before the move (to check what char we passed)
 * @param new_offset The offset after moving one character
 * @param preserve_ws If true, don't skip whitespace (pre/pre-wrap mode)
 */
static int skip_collapsed_whitespace_forward(unsigned char* str, int prev_offset, int new_offset, int text_length, bool preserve_ws) {
    if (!str || new_offset >= text_length || preserve_ws) return new_offset;

    // Check the character we just passed (the one between prev_offset and new_offset)
    // For ASCII whitespace (single byte), this is str[prev_offset]
    bool passed_whitespace = (prev_offset < text_length) && is_collapsible_whitespace(str[prev_offset]);
    bool facing_whitespace = is_collapsible_whitespace(str[new_offset]);

    // Only skip if we passed whitespace AND there's more whitespace ahead
    // This prevents skipping at word boundaries (non-ws → ws)
    if (passed_whitespace && facing_whitespace) {
        // Skip all consecutive whitespace
        while (new_offset < text_length && is_collapsible_whitespace(str[new_offset])) {
            new_offset++;
        }
    }
    return new_offset;
}

/**
 * Skip over collapsed whitespace when moving backward (left).
 * Only skip if we PASSED a whitespace char AND there's more whitespace behind.
 * @param prev_offset The offset before the move
 * @param new_offset The offset after moving one character backward
 * @param preserve_ws If true, don't skip whitespace (pre/pre-wrap mode)
 */
static int skip_collapsed_whitespace_backward(unsigned char* str, int prev_offset, int new_offset, bool preserve_ws) {
    if (!str || new_offset <= 0 || preserve_ws) return new_offset;

    // Check the character we just passed (the one at prev_offset - 1, or new_offset for leftward move)
    // When moving left from prev_offset to new_offset, we passed str[new_offset]
    bool passed_whitespace = is_collapsible_whitespace(str[new_offset]);
    bool facing_whitespace = (new_offset > 0) && is_collapsible_whitespace(str[new_offset - 1]);

    // Only skip if we passed whitespace AND there's more whitespace behind
    if (passed_whitespace && facing_whitespace) {
        // Skip back over all consecutive whitespace
        while (new_offset > 0 && is_collapsible_whitespace(str[new_offset - 1])) {
            new_offset--;
        }
    }
    return new_offset;
}

static bool state_store_commit_collapsed_caret(DocState* state,
                                               View* view,
                                               int char_offset,
                                               const char* source) {
    if (!state || !view) return false;
    if (state->selection && state->selection->is_selecting) {
        if (state->caret) {
            state->caret->visible = true;
            state->caret->blink_time = 0;
        }
        state->needs_repaint = true;
        log_debug("%s: deferred canonical collapse during active selection",
            source ? source : "state_store_commit_collapsed_caret");
        return true;
    }
    if (selection_is_text_control_view(view)) {
        if (!text_control_set_selection_from_byte_offsets(state, view, char_offset,
                char_offset, source ? source : "state_store_commit_collapsed_caret")) {
            log_debug("%s: text-control canonical collapse rejected",
                source ? source : "state_store_commit_collapsed_caret");
            return false;
        }
        if (state->selection) state->selection->is_selecting = false;
        if (state->caret) {
            state->caret->visible = true;
            state->caret->blink_time = 0;
        }
        state->needs_repaint = true;
        return true;
    }
    DomBoundary boundary = boundary_from_legacy(view, char_offset);
    if (!boundary.node) return false;
    const char* exc = NULL;
    if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
        log_debug("%s: canonical collapse rejected: %s",
            source ? source : "state_store_commit_collapsed_caret",
            exc ? exc : "?");
        return false;
    }
    if (state->selection) state->selection->is_selecting = false;
    if (state->caret) {
        state->caret->visible = true;
        state->caret->blink_time = 0;
    }
    state->needs_repaint = true;
    return true;
}

void state_store_legacy_caret_move(DocState* state, int delta) {
    if (!state || !state->caret || !state->caret->view) {
        log_debug("caret_move: early return - state=%p, caret=%p, view=%p",
            state, state ? state->caret : nullptr,
            (state && state->caret) ? state->caret->view : nullptr);
        return;
    }

    CaretState* caret = state->caret;
    View* view = caret->view;
    int current_offset = caret->char_offset;

    log_debug("caret_move: delta=%d, view=%p, view_type=%d, current_offset=%d",
        delta, view, view->view_type, current_offset);

    // Check if whitespace should be preserved (CSS white-space: pre/pre-wrap/pre-line)
    bool preserve_ws = should_preserve_whitespace(view);

    // For text views, we need to properly handle UTF-8 character boundaries
    if (view->is_text()) {
        ViewText* text_view = lam::view_require_text(view);
        unsigned char* str = text_view->text_data();
        int text_length = str ? strlen((char*)str) : 0;

        if (delta > 0) {
            // Moving right
            if (str && current_offset < text_length) {
                // Move by one UTF-8 character
                int new_offset = utf8_offset_by_chars(str, current_offset, 1);

                // Skip consecutive whitespace (only if we passed whitespace and there's more ahead)
                new_offset = skip_collapsed_whitespace_forward(str, current_offset, new_offset, text_length, preserve_ws);

                // If we reached the end of text, stop at boundary first (don't cross yet)
                if (new_offset >= text_length) {
                    caret->char_offset = text_length;
                    log_debug("caret_move: stopped at end of view %p (offset=%d)", view, text_length);
                } else {
                    caret->char_offset = new_offset;
                }
            } else {
                // Already at end of text, now cross to next view
                View* next_view = find_next_navigable_view(view);
                while (next_view && !has_meaningful_content(next_view)) {
                    next_view = find_next_navigable_view(next_view);
                }
                if (next_view) {
                    caret->view = next_view;
                    // Skip leading whitespace in new view (when crossing view boundary, treat as if we passed whitespace)
                    if (next_view->is_text()) {
                        ViewText* next_text = lam::view_require_text(next_view);
                        unsigned char* next_str = next_text->text_data();
                        int next_len = next_str ? strlen((char*)next_str) : 0;
                        bool next_preserve_ws = should_preserve_whitespace(next_view);
                        // Skip all leading whitespace when crossing view boundary
                        int offset = 0;
                        if (!next_preserve_ws && next_str) {
                            while (offset < next_len && is_collapsible_whitespace(next_str[offset])) {
                                offset++;
                            }
                        }
                        caret->char_offset = offset;
                    } else {
                        caret->char_offset = 0;
                    }
                    caret->line = 0;
                    caret->column = 0;
                    log_debug("caret_move: crossed to next view %p (type=%d)",
                        next_view, next_view->view_type);
                }
                // else: stay at end of current view
            }
        } else if (delta < 0) {
            // Moving left
            if (current_offset > 0) {
                // Move back by one UTF-8 character
                int new_offset = utf8_offset_by_chars(str, current_offset, -1);

                // Skip consecutive whitespace (only if we passed whitespace and there's more behind)
                new_offset = skip_collapsed_whitespace_backward(str, current_offset, new_offset, preserve_ws);

                // If we reached the start, stop at boundary first (don't cross yet)
                if (new_offset <= 0) {
                    caret->char_offset = 0;
                    log_debug("caret_move: stopped at start of view %p", view);
                } else {
                    caret->char_offset = new_offset;
                }
            } else {
                // At start of text, now cross to previous view
                View* prev_view = find_prev_navigable_view(view);
                while (prev_view && !has_meaningful_content(prev_view)) {
                    prev_view = find_prev_navigable_view(prev_view);
                }
                if (prev_view) {
                    caret->view = prev_view;
                    // Position at end of prev view, skipping trailing whitespace (when crossing view boundary)
                    int prev_length = get_view_content_length(prev_view);
                    if (prev_view->is_text()) {
                        ViewText* prev_text = lam::view_require_text(prev_view);
                        unsigned char* prev_str = prev_text->text_data();
                        bool prev_preserve_ws = should_preserve_whitespace(prev_view);
                        // Skip all trailing whitespace when crossing view boundary
                        int offset = prev_length;
                        if (!prev_preserve_ws && prev_str) {
                            while (offset > 0 && is_collapsible_whitespace(prev_str[offset - 1])) {
                                offset--;
                            }
                        }
                        caret->char_offset = offset;
                    } else {
                        caret->char_offset = prev_length;
                    }
                    caret->line = 0;
                    caret->column = caret->char_offset;
                    log_debug("caret_move: crossed to prev view %p (type=%d) at offset %d",
                        prev_view, prev_view->view_type, caret->char_offset);
                }
                // else: stay at start of current view
            }
        }
    } else if (view->view_type == RDT_VIEW_MARKER) {
        // Atomic elements: offset is 0 (before) or 1 (after)
        if (delta > 0) {
            if (current_offset == 0) {
                // Move from before to after the marker
                caret->char_offset = 1;
            } else {
                // Already after, move to next view with meaningful content
                View* next_view = find_next_navigable_view(view);
                while (next_view && !has_meaningful_content(next_view)) {
                    next_view = find_next_navigable_view(next_view);
                }
                if (next_view) {
                    caret->view = next_view;
                    caret->char_offset = 0;
                    caret->line = 0;
                    caret->column = 0;
                    log_debug("caret_move: crossed from marker to next view %p", next_view);
                }
            }
        } else if (delta < 0) {
            if (current_offset > 0) {
                // Move from after to before the marker
                caret->char_offset = 0;
            } else {
                // Already before, move to previous view with meaningful content
                View* prev_view = find_prev_navigable_view(view);
                while (prev_view && !has_meaningful_content(prev_view)) {
                    prev_view = find_prev_navigable_view(prev_view);
                }
                if (prev_view) {
                    caret->view = prev_view;
                    int prev_length = get_view_content_length(prev_view);
                    caret->char_offset = prev_length;
                    caret->line = 0;
                    caret->column = prev_length;
                    log_debug("caret_move: crossed from marker to prev view %p", prev_view);
                }
            }
        }
    } else {
        // Other non-text views: try to navigate to adjacent views with meaningful content
        if (delta > 0) {
            View* next_view = find_next_navigable_view(view);
            while (next_view && !has_meaningful_content(next_view)) {
                next_view = find_next_navigable_view(next_view);
            }
            if (next_view) {
                caret->view = next_view;
                caret->char_offset = 0;
            }
        } else if (delta < 0) {
            View* prev_view = find_prev_navigable_view(view);
            while (prev_view && !has_meaningful_content(prev_view)) {
                prev_view = find_prev_navigable_view(prev_view);
            }
            if (prev_view) {
                caret->view = prev_view;
                caret->char_offset = get_view_content_length(prev_view);
            }
        }
    }

    state_store_commit_collapsed_caret(state, caret->view, caret->char_offset,
        "caret_move");

    log_debug("caret_move: delta=%d, new_view=%p, new_offset=%d",
        delta, caret->view, caret->char_offset);
}

void state_store_legacy_caret_move_to(DocState* state, int where) {
    if (!state || !state->caret || !state->caret->view) return;

    CaretState* caret = state->caret;
    View* view = caret->view;

    // Handle text views with proper line/offset calculation
    if (view->is_text()) {
        ViewText* text = lam::view_require_text(view);
        TextRect* rect = text->rect;

        switch (where) {
            case 0: {  // line start
                // Find current line's rect
                TextRect* current_rect = rect;
                int line = 0;
                while (current_rect) {
                    int rect_end = current_rect->start_index + current_rect->length;
                    if (caret->char_offset >= current_rect->start_index &&
                        caret->char_offset <= rect_end) {
                        break;
                    }
                    line++;
                    current_rect = current_rect->next;
                }
                if (current_rect) {
                    caret->char_offset = current_rect->start_index;
                    caret->line = line;
                    caret->column = 0;
                }
                break;
            }
            case 1: {  // line end
                // Find current line's rect
                TextRect* current_rect = rect;
                int line = 0;
                while (current_rect) {
                    int rect_end = current_rect->start_index + current_rect->length;
                    if (caret->char_offset >= current_rect->start_index &&
                        caret->char_offset <= rect_end) {
                        break;
                    }
                    line++;
                    current_rect = current_rect->next;
                }
                if (current_rect) {
                    caret->char_offset = current_rect->start_index + current_rect->length;
                    caret->line = line;
                    caret->column = current_rect->length;
                }
                break;
            }
            case 2:  // doc start
                caret->char_offset = 0;
                caret->line = 0;
                caret->column = 0;
                break;
            case 3: {  // doc end
                // Find last rect and its end
                TextRect* last_rect = rect;
                int line = 0;
                while (last_rect && last_rect->next) {
                    line++;
                    last_rect = last_rect->next;
                }
                if (last_rect) {
                    caret->char_offset = last_rect->start_index + last_rect->length;
                    caret->line = line;
                    caret->column = last_rect->length;
                } else if (text->text) {
                    // Fallback: use text length
                    caret->char_offset = strlen(text->text);
                }
                break;
            }
        }
    } else {
        // Non-text views: simple handling
        switch (where) {
            case 0:  // line start
                caret->column = 0;
                break;
            case 1:  // line end
                break;
            case 2:  // doc start
                caret->char_offset = 0;
                caret->line = 0;
                caret->column = 0;
                break;
            case 3:  // doc end
                break;
        }
    }

    state_store_commit_collapsed_caret(state, caret->view, caret->char_offset,
        "caret_move_to");

    log_debug("caret_move_to: where=%d, offset=%d", where, caret->char_offset);
}

/**
 * Get the absolute visual Y position of a view
 * Walks up the parent chain to accumulate Y offsets
 */
static float get_absolute_y(View* view) {
    float y = 0;
    View* v = view;
    while (v) {
        y += v->y;
        v = v->parent;
    }
    return y;
}

/**
 * Get the absolute visual X position of a view
 */
static float get_absolute_x(View* view) {
    float x = 0;
    View* v = view;
    while (v) {
        x += v->x;
        v = v->parent;
    }
    return x;
}

/**
 * Get the absolute visual Y position of a TextRect within a text view
 */
static float get_rect_absolute_y(View* view, TextRect* rect) {
    float base_y = get_absolute_y(view);
    // TextRect y is relative to the parent block, same as the text view's y
    // So we need to use parent's absolute position + rect->y
    if (view->parent) {
        base_y = get_absolute_y(view->parent) + rect->y;
    }
    return base_y;
}

/**
 * Get the absolute visual X position of a TextRect within a text view.
 * TextRect coordinates are relative to the text view's parent block.
 */
static float get_rect_absolute_x(View* view, TextRect* rect) {
    if (view && view->parent) {
        return get_absolute_x(view->parent) + rect->x;
    }
    return get_absolute_x(view) + (rect ? rect->x : 0);
}

/**
 * Get the visual Y position of the caret's current position (absolute)
 */
static float get_caret_visual_y(View* view, int char_offset) {
    if (!view) return 0;

    if (view->is_text()) {
        ViewText* text = lam::view_require_text(view);
        TextRect* rect = text->rect;
        while (rect) {
            int rect_end = rect->start_index + rect->length;
            if (char_offset >= rect->start_index && char_offset <= rect_end) {
                return get_rect_absolute_y(view, rect);
            }
            rect = rect->next;
        }
        // Default to first rect's Y
        if (text->rect) return get_rect_absolute_y(view, text->rect);
    }

    return get_absolute_y(view);
}

/**
 * Find a navigable view/rect at a different visual Y position.
 * For down: find the next view/rect with Y > current_y.
 * For up: find the prev view/rect with Y < current_y.
 *
 * After locating the new line's Y, scans all rects on that same line and
 * picks the one whose absolute X range best matches `current_abs_x`. This
 * preserves the visual column when navigating across multiple inline text
 * segments split by inline elements (e.g. <code>) on the new line.
 *
 * Returns the chosen view, sets *out_offset to a starting offset, and
 * sets *out_rect to the chosen TextRect (NULL for non-text views).
 */
static View* find_view_at_different_y(View* current_view, int current_offset,
    int direction, float current_y, float current_abs_x, int* out_offset,
    TextRect** out_rect) {

    // Tolerance for "same line" detection (half line height)
    const float Y_TOLERANCE = 5.0f;
    if (out_rect) *out_rect = nullptr;

    // Result accumulators
    View*     best_view = nullptr;
    TextRect* best_rect = nullptr;
    float     best_score = 0;       // distance from current_abs_x to rect center; smaller is better
    bool      best_contains = false; // whether current_abs_x lies within rect's X range
    float     line_y = 0;            // Y of the new line, set when first candidate found

    auto consider_rect = [&](View* v, TextRect* r) {
        float r_abs_x = get_rect_absolute_x(v, r);
        bool contains = (current_abs_x >= r_abs_x &&
                         current_abs_x <= r_abs_x + r->width);
        float center = r_abs_x + r->width / 2.0f;
        float score = fabsf(current_abs_x - center);
        if (!best_view) {
            best_view = v; best_rect = r;
            best_score = score; best_contains = contains;
            return;
        }
        // Prefer rects that contain the X; among containers, any works (pick first).
        if (contains && !best_contains) {
            best_view = v; best_rect = r;
            best_score = score; best_contains = true;
        } else if (contains == best_contains && score < best_score) {
            best_view = v; best_rect = r;
            best_score = score;
        }
    };

    auto consider_non_text_view = [&](View* v) {
        // Non-text views: treat as a single point at their absolute origin.
        if (best_view) return; // prefer text candidates over non-text
        best_view = v;
        best_rect = nullptr;
        best_contains = false;
        best_score = fabsf(current_abs_x - get_absolute_x(v));
    };

    if (direction > 0) {
        // === Moving down ===
        View* view = current_view;

        // Phase 1: locate line_y by scanning forward.
        // Phase 2: collect all rects on that line.
        bool found_current_in_view = false;

        if (view->is_text()) {
            ViewText* text = lam::view_require_text(view);
            TextRect* rect = text->rect;
            while (rect) {
                int rect_end = rect->start_index + rect->length;
                if (!found_current_in_view) {
                    if (current_offset >= rect->start_index && current_offset <= rect_end) {
                        found_current_in_view = true;
                    }
                } else {
                    float rect_y = get_rect_absolute_y(view, rect);
                    if (rect_y > current_y + Y_TOLERANCE) {
                        if (!best_view) line_y = rect_y;
                        if (fabsf(rect_y - line_y) <= Y_TOLERANCE) {
                            consider_rect(view, rect);
                        } else if (best_view) {
                            break;
                        }
                    }
                }
                rect = rect->next;
            }
        }

        View* next = find_next_navigable_view(view);
        while (next) {
            if (next->is_text()) {
                ViewText* next_text = lam::view_require_text(next);
                TextRect* rect = next_text->rect;
                while (rect) {
                    float rect_y = get_rect_absolute_y(next, rect);
                    if (rect_y > current_y + Y_TOLERANCE) {
                        if (!best_view) line_y = rect_y;
                        if (fabsf(rect_y - line_y) <= Y_TOLERANCE) {
                            consider_rect(next, rect);
                        } else if (best_view) {
                            goto done_down;
                        }
                    }
                    rect = rect->next;
                }
            } else {
                float view_y = get_absolute_y(next);
                if (view_y > current_y + Y_TOLERANCE) {
                    if (!best_view) {
                        line_y = view_y;
                        consider_non_text_view(next);
                    } else if (fabsf(view_y - line_y) > Y_TOLERANCE) {
                        goto done_down;
                    }
                }
            }
            next = find_next_navigable_view(next);
        }
done_down: ;

    } else {
        // === Moving up ===
        View* view = current_view;

        if (view->is_text()) {
            ViewText* text = lam::view_require_text(view);
            TextRect* rect = text->rect;
            // Collect rects with Y < current_y; we want the highest such Y (closest to current).
            // First pass: find max rect_y satisfying rect_y < current_y - tol, restricted to
            //  rects before current_offset.
            // Then second pass: collect rects on that line.
            float line_y_local = -1e9f;
            bool any = false;
            TextRect* r = rect;
            while (r) {
                int rect_end = r->start_index + r->length;
                if (current_offset >= r->start_index && current_offset <= rect_end) break;
                float r_y = get_rect_absolute_y(view, r);
                if (r_y < current_y - Y_TOLERANCE) {
                    if (!any || r_y > line_y_local) { line_y_local = r_y; any = true; }
                }
                r = r->next;
            }
            if (any) {
                line_y = line_y_local;
                r = rect;
                while (r) {
                    int rect_end = r->start_index + r->length;
                    if (current_offset >= r->start_index && current_offset <= rect_end) break;
                    float r_y = get_rect_absolute_y(view, r);
                    if (fabsf(r_y - line_y) <= Y_TOLERANCE) consider_rect(view, r);
                    r = r->next;
                }
            }
        }

        if (!best_view) {
            // Walk previous views; first establish line_y from the closest prior view's max Y < current.
            View* prev = find_prev_navigable_view(view);
            while (prev) {
                float prev_max_y = -1e9f;
                bool prev_any = false;
                if (prev->is_text()) {
                    ViewText* pt = lam::view_require_text(prev);
                    for (TextRect* r = pt->rect; r; r = r->next) {
                        float r_y = get_rect_absolute_y(prev, r);
                        if (r_y < current_y - Y_TOLERANCE) {
                            if (!prev_any || r_y > prev_max_y) { prev_max_y = r_y; prev_any = true; }
                        }
                    }
                } else {
                    float v_y = get_absolute_y(prev);
                    if (v_y < current_y - Y_TOLERANCE) { prev_max_y = v_y; prev_any = true; }
                }
                if (prev_any) {
                    if (!best_view) line_y = prev_max_y;
                    // Collect rects on this line from prev.
                    if (prev->is_text()) {
                        ViewText* pt = lam::view_require_text(prev);
                        for (TextRect* r = pt->rect; r; r = r->next) {
                            float r_y = get_rect_absolute_y(prev, r);
                            if (fabsf(r_y - line_y) <= Y_TOLERANCE) consider_rect(prev, r);
                        }
                    } else {
                        consider_non_text_view(prev);
                    }
                    // Continue further back to pick up sibling segments on the same line.
                    prev = find_prev_navigable_view(prev);
                    while (prev) {
                        bool any_on_line = false;
                        if (prev->is_text()) {
                            ViewText* pt = lam::view_require_text(prev);
                            for (TextRect* r = pt->rect; r; r = r->next) {
                                float r_y = get_rect_absolute_y(prev, r);
                                if (fabsf(r_y - line_y) <= Y_TOLERANCE) {
                                    consider_rect(prev, r);
                                    any_on_line = true;
                                }
                            }
                        } else {
                            float v_y = get_absolute_y(prev);
                            if (fabsf(v_y - line_y) <= Y_TOLERANCE) any_on_line = true;
                        }
                        if (!any_on_line) break;
                        prev = find_prev_navigable_view(prev);
                    }
                    break;
                }
                prev = find_prev_navigable_view(prev);
            }
        }
    }

    if (!best_view) return nullptr;
    if (best_rect) {
        *out_offset = best_rect->start_index;
        if (out_rect) *out_rect = best_rect;
    } else {
        *out_offset = 0;
        if (out_rect) *out_rect = nullptr;
    }
    return best_view;
}

void state_store_legacy_caret_move_line(DocState* state, int delta, struct UiContext* uicon) {
    if (!state || !state->caret || !state->caret->view) return;

    CaretState* caret = state->caret;
    View* view = caret->view;

    // Get current visual position. caret->x is rect-relative (same coord space
    // as the parent block). Convert to absolute X so it can be compared across
    // sibling text segments under different inline ancestors.
    float current_y = get_caret_visual_y(view, caret->char_offset);
    float current_abs_x = (view->parent ? get_absolute_x(view->parent) : 0) + caret->x;

    // Find view/rect at different Y position whose X best matches current_abs_x
    int new_offset = 0;
    TextRect* new_rect = nullptr;
    View* new_view = find_view_at_different_y(view, caret->char_offset,
        delta, current_y, current_abs_x, &new_offset, &new_rect);

    if (new_view) {
        // For text targets, refine new_offset using the same visual column so
        // the caret lands directly under its previous position rather than at
        // the start of the new line.
        if (new_view->is_text() && new_rect) {
            float new_parent_abs_x = (new_view->parent ? get_absolute_x(new_view->parent) : 0);
            float target_local_x = current_abs_x - new_parent_abs_x;
            new_offset = dom_range_byte_offset_for_x(uicon,
                lam::view_require_text(new_view), new_rect, target_local_x);
        }
        caret->view = new_view;
        caret->char_offset = new_offset;
        caret->line = 0;
        caret->column = new_offset;

        log_debug("caret_move_line: moved to view %p (type=%d) offset=%d, y: %.1f -> new_y, abs_x=%.1f",
            new_view, new_view->view_type, new_offset, current_y, current_abs_x);
    } else {
        log_debug("caret_move_line: no line found in direction %d from y=%.1f", delta, current_y);
    }

    state_store_commit_collapsed_caret(state, caret->view, caret->char_offset,
        "caret_move_line");
}

void state_store_legacy_caret_clear(DocState* state) {
    if (!state) return;

    const char* exc = NULL;
    state_store_set_selection(state, NULL, NULL, &exc);

    if (state->caret) {
        memset(state->caret, 0, sizeof(CaretState));
    }

    state->needs_repaint = true;
    log_debug("caret_clear");
}

static void projection_chain_offset(View* view, float* out_x, float* out_y) {
    float chain_x = 0;
    float chain_y = 0;
    for (View* parent = view ? view->parent : NULL; parent; parent = parent->parent) {
        if (parent->view_type == RDT_VIEW_BLOCK ||
            parent->view_type == RDT_VIEW_INLINE_BLOCK ||
            parent->view_type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = lam::view_require_block(parent);
            chain_x += block->x;
            chain_y += block->y;
        }
    }
    if (out_x) *out_x = chain_x;
    if (out_y) *out_y = chain_y;
}

void caret_project_visual(DocState* state, float x, float y, float height) {
    if (!state || !state->caret) return;
    state->caret->x = x;
    state->caret->y = y;
    state->caret->height = height;
    state->needs_repaint = true;
}

void caret_project_visual_from_block(DocState* state, View* view,
                                     float x, float y, float height,
                                     float block_x, float block_y) {
    if (!state || !state->caret) return;
    caret_project_visual(state, x, y, height);
    float chain_x = 0;
    float chain_y = 0;
    projection_chain_offset(view, &chain_x, &chain_y);
    state->caret->iframe_offset_x = block_x - chain_x;
    state->caret->iframe_offset_y = block_y - chain_y;
}

void caret_project_visual_from_selection(DocState* state, float x, float y, float height) {
    if (!state || !state->caret) return;
    caret_project_visual(state, x, y, height);
    if (state->selection) {
        state->caret->iframe_offset_x = state->selection->iframe_offset_x;
        state->caret->iframe_offset_y = state->selection->iframe_offset_y;
    }
}

void selection_project_anchor_visual_from_caret(DocState* state, float x, float y, float height) {
    if (!state || !state->selection) return;
    state->selection->start_x = x;
    state->selection->start_y = y;
    state->selection->end_x = x;
    state->selection->end_y = y + height;
    if (state->caret) {
        state->selection->iframe_offset_x = state->caret->iframe_offset_x;
        state->selection->iframe_offset_y = state->caret->iframe_offset_y;
    }
    state->selection_layout_dirty = true;
    state->needs_repaint = true;
}

void selection_project_focus_visual(DocState* state, float x, float y, float height) {
    if (!state || !state->selection) return;
    state->selection->end_x = x;
    state->selection->end_y = y + height;
    state->selection_layout_dirty = true;
    state->needs_repaint = true;
}

void selection_finish_active_gesture(DocState* state) {
    if (!state || !state->selection) return;
    state->selection->is_selecting = false;
    state->editing.pointer_selecting = false;
    state->editing.drag_anchor_view = NULL;
    state->editing.drag_anchor_offset = 0;
    state->editing.drag_mode = EDITING_DRAG_CHAR;
}

void selection_press_in_range_begin(DocState* state, View* view, int offset) {
    if (!state) return;
    state->text_selection_press_in_range = true;
    state->text_selection_press_view = view;
    state->text_selection_press_offset = offset;
}

void selection_press_in_range_clear(DocState* state) {
    if (!state) return;
    state->text_selection_press_in_range = false;
    state->text_selection_press_view = NULL;
    state->text_selection_press_offset = 0;
    state->editing.drag_anchor_view = NULL;
    state->editing.drag_anchor_offset = 0;
    state->editing.drag_mode = EDITING_DRAG_CHAR;
    editing_interaction_sync_projection(state);
}

bool selection_press_in_range_pending(DocState* state, View** out_view, int* out_offset) {
    if (!state || !state->text_selection_press_in_range) return false;
    if (out_view) *out_view = state->text_selection_press_view;
    if (out_offset) *out_offset = state->text_selection_press_offset;
    return true;
}

bool caret_prepare_selective_repaint(DocState* state) {
    if (!state || state->needs_reflow || state->dirty_tracker.full_repaint ||
        dirty_has_regions(&state->dirty_tracker) || !state->caret || !state->caret->view) {
        return false;
    }

    float caret_w = 5.0f;
    if (state->caret->prev_abs_x >= 0) {
        dirty_mark_rect(&state->dirty_tracker,
            state->caret->prev_abs_x - 1, state->caret->prev_abs_y - 1,
            caret_w + 2, state->caret->prev_abs_height + 2);
    }
    View* caret_parent = state->caret->view->parent;
    if (caret_parent) {
        dirty_mark_element(state, caret_parent);
    }
    return true;
}

bool caret_get_position(DocState* state, View** out_view, int* out_offset) {
    if (out_view) *out_view = NULL;
    if (out_offset) *out_offset = 0;
    if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
        state->sel.control && tc_is_text_control(state->sel.control)) {
        DomElement* control = state->sel.control;
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        if (form) {
            uint32_t focus_u16 = state->sel.direction == DOM_SEL_DIR_BACKWARD ?
                state->sel.start_u16 : state->sel.end_u16;
            uint32_t focus_byte = text_control_u16_offset_to_byte(form, focus_u16);
            if (out_view) *out_view = static_cast<View*>(control);
            if (out_offset) {
                *out_offset = static_cast<int>(focus_byte); // INT_CAST_OK: legacy caret API exposes text-control byte offsets as int.
            }
            return true;
        }
    }
    DomBoundary focus = dom_selection_focus_boundary(state ? state->dom_selection : NULL);
    if (state && state->dom_selection && state->dom_selection->range_count > 0 &&
        focus.node) {
        dom_boundary_to_legacy_projection(focus, out_view, out_offset);
        return out_view ? *out_view != NULL : true;
    }
    if (!state || !state->caret || !state->caret->view) return false;
    if (out_view) *out_view = state->caret->view;
    if (out_offset) *out_offset = state->caret->char_offset;
    return true;
}

bool caret_get_offset(DocState* state, int* out_offset) {
    if (out_offset) *out_offset = 0;
    if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
        state->sel.control && tc_is_text_control(state->sel.control)) {
        DomElement* control = state->sel.control;
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        if (form) {
            uint32_t focus_u16 = state->sel.direction == DOM_SEL_DIR_BACKWARD ?
                state->sel.start_u16 : state->sel.end_u16;
            uint32_t focus_byte = text_control_u16_offset_to_byte(form, focus_u16);
            if (out_offset) {
                *out_offset = static_cast<int>(focus_byte); // INT_CAST_OK: legacy caret API exposes text-control byte offsets as int.
            }
            return true;
        }
    }
    DomBoundary focus = dom_selection_focus_boundary(state ? state->dom_selection : NULL);
    if (state && state->dom_selection && state->dom_selection->range_count > 0 &&
        focus.node) {
        dom_boundary_to_legacy_projection(focus, NULL, out_offset);
        return true;
    }
    if (!state || !state->caret) return false;
    if (out_offset) *out_offset = state->caret->char_offset;
    return true;
}

View* caret_get_view(DocState* state) {
    if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
        state->sel.control && tc_is_text_control(state->sel.control)) {
        return static_cast<View*>(state->sel.control);
    }
    DomBoundary focus = dom_selection_focus_boundary(state ? state->dom_selection : NULL);
    if (state && state->dom_selection && state->dom_selection->range_count > 0 &&
        focus.node) {
        View* view = NULL;
        dom_boundary_to_legacy_projection(focus, &view, NULL);
        return view;
    }
    if (!state || !state->caret) return NULL;
    return state->caret->view;
}

bool caret_get_visual_snapshot(DocState* state, float* out_x, float* out_y,
                               float* out_height, float* out_iframe_offset_x,
                               float* out_iframe_offset_y) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_height) *out_height = 0;
    if (out_iframe_offset_x) *out_iframe_offset_x = 0;
    if (out_iframe_offset_y) *out_iframe_offset_y = 0;
    if (!state || !state->caret) return false;
    if (out_x) *out_x = state->caret->x;
    if (out_y) *out_y = state->caret->y;
    if (out_height) *out_height = state->caret->height;
    if (out_iframe_offset_x) *out_iframe_offset_x = state->caret->iframe_offset_x;
    if (out_iframe_offset_y) *out_iframe_offset_y = state->caret->iframe_offset_y;
    return true;
}

bool caret_get_render_snapshot(DocState* state, View** out_view,
                               int* out_offset, float* out_x, float* out_y,
                               float* out_height, float* out_iframe_offset_x,
                               float* out_iframe_offset_y, bool* out_visible) {
    if (out_view) *out_view = NULL;
    if (out_offset) *out_offset = 0;
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_height) *out_height = 0;
    if (out_iframe_offset_x) *out_iframe_offset_x = 0;
    if (out_iframe_offset_y) *out_iframe_offset_y = 0;
    if (out_visible) *out_visible = false;
    if (!state || !state->caret) return false;
    if (out_view) *out_view = state->caret->view;
    if (out_offset) *out_offset = state->caret->char_offset;
    if (out_x) *out_x = state->caret->x;
    if (out_y) *out_y = state->caret->y;
    if (out_height) *out_height = state->caret->height;
    if (out_iframe_offset_x) *out_iframe_offset_x = state->caret->iframe_offset_x;
    if (out_iframe_offset_y) *out_iframe_offset_y = state->caret->iframe_offset_y;
    if (out_visible) *out_visible = state->caret->visible;
    return state->caret->view != NULL;
}

bool caret_get_debug_snapshot(DocState* state, View** out_view,
                              int* out_offset, int* out_line, int* out_column,
                              float* out_x, float* out_y, float* out_height,
                              bool* out_visible) {
    if (out_view) *out_view = NULL;
    if (out_offset) *out_offset = 0;
    if (out_line) *out_line = 0;
    if (out_column) *out_column = 0;
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_height) *out_height = 0;
    if (out_visible) *out_visible = false;
    if (!state || !state->caret) return false;
    if (out_view) *out_view = state->caret->view;
    if (out_offset) *out_offset = state->caret->char_offset;
    if (out_line) *out_line = state->caret->line;
    if (out_column) *out_column = state->caret->column;
    if (out_x) *out_x = state->caret->x;
    if (out_y) *out_y = state->caret->y;
    if (out_height) *out_height = state->caret->height;
    if (out_visible) *out_visible = state->caret->visible;
    return true;
}

bool caret_has_projection(DocState* state) {
    if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        return state->caret && state->caret->view != NULL;
    }
    if (state && state->dom_selection) {
        DomBoundary focus = dom_selection_focus_boundary(state->dom_selection);
        return state->dom_selection->range_count > 0 && focus.node != NULL;
    }
    return state && state->caret && state->caret->view != NULL;
}

bool caret_is_visible(DocState* state) {
    return state && state->caret && state->caret->visible;
}

void caret_project_previous_visual_rect(DocState* state, float x, float y, float height) {
    if (!state || !state->caret) return;
    state->caret->prev_abs_x = x;
    state->caret->prev_abs_y = y;
    state->caret->prev_abs_height = height;
}

void caret_toggle_blink(DocState* state) {
    if (!state || !state->caret) return;

    // Guard invariant: caret visibility is only meaningful when projection
    // has a concrete target view.
    if (!state->caret->view) {
        if (state->caret->visible) {
            state->caret->visible = false;
            state->needs_repaint = true;
        }
        state->caret->blink_time = 0;
        return;
    }

    // DISABLED for debugging - keep caret always visible
    // state->caret->visible = !state->caret->visible;
    state->caret->visible = true;  // always visible
    state->needs_repaint = true;
}

// ============================================================================
// Selection compatibility writes
// ============================================================================

void state_store_legacy_selection_start(DocState* state, View* view, int char_offset) {
    if (!state) return;

    if (state->transition_depth == 0) {
        SelectionTransitionArgs args = {
            .target = view,
            .anchor_offset = char_offset,
            .focus_offset = char_offset,
        };
        selection_transition(state, SELECTION_TRANSITION_START_POINTER_SELECTION, &args);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection || !state->selection) {
        log_error("selection_start: failed to ensure DomSelection / projection storage");
        return;
    }

    if (selection_is_text_control_view(view)) {
        if (!text_control_set_selection_from_byte_offsets(state, view, char_offset,
                char_offset, "selection_start")) {
            log_debug("selection_start: text-control collapse rejected");
            return;
        }
        if (state->selection) state->selection->is_selecting = true;
        state->editing.pointer_selecting = true;
        state->editing.drag_anchor_view = view;
        state->editing.drag_anchor_offset = char_offset;
        state->editing.drag_mode = EDITING_DRAG_CHAR;
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, "start_text_control_selection",
            view, char_offset, view, char_offset);
        log_debug("selection_start: text-control view=%p, offset=%d", view, char_offset);
        return;
    }

    DomBoundary boundary = boundary_from_legacy(view, char_offset);
    if (!boundary.node) return;

    const char* exc = NULL;
    if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
        log_debug("selection_start: collapse rejected: %s", exc ? exc : "?");
        return;
    }
    if (state->selection) {
        state->selection->is_selecting = true;
    }
    state->editing.pointer_selecting = true;
    state->editing.drag_anchor_view = view;
    state->editing.drag_anchor_offset = char_offset;
    state->editing.drag_mode = EDITING_DRAG_CHAR;
    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, "start_pointer_selection", view, char_offset, view, char_offset);

    log_debug("selection_start: view=%p, offset=%d", view, char_offset);
}

void state_store_legacy_selection_extend(DocState* state, int char_offset) {
    if (!state) return;

    if (state->transition_depth == 0) {
        SelectionTransitionArgs args = {
            .target = NULL,
            .anchor_offset = 0,
            .focus_offset = char_offset,
        };
        selection_transition(state, SELECTION_TRANSITION_EXTEND_TO_BOUNDARY, &args);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection || !state->selection) return;

    SelectionState* sel = state->selection;
    bool was_selecting = sel->is_selecting;
    View* focus_view = sel->focus_view ? sel->focus_view : sel->anchor_view;
    if (!focus_view && state->caret) focus_view = state->caret->view;
    if (selection_is_text_control_view(focus_view)) {
        int anchor_offset = sel->anchor_view == focus_view ? sel->anchor_offset : sel->focus_offset;
        if (!text_control_set_selection_from_byte_offsets(state, focus_view,
                anchor_offset, char_offset, "selection_extend")) {
            log_debug("selection_extend: text-control extend rejected");
            return;
        }
        if (state->selection) state->selection->is_selecting = was_selecting;
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, "extend_text_control_selection",
            sel->anchor_view, sel->anchor_offset, focus_view, char_offset);
        log_debug("selection_extend: text-control focus=%d", char_offset);
        return;
    }
    DomBoundary focus = boundary_from_legacy(focus_view, char_offset);
    if (!focus.node) return;

    const char* exc = NULL;
    if (!selection_extend_dom_to_focus(selection, &focus, &exc)) {
        log_debug("selection_extend: extend rejected: %s", exc ? exc : "?");
        return;
    }
    if (state->selection) state->selection->is_selecting = was_selecting;
    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, "extend_to_boundary",
        state->selection ? state->selection->anchor_view : NULL,
        state->selection ? state->selection->anchor_offset : 0,
        focus_view, char_offset);

    log_debug("selection_extend: focus=%d, collapsed=%d", char_offset, sel->is_collapsed);
}

void state_store_legacy_selection_extend_to_view(DocState* state, View* view, int char_offset) {
    if (!state) return;

    if (state->transition_depth == 0) {
        SelectionTransitionArgs args = {
            .target = view,
            .anchor_offset = 0,
            .focus_offset = char_offset,
        };
        selection_transition(state, SELECTION_TRANSITION_EXTEND_TO_VIEW, &args);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection || !state->selection) return;
    bool was_selecting = state->selection->is_selecting;

    if (selection_is_text_control_view(view)) {
        int anchor_offset = state->selection->anchor_view == view ?
            state->selection->anchor_offset : state->selection->focus_offset;
        if (!text_control_set_selection_from_byte_offsets(state, view,
                anchor_offset, char_offset, "selection_extend_to_view")) {
            log_debug("selection_extend_to_view: text-control extend rejected");
            return;
        }
        if (state->selection) state->selection->is_selecting = was_selecting;
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, "extend_text_control_selection",
            state->selection ? state->selection->anchor_view : NULL,
            state->selection ? state->selection->anchor_offset : 0,
            view, char_offset);
        log_debug("selection_extend_to_view: text-control focus_view=%p, focus_offset=%d", view, char_offset);
        return;
    }

    DomBoundary focus = boundary_from_legacy(view, char_offset);
    if (!focus.node) return;

    const char* exc = NULL;
    if (!selection_extend_dom_to_focus(selection, &focus, &exc)) {
        log_debug("selection_extend_to_view: extend rejected: %s", exc ? exc : "?");
        return;
    }
    if (state->selection) state->selection->is_selecting = was_selecting;
    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, "extend_to_boundary",
        state->selection ? state->selection->anchor_view : NULL,
        state->selection ? state->selection->anchor_offset : 0,
        view, char_offset);

    log_debug("selection_extend_to_view: focus_view=%p, focus_offset=%d, anchor_view=%p, collapsed=%d",
        view, char_offset, state->selection->anchor_view, state->selection->is_collapsed);
}

void state_store_legacy_selection_set(DocState* state, View* view, int anchor_offset, int focus_offset) {
    if (!state) return;

    if (state->transition_depth == 0) {
        SelectionTransitionArgs args = {
            .target = view,
            .anchor_offset = anchor_offset,
            .focus_offset = focus_offset,
        };
        selection_transition(state, SELECTION_TRANSITION_SET_BASE_AND_EXTENT, &args);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection || !state->selection) return;

    if (selection_is_text_control_view(view)) {
        if (!text_control_set_selection_from_byte_offsets(state, view,
                anchor_offset, focus_offset, "selection_set")) {
            log_debug("selection_set: text-control set_base_and_extent rejected");
            return;
        }
        if (state->selection) state->selection->is_selecting = false;
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, "set_text_control_extent", view, anchor_offset, view, focus_offset);
        log_debug("selection_set: text-control anchor=%d, focus=%d", anchor_offset, focus_offset);
        return;
    }

    DomBoundary anchor = boundary_from_legacy(view, anchor_offset);
    DomBoundary focus = boundary_from_legacy(view, focus_offset);
    if (!anchor.node || !focus.node) return;

    const char* exc = NULL;
    if (!state_store_set_selection(state, &anchor, &focus, &exc)) {
        log_debug("selection_set: set_base_and_extent rejected: %s", exc ? exc : "?");
        return;
    }
    if (state->selection) {
        state->selection->is_selecting = false;
    }
    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, "set_base_and_extent", view, anchor_offset, view, focus_offset);

    log_debug("selection_set: anchor=%d, focus=%d", anchor_offset, focus_offset);
}

void state_store_legacy_selection_select_all(DocState* state) {
    if (!state) return;

    if (state->transition_depth == 0) {
        selection_transition(state, SELECTION_TRANSITION_SELECT_ALL, NULL);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection || !state->selection || !state->selection->view) return;

    SelectionState* sel = state->selection;
    if (selection_is_text_control_view(sel->view)) {
        DomElement* elem = lam::dom_require_element(sel->view);
        tc_ensure_init(elem);
        uint32_t len = 0;
        if (elem->form && elem->form->current_value) {
            len = elem->form->current_value_len;
        } else if (elem->form && elem->form->value) {
            len = (uint32_t)strlen(elem->form->value);
        }
        if (!text_control_set_selection_from_byte_offsets(state, static_cast<View*>(elem),
                0, static_cast<int>(len), "selection_select_all")) { // INT_CAST_OK: StateStore selection API uses int byte offsets.
            log_debug("selection_select_all: text-control set_base_and_extent rejected");
            return;
        }
        if (state->selection) state->selection->is_selecting = false;
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, "select_text_control_all",
            sel->anchor_view, sel->anchor_offset, sel->focus_view, sel->focus_offset);
        log_debug("selection_select_all: text-control len=%u", len);
        return;
    }

    DomNode* node = static_cast<DomNode*>(sel->view);
    const char* exc = NULL;
    DomBoundary anchor = { node, 0 };
    DomBoundary focus = { node, 0 };
    if (node->is_text()) {
        focus.offset = dom_node_boundary_length(node);
    } else {
        uint32_t child_count = 0;
        if (node->is_element()) {
            for (DomNode* child = node->as_element()->first_child; child; child = child->next_sibling) {
                child_count++;
            }
        }
        focus.offset = child_count;
    }
    if (!state_store_set_selection(state, &anchor, &focus, &exc)) {
        log_debug("selection_select_all: rejected: %s", exc ? exc : "?");
        return;
    }
    if (state->selection) {
        state->selection->is_selecting = false;
    }
    state_store_refresh_caret_projection(state);

    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, "select_all",
        state->selection ? state->selection->anchor_view : NULL,
        state->selection ? state->selection->anchor_offset : 0,
        state->selection ? state->selection->focus_view : NULL,
        state->selection ? state->selection->focus_offset : 0);

    log_debug("selection_select_all");
}

void state_store_legacy_selection_collapse(DocState* state, bool to_start) {
    if (!state) return;

    if (state->transition_depth == 0) {
        selection_transition(state,
            to_start ? SELECTION_TRANSITION_COLLAPSE_TO_START : SELECTION_TRANSITION_COLLAPSE_TO_END,
            NULL);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection || !state->selection) return;

    if (selection_is_text_control_view(state->selection->view)) {
        SelectionState* sel = state->selection;
        int pos = to_start ?
            (sel->anchor_offset < sel->focus_offset ? sel->anchor_offset : sel->focus_offset) :
            (sel->anchor_offset > sel->focus_offset ? sel->anchor_offset : sel->focus_offset);
        if (!text_control_set_selection_from_byte_offsets(state, sel->view, pos, pos,
                "selection_collapse")) {
            log_debug("selection_collapse: text-control collapse rejected");
            return;
        }
        if (state->selection) state->selection->is_selecting = false;
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, to_start ? "collapse_text_control_to_start" : "collapse_text_control_to_end",
            sel->anchor_view, sel->anchor_offset, sel->focus_view, sel->focus_offset);
        log_debug("selection_collapse: text-control to_start=%d, pos=%d", to_start, pos);
        return;
    }

    if (selection->range_count == 0) return;

    const char* exc = NULL;
    DomBoundary boundary = to_start ? selection->ranges[0]->start : selection->ranges[0]->end;
    if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
        log_debug("selection_collapse: rejected: %s", exc ? exc : "?");
        return;
    }
    if (state->selection) {
        state->selection->is_selecting = false;
    }

    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, to_start ? "collapse_to_start" : "collapse_to_end",
        state->selection ? state->selection->anchor_view : NULL,
        state->selection ? state->selection->anchor_offset : 0,
        state->selection ? state->selection->focus_view : NULL,
        state->selection ? state->selection->focus_offset : 0);

    log_debug("selection_collapse: to_start=%d", to_start);
}

void state_store_legacy_selection_clear(DocState* state) {
    if (!state) return;

    if (state->transition_depth == 0) {
        selection_transition(state, SELECTION_TRANSITION_CLEAR_SELECTION, NULL);
        return;
    }

    DomSelection* selection = sync_ensure_selection(state);
    if (!selection) return;

    View* caret_view = state->caret ? state->caret->view : NULL;
    int caret_offset = state->caret ? state->caret->char_offset : 0;
    if (selection_is_text_control_view(caret_view)) {
        if (!text_control_set_selection_from_byte_offsets(state, caret_view,
                caret_offset, caret_offset, "selection_clear")) {
            log_debug("selection_clear: text-control collapse rejected");
            return;
        }
        if (state->selection) state->selection->is_selecting = false;
        state->editing.pointer_selecting = false;
        state->editing.drag_anchor_view = NULL;
        state->editing.drag_anchor_offset = 0;
        state->editing.drag_mode = EDITING_DRAG_CHAR;
        state->selection_layout_dirty = true;
        state->needs_repaint = true;
        selection_log_transition(state, "clear_text_control_selection",
            caret_view, caret_offset, caret_view, caret_offset);
        log_debug("selection_clear: text-control");
        return;
    }

    DomBoundary boundary = boundary_from_legacy(caret_view, caret_offset);
    const char* exc = NULL;
    if (boundary.node) {
        if (!state_store_set_selection(state, &boundary, &boundary, &exc)) {
            log_debug("selection_clear: collapse rejected: %s", exc ? exc : "?");
            return;
        }
    } else {
        state_store_set_selection(state, NULL, NULL, &exc);
    }
    state_store_refresh_caret_projection(state);
    if (state->selection) {
        state->selection->is_selecting = false;
    }
    state->editing.pointer_selecting = false;
    state->editing.drag_anchor_view = NULL;
    state->editing.drag_anchor_offset = 0;
    state->editing.drag_mode = EDITING_DRAG_CHAR;

    state->selection_layout_dirty = true;
    state->needs_repaint = true;
    selection_log_transition(state, "clear_selection",
        state->selection ? state->selection->anchor_view : NULL,
        state->selection ? state->selection->anchor_offset : 0,
        state->selection ? state->selection->focus_view : NULL,
        state->selection ? state->selection->focus_offset : 0);
    log_debug("selection_clear");
}

bool selection_has(DocState* state) {
    if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        return state->sel.start_u16 != state->sel.end_u16;
    }
    if (state && state->dom_selection) {
        return state->dom_selection->range_count > 0 &&
            !dom_selection_is_collapsed(state->dom_selection);
    }
    if (!state || !state->selection) return false;
    return !state->selection->is_collapsed;
}

bool selection_is_pointer_range_active(DocState* state) {
    if (!state || !state->selection) return false;
    if (state->dom_selection && state->dom_selection->range_count > 0 &&
        dom_selection_is_collapsed(state->dom_selection)) {
        return false;
    }
    return state->selection->is_selecting && !state->selection->is_collapsed;
}

bool selection_get_pointer_anchor(DocState* state, View** out_anchor_view,
                                  int* out_anchor_offset) {
    if (out_anchor_view) *out_anchor_view = NULL;
    if (out_anchor_offset) *out_anchor_offset = 0;
    if (!state || !state->selection || !state->selection->is_selecting) return false;
    if (out_anchor_view) *out_anchor_view = state->selection->anchor_view;
    if (out_anchor_offset) *out_anchor_offset = state->selection->anchor_offset;
    return true;
}

bool selection_get_anchor_snapshot(DocState* state, View** out_anchor_view,
                                   int* out_anchor_offset,
                                   bool* out_collapsed) {
    if (out_anchor_view) *out_anchor_view = NULL;
    if (out_anchor_offset) *out_anchor_offset = 0;
    if (out_collapsed) *out_collapsed = true;
    if (!state || !state->selection) return false;

    if (out_anchor_view) *out_anchor_view = state->selection->anchor_view;
    if (out_anchor_offset) *out_anchor_offset = state->selection->anchor_offset;
    if (out_collapsed) *out_collapsed = state->selection->is_collapsed;
    return true;
}

bool selection_get_focus_snapshot(DocState* state, View** out_focus_view,
                                  int* out_focus_offset,
                                  float* out_iframe_offset_x,
                                  float* out_iframe_offset_y,
                                  bool* out_collapsed) {
    if (out_focus_view) *out_focus_view = NULL;
    if (out_focus_offset) *out_focus_offset = 0;
    if (out_iframe_offset_x) *out_iframe_offset_x = 0;
    if (out_iframe_offset_y) *out_iframe_offset_y = 0;
    if (out_collapsed) *out_collapsed = true;
    if (!state || !state->selection) return false;

    if (out_focus_view) *out_focus_view = state->selection->focus_view;
    if (out_focus_offset) *out_focus_offset = state->selection->focus_offset;
    if (out_iframe_offset_x) *out_iframe_offset_x = state->selection->iframe_offset_x;
    if (out_iframe_offset_y) *out_iframe_offset_y = state->selection->iframe_offset_y;
    if (out_collapsed) *out_collapsed = state->selection->is_collapsed;
    return true;
}

bool selection_get_focus_visual_snapshot(DocState* state, float* out_x,
                                         float* out_y, bool* out_collapsed) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_collapsed) *out_collapsed = true;
    if (!state || !state->selection) return false;
    if (out_x) *out_x = state->selection->end_x;
    if (out_y) *out_y = state->selection->end_y;
    if (out_collapsed) *out_collapsed = state->selection->is_collapsed;
    return true;
}

bool selection_get_iframe_offset(DocState* state, float* out_x, float* out_y) {
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (!state || !state->selection) return false;
    if (out_x) *out_x = state->selection->iframe_offset_x;
    if (out_y) *out_y = state->selection->iframe_offset_y;
    return true;
}

bool selection_get_anchor_range(DocState* state, View* anchor_view,
                                int* out_start, int* out_end) {
    if (out_start) *out_start = 0;
    if (out_end) *out_end = 0;
    if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL && anchor_view &&
        anchor_view->is_element() &&
        state->sel.control == lam::dom_require_element(anchor_view)) {
        selection_get_range(state, out_start, out_end);
        return state->sel.start_u16 != state->sel.end_u16;
    }
    if (state && state->dom_selection && state->dom_selection->range_count > 0 &&
        !dom_selection_is_collapsed(state->dom_selection) && anchor_view) {
        View* anchor_projection = NULL;
        DomBoundary anchor = dom_selection_anchor_boundary(state->dom_selection);
        dom_boundary_to_legacy_projection(anchor, &anchor_projection, NULL);
        if (anchor_projection != anchor_view) return false;
        selection_get_range(state, out_start, out_end);
        return true;
    }
    if (!state || !state->selection || !anchor_view || state->selection->is_collapsed ||
        state->selection->anchor_view != anchor_view) {
        return false;
    }
    selection_get_range(state, out_start, out_end);
    return true;
}

bool selection_get_debug_snapshot(DocState* state, View** out_view,
                                  bool* out_collapsed, bool* out_selecting,
                                  int* out_anchor_offset, int* out_anchor_line,
                                  int* out_focus_offset, int* out_focus_line,
                                  float* out_start_x, float* out_start_y,
                                  float* out_end_x, float* out_end_y) {
    if (out_view) *out_view = NULL;
    if (out_collapsed) *out_collapsed = true;
    if (out_selecting) *out_selecting = false;
    if (out_anchor_offset) *out_anchor_offset = 0;
    if (out_anchor_line) *out_anchor_line = 0;
    if (out_focus_offset) *out_focus_offset = 0;
    if (out_focus_line) *out_focus_line = 0;
    if (out_start_x) *out_start_x = 0;
    if (out_start_y) *out_start_y = 0;
    if (out_end_x) *out_end_x = 0;
    if (out_end_y) *out_end_y = 0;
    if (!state || !state->selection) return false;

    SelectionState* sel = state->selection;
    if (out_view) *out_view = sel->view;
    if (out_collapsed) *out_collapsed = sel->is_collapsed;
    if (out_selecting) *out_selecting = sel->is_selecting;
    if (out_anchor_offset) *out_anchor_offset = sel->anchor_offset;
    if (out_anchor_line) *out_anchor_line = sel->anchor_line;
    if (out_focus_offset) *out_focus_offset = sel->focus_offset;
    if (out_focus_line) *out_focus_line = sel->focus_line;
    if (out_start_x) *out_start_x = sel->start_x;
    if (out_start_y) *out_start_y = sel->start_y;
    if (out_end_x) *out_end_x = sel->end_x;
    if (out_end_y) *out_end_y = sel->end_y;
    return true;
}

bool selection_get_extent_views(DocState* state, View** out_anchor_view,
                                View** out_focus_view) {
    if (out_anchor_view) *out_anchor_view = NULL;
    if (out_focus_view) *out_focus_view = NULL;
    if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
        state->sel.start_u16 != state->sel.end_u16 && state->sel.control) {
        View* view = static_cast<View*>(state->sel.control);
        if (out_anchor_view) *out_anchor_view = view;
        if (out_focus_view) *out_focus_view = view;
        return true;
    }
    if (state && state->dom_selection && state->dom_selection->range_count > 0 &&
        !dom_selection_is_collapsed(state->dom_selection)) {
        DomBoundary anchor = dom_selection_anchor_boundary(state->dom_selection);
        DomBoundary focus = dom_selection_focus_boundary(state->dom_selection);
        dom_boundary_to_legacy_projection(anchor, out_anchor_view, NULL);
        dom_boundary_to_legacy_projection(focus, out_focus_view, NULL);
        return (!out_anchor_view || *out_anchor_view) &&
               (!out_focus_view || *out_focus_view);
    }
    if (!state || !state->selection || state->selection->is_collapsed) return false;
    if (out_anchor_view) *out_anchor_view = state->selection->anchor_view;
    if (out_focus_view) *out_focus_view = state->selection->focus_view;
    return state->selection->anchor_view && state->selection->focus_view;
}

bool selection_has_projection(DocState* state) {
    return state && state->selection != NULL;
}

void selection_get_range(DocState* state, int* start, int* end) {
    if (!state || !state->selection || !start || !end) return;

    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL && state->sel.control) {
        DomElement* control = state->sel.control;
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        if (!form) return;
        uint32_t start_byte = text_control_u16_offset_to_byte(form, state->sel.start_u16);
        uint32_t end_byte = text_control_u16_offset_to_byte(form, state->sel.end_u16);
        *start = static_cast<int>(start_byte); // INT_CAST_OK: StateStore selection API uses int byte offsets.
        *end = static_cast<int>(end_byte); // INT_CAST_OK: StateStore selection API uses int byte offsets.
        return;
    }

    if (state->dom_selection && state->dom_selection->range_count > 0) {
        int anchor_offset = 0;
        int focus_offset = 0;
        DomBoundary anchor = dom_selection_anchor_boundary(state->dom_selection);
        DomBoundary focus = dom_selection_focus_boundary(state->dom_selection);
        dom_boundary_to_legacy_projection(anchor, NULL, &anchor_offset);
        dom_boundary_to_legacy_projection(focus, NULL, &focus_offset);
        DomBoundaryOrder order = dom_boundary_compare(&anchor, &focus);
        if (order == DOM_BOUNDARY_AFTER) {
            *start = focus_offset;
            *end = anchor_offset;
        } else {
            *start = anchor_offset;
            *end = focus_offset;
        }
        return;
    }

    SelectionState* sel = state->selection;
    if (sel->anchor_offset <= sel->focus_offset) {
        *start = sel->anchor_offset;
        *end = sel->focus_offset;
    } else {
        *start = sel->focus_offset;
        *end = sel->anchor_offset;
    }
}

// ============================================================================
// Focus API
// ============================================================================

static void focus_set_pseudo(DocState* state, View* view,
                             const char* state_name, uint32_t pseudo_state,
                             bool set) {
    (void)pseudo_state;
    if (!state || !view || !state_name) return;

    state_set_bool(state, view, state_name, set);
}

static void focus_set_within_chain(DocState* state, View* view, bool set) {
    View* node = view;
    while (node) {
        focus_set_pseudo(state, node, STATE_FOCUS_WITHIN,
                         PSEUDO_STATE_FOCUS_WITHIN, set);
        node = static_cast<View*>(node->parent);
    }
}

static void focus_write_optional_ref(JsonWriter* w, const char* key, View* view) {
    event_state_log_write_node_ref(w, key, (const DomNode*)view);
}

static void focus_write_editing_surface_ref(JsonWriter* w,
                                            const char* key,
                                            View* view) {
    jw_key(w, key ? key : "surface");
    jw_obj_begin(w);
        EditingSurface surface;
        EditingSurface* surface_ptr = nullptr;
        if (editing_surface_from_target(view, &surface)) {
            surface_ptr = &surface;
        }
        jw_kv_str(w, "kind", editing_surface_kind_name(
            surface_ptr ? surface_ptr->kind : EDIT_SURFACE_NONE));
        jw_kv_str(w, "mode", editing_mode_name(
            surface_ptr ? surface_ptr->mode : EDIT_MODE_RICH));
        event_state_log_write_node_ref(w, "owner",
            surface_ptr ? (const DomNode*)surface_ptr->owner : nullptr);
        event_state_log_write_node_ref(w, "target",
            surface_ptr ? (const DomNode*)surface_ptr->view : (const DomNode*)view);
    jw_obj_end(w);
}

static bool focus_editing_ime_active(View* view) {
    if (!view || !view->is_element()) return false;
    DomElement* elem = lam::dom_require_element(view);
    return tc_is_text_control(elem) && te_ime_is_composing(elem);
}

static void focus_log_transition(DocState* state, const char* transition,
                                 View* from, View* to,
                                 bool from_keyboard, bool focus_visible) {
    if (!state || !event_state_log_enabled(state->active_event_log)) return;

    char buf[1024];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "state.transition", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "machine", "focus");
        jw_kv_str(&w, "transition", transition ? transition : "focus_update");
        jw_kv_str(&w, "cause", from_keyboard ? "KEYBOARD_TAB" : "MOUSE");
        focus_write_optional_ref(&w, "from", from);
        focus_write_optional_ref(&w, "to", to);
        jw_kv_bool(&w, "from_keyboard", from_keyboard);
        jw_kv_bool(&w, "focus_visible", focus_visible);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);

    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "editing.focus", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "operation", transition ? transition : "focus_update");
        focus_write_editing_surface_ref(&w, "from", from);
        focus_write_editing_surface_ref(&w, "to", to);
        jw_kv_bool(&w, "from_keyboard", from_keyboard);
        jw_kv_bool(&w, "focus_visible", focus_visible);
        jw_kv_bool(&w, "ime_active",
                   focus_editing_ime_active(from) ||
                   focus_editing_ime_active(to));
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void focus_sync_text_control_state(DocState* state, View* view) {
    if (!state) return;

    if (view && view->is_element() && tc_is_text_control(lam::dom_require_element(view))) {
        DomElement* elem = lam::dom_require_element(view);
        tc_ensure_init(elem);
        FormControlProp* form = elem->form;
        tc_set_active_element(state, elem);
        tc_set_last_focused_text_control(state, elem);
        if (!form) return;

        state_store_set_text_control_selection(state, elem,
            form->selection_start, form->selection_end, form->selection_direction);
        return;
    }

    if (tc_get_active_element(state)) tc_set_active_element(state, NULL);
    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        state_store_refresh_caret_projection(state);
    } else if (state->caret && selection_is_text_control_view(state->caret->view)) {
        state->caret->visible = false;
        state->caret->blink_time = 0;
        state->needs_repaint = true;
    }
}

void focus_set(DocState* state, View* view, bool from_keyboard) {
    if (!state) return;
    if (view && !is_view_focusable(view)) return;

    if (state->transition_depth == 0) {
        FocusTransitionArgs args = { .target = view, .from_keyboard = from_keyboard };
        focus_transition(state, FOCUS_TRANSITION_FOCUS_ELEMENT, &args);
        return;
    }

    // Allocate focus state if needed
    if (!state->focus) {
        state->focus = (FocusState*)arena_alloc(state->arena, sizeof(FocusState));
        if (!state->focus) {
            log_error("focus_set: failed to allocate FocusState");
            return;
        }
        memset(state->focus, 0, sizeof(FocusState));
    }

    FocusState* focus = state->focus;
    View* old_focus = focus->current;

    if (old_focus && old_focus != view && old_focus->is_element() &&
        tc_is_text_control(lam::dom_require_element(old_focus))) {
        tc_set_active_element(state, NULL);
    }

    // Store previous focus for restoration
    focus->previous = old_focus;
    focus->current = view;
    focus->from_keyboard = from_keyboard;
    focus->from_mouse = !from_keyboard;
    focus->focus_visible = from_keyboard;  // :focus-visible only for keyboard

    // Update :focus pseudo-state on old element
    if (old_focus && old_focus != view) {
        focus_set_pseudo(state, old_focus, STATE_FOCUS, PSEUDO_STATE_FOCUS, false);
        focus_set_pseudo(state, old_focus, STATE_FOCUS_VISIBLE,
                         PSEUDO_STATE_FOCUS_VISIBLE, false);
        focus_set_within_chain(state, old_focus, false);
    }

    // Update :focus pseudo-state on new element
    if (view) {
        focus_set_pseudo(state, view, STATE_FOCUS, PSEUDO_STATE_FOCUS, true);
        focus_set_pseudo(state, view, STATE_FOCUS_VISIBLE,
                         PSEUDO_STATE_FOCUS_VISIBLE, from_keyboard);
        focus_set_within_chain(state, view, true);
    }

    if (old_focus != view) {
        focus_sync_text_control_state(state, view);
    }

    state->needs_repaint = true;

    focus_log_transition(state,
        old_focus == view ? "focus_update" : (view ? "focus_element" : "blur_current"),
        old_focus, view, from_keyboard, focus->focus_visible);

    log_debug("focus_set: view=%p, from_keyboard=%d", view, from_keyboard);
}

static void focus_clear_internal(DocState* state, bool preserve_selection) {
    FocusState* focus = state->focus;

    // Clear pseudo-states on current element
    if (focus->current) {
        focus_set_pseudo(state, focus->current, STATE_FOCUS, PSEUDO_STATE_FOCUS, false);
        focus_set_pseudo(state, focus->current, STATE_FOCUS_VISIBLE,
                         PSEUDO_STATE_FOCUS_VISIBLE, false);
        focus_set_within_chain(state, focus->current, false);
    }

    View* old_focus = focus->current;
    if (old_focus && old_focus->is_element() &&
        tc_is_text_control(lam::dom_require_element(old_focus))) {
        tc_set_active_element(state, NULL);
    }
    focus->previous = focus->current;
    focus->current = NULL;
    focus->from_keyboard = false;
    focus->from_mouse = false;
    focus->focus_visible = false;

    bool preserve_rich_editing =
        state->editing.has_active_surface &&
        editing_surface_is_rich(&state->editing.active_surface);
    bool preserve_text_control_selection = state->sel.kind == EDIT_SEL_TEXT_CONTROL;
    if (!preserve_selection && !preserve_rich_editing && !preserve_text_control_selection) {
        // Also clear caret and selection
        state_store_legacy_caret_clear(state);
        state_store_legacy_selection_clear(state);
    } else if (preserve_text_control_selection) {
        state_store_refresh_caret_projection(state);
    }

    state->needs_repaint = true;

    focus_log_transition(state, "blur_current", old_focus, NULL, false, false);

    log_debug("focus_clear");
}

void focus_clear(DocState* state) {
    if (!state || !state->focus) return;

    if (state->transition_depth == 0) {
        focus_transition(state, FOCUS_TRANSITION_BLUR_CURRENT, NULL);
        return;
    }

    focus_clear_internal(state, false);
}

void focus_clear_preserve_selection(DocState* state) {
    if (!state || !state->focus) return;

    // Clearing focus touches several flags (:focus, :focus-visible, and the
    // :focus-within ancestor chain) across multiple mutations that are only
    // mutually consistent once the whole operation completes. The per-mutation
    // interaction invariant ties every flag to focus->current, so the
    // intermediate states are necessarily invalid. Bracket the clear as a
    // transition (mirroring focus_clear → focus_transition) to suppress the
    // mid-operation assert and validate only the final, consistent state.
    bool own_transition = (state->transition_depth == 0);
    if (own_transition) state->transition_depth++;
    focus_clear_internal(state, true);
    if (own_transition) {
        state->transition_depth--;
        state_assert_after_mutation(state, "focus_clear_preserve_selection");
    }
}

// collect focusable elements from view tree in DOM order
static void collect_focusable(View* view, ArrayList* list) {
    if (!view) return;
    if (is_view_focusable(view)) {
        arraylist_append(list, view);
    }
    // recurse into children (only element nodes have children)
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        DomNode* child = elem->first_child;
        while (child) {
            collect_focusable(static_cast<View*>(child), list);
            child = child->next_sibling;
        }
    }
}

bool focus_move(DocState* state, View* root, bool forward) {
    if (!state || !root) return false;

    if (state->transition_depth == 0) {
        FocusTransitionArgs args = {
            .target = NULL,
            .from_keyboard = true,
            .root = root,
            .forward = forward,
        };
        return focus_transition(state, FOCUS_TRANSITION_MOVE, &args);
    }

    // build list of focusable elements in DOM order
    ArrayList* focusable = arraylist_new(32);
    collect_focusable(root, focusable);

    if (focusable->length == 0) {
        arraylist_free(focusable);
        log_debug("focus_move: no focusable elements found");
        return false;
    }

    // find current focus position in the list
    View* current = state->focus ? state->focus->current : NULL;
    int current_idx = -1;
    for (int i = 0; i < focusable->length; i++) {
        if (static_cast<View*>(focusable->data[i]) == current) {
            current_idx = i;
            break;
        }
    }

    // compute next index
    int next_idx;
    if (current_idx < 0) {
        // no current focus — go to first or last
        next_idx = forward ? 0 : focusable->length - 1;
    } else if (forward) {
        next_idx = (current_idx + 1) % focusable->length;
    } else {
        next_idx = (current_idx - 1 + focusable->length) % focusable->length;
    }

    View* next_view = static_cast<View*>(focusable->data[next_idx]);
    focus_set(state, next_view, true);  // from_keyboard=true

    log_debug("focus_move: %s to index %d/%d", forward ? "forward" : "backward",
              next_idx, focusable->length);

    arraylist_free(focusable);
    return true;
}

bool focus_restore(DocState* state) {
    if (!state || !state->focus || !state->focus->previous) return false;

    View* prev = state->focus->previous;
    focus_set(state, prev, false);

    log_debug("focus_restore: view=%p", prev);
    return true;
}

View* focus_get(DocState* state) {
    if (!state || !state->focus) return NULL;
    return state->focus->current;
}

bool focus_has_current(DocState* state) {
    return focus_get(state) != NULL;
}

View* focus_get_visible(DocState* state) {
    if (!state || !state->focus || !state->focus->focus_visible) return NULL;
    return state->focus->current;
}

bool focus_within(DocState* state, View* view) {
    if (!state || !state->focus || !view) return false;

    View* focused = state->focus->current;
    if (!focused) return false;

    // Check if focused element is view or descendant
    View* node = focused;
    while (node) {
        if (node == view) return true;
        node = static_cast<View*>(node->parent);
    }

    return false;
}

// ============================================================================
// Text Extraction and Clipboard Operations
// ============================================================================

#include "../lib/strbuf.h"
#include <GLFW/glfw3.h>

/**
 * Helper: recursively extract text from view tree
 */
static void extract_text_recursive(View* view, StrBuf* sb) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require_text(view);
        const char* text_data = (const char*)text->text_data();
        if (text_data) {
            // Extract text from all TextRects
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->length > 0) {
                    strbuf_append_str_n(sb, text_data + rect->start_index, rect->length);
                }
                rect = rect->next;
            }
        }
    }

    // Recurse into children
    View* child = view->is_element() ? (lam::view_require_element(view))->first_child : nullptr;
    while (child) {
        extract_text_recursive(child, sb);

        // Add space or newline between block-level elements
        if (child->is_block()) {
            strbuf_append_char(sb, '\n');
        }

        child = child->next_sibling;
    }
}

char* extract_text_from_view(View* view, Arena* arena) {
    if (!view || !arena) return NULL;

    StrBuf* sb = strbuf_new();
    if (!sb) return NULL;

    extract_text_recursive(view, sb);

    if (sb->length == 0) {
        strbuf_free(sb);
        return NULL;
    }

    // Copy to arena
    char* result = (char*)arena_alloc(arena, sb->length + 1);
    if (result) {
        memcpy(result, sb->str, sb->length);
        result[sb->length] = '\0';
    }

    strbuf_free(sb);
    return result;
}

static char* arena_copy_cstr(Arena* arena, const char* text) {
    if (!arena || !text) return NULL;
    size_t len = strlen(text);
    char* result = (char*)arena_alloc(arena, len + 1);
    if (!result) return NULL;
    memcpy(result, text, len);
    result[len] = '\0';
    return result;
}

static void append_html_escaped(StrBuf* sb, const char* text, size_t len) {
    if (!sb || !text) return;
    const char* p = text;
    const char* end = text + len;
    while (p < end) {
        char c = *p++;
        switch (c) {
            case '<': strbuf_append_str(sb, "&lt;"); break;
            case '>': strbuf_append_str(sb, "&gt;"); break;
            case '&': strbuf_append_str(sb, "&amp;"); break;
            case '"': strbuf_append_str(sb, "&quot;"); break;
            default: strbuf_append_char(sb, c); break;
        }
    }
}

static void append_html_attr_escaped(StrBuf* sb, const char* text) {
    if (!text) return;
    append_html_escaped(sb, text, strlen(text));
}

static bool clipboard_inline_tag(const char* tag) {
    if (!tag) return false;
    return strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0 ||
           strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0 ||
           strcmp(tag, "u") == 0 || strcmp(tag, "code") == 0 ||
           strcmp(tag, "a") == 0;
}

static void append_open_tag_for_clipboard(StrBuf* sb, DomElement* element) {
    if (!sb || !element || !element->tag_name) return;
    strbuf_append_char(sb, '<');
    strbuf_append_str(sb, element->tag_name);
    if (strcmp(element->tag_name, "a") == 0) {
        const char* href = (static_cast<DomNode*>(element))->get_attribute("href");
        const char* title = (static_cast<DomNode*>(element))->get_attribute("title");
        if (href) {
            strbuf_append_str(sb, " href=\"");
            append_html_attr_escaped(sb, href);
            strbuf_append_char(sb, '"');
        }
        if (title) {
            strbuf_append_str(sb, " title=\"");
            append_html_attr_escaped(sb, title);
            strbuf_append_char(sb, '"');
        }
    }
    strbuf_append_char(sb, '>');
}

static void append_close_tag_for_clipboard(StrBuf* sb, DomElement* element) {
    if (!sb || !element || !element->tag_name) return;
    strbuf_append_str(sb, "</");
    strbuf_append_str(sb, element->tag_name);
    strbuf_append_char(sb, '>');
}

static DomText* first_text_descendant_for_clipboard(DomNode* node) {
    if (!node) return NULL;
    if (node->is_text()) return node->as_text();
    if (node->is_element()) {
        for (DomNode* child = node->as_element()->first_child; child; child = child->next_sibling) {
            DomText* hit = first_text_descendant_for_clipboard(child);
            if (hit) return hit;
        }
    }
    return NULL;
}

static DomText* next_text_after_for_clipboard(DomNode* node) {
    for (DomNode* n = node; n; n = n->parent) {
        for (DomNode* sibling = n->next_sibling; sibling; sibling = sibling->next_sibling) {
            DomText* hit = first_text_descendant_for_clipboard(sibling);
            if (hit) return hit;
        }
    }
    return NULL;
}

static DomNode* child_at_dom_offset(DomElement* element, uint32_t offset) {
    if (!element) return NULL;
    DomNode* child = element->first_child;
    for (uint32_t i = 0; child && i < offset; i++) child = child->next_sibling;
    return child;
}

static DomText* first_text_in_range_for_clipboard(const DomRange* range) {
    if (!range || !range->start.node) return NULL;
    DomNode* start = range->start.node;
    if (start->is_text()) return start->as_text();
    if (start->is_element()) {
        DomNode* child = child_at_dom_offset(start->as_element(), range->start.offset);
        if (child) {
            DomText* hit = first_text_descendant_for_clipboard(child);
            if (hit) return hit;
            return next_text_after_for_clipboard(child);
        }
    }
    return next_text_after_for_clipboard(start);
}

static bool boundary_before_or_equal(const DomBoundary* a, const DomBoundary* b) {
    DomBoundaryOrder order = dom_boundary_compare(a, b);
    return order == DOM_BOUNDARY_BEFORE || order == DOM_BOUNDARY_EQUAL;
}

static void append_selected_text_html(StrBuf* sb, DomText* text,
                                      uint32_t start_u16, uint32_t end_u16) {
    if (!sb || !text || start_u16 >= end_u16) return;
    enum { MAX_INLINE_ANCESTORS = 32 };
    DomElement* wrappers[MAX_INLINE_ANCESTORS];
    int wrapper_count = 0;
    for (DomNode* n = text->parent; n && wrapper_count < MAX_INLINE_ANCESTORS; n = n->parent) {
        if (n->is_element()) {
            DomElement* element = n->as_element();
            if (clipboard_inline_tag(element->tag_name)) wrappers[wrapper_count++] = element;
        }
    }
    for (int i = wrapper_count - 1; i >= 0; i--) append_open_tag_for_clipboard(sb, wrappers[i]);

    uint32_t start_u8 = dom_text_utf16_to_utf8(text, start_u16);
    uint32_t end_u8 = dom_text_utf16_to_utf8(text, end_u16);
    if (end_u8 > start_u8 && end_u8 <= text->length) {
        append_html_escaped(sb, text->text + start_u8, end_u8 - start_u8);
    }

    for (int i = 0; i < wrapper_count; i++) append_close_tag_for_clipboard(sb, wrappers[i]);
}

static void append_selected_text_plain(StrBuf* sb, DomText* text,
                                       uint32_t start_u16, uint32_t end_u16) {
    if (!sb || !text || start_u16 >= end_u16) return;
    uint32_t start_u8 = dom_text_utf16_to_utf8(text, start_u16);
    uint32_t end_u8 = dom_text_utf16_to_utf8(text, end_u16);
    if (end_u8 > start_u8 && end_u8 <= text->length) {
        strbuf_append_str_n(sb, text->text + start_u8, end_u8 - start_u8);
    }
}

static char* extract_dom_range_text_to_arena(DomRange* range, Arena* arena) {
    if (!range || !arena) return NULL;
    StrBuf* sb = strbuf_new_cap(256);
    if (!sb) return NULL;
    DomText* text = first_text_in_range_for_clipboard(range);
    while (text) {
        DomBoundary text_start{ static_cast<DomNode*>(text), 0 };
        DomBoundary text_end{ static_cast<DomNode*>(text), dom_text_utf16_length(text) };
        if (!boundary_before_or_equal(&text_start, &range->end)) break;

        DomBoundary slice_start = text_start;
        DomBoundary slice_end = text_end;
        if (dom_boundary_compare(&slice_start, &range->start) == DOM_BOUNDARY_BEFORE) slice_start = range->start;
        if (dom_boundary_compare(&slice_end, &range->end) == DOM_BOUNDARY_AFTER) slice_end = range->end;
        if (slice_start.node == static_cast<DomNode*>(text) && slice_end.node == static_cast<DomNode*>(text) &&
            slice_start.offset < slice_end.offset) {
            append_selected_text_plain(sb, text, slice_start.offset, slice_end.offset);
        }
        if (!boundary_before_or_equal(&text_end, &range->end) || text_end.node == range->end.node) break;
        text = next_text_after_for_clipboard(static_cast<DomNode*>(text));
    }
    char* result = sb->length > 0 ? arena_copy_cstr(arena, sb->str) : NULL;
    strbuf_free(sb);
    return result;
}

/**
 * Helper: recursively extract HTML from view tree
 */
static void extract_html_recursive(View* view, StrBuf* sb) {
    if (!view) return;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require_text(view);
        const char* text_data = (const char*)text->text_data();
        if (text_data) {
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->length > 0) {
                    // HTML-escape text content
                    const char* p = text_data + rect->start_index;
                    const char* end = p + rect->length;
                    while (p < end) {
                        char c = *p++;
                        switch (c) {
                            case '<': strbuf_append_str(sb, "&lt;"); break;
                            case '>': strbuf_append_str(sb, "&gt;"); break;
                            case '&': strbuf_append_str(sb, "&amp;"); break;
                            case '"': strbuf_append_str(sb, "&quot;"); break;
                            default: strbuf_append_char(sb, c); break;
                        }
                    }
                }
                rect = rect->next;
            }
        }
    } else if (view->is_element()) {
        ViewElement* element = lam::view_require_element(view);

        // Opening tag
        const char* tag_name = element->tag_name;
        if (tag_name) {
            strbuf_append_char(sb, '<');
            strbuf_append_str(sb, tag_name);
            // TODO: add attributes if needed
            strbuf_append_char(sb, '>');
        }

        // Recurse into children
        View* child = element->first_child;
        while (child) {
            extract_html_recursive(child, sb);
            child = child->next_sibling;
        }

        // Closing tag
        if (tag_name) {
            strbuf_append_str(sb, "</");
            strbuf_append_str(sb, tag_name);
            strbuf_append_char(sb, '>');
        }
    }
}

char* extract_html_from_view(View* view, Arena* arena) {
    if (!view || !arena) return NULL;

    StrBuf* sb = strbuf_new_cap(4096);
    if (!sb) return NULL;

    extract_html_recursive(view, sb);

    if (sb->length == 0) {
        strbuf_free(sb);
        return NULL;
    }

    // Copy to arena
    char* result = (char*)arena_alloc(arena, sb->length + 1);
    if (result) {
        memcpy(result, sb->str, sb->length);
        result[sb->length] = '\0';
    }

    strbuf_free(sb);
    return result;
}

static char* extract_dom_range_html_to_arena(DomRange* range, Arena* arena) {
    if (!range || !arena) return NULL;
    StrBuf* sb = strbuf_new_cap(256);
    if (!sb) return NULL;
    DomText* text = first_text_in_range_for_clipboard(range);
    while (text) {
        DomBoundary text_start{ static_cast<DomNode*>(text), 0 };
        DomBoundary text_end{ static_cast<DomNode*>(text), dom_text_utf16_length(text) };
        if (!boundary_before_or_equal(&text_start, &range->end)) break;

        DomBoundary slice_start = text_start;
        DomBoundary slice_end = text_end;
        if (dom_boundary_compare(&slice_start, &range->start) == DOM_BOUNDARY_BEFORE) slice_start = range->start;
        if (dom_boundary_compare(&slice_end, &range->end) == DOM_BOUNDARY_AFTER) slice_end = range->end;
        if (slice_start.node == static_cast<DomNode*>(text) && slice_end.node == static_cast<DomNode*>(text) &&
            slice_start.offset < slice_end.offset) {
            append_selected_text_html(sb, text, slice_start.offset, slice_end.offset);
        }
        if (!boundary_before_or_equal(&text_end, &range->end) || text_end.node == range->end.node) break;
        text = next_text_after_for_clipboard(static_cast<DomNode*>(text));
    }
    char* result = sb->length > 0 ? arena_copy_cstr(arena, sb->str) : NULL;
    strbuf_free(sb);
    return result;
}

static char* extract_text_control_selection_to_arena(DocState* state,
                                                     Arena* arena) {
    if (!state || !arena || state->sel.kind != EDIT_SEL_TEXT_CONTROL ||
        !state->sel.control) {
        return NULL;
    }
    DomElement* control = state->sel.control;
    if (!tc_is_text_control(control)) return NULL;
    tc_ensure_init(control);
    FormControlProp* form = control->form;
    if (!form) return NULL;

    uint32_t start_u16 = state->sel.start_u16;
    uint32_t end_u16 = state->sel.end_u16;
    if (start_u16 > form->current_value_u16_len) start_u16 = form->current_value_u16_len;
    if (end_u16 > form->current_value_u16_len) end_u16 = form->current_value_u16_len;
    if (end_u16 <= start_u16) return NULL;

    const char* value = form->current_value ? form->current_value :
        (form->value ? form->value : "");
    uint32_t value_len = form->current_value ? form->current_value_len :
        (uint32_t)strlen(value);
    uint32_t start_byte = tc_utf16_to_utf8_offset(value, value_len, start_u16);
    uint32_t end_byte = tc_utf16_to_utf8_offset(value, value_len, end_u16);
    if (end_byte <= start_byte || end_byte > value_len) return NULL;

    uint32_t len = end_byte - start_byte;
    char* result = (char*)arena_alloc(arena, len + 1);
    if (!result) return NULL;
    memcpy(result, value + start_byte, len);
    result[len] = '\0';
    return result;
}

char* state_store_extract_selection_text(DocState* state, Arena* arena) {
    if (!state || !arena) return NULL;

    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        return extract_text_control_selection_to_arena(state, arena);
    }

    state_store_refresh_editing_selection_shadow(state);
    if (state->sel.kind == EDIT_SEL_DOM_RANGE && state->sel.range &&
        !dom_range_collapsed(state->sel.range)) {
        return extract_dom_range_text_to_arena(state->sel.range, arena);
    }

    return NULL;
}

char* state_store_extract_selection_html(DocState* state, Arena* arena) {
    if (!state || !arena) return NULL;

    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        return NULL;
    }

    state_store_refresh_editing_selection_shadow(state);
    if (state->sel.kind == EDIT_SEL_DOM_RANGE && state->sel.range &&
        !dom_range_collapsed(state->sel.range)) {
        return extract_dom_range_html_to_arena(state->sel.range, arena);
    }

    return NULL;
}

char* extract_selected_text(DocState* state, Arena* arena) {
    return state_store_extract_selection_text(state, arena);
}

char* extract_selected_html(DocState* state, Arena* arena) {
    return state_store_extract_selection_html(state, arena);
}

// Clipboard helpers — delegate to the canonical Radiant ClipboardStore
// (see radiant/clipboard.hpp). The store mirrors writes to the OS
// pasteboard via its installed backend; for headless / test builds the
// default in-memory backend is used and writes never touch GLFW.

#include "clipboard.hpp"

void clipboard_copy_text(const char* text) {
    if (!text) return;
    clipboard_store_write_text(text);
    // Best-effort GLFW mirror so existing interactive flows keep working
    // in builds where a GLFW window is present. Skip in headless mode to
    // avoid cross-process races on the shared OS pasteboard during tests.
    extern UiContext ui_context;
    if (ui_context.window && !ui_context.headless) {
        glfwSetClipboardString(ui_context.window, text);
    }
    log_info("Copied %zu bytes to clipboard", strlen(text));
}

const char* clipboard_get_text() {
    extern UiContext ui_context;
    if (ui_context.window && !ui_context.headless) {
        const char* s = glfwGetClipboardString(ui_context.window);
        if (s) return s;
    }
    return clipboard_store_read_text();
}

void clipboard_copy_html(const char* html) {
    if (!html) return;
    // Write both representations so paste handlers that ask for text/html
    // get rich content and plain-text consumers still see something useful.
    clipboard_store_write_html(html, html);
    extern UiContext ui_context;
    if (ui_context.window && !ui_context.headless) {
        glfwSetClipboardString(ui_context.window, html);
    }
    log_debug("clipboard_copy_html: wrote text/html (%zu bytes) + text/plain mirror", strlen(html));
}

void clipboard_copy_rich(const char* html, const char* plain_text) {
    if (!html && !plain_text) return;
    const char* text = plain_text ? plain_text : "";
    if (html && html[0]) {
        clipboard_store_write_html(html, text);
    } else {
        clipboard_store_write_text(text);
    }
    extern UiContext ui_context;
    if (ui_context.window && !ui_context.headless) {
        glfwSetClipboardString(ui_context.window, text);
    }
    log_debug("clipboard_copy_rich: wrote html=%zu plain=%zu",
              html ? strlen(html) : 0, strlen(text));
}

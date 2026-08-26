#include "node_trace_events.hpp"
#include "../../jube/jube_registry.h"
#include "../../jube/jube_interface.h"
#include "../../../lib/log.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/uv_loop.h"

#include <cstring>
#include <stdio.h>

static const JubeHostAPI* node_trace_host = NULL;

static Item node_trace_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_trace_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static NodeTraceState* node_trace_state(void) {
    void* session = jube_node_runtime_current_session();
    return session ? jube_node_trace_state(session) : NULL;
}

static void node_trace_copy_cstr(char* dst, int dst_size, const char* src, int src_len) {
    if (!dst || dst_size <= 0) return;
    if (!src) src = "";
    if (src_len < 0) src_len = (int)strlen(src);
    if (src_len >= dst_size) src_len = dst_size - 1;
    memcpy(dst, src, (size_t)src_len);
    dst[src_len] = '\0';
}

static bool node_trace_copy_value_string(Item value, char* out, int out_size,
                                         int* out_length) {
    if (out_length) *out_length = 0;
    if (!node_trace_host || !node_trace_host->value || !out || out_size <= 0 ||
            node_trace_host->value->kind(value) != JUBE_VALUE_STRING ||
            !node_trace_host->value->string_length ||
            !node_trace_host->value->string_bytes) {
        return false;
    }
    size_t source_length = node_trace_host->value->string_length(value);
    const uint8_t* source = node_trace_host->value->string_bytes(value);
    if (!source && source_length > 0) return false;
    int copied = source_length >= (size_t)out_size ? out_size - 1 : (int)source_length;
    if (copied > 0) memcpy(out, source, (size_t)copied);
    out[copied] = '\0';
    if (out_length) *out_length = copied;
    return true;
}

static bool node_trace_category_matches(const char* enabled, const char* category) {
    if (!enabled || !enabled[0] || !category || !category[0]) return false;
    if (strcmp(enabled, category) == 0) return true;
    if (strcmp(enabled, "node") == 0 && strncmp(category, "node.", 5) == 0) return true;
    return strcmp(enabled, "*") == 0;
}

static int node_trace_find_category(NodeTraceState* state, const char* name) {
    if (!state || !name) return -1;
    for (int i = 0; i < state->category_count; i++) {
        if (strcmp(state->categories[i].name, name) == 0) return i;
    }
    return -1;
}

static void node_trace_add_category(NodeTraceState* state, const char* name,
                                    bool from_exec_argv) {
    if (!state || !name || !name[0]) return;
    int idx = node_trace_find_category(state, name);
    if (idx >= 0) {
        state->categories[idx].refs++;
        state->categories[idx].from_exec_argv =
            state->categories[idx].from_exec_argv || from_exec_argv;
        return;
    }
    if (state->category_count >= NODE_TRACE_MAX_CATEGORIES) return;
    NodeTraceCategory* category = &state->categories[state->category_count++];
    node_trace_copy_cstr(category->name, (int)sizeof(category->name), name, -1);
    category->refs = 1;
    category->from_exec_argv = from_exec_argv;
}

static void node_trace_remove_category(NodeTraceState* state, const char* name) {
    if (!state) return;
    int idx = node_trace_find_category(state, name);
    if (idx < 0 || state->categories[idx].from_exec_argv) return;
    if (state->categories[idx].refs > 0) state->categories[idx].refs--;
    if (state->categories[idx].refs > 0) return;
    for (int i = idx; i + 1 < state->category_count; i++) {
        state->categories[i] = state->categories[i + 1];
    }
    state->category_count--;
}

static void node_trace_add_categories_from_chars(NodeTraceState* state, const char* chars,
                                                 int len, bool from_exec_argv) {
    if (!state || !chars || len <= 0) return;
    int start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || chars[i] == ',') {
            int end = i;
            while (start < end && (chars[start] == ' ' || chars[start] == '\t')) start++;
            while (end > start && (chars[end - 1] == ' ' || chars[end - 1] == '\t')) end--;
            if (end > start) {
                char name[64];
                node_trace_copy_cstr(name, (int)sizeof(name), chars + start, end - start);
                node_trace_add_category(state, name, from_exec_argv);
            }
            start = i + 1;
        }
    }
}

static void node_trace_init_from_exec_argv(NodeTraceState* state) {
    if (!state || state->initialized) return;
    state->initialized = true;
    if (!node_trace_host || !node_trace_host->node || !node_trace_host->node->runtime ||
            !node_trace_host->node->runtime->session_process || !node_trace_host->value ||
            !node_trace_host->value->property_get || !node_trace_host->value->array_length ||
            !node_trace_host->value->array_get || !node_trace_host->value->string_length ||
            !node_trace_host->value->string_bytes || !node_trace_host->value->kind ||
            !node_trace_host->value->string_from_utf8_n || !node_trace_host->node->roots ||
            !node_trace_host->node->roots->root_frame_begin ||
            !node_trace_host->node->roots->root_frame_take_slot ||
            !node_trace_host->node->roots->root_frame_end) {
        return;
    }
    void* session = jube_node_runtime_current_session();
    if (!session) return;
    JubeRootFrame frame = {};
    if (!node_trace_host->node->roots->root_frame_begin(&frame, 3)) return;
    uint64_t* process_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* argv_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    if (!process_root || !key_root || !argv_root) {
        node_trace_host->node->roots->root_frame_end(&frame);
        return;
    }
    *process_root = node_trace_host->node->runtime->session_process(session).item;
    *key_root = node_trace_host->value->string_from_utf8_n("execArgv", 8).item;
    *argv_root = node_trace_host->value->property_get(node_trace_root_value(process_root),
                                                       node_trace_root_value(key_root)).item;
    Item argv = node_trace_root_value(argv_root);
    if (node_trace_host->value->kind(argv) == JUBE_VALUE_ARRAY) {
        int64_t length = node_trace_host->value->array_length(argv);
        for (int64_t i = 0; i < length; i++) {
            Item arg = node_trace_host->value->array_get(argv, i);
            char text[4096];
            int text_length = 0;
            if (!node_trace_copy_value_string(arg, text, (int)sizeof(text), &text_length)) {
                continue;
            }
            const char* prefix = "--trace-event-categories=";
            int prefix_length = (int)strlen(prefix);
            if (text_length > prefix_length &&
                    memcmp(text, prefix, (size_t)prefix_length) == 0) {
                node_trace_add_categories_from_chars(state, text + prefix_length,
                    text_length - prefix_length, true);
            } else if (text_length == 24 &&
                    memcmp(text, "--trace-event-categories", 24) == 0 && i + 1 < length) {
                Item next = node_trace_host->value->array_get(argv, i + 1);
                char next_text[4096];
                int next_length = 0;
                if (node_trace_copy_value_string(next, next_text, (int)sizeof(next_text),
                                                 &next_length)) {
                    node_trace_add_categories_from_chars(state, next_text, next_length, true);
                }
                i++;
            }
        }
    }
    node_trace_host->node->roots->root_frame_end(&frame);
}

static bool node_trace_is_category_enabled_cstr(const char* category) {
    NodeTraceState* state = node_trace_state();
    if (!state) return false;
    node_trace_init_from_exec_argv(state);
    for (int i = 0; i < state->category_count; i++) {
        if (state->categories[i].refs > 0 &&
                node_trace_category_matches(state->categories[i].name, category)) {
            return true;
        }
    }
    return false;
}

static void node_trace_append_json_string(StrBuf* sb, const char* chars) {
    if (!chars) chars = "";
    strbuf_append_char(sb, '"');
    for (int i = 0; chars[i]; i++) {
        unsigned char c = (unsigned char)chars[i];
        switch (c) {
            case '"': strbuf_append_str_n(sb, "\\\"", 2); break;
            case '\\': strbuf_append_str_n(sb, "\\\\", 2); break;
            case '\n': strbuf_append_str_n(sb, "\\n", 2); break;
            case '\r': strbuf_append_str_n(sb, "\\r", 2); break;
            case '\t': strbuf_append_str_n(sb, "\\t", 2); break;
            default:
                if (c < 0x20) {
                    char escaped[8];
                    snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    strbuf_append_str_n(sb, escaped, 6);
                } else {
                    strbuf_append_char(sb, (char)c);
                }
                break;
        }
    }
    strbuf_append_char(sb, '"');
}

static Item node_trace_get_property(Item object, const char* name) {
    if (!node_trace_host || !node_trace_host->value || !node_trace_host->value->property_get ||
            !node_trace_host->value->string_from_utf8_n || !node_trace_host->node ||
            !node_trace_host->node->roots) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_trace_host->node->roots->root_frame_begin(&frame, 3)) return ItemNull;
    uint64_t* object_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* result_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !result_root) {
        node_trace_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *object_root = object.item;
    *key_root = node_trace_host->value->string_from_utf8_n(name, strlen(name)).item;
    *result_root = node_trace_host->value->property_get(node_trace_root_value(object_root),
                                                         node_trace_root_value(key_root)).item;
    Item result = node_trace_root_value(result_root);
    node_trace_host->node->roots->root_frame_end(&frame);
    return result;
}

static bool node_trace_set_property(Item object, const char* name, Item value) {
    if (!node_trace_host || !node_trace_host->value || !node_trace_host->value->property_set ||
            !node_trace_host->value->string_from_utf8_n || !node_trace_host->node ||
            !node_trace_host->node->roots) return false;
    JubeRootFrame frame = {};
    if (!node_trace_host->node->roots->root_frame_begin(&frame, 3)) return false;
    uint64_t* object_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_trace_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *object_root = object.item;
    *key_root = node_trace_host->value->string_from_utf8_n(name, strlen(name)).item;
    *value_root = value.item;
    node_trace_host->value->property_set(node_trace_root_value(object_root),
                                          node_trace_root_value(key_root),
                                          node_trace_root_value(value_root));
    node_trace_host->node->roots->root_frame_end(&frame);
    return true;
}

static Item node_trace_throw_invalid_arg(void) {
    return node_trace_host && node_trace_host->script &&
            node_trace_host->script->throw_type_error_code ?
        node_trace_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"options.categories\" property must be an array of strings.") : ItemNull;
}

static bool node_trace_value_to_category_string(Item value, char* out, int out_size) {
    int length = 0;
    return node_trace_copy_value_string(value, out, out_size, &length) && length > 0;
}

static Item node_trace_enable(void);
static Item node_trace_disable(void);
static Item node_trace_create_tracing(Item options);
static Item node_trace_get_enabled_categories(void);
static Item node_trace_is_category_enabled(Item category);
static Item node_trace_manual_trace(Item phase, Item category, Item name, Item id, Item args);

static NodeTraceState* node_trace_ensure_state(void) {
    void* session = jube_node_runtime_current_session();
    return session ? jube_node_trace_state(session) : NULL;
}

static Item node_trace_namespace_item(NodeTraceState* state) {
    return state ? (Item){.item = state->namespace_item} : ItemNull;
}

Item node_trace_events_namespace(void) {
    jube_modules_runtime_attach();
    NodeTraceState* state = node_trace_ensure_state();
    if (!state || !node_trace_host || !node_trace_host->value ||
            !node_trace_host->value->new_object || !node_trace_host->node ||
            !node_trace_host->node->roots ||
            !node_trace_host->node->roots->persistent_root_register) return ItemNull;
    if (state->namespace_item == 0) {
        Item namespace_item = node_trace_host->value->new_object();
        state->namespace_item = namespace_item.item;
        void* session = jube_node_runtime_current_session();
        if (!session || node_trace_host->node->roots->persistent_root_register(
                session, &state->namespace_item) != 0) {
            state->namespace_item = 0;
            return ItemNull;
        }
        state->namespace_rooted = true;
        Item create_tracing = jube_new_function(node_trace_host->script,
            node_trace_create_tracing, 1);
        Item get_enabled = jube_new_function(node_trace_host->script,
            node_trace_get_enabled_categories, 0);
        node_trace_set_property(node_trace_namespace_item(state), "createTracing", create_tracing);
        node_trace_set_property(node_trace_namespace_item(state), "getEnabledCategories", get_enabled);
        node_trace_set_property(node_trace_namespace_item(state), "default",
                                node_trace_namespace_item(state));
    }
    return node_trace_namespace_item(state);
}

static Item node_trace_get_enabled_categories(void) {
    NodeTraceState* state = node_trace_state();
    if (!state || !node_trace_host || !node_trace_host->value ||
            !node_trace_host->value->string_from_utf8_n) return ItemNull;
    node_trace_init_from_exec_argv(state);
    StrBuf* sb = strbuf_new();
    if (!sb) return ItemNull;
    bool first = true;
    for (int i = 0; i < state->category_count; i++) {
        if (state->categories[i].refs <= 0) continue;
        if (!first) strbuf_append_char(sb, ',');
        first = false;
        strbuf_append_str_n(sb, state->categories[i].name,
                            strlen(state->categories[i].name));
    }
    Item result = node_trace_host->value->string_from_utf8_n(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

static Item node_trace_enable(void) {
    NodeTraceState* state = node_trace_state();
    Item self = node_trace_host && node_trace_host->script && node_trace_host->script->current_this
        ? node_trace_host->script->current_this() : ItemNull;
    Item enabled = node_trace_get_property(self, "enabled");
    if (!node_trace_host || !node_trace_host->value ||
            node_trace_host->value->kind(enabled) != JUBE_VALUE_BOOLEAN ||
            !it2b(enabled)) {
        Item categories = node_trace_get_property(self, "__lambda_trace_categories__");
        char text[4096];
        int length = 0;
        if (node_trace_copy_value_string(categories, text, (int)sizeof(text), &length)) {
            node_trace_add_categories_from_chars(state, text, length, false);
        }
        node_trace_set_property(self, "enabled", (Item){.item = b2it(true)});
    }
    return self;
}

static Item node_trace_disable(void) {
    NodeTraceState* state = node_trace_state();
    Item self = node_trace_host && node_trace_host->script && node_trace_host->script->current_this
        ? node_trace_host->script->current_this() : ItemNull;
    Item enabled = node_trace_get_property(self, "enabled");
    if (node_trace_host && node_trace_host->value &&
            node_trace_host->value->kind(enabled) == JUBE_VALUE_BOOLEAN && it2b(enabled)) {
        Item categories = node_trace_get_property(self, "__lambda_trace_categories__");
        char text[4096];
        int length = 0;
        if (node_trace_copy_value_string(categories, text, (int)sizeof(text), &length)) {
            int start = 0;
            for (int i = 0; i <= length; i++) {
                if (i == length || text[i] == ',') {
                    char category[64];
                    node_trace_copy_cstr(category, (int)sizeof(category), text + start, i - start);
                    node_trace_remove_category(state, category);
                    start = i + 1;
                }
            }
        }
        node_trace_set_property(self, "enabled", (Item){.item = b2it(false)});
    }
    return self;
}

static Item node_trace_create_tracing(Item options) {
    if (!node_trace_host || !node_trace_host->value ||
            node_trace_host->value->kind(options) != JUBE_VALUE_OBJECT) {
        return node_trace_host && node_trace_host->script &&
                node_trace_host->script->throw_type_error_code ?
            node_trace_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE",
                "The \"options\" argument must be an object.") : ItemNull;
    }
    Item categories = node_trace_get_property(options, "categories");
    if (!node_trace_host->value->is_array(categories) ||
            !node_trace_host->value->array_length || !node_trace_host->value->array_get) {
        return node_trace_throw_invalid_arg();
    }
    int64_t length = node_trace_host->value->array_length(categories);
    if (length <= 0) {
        return node_trace_host->script->throw_type_error_code("ERR_TRACE_EVENTS_CATEGORY_REQUIRED",
            "At least one category is required.");
    }
    StrBuf* joined = strbuf_new();
    if (!joined) return ItemNull;
    for (int64_t i = 0; i < length; i++) {
        char category[64];
        if (!node_trace_value_to_category_string(
                node_trace_host->value->array_get(categories, i), category,
                (int)sizeof(category))) {
            strbuf_free(joined);
            return node_trace_throw_invalid_arg();
        }
        if (i > 0) strbuf_append_char(joined, ',');
        strbuf_append_str_n(joined, category, strlen(category));
    }
    Item tracing = node_trace_host->value->new_object();
    Item joined_item = node_trace_host->value->string_from_utf8_n(joined->str, joined->length);
    strbuf_free(joined);
    JubeRootFrame frame = {};
    if (!node_trace_host->node->roots->root_frame_begin(&frame, 2)) return ItemNull;
    uint64_t* tracing_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* categories_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    if (!tracing_root || !categories_root) {
        node_trace_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *tracing_root = tracing.item;
    *categories_root = joined_item.item;
    Item tracing_value = node_trace_root_value(tracing_root);
    Item categories_value = node_trace_root_value(categories_root);
    node_trace_set_property(tracing_value, "categories", categories_value);
    node_trace_set_property(tracing_value, "__lambda_trace_categories__", categories_value);
    node_trace_set_property(tracing_value, "enabled", (Item){.item = b2it(false)});
    node_trace_set_property(tracing_value, "enable",
        jube_new_function(node_trace_host->script, node_trace_enable, 0));
    node_trace_set_property(tracing_value, "disable",
        jube_new_function(node_trace_host->script, node_trace_disable, 0));
    Item result = node_trace_root_value(tracing_root);
    node_trace_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_trace_is_category_enabled(Item category) {
    char text[128];
    if (!node_trace_value_to_category_string(category, text, (int)sizeof(text))) {
        return (Item){.item = b2it(false)};
    }
    return (Item){.item = b2it(node_trace_is_category_enabled_cstr(text))};
}

static Item node_trace_manual_trace(Item phase, Item category, Item name, Item id, Item args) {
    (void)args;
    NodeTraceState* state = node_trace_state();
    char category_text[128];
    char trace_name[96];
    if (!state || !node_trace_value_to_category_string(category, category_text,
                                                        (int)sizeof(category_text)) ||
            !node_trace_is_category_enabled_cstr(category_text) ||
            !node_trace_value_to_category_string(name, trace_name, (int)sizeof(trace_name)) ||
            state->event_count >= NODE_TRACE_MAX_EVENTS) {
        return node_trace_undefined();
    }
    NodeTraceEvent* event = &state->events[state->event_count++];
    int64_t phase_number = 0;
    char phase_text[8];
    int phase_length = 0;
    if (node_trace_host->value->number_to_int64_exact &&
            node_trace_host->value->number_to_int64_exact(phase, &phase_number)) {
        event->ph = (char)phase_number;
    } else if (node_trace_copy_value_string(phase, phase_text, (int)sizeof(phase_text),
                                            &phase_length) && phase_length > 0) {
        event->ph = phase_text[0];
    } else {
        event->ph = 'i';
    }
    node_trace_copy_cstr(event->cat, (int)sizeof(event->cat), category_text, -1);
    node_trace_copy_cstr(event->name, (int)sizeof(event->name), trace_name, -1);
    event->ts = uv_hrtime() / 1000;
    int64_t id_number = 0;
    event->has_id = node_trace_host->value->number_to_int64_exact &&
        node_trace_host->value->number_to_int64_exact(id, &id_number);
    event->id = event->has_id ? id_number : 0;
    state->file_written = false;
    return node_trace_undefined();
}

Item node_trace_events_internal_binding(void) {
    jube_modules_runtime_attach();
    if (!node_trace_state() || !node_trace_host || !node_trace_host->value ||
            !node_trace_host->value->new_object || !node_trace_host->node ||
            !node_trace_host->node->roots) return ItemNull;
    Item binding = node_trace_host->value->new_object();
    JubeRootFrame frame = {};
    if (!node_trace_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* binding_root = node_trace_host->node->roots->root_frame_take_slot(&frame);
    if (!binding_root) {
        node_trace_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *binding_root = binding.item;
    Item binding_value = node_trace_root_value(binding_root);
    node_trace_set_property(binding_value, "isTraceCategoryEnabled",
        jube_new_function(node_trace_host->script, node_trace_is_category_enabled, 1));
    node_trace_set_property(binding_value, "trace",
        jube_new_function(node_trace_host->script, node_trace_manual_trace, 5));
    node_trace_set_property(binding_value, "getCategoryEnabledBuffer",
                            node_trace_host->value->new_object());
    Item result = node_trace_root_value(binding_root);
    node_trace_host->node->roots->root_frame_end(&frame);
    return result;
}

void node_trace_events_emit_async_hooks_init(const char* type_chars, int type_len,
                                             int64_t async_id, int64_t trigger_id) {
    (void)trigger_id;
    NodeTraceState* state = node_trace_state();
    if (!state || !node_trace_is_category_enabled_cstr("node.async_hooks") ||
            state->event_count >= NODE_TRACE_MAX_EVENTS) return;
    NodeTraceEvent* event = &state->events[state->event_count++];
    event->ph = 'b';
    node_trace_copy_cstr(event->cat, (int)sizeof(event->cat), "node,node.async_hooks", -1);
    node_trace_copy_cstr(event->name, (int)sizeof(event->name), type_chars, type_len);
    event->ts = uv_hrtime() / 1000;
    event->id = async_id;
    event->has_id = true;
    state->file_written = false;
}

void node_trace_events_flush(void) {
    NodeTraceState* state = node_trace_state();
    if (!state) return;
    node_trace_init_from_exec_argv(state);
    if (state->file_written || state->event_count <= 0) return;
    FILE* file = fopen("node_trace.1.log", "wb");
    if (!file) {
        log_error("node-trace-events: failed to open node_trace.1.log");
        return;
    }
    StrBuf* sb = strbuf_new();
    if (!sb) {
        fclose(file);
        return;
    }
    strbuf_append_str_n(sb, "{\"traceEvents\":[", 16);
    long pid = (long)uv_os_getpid();
    for (int i = 0; i < state->event_count; i++) {
        NodeTraceEvent* event = &state->events[i];
        if (i > 0) strbuf_append_char(sb, ',');
        strbuf_append_str_n(sb, "{\"pid\":", 7);
        strbuf_append_int64(sb, (int64_t)pid);
        strbuf_append_str_n(sb, ",\"tid\":0,\"ts\":", 14);
        strbuf_append_int64(sb, (int64_t)event->ts);
        strbuf_append_str_n(sb, ",\"ph\":\"", 7);
        strbuf_append_char(sb, event->ph);
        strbuf_append_str_n(sb, "\",\"cat\":", 8);
        node_trace_append_json_string(sb, event->cat);
        strbuf_append_str_n(sb, ",\"name\":", 8);
        node_trace_append_json_string(sb, event->name);
        if (event->has_id) {
            char id_buffer[32];
            snprintf(id_buffer, sizeof(id_buffer), "0x%llx", (long long)event->id);
            strbuf_append_str_n(sb, ",\"id\":", 6);
            node_trace_append_json_string(sb, id_buffer);
        }
        strbuf_append_str_n(sb, ",\"args\":{}}", 11);
    }
    strbuf_append_str_n(sb, "]}", 2);
    fwrite(sb->str, 1, sb->length, file);
    fclose(file);
    strbuf_free(sb);
    state->file_written = true;
}

int node_trace_events_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots ||
            !host->node->roots->root_frame_begin ||
            !host->node->roots->root_frame_take_slot ||
            !host->node->roots->root_frame_end ||
            !host->node->roots->persistent_root_register ||
            !host->node->roots->persistent_root_unregister || !host->value ||
            !host->value->new_object || !host->value->property_get ||
            !host->value->property_set || !host->value->string_from_utf8_n ||
            !host->value->kind || !host->value->is_array || !host->value->array_length ||
            !host->value->array_get || !host->script || !host->script->new_function ||
            !host->script->current_this || !host->script->throw_type_error_code) return -1;
    node_trace_host = host;
    return 0;
}

void node_trace_events_shutdown(void) {
    node_trace_host = NULL;
}

void node_trace_events_runtime_reset(void* session) {
    NodeTraceState* state = jube_node_trace_state(session);
    if (!state) return;
    state->category_count = 0;
    state->event_count = 0;
    state->initialized = false;
    state->file_written = false;
}

void node_trace_events_runtime_detach(void* session) {
    NodeTraceState* state = jube_node_trace_state(session);
    if (!state) return;
    if (state->namespace_rooted && node_trace_host && node_trace_host->node &&
            node_trace_host->node->roots &&
            node_trace_host->node->roots->persistent_root_unregister) {
        node_trace_host->node->roots->persistent_root_unregister(session,
                                                                  &state->namespace_item);
    }
    memset(state, 0, sizeof(*state));
}

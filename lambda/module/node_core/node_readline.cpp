// Node stream objects are an adapter around the retained Lambda editor.
#include "node_readline.hpp"
#include "node_core_common.hpp"
#include "node_events.hpp"
#include "../../jube/jube_registry.h"
#include "../../../lib/memtrack.h"
#include "../../../lib/log.h"

#include <cstring>

struct NodeReadlinePending {
    NodeReadlinePending* next;
    char* text;
    size_t length;
    int kind;
    int64_t frame_id;
    int64_t generation;
};

enum NodeReadlinePendingKind {
    NODE_READLINE_LINE,
    NODE_READLINE_ANSWER,
    NODE_READLINE_COMPLETION,
};

struct NodeReadlineHandle {
    NodeReadlineHandle* next;
    void* mounted;
    NodeReadlinePending* first;
    NodeReadlinePending* last;
    uint64_t revision;
    bool closed;
    bool close_event_pending;
};

struct NodeReadlineSessionState {
    JubePersistentValueSlots cache_values;
    Item cache_items[4];
    NodeReadlineHandle* handles;
};

struct NodeReadlineSnapshot {
    char* line;
    size_t line_length;
    char* bytes;
    size_t byte_length;
    int64_t cursor;
    int64_t cols;
    int64_t rows;
};

static const JubeHostAPI* node_readline_host = NULL;
static const JubeTemplateTarget node_readline_frame_target = {
    "active_frame", "frame", true
};
static Item node_readline_call(Item receiver, const char* method,
                               Item* arguments, int count);
static Item node_readline_method_prompt(Item preserve_cursor);
static void node_readline_sync_snapshot(Item interface_item,
                                        NodeReadlineHandle* handle);

static NodeReadlineSessionState* node_readline_state(void) {
    return (NodeReadlineSessionState*)jube_node_current_module_state(
        JUBE_NODE_MODULE_STATE_READLINE);
}

static Item node_readline_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static bool node_readline_missing(Item value) {
    return value.item == 0 || value.item == ITEM_NULL ||
        value.item == ITEM_JS_UNDEFINED;
}

static Item node_readline_string(const char* bytes, size_t length) {
    return node_readline_host->value->string_from_utf8_n(bytes, length);
}

static Item node_readline_get(Item object, const char* name) {
    JubeScopedRoots roots(node_readline_host, 2);
    uint64_t* object_root = roots.slot(object);
    if (!object_root) return ItemNull;
    Item key = node_readline_string(name, strlen(name));
    uint64_t* key_root = roots.slot(key);
    if (!key_root) return ItemNull;
    return node_readline_host->value->property_get(jube_root_item(object_root),
                                                    jube_root_item(key_root));
}

static void node_readline_set(Item object, const char* name, Item value) {
    jube_node_object_set(node_readline_host, object, name, value);
}

static bool node_readline_copy_template_string(Item value, char** out,
                                                size_t* length) {
    *out = NULL;
    *length = 0;
    size_t needed = 0;
    node_readline_host->templates->string_copy(value, NULL, 0, &needed);
    char* copy = (char*)mem_alloc(needed + 1, MEM_CAT_SYSTEM);
    if (!copy) return false;
    if (!node_readline_host->templates->string_copy(value, copy, needed + 1,
                                                     &needed)) {
        mem_free(copy);
        return false;
    }
    *out = copy;
    *length = needed;
    return true;
}

static int node_readline_snapshot_copy(void* user, void*, Item snapshot) {
    NodeReadlineSnapshot* copied = (NodeReadlineSnapshot*)user;
    const JubeHostTemplateAPI* api = node_readline_host->templates;
    Item rendered = api->child_at(snapshot, 0);
    Item caret = api->attribute(rendered, "caret");
    Item cursor = api->attribute(caret, "utf16_offset");
    copied->cursor = get_type_id(cursor) == LMD_TYPE_INT ? it2i(cursor) : 0;
    Item screen = api->attribute(snapshot, "screen");
    Item cell_caret = api->attribute(screen, "caret");
    Item col = api->attribute(cell_caret, "col");
    Item row = api->attribute(cell_caret, "row");
    copied->cols = get_type_id(col) == LMD_TYPE_INT ? it2i(col) : 0;
    copied->rows = get_type_id(row) == LMD_TYPE_INT ? it2i(row) : 0;
    if (!node_readline_copy_template_string(api->attribute(rendered, "text"),
            &copied->line, &copied->line_length)) return -1;
    if (!node_readline_copy_template_string(api->attribute(snapshot, "bytes"),
            &copied->bytes, &copied->byte_length)) return -1;
    return 0;
}

static void node_readline_snapshot_release(NodeReadlineSnapshot* snapshot) {
    mem_free(snapshot->line);
    mem_free(snapshot->bytes);
    memset(snapshot, 0, sizeof(*snapshot));
}

static void node_readline_pending_clear(NodeReadlineHandle* handle) {
    NodeReadlinePending* pending = handle->first;
    while (pending) {
        NodeReadlinePending* next = pending->next;
        mem_free(pending->text);
        mem_free(pending);
        pending = next;
    }
    handle->first = handle->last = NULL;
}

static bool node_readline_pending_append(NodeReadlineHandle* handle, int kind,
                                         Item event_data, const char* text_field) {
    const JubeHostTemplateAPI* api = node_readline_host->templates;
    NodeReadlinePending* pending = (NodeReadlinePending*)mem_calloc(1,
        sizeof(NodeReadlinePending), MEM_CAT_SYSTEM);
    if (!pending) return false;
    if (!node_readline_copy_template_string(api->attribute(event_data,
            text_field), &pending->text, &pending->length)) {
        mem_free(pending);
        return false;
    }
    pending->kind = kind;
    if (kind == NODE_READLINE_COMPLETION) {
        Item id = api->attribute(event_data, "frame_id");
        Item generation = api->attribute(event_data, "generation");
        pending->frame_id = get_type_id(id) == LMD_TYPE_INT ? it2i(id) : -1;
        pending->generation = get_type_id(generation) == LMD_TYPE_INT
            ? it2i(generation) : -1;
    }
    if (handle->last) handle->last->next = pending;
    else handle->first = pending;
    handle->last = pending;
    return true;
}

static void node_readline_handle_close(NodeReadlineHandle* handle) {
    if (!handle || !handle->mounted) return;
    node_readline_host->templates->close(handle->mounted);
    handle->mounted = NULL;
    handle->closed = true;
    node_readline_pending_clear(handle);
}

static void node_readline_detach_input(Item interface_item) {
    JubeScopedRoots roots(node_readline_host, 4);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return;
    Item input = node_readline_get(jube_root_item(interface_root), "input");
    uint64_t* input_root = roots.slot(input);
    if (!input_root || node_readline_missing(input)) return;
    Item event = node_readline_string("data", 4);
    uint64_t* event_root = roots.slot(event);
    if (!event_root) return;
    Item listener = node_readline_get(jube_root_item(interface_root),
                                      "_dataListener");
    Item args[2] = {jube_root_item(event_root), listener};
    node_readline_call(jube_root_item(input_root), "off", args, 2);
    *event_root = node_readline_string("end", 3).item;
    args[0] = jube_root_item(event_root);
    args[1] = node_readline_get(jube_root_item(interface_root), "_endListener");
    node_readline_call(jube_root_item(input_root), "off", args, 2);
    node_readline_set(jube_root_item(input_root), "_lambdaReadline", ItemNull);
}

static void node_readline_cancel_question(Item interface_item) {
    JubeScopedRoots roots(node_readline_host, 5);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return;
    Item reject = node_readline_get(jube_root_item(interface_root),
                                    "_questionReject");
    uint64_t* reject_root = roots.slot(reject);
    node_readline_set(jube_root_item(interface_root),
                      "_questionResponder", ItemNull);
    node_readline_set(jube_root_item(interface_root),
                      "_questionReject", ItemNull);
    if (!reject_root || node_readline_host->value->kind(
            jube_root_item(reject_root)) != JUBE_VALUE_FUNCTION) return;
    Item name = node_readline_string("Error", 5);
    uint64_t* name_root = roots.slot(name);
    Item message = node_readline_string("readline closed", 15);
    uint64_t* message_root = roots.slot(message);
    if (!name_root || !message_root) return;
    Item error = node_readline_host->script->new_error_with_name(
        jube_root_item(name_root), jube_root_item(message_root));
    uint64_t* error_root = roots.slot(error);
    if (!error_root) return;
    Item args[1] = {jube_root_item(error_root)};
    node_readline_host->script->call_function(jube_root_item(reject_root),
        node_readline_undefined(), args, 1);
}

static const JubeTypeDef node_readline_handle_type = {
    "NodeReadlineTemplate", JUBE_TYPE_NON_OWNING_HOST, NULL,
    NULL, JUBE_CARRIER_VMAP
};

struct NodeReadlineMountGuard {
    NodeReadlineHandle* handle;
    bool committed;
    ~NodeReadlineMountGuard() {
        if (!committed) node_readline_handle_close(handle);
    }
};

static NodeReadlineHandle* node_readline_handle(Item interface_item) {
    Item carrier = node_readline_get(interface_item, "_templateHandle");
    return (NodeReadlineHandle*)node_readline_host->value->native_object_data(
        carrier, &node_readline_handle_type);
}

static Item node_readline_call(Item receiver, const char* method,
                               Item* arguments, int count) {
    JubeScopedRoots roots(node_readline_host, (size_t)count + 2);
    uint64_t* receiver_root = roots.slot(receiver);
    if (!receiver_root) return ItemNull;
    uint64_t* argument_roots[3] = {};
    for (int i = 0; i < count; i++) {
        argument_roots[i] = roots.slot(arguments[i]);
        if (!argument_roots[i]) return ItemNull;
    }
    Item function = node_readline_get(jube_root_item(receiver_root), method);
    uint64_t* function_root = roots.slot(function);
    if (!function_root || node_readline_host->value->kind(function) !=
            JUBE_VALUE_FUNCTION) return ItemNull;
    Item rooted_arguments[3] = {};
    for (int i = 0; i < count; i++)
        rooted_arguments[i] = jube_root_item(argument_roots[i]);
    return node_readline_host->script->call_function(jube_root_item(function_root),
        jube_root_item(receiver_root), rooted_arguments, count);
}

static Item node_readline_emit_js(Item interface_item, const char* event,
                                  const char* text, size_t length) {
    JubeScopedRoots roots(node_readline_host, 3);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return ItemNull;
    Item event_item = node_readline_string(event, strlen(event));
    uint64_t* event_root = roots.slot(event_item);
    if (!event_root) return ItemNull;
    Item args[2] = {jube_root_item(event_root), ItemNull};
    int count = 1;
    if (text) {
        args[1] = node_readline_string(text, length);
        count = 2;
    }
    return node_readline_call(jube_root_item(interface_root), "emit", args, count);
}

// The emit phase is Lambda-only. JS listeners run after dispatch has restored
// the owning JS activation (D7.4.2v2), including when one chunk has many lines.
static Item node_readline_template_emit(void* user, void* mounted,
                                        Item event_name, Item event_data) {
    NodeReadlineHandle* handle = (NodeReadlineHandle*)user;
    const JubeHostTemplateAPI* api = node_readline_host->templates;
    char name[64];
    if (!api->string_copy(event_name, name, sizeof(name), NULL)) return ItemNull;
    if (strcmp(name, "readline_line") == 0 ||
            strcmp(name, "readline_answer") == 0) {
        return node_readline_pending_append(handle,
            strcmp(name, "readline_answer") == 0 ? NODE_READLINE_ANSWER
                                                : NODE_READLINE_LINE,
            event_data, "text") ? ItemNull : ItemError;
    }
    if (strcmp(name, "readline_completion_request") == 0) {
        return node_readline_pending_append(handle, NODE_READLINE_COMPLETION,
            event_data, "line") ? ItemNull : ItemError;
    }
    if (strcmp(name, "readline_close") == 0 ||
            strcmp(name, "readline_cancel") == 0) {
        handle->close_event_pending = true;
        return ItemNull;
    }
    if (strcmp(name, "readline_frame_event") == 0) {
        Item child_name = api->attribute(event_data, "name");
        char event[64];
        if (!api->string_copy(child_name, event, sizeof(event), NULL)) return ItemNull;
        int status = api->dispatch_item(mounted, &node_readline_frame_target,
            event, api->attribute(event_data, "event"),
            node_readline_template_emit, user);
        return {.item = b2it(status == 0)};
    }
    if (strncmp(name, "readline_", 9) == 0) {
        int status = api->dispatch_item(mounted, NULL, name, event_data,
            node_readline_template_emit, user);
        return {.item = b2it(status == 0)};
    }
    return ItemNull;
}

static int node_readline_dispatch(NodeReadlineHandle* handle, const char* event,
                                  const JubeTemplateValue* value) {
    if (!handle || !handle->mounted || handle->closed) return -1;
    handle->revision++;
    int status = node_readline_host->templates->dispatch(handle->mounted, NULL, event,
        value, node_readline_template_emit, handle);
    if (status != 0) log_error("node-readline-dispatch: template rejected event %s", event);
    return status;
}

static bool node_readline_apply_completion(Item interface_item,
        NodeReadlineHandle* handle, const NodeReadlinePending* request,
        Item reply) {
    JubeScopedRoots roots(node_readline_host, 5);
    uint64_t* interface_root = roots.slot(interface_item);
    uint64_t* reply_root = roots.slot(reply);
    if (!interface_root || !reply_root || !handle->mounted ||
            !node_readline_host->value->is_array(jube_root_item(reply_root)) ||
            node_readline_host->value->array_length(jube_root_item(reply_root)) < 2)
        return false;
    Item matches = node_readline_host->value->array_get(jube_root_item(reply_root), 0);
    uint64_t* matches_root = roots.slot(matches);
    if (!matches_root || !node_readline_host->value->is_array(
            jube_root_item(matches_root))) return false;
    Item complete_on = node_readline_host->value->array_get(
        jube_root_item(reply_root), 1);
    uint64_t* complete_on_root = roots.slot(complete_on);
    if (!complete_on_root || node_readline_host->value->kind(
            jube_root_item(complete_on_root)) != JUBE_VALUE_STRING) return false;
    uint64_t* candidate_root = roots.slot(ItemNull);
    if (!candidate_root) return false;
    int64_t count = node_readline_host->value->array_length(
        jube_root_item(matches_root));
    if (count < 0 || (uint64_t)count > SIZE_MAX / sizeof(JubeTemplateValue))
        return false;
    JubeTemplateValue* entries = count ? (JubeTemplateValue*)mem_calloc(
        (size_t)count, sizeof(JubeTemplateValue), MEM_CAT_SYSTEM) : NULL;
    if (count && !entries) return false;
    int64_t copied = 0;
    for (; copied < count; copied++) {
        *candidate_root = node_readline_host->value->array_get(
            jube_root_item(matches_root), copied).item;
        if (node_readline_host->value->kind(jube_root_item(candidate_root)) !=
                JUBE_VALUE_STRING) break;
        size_t length = node_readline_host->value->string_length(
            jube_root_item(candidate_root));
        char* bytes = (char*)mem_alloc(length + 1, MEM_CAT_SYSTEM);
        if (!bytes) break;
        memcpy(bytes, node_readline_host->value->string_bytes(
            jube_root_item(candidate_root)), length);
        bytes[length] = '\0';
        entries[copied].kind = JUBE_TEMPLATE_STRING;
        entries[copied].bytes = bytes;
        entries[copied].byte_length = length;
    }
    int status = -1;
    if (copied == count && handle->mounted) {
        JubeTemplateValue id = {}, generation = {}, candidates = {},
                          fragment = {}, response = {};
        id.kind = JUBE_TEMPLATE_INT;
        id.integer = request->frame_id;
        generation.kind = JUBE_TEMPLATE_INT;
        generation.integer = request->generation;
        candidates.kind = JUBE_TEMPLATE_ARRAY;
        candidates.elements = entries;
        candidates.element_count = (size_t)count;
        fragment.kind = JUBE_TEMPLATE_STRING;
        fragment.bytes = (const char*)node_readline_host->value->string_bytes(
            jube_root_item(complete_on_root));
        fragment.byte_length = node_readline_host->value->string_length(
            jube_root_item(complete_on_root));
        JubeTemplateField fields[4] = {{"frame_id", &id},
                                       {"generation", &generation},
                                       {"matches", &candidates},
                                       {"complete_on", &fragment}};
        response.kind = JUBE_TEMPLATE_MAP;
        response.fields = fields;
        response.field_count = 4;
        handle->revision++;
        status = node_readline_host->templates->dispatch(handle->mounted,
            &node_readline_frame_target, "completion_reply", &response,
            node_readline_template_emit, handle);
    }
    for (int64_t i = 0; i < copied; i++) mem_free((void*)entries[i].bytes);
    mem_free(entries);
    return status == 0;
}

// A JS completion result is copied before reentering the Lambda session.
// Frame identity and generation make late callbacks harmless (S12.1.3).
static Item node_readline_completion_settle(Item env_item, Item arguments,
                                            bool callback_style) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return node_readline_undefined();
    NodeReadlinePending request = {};
    if (!node_readline_host->value->number_to_int64_exact(env[1],
            &request.frame_id) ||
            !node_readline_host->value->number_to_int64_exact(env[2],
            &request.generation)) return node_readline_undefined();
    JubeScopedRoots roots(node_readline_host, 3);
    uint64_t* interface_root = roots.slot(env[0]);
    uint64_t* arguments_root = roots.slot(arguments);
    if (!interface_root || !arguments_root) return ItemError;
    int64_t length = node_readline_host->value->array_length(
        jube_root_item(arguments_root));
    if (callback_style) {
        if (length < 2 || !node_readline_missing(
                node_readline_host->value->array_get(
                    jube_root_item(arguments_root), 0)))
            return node_readline_undefined();
    } else if (length < 1) return node_readline_undefined();
    Item reply = node_readline_host->value->array_get(
        jube_root_item(arguments_root), callback_style ? 1 : 0);
    uint64_t* reply_root = roots.slot(reply);
    if (!reply_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (handle && handle->mounted && node_readline_apply_completion(
            jube_root_item(interface_root), handle, &request,
            jube_root_item(reply_root)))
        node_readline_sync_snapshot(jube_root_item(interface_root), handle);
    return node_readline_undefined();
}

static Item node_readline_completion_callback(Item env, Item arguments) {
    return node_readline_completion_settle(env, arguments, true);
}

static Item node_readline_completion_fulfilled(Item env, Item arguments) {
    return node_readline_completion_settle(env, arguments, false);
}

static Item node_readline_completion_rejected(Item) {
    return node_readline_undefined();
}

static Item node_readline_completion_handler(Item interface_item,
        const NodeReadlinePending* request,
        Item (*target)(Item, Item)) {
    Item* env = node_readline_host->script->closure_env_new(3);
    if (!env) return ItemError;
    env[0] = interface_item;
    env[1] = {.item = i2it(request->frame_id)};
    env[2] = {.item = i2it(request->generation)};
    return jube_new_closure(node_readline_host->script, target, -1, env, 3);
}

static bool node_readline_complete(Item interface_item, NodeReadlineHandle* handle,
                                   const NodeReadlinePending* request) {
    JubeScopedRoots roots(node_readline_host, 8);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root || !handle->mounted) return false;
    Item completer = node_readline_get(jube_root_item(interface_root),
                                       "_completer");
    uint64_t* completer_root = roots.slot(completer);
    if (!completer_root || node_readline_host->value->kind(
            jube_root_item(completer_root)) != JUBE_VALUE_FUNCTION) return false;
    Item line = node_readline_string(request->text, request->length);
    uint64_t* line_root = roots.slot(line);
    if (!line_root) return false;
    Item arity = node_readline_get(jube_root_item(completer_root), "length");
    int64_t parameter_count = 0;
    bool callback_style = node_readline_host->value->number_to_int64_exact(
        arity, &parameter_count) && parameter_count >= 2;
    Item args[2] = {jube_root_item(line_root), ItemNull};
    if (callback_style) {
        args[1] = node_readline_completion_handler(jube_root_item(interface_root),
            request, node_readline_completion_callback);
        uint64_t* callback_root = roots.slot(args[1]);
        if (!callback_root) return false;
        args[0] = jube_root_item(line_root);
        args[1] = jube_root_item(callback_root);
    }
    Item reply = node_readline_host->script->call_function(
        jube_root_item(completer_root), jube_root_item(interface_root), args,
        callback_style ? 2 : 1);
    uint64_t* reply_root = roots.slot(reply);
    if (!reply_root || callback_style) return false;
    if (node_readline_host->value->is_array(jube_root_item(reply_root)))
        return node_readline_apply_completion(jube_root_item(interface_root),
                                              handle, request, jube_root_item(reply_root));
    Item then = node_readline_get(jube_root_item(reply_root), "then");
    uint64_t* then_root = roots.slot(then);
    if (!then_root || node_readline_host->value->kind(jube_root_item(then_root)) !=
            JUBE_VALUE_FUNCTION) return false;
    Item fulfilled = node_readline_completion_handler(
        jube_root_item(interface_root), request,
        node_readline_completion_fulfilled);
    uint64_t* fulfilled_root = roots.slot(fulfilled);
    if (!fulfilled_root) return false;
    Item rejected = jube_new_function(node_readline_host->script,
        node_readline_completion_rejected, 1);
    uint64_t* rejected_root = roots.slot(rejected);
    if (!rejected_root) return false;
    Item then_args[2] = {jube_root_item(fulfilled_root),
                         jube_root_item(rejected_root)};
    node_readline_host->script->call_function(jube_root_item(then_root),
        jube_root_item(reply_root), then_args, 2);
    return false;
}

static void node_readline_sync_snapshot(Item interface_item,
                                        NodeReadlineHandle* handle) {
    if (!handle || !handle->mounted) return;
    NodeReadlineSnapshot snapshot = {};
    if (node_readline_host->templates->render_item(handle->mounted,
            node_readline_snapshot_copy, &snapshot) == 0) {
        Item line = node_readline_string(snapshot.line, snapshot.line_length);
        node_readline_set(interface_item, "line", line);
        Item cursor = node_readline_host->script->make_number((double)snapshot.cursor);
        node_readline_set(interface_item, "cursor", cursor);
    }
    node_readline_snapshot_release(&snapshot);
}

static void node_readline_publish(Item interface_item,
                                  NodeReadlineHandle* handle) {
    if (!handle || !handle->mounted) return;
    JubeScopedRoots roots(node_readline_host, 1);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return;
    node_readline_sync_snapshot(jube_root_item(interface_root), handle);
    while (handle->first) {
        NodeReadlinePending* next = handle->first;
        handle->first = next->next;
        if (!handle->first) handle->last = NULL;
        if (next->kind == NODE_READLINE_ANSWER) {
            JubeScopedRoots answer_roots(node_readline_host, 2);
            Item responder = node_readline_get(jube_root_item(interface_root),
                                               "_questionResponder");
            uint64_t* responder_root = answer_roots.slot(responder);
            node_readline_set(jube_root_item(interface_root),
                              "_questionResponder", ItemNull);
            node_readline_set(jube_root_item(interface_root),
                              "_questionReject", ItemNull);
            Item answer = node_readline_string(next->text, next->length);
            uint64_t* answer_root = answer_roots.slot(answer);
            if (responder_root && answer_root &&
                    node_readline_host->value->kind(jube_root_item(responder_root)) ==
                        JUBE_VALUE_FUNCTION) {
                Item args[1] = {jube_root_item(answer_root)};
                node_readline_host->script->call_function(
                    jube_root_item(responder_root),
                    jube_root_item(interface_root), args, 1);
            }
        } else if (next->kind == NODE_READLINE_LINE) {
            node_readline_emit_js(jube_root_item(interface_root), "line",
                                  next->text, next->length);
        } else if (next->kind == NODE_READLINE_COMPLETION) {
            if (node_readline_complete(jube_root_item(interface_root), handle,
                    next)) {
                node_readline_sync_snapshot(jube_root_item(interface_root), handle);
            }
        }
        mem_free(next->text);
        mem_free(next);
        // A listener can synchronously close or write into the same interface.
        if (!handle->mounted) return;
    }
    if (handle->close_event_pending) {
        handle->close_event_pending = false;
        node_readline_cancel_question(jube_root_item(interface_root));
        node_readline_detach_input(jube_root_item(interface_root));
        node_readline_handle_close(handle);
        node_readline_set(jube_root_item(interface_root), "closed",
                          (Item){.item = ITEM_TRUE});
        node_readline_emit_js(jube_root_item(interface_root), "close", NULL, 0);
    }
}

static Item node_readline_feed(Item interface_item, Item data) {
    // The handle lookup allocates a JS property key. Keep the caller's chunk
    // alive before that allocation, then carry its bytes across Lambda work.
    JubeScopedRoots roots(node_readline_host, 2);
    uint64_t* interface_root = roots.slot(interface_item);
    uint64_t* data_root = roots.slot(data);
    if (!interface_root || !data_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return node_readline_undefined();
    data = jube_root_item(data_root);
    const JubeHostValueAPI* value_api = node_readline_host->value;
    const uint8_t* source = NULL;
    size_t length = 0;
    char* owned = NULL;
    if (value_api->kind(data) == JUBE_VALUE_STRING) {
        source = value_api->string_bytes(data);
        length = value_api->string_length(data);
    } else {
        Item length_item = node_readline_get(data, "length");
        int64_t count = 0;
        if (!value_api->number_to_int64_exact(length_item, &count) ||
                count < 0 || count > 1 << 20) return node_readline_undefined();
        owned = (char*)mem_alloc((size_t)count + 1, MEM_CAT_SYSTEM);
        if (!owned) return ItemError;
        for (int64_t i = 0; i < count; i++) {
            int64_t byte = 0;
            Item element = value_api->array_get(data, i);
            if (!value_api->number_to_int64_exact(element, &byte)) byte = 0;
            owned[i] = (char)(byte & 255);
        }
        source = (const uint8_t*)owned;
        length = (size_t)count;
    }
    JubeTemplateValue packet = {};
    packet.kind = JUBE_TEMPLATE_BINARY;
    packet.bytes = (const char*)source;
    packet.byte_length = length;
    int result = node_readline_dispatch(handle, "readline_bytes", &packet);
    mem_free(owned);
    if (result == 0) node_readline_publish(jube_root_item(interface_root), handle);
    return result == 0 ? node_readline_undefined() : ItemError;
}

static Item node_readline_method_write(Item data, Item key) {
    Item interface_item = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 4);
    uint64_t* interface_root = roots.slot(interface_item);
    uint64_t* key_root = roots.slot(key);
    if (!interface_root || !key_root) return ItemError;
    if (node_readline_host->value->kind(jube_root_item(key_root)) !=
            JUBE_VALUE_OBJECT)
        return node_readline_feed(jube_root_item(interface_root), data);
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return node_readline_undefined();
    Item name_item = node_readline_get(jube_root_item(key_root), "name");
    uint64_t* name_root = roots.slot(name_item);
    Item sequence_item = node_readline_get(jube_root_item(key_root), "sequence");
    uint64_t* sequence_root = roots.slot(sequence_item);
    if (!name_root || !sequence_root) return ItemError;
    JubeTemplateValue name = {}, sequence = {}, ctrl = {}, meta = {}, shift = {},
                      event = {};
    name.kind = sequence.kind = JUBE_TEMPLATE_STRING;
    Item rooted_name = jube_root_item(name_root);
    if (node_readline_host->value->kind(rooted_name) == JUBE_VALUE_STRING) {
        name.bytes = (const char*)node_readline_host->value->string_bytes(rooted_name);
        name.byte_length = node_readline_host->value->string_length(rooted_name);
    } else name.bytes = "";
    Item rooted_sequence = jube_root_item(sequence_root);
    if (node_readline_host->value->kind(rooted_sequence) == JUBE_VALUE_STRING) {
        sequence.bytes = (const char*)node_readline_host->value->string_bytes(
            rooted_sequence);
        sequence.byte_length = node_readline_host->value->string_length(
            rooted_sequence);
    } else sequence.bytes = "";
    ctrl.kind = meta.kind = shift.kind = JUBE_TEMPLATE_BOOL;
    ctrl.boolean = node_readline_host->script->is_truthy(node_readline_get(
        jube_root_item(key_root), "ctrl"));
    meta.boolean = node_readline_host->script->is_truthy(node_readline_get(
        jube_root_item(key_root), "meta"));
    shift.boolean = node_readline_host->script->is_truthy(node_readline_get(
        jube_root_item(key_root), "shift"));
    JubeTemplateField fields[5] = {{"name", &name}, {"sequence", &sequence},
                                   {"ctrl", &ctrl}, {"meta", &meta},
                                   {"shift", &shift}};
    event.kind = JUBE_TEMPLATE_MAP;
    event.fields = fields;
    event.field_count = 5;
    int status = node_readline_dispatch(handle, "readline_key", &event);
    if (status == 0) node_readline_publish(jube_root_item(interface_root), handle);
    return status == 0 ? node_readline_undefined() : ItemError;
}

static Item node_readline_input_data(Item data) {
    Item input = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 2);
    uint64_t* input_root = roots.slot(input);
    uint64_t* data_root = roots.slot(data);
    if (!input_root || !data_root) return ItemError;
    Item interface_item = node_readline_get(jube_root_item(input_root),
                                            "_lambdaReadline");
    return node_readline_feed(interface_item, jube_root_item(data_root));
}

static Item node_readline_input_end(void) {
    Item input = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 2);
    uint64_t* input_root = roots.slot(input);
    if (!input_root) return ItemError;
    Item interface_item = node_readline_get(jube_root_item(input_root),
                                            "_lambdaReadline");
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(interface_item);
    if (handle && handle->mounted) {
        JubeTemplateValue empty = {};
        if (node_readline_dispatch(handle, "readline_end", &empty) == 0)
            node_readline_publish(jube_root_item(interface_root), handle);
    }
    return node_readline_undefined();
}

static Item node_readline_method_close(void) {
    Item interface_item = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 1);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return jube_root_item(interface_root);
    node_readline_cancel_question(jube_root_item(interface_root));
    node_readline_detach_input(jube_root_item(interface_root));
    node_readline_handle_close(handle);
    node_readline_set(jube_root_item(interface_root), "closed",
                      (Item){.item = ITEM_TRUE});
    node_readline_emit_js(jube_root_item(interface_root), "close", NULL, 0);
    return jube_root_item(interface_root);
}

static Item node_readline_method_set_prompt(Item prompt) {
    Item interface_item = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 3);
    uint64_t* interface_root = roots.slot(interface_item);
    uint64_t* prompt_root = roots.slot(prompt);
    if (!interface_root || !prompt_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return node_readline_undefined();
    Item text = node_readline_host->script->to_string(jube_root_item(prompt_root));
    uint64_t* text_root = roots.slot(text);
    if (!text_root) return ItemError;
    JubeTemplateValue value = {};
    value.kind = JUBE_TEMPLATE_STRING;
    value.bytes = (const char*)node_readline_host->value->string_bytes(
        jube_root_item(text_root));
    value.byte_length = node_readline_host->value->string_length(
        jube_root_item(text_root));
    if (node_readline_dispatch(handle, "readline_prompt", &value) == 0) {
        node_readline_set(jube_root_item(interface_root), "_prompt",
                          jube_root_item(text_root));
        node_readline_publish(jube_root_item(interface_root), handle);
    }
    return node_readline_undefined();
}

static Item node_readline_method_get_prompt(void) {
    return node_readline_get(node_readline_host->script->current_this(), "_prompt");
}

static Item node_readline_method_get_cursor_pos(void) {
    Item interface_item = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 2);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return ItemError;
    NodeReadlineSnapshot snapshot = {};
    if (node_readline_host->templates->render_item(handle->mounted,
            node_readline_snapshot_copy, &snapshot) != 0) {
        node_readline_snapshot_release(&snapshot);
        return ItemError;
    }
    Item result = node_readline_host->value->new_object();
    uint64_t* result_root = roots.slot(result);
    if (result_root) {
        node_readline_set(jube_root_item(result_root), "cols",
            node_readline_host->script->make_number((double)snapshot.cols));
        node_readline_set(jube_root_item(result_root), "rows",
            node_readline_host->script->make_number((double)snapshot.rows));
    }
    node_readline_snapshot_release(&snapshot);
    return result_root ? jube_root_item(result_root) : ItemError;
}

static Item node_readline_pause_resume(bool pause) {
    Item interface_item = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 2);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return jube_root_item(interface_root);
    Item previous = node_readline_get(jube_root_item(interface_root), "paused");
    if ((previous.item == ITEM_TRUE) == pause)
        return jube_root_item(interface_root);
    JubeTemplateValue empty = {};
    if (node_readline_dispatch(handle,
            pause ? "readline_pause" : "readline_resume", &empty) != 0)
        return ItemError;
    Item input = node_readline_get(jube_root_item(interface_root), "input");
    uint64_t* input_root = roots.slot(input);
    if (input_root && !node_readline_missing(input))
        node_readline_call(jube_root_item(input_root),
                           pause ? "pause" : "resume", NULL, 0);
    node_readline_set(jube_root_item(interface_root), "paused",
                      {.item = b2it(pause)});
    node_readline_emit_js(jube_root_item(interface_root),
                          pause ? "pause" : "resume", NULL, 0);
    return jube_root_item(interface_root);
}

static Item node_readline_method_pause(void) {
    return node_readline_pause_resume(true);
}

static Item node_readline_method_resume(void) {
    return node_readline_pause_resume(false);
}

static Item node_readline_question_start(Item interface_item, Item query,
                                         Item responder) {
    JubeScopedRoots roots(node_readline_host, 4);
    uint64_t* interface_root = roots.slot(interface_item);
    uint64_t* query_root = roots.slot(query);
    uint64_t* responder_root = roots.slot(responder);
    if (!interface_root || !query_root || !responder_root) return ItemError;
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return ItemError;
    Item existing = node_readline_get(jube_root_item(interface_root),
                                      "_questionResponder");
    if (!node_readline_missing(existing))
        return node_readline_method_prompt(ItemNull);
    Item text = node_readline_host->script->to_string(jube_root_item(query_root));
    uint64_t* text_root = roots.slot(text);
    if (!text_root) return ItemError;
    node_readline_set(jube_root_item(interface_root), "_questionResponder",
                      jube_root_item(responder_root));
    JubeTemplateValue value = {};
    value.kind = JUBE_TEMPLATE_STRING;
    value.bytes = (const char*)node_readline_host->value->string_bytes(
        jube_root_item(text_root));
    value.byte_length = node_readline_host->value->string_length(
        jube_root_item(text_root));
    if (node_readline_dispatch(handle, "readline_question", &value) != 0) {
        node_readline_set(jube_root_item(interface_root), "_questionResponder",
                          ItemNull);
        return ItemError;
    }
    return node_readline_method_prompt(ItemNull);
}

static Item node_readline_method_question(Item query, Item options_or_callback,
                                          Item callback) {
    Item responder = node_readline_host->value->kind(options_or_callback) ==
            JUBE_VALUE_FUNCTION ? options_or_callback : callback;
    if (node_readline_host->value->kind(responder) != JUBE_VALUE_FUNCTION)
        return node_readline_undefined();
    return node_readline_question_start(
        node_readline_host->script->current_this(), query, responder);
}

static Item node_readline_method_question_promise(Item query, Item) {
    Item interface_item = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 5);
    uint64_t* interface_root = roots.slot(interface_item);
    uint64_t* query_root = roots.slot(query);
    if (!interface_root || !query_root) return ItemError;
    Item resolvers = node_readline_host->script->promise_with_resolvers();
    uint64_t* resolvers_root = roots.slot(resolvers);
    if (!resolvers_root) return ItemError;
    Item promise = node_readline_get(jube_root_item(resolvers_root), "promise");
    uint64_t* promise_root = roots.slot(promise);
    Item resolve = node_readline_get(jube_root_item(resolvers_root), "resolve");
    uint64_t* resolve_root = roots.slot(resolve);
    if (!promise_root || !resolve_root) return ItemError;
    Item existing = node_readline_get(jube_root_item(interface_root),
                                      "_questionResponder");
    if (node_readline_missing(existing)) {
        Item reject = node_readline_get(jube_root_item(resolvers_root), "reject");
        node_readline_set(jube_root_item(interface_root), "_questionReject",
                          reject);
    }
    if (node_readline_question_start(jube_root_item(interface_root),
            jube_root_item(query_root), jube_root_item(resolve_root)).item ==
            ITEM_ERROR) {
        node_readline_cancel_question(jube_root_item(interface_root));
    }
    return jube_root_item(promise_root);
}

static int node_readline_ack_snapshot(void* user, void* mounted, Item snapshot) {
    NodeReadlineHandle* handle = (NodeReadlineHandle*)user;
    return node_readline_host->templates->dispatch_item(mounted, NULL,
        "readline_presented", snapshot, node_readline_template_emit, handle);
}

static Item node_readline_method_prompt(Item) {
    Item interface_item = node_readline_host->script->current_this();
    JubeScopedRoots roots(node_readline_host, 3);
    uint64_t* interface_root = roots.slot(interface_item);
    if (!interface_root) return ItemError;
    if (node_readline_get(jube_root_item(interface_root), "paused").item ==
            ITEM_TRUE) node_readline_pause_resume(false);
    NodeReadlineHandle* handle = node_readline_handle(jube_root_item(interface_root));
    if (!handle || !handle->mounted) return node_readline_undefined();
    NodeReadlineSnapshot snapshot = {};
    if (node_readline_host->templates->render_item(handle->mounted,
            node_readline_snapshot_copy, &snapshot) != 0) {
        node_readline_snapshot_release(&snapshot);
        return ItemError;
    }
    uint64_t revision = handle->revision;
    Item output = node_readline_get(jube_root_item(interface_root), "output");
    uint64_t* output_root = roots.slot(output);
    if (!output_root) {
        node_readline_snapshot_release(&snapshot);
        return ItemError;
    }
    if (!node_readline_missing(output) && snapshot.byte_length) {
        Item writer = node_readline_get(jube_root_item(output_root), "write");
        if (node_readline_host->value->kind(writer) != JUBE_VALUE_FUNCTION) {
            node_readline_snapshot_release(&snapshot);
            return ItemError;
        }
        Item bytes = node_readline_string(snapshot.bytes, snapshot.byte_length);
        uint64_t* bytes_root = roots.slot(bytes);
        if (!bytes_root) {
            node_readline_snapshot_release(&snapshot);
            return ItemError;
        }
        Item args[1] = {jube_root_item(bytes_root)};
        Item written = node_readline_call(jube_root_item(output_root),
                                          "write", args, 1);
        if (get_type_id(written) == LMD_TYPE_ERROR) {
            node_readline_snapshot_release(&snapshot);
            return written;
        }
        if (handle->mounted && handle->revision == revision)
            node_readline_host->templates->render_item(handle->mounted,
                node_readline_ack_snapshot, handle);
    }
    node_readline_snapshot_release(&snapshot);
    return node_readline_undefined();
}

template <typename Target>
static void node_readline_set_method(Item object, const char* name,
                                     Target target, int arity) {
    Item method = jube_new_function(node_readline_host->script, target, arity);
    node_readline_set(object, name, method);
}

static Item node_readline_create_with_profile(Item options, bool promises) {
    NodeReadlineSessionState* state = node_readline_state();
    if (!state) return ItemError;
    JubeScopedRoots roots(node_readline_host, 5);
    uint64_t* options_root = roots.slot(options);
    Item interface_item = node_readline_host->value->new_object();
    uint64_t* interface_root = roots.slot(interface_item);
    if (!options_root || !interface_root) return ItemError;
    node_readline_host->script->set_prototype(jube_root_item(interface_root),
                                               state->cache_items[promises ? 3 : 2]);
    NodeReadlineHandle* handle = (NodeReadlineHandle*)mem_calloc(1,
        sizeof(NodeReadlineHandle), MEM_CAT_SYSTEM);
    if (!handle) return ItemError;
    handle->next = state->handles;
    state->handles = handle;
    handle->mounted = node_readline_host->templates->open(
        "import readline: lambda.io.terminal\n"
        "readline.terminal(\"node\", 0, \"\")\n", "<node-readline>");
    if (!handle->mounted) {
        state->handles = handle->next;
        mem_free(handle);
        return ItemError;
    }
    NodeReadlineMountGuard mount_guard = {handle, false};
    Item carrier = node_readline_host->value->native_object_new(
        &node_readline_handle_type, handle);
    uint64_t* carrier_root = roots.slot(carrier);
    if (!carrier_root) {
        log_error("node-readline-create: carrier root unavailable after mount");
        return ItemError;
    }
    node_readline_set(jube_root_item(interface_root), "_templateHandle",
                      jube_root_item(carrier_root));
    Item prompt = node_readline_get(jube_root_item(options_root), "prompt");
    if (node_readline_missing(prompt)) prompt = node_readline_string("> ", 2);
    uint64_t* prompt_root = roots.slot(prompt);
    if (!prompt_root) {
        log_error("node-readline-create: prompt root unavailable after mount");
        return ItemError;
    }
    Item input = node_readline_get(jube_root_item(options_root), "input");
    uint64_t* input_root = roots.slot(input);
    if (!input_root) {
        log_error("node-readline-create: input root unavailable after mount");
        return ItemError;
    }
    Item output = node_readline_get(jube_root_item(options_root), "output");
    node_readline_set(jube_root_item(interface_root), "input", jube_root_item(input_root));
    node_readline_set(jube_root_item(interface_root), "output", output);
    node_readline_set(jube_root_item(interface_root), "_prompt", jube_root_item(prompt_root));
    Item completer = node_readline_get(jube_root_item(options_root), "completer");
    bool has_completer = node_readline_host->value->kind(completer) ==
        JUBE_VALUE_FUNCTION;
    node_readline_set(jube_root_item(interface_root), "_completer", completer);
    node_readline_set(jube_root_item(interface_root), "closed",
                      (Item){.item = ITEM_FALSE});
    node_readline_set(jube_root_item(interface_root), "paused",
                      (Item){.item = ITEM_FALSE});
    JubeTemplateValue tty = {};
    tty.kind = JUBE_TEMPLATE_BOOL;
    Item terminal = node_readline_get(jube_root_item(options_root), "terminal");
    if (node_readline_missing(terminal)) {
        Item output_for_tty = node_readline_get(jube_root_item(interface_root),
                                                 "output");
        Item is_tty = node_readline_missing(output_for_tty)
            ? ItemNull : node_readline_get(output_for_tty, "isTTY");
        tty.boolean = !node_readline_missing(is_tty) &&
            node_readline_host->script->is_truthy(is_tty);
    } else {
        tty.boolean = node_readline_host->script->is_truthy(terminal);
    }
    node_readline_set(jube_root_item(interface_root), "terminal",
        {.item = b2it(tty.boolean)});
    if (node_readline_dispatch(handle, "readline_device", &tty) != 0)
        return ItemError;
    Item sized_output = node_readline_get(jube_root_item(interface_root),
                                          "output");
    if (!node_readline_missing(sized_output)) {
        Item columns = node_readline_get(sized_output, "columns");
        int64_t width = 0;
        if (node_readline_host->value->number_to_int64_exact(columns, &width) &&
                width > 0) {
            JubeTemplateValue resize = {};
            resize.kind = JUBE_TEMPLATE_INT;
            resize.integer = width;
            if (node_readline_dispatch(handle, "readline_resize", &resize) != 0)
                return ItemError;
        }
    }
    Item size_option = node_readline_get(jube_root_item(options_root),
                                         "historySize");
    int64_t history_size = 30;
    if (!node_readline_missing(size_option) &&
            (!node_readline_host->value->number_to_int64_exact(size_option,
                &history_size) || history_size < 0)) return ItemError;
    JubeTemplateValue history_value = {};
    history_value.kind = JUBE_TEMPLATE_INT;
    history_value.integer = history_size;
    if (node_readline_dispatch(handle, "readline_history_size",
            &history_value) != 0) return ItemError;
    node_readline_set(jube_root_item(interface_root), "historySize",
        node_readline_host->script->make_number((double)history_size));
    JubeTemplateValue policy = {};
    policy.kind = JUBE_TEMPLATE_BOOL;
    policy.boolean = false;
    if (node_readline_dispatch(handle, "readline_history_ignore_dot",
            &policy) != 0) return ItemError;
    Item unique_option = node_readline_get(jube_root_item(options_root),
                                           "removeHistoryDuplicates");
    policy.boolean = node_readline_host->script->is_truthy(unique_option);
    if (node_readline_dispatch(handle, "readline_history_unique",
            &policy) != 0) return ItemError;
    JubeTemplateValue frame_id = {}, frame_prompt = {}, frame_completer = {},
                      next = {};
    frame_id.kind = JUBE_TEMPLATE_INT;
    frame_id.integer = 1;
    frame_prompt.kind = JUBE_TEMPLATE_STRING;
    frame_prompt.bytes = (const char*)node_readline_host->value->string_bytes(
        jube_root_item(prompt_root));
    frame_prompt.byte_length = node_readline_host->value->string_length(
        jube_root_item(prompt_root));
    frame_completer.kind = JUBE_TEMPLATE_BOOL;
    frame_completer.boolean = has_completer;
    JubeTemplateField fields[3] = {{"frame_id", &frame_id},
                                   {"prompt", &frame_prompt},
                                   {"completer", &frame_completer}};
    next.kind = JUBE_TEMPLATE_MAP;
    next.fields = fields;
    next.field_count = 3;
    if (node_readline_dispatch(handle, "readline_next", &next) != 0)
        return ItemError;
    node_readline_publish(jube_root_item(interface_root), handle);
    if (!node_readline_missing(jube_root_item(input_root))) {
        node_readline_set(jube_root_item(input_root), "_lambdaReadline",
                          jube_root_item(interface_root));
        Item data_listener = jube_new_function(node_readline_host->script,
                                                node_readline_input_data, 1);
        node_readline_set(jube_root_item(interface_root), "_dataListener", data_listener);
        Item args[2] = {node_readline_string("data", 4), data_listener};
        node_readline_call(jube_root_item(input_root), "on", args, 2);
        Item end_listener = jube_new_function(node_readline_host->script,
                                               node_readline_input_end, 0);
        node_readline_set(jube_root_item(interface_root), "_endListener", end_listener);
        args[0] = node_readline_string("end", 3);
        args[1] = end_listener;
        node_readline_call(jube_root_item(input_root), "on", args, 2);
    }
    mount_guard.committed = true;
    return jube_root_item(interface_root);
}

static Item node_readline_constructor_with_profile(Item input, Item output,
        Item completer, Item terminal, bool promises) {
    JubeScopedRoots roots(node_readline_host, 5);
    uint64_t* input_root = roots.slot(input);
    uint64_t* output_root = roots.slot(output);
    uint64_t* completer_root = roots.slot(completer);
    uint64_t* terminal_root = roots.slot(terminal);
    if (!input_root || !output_root || !completer_root || !terminal_root)
        return ItemError;
    if (node_readline_missing(output) &&
            node_readline_host->value->kind(jube_root_item(input_root)) ==
                JUBE_VALUE_OBJECT) {
        Item nested_input = node_readline_get(jube_root_item(input_root), "input");
        if (!node_readline_missing(nested_input))
            return node_readline_create_with_profile(jube_root_item(input_root),
                                                     promises);
    }
    Item options = node_readline_host->value->new_object();
    uint64_t* options_root = roots.slot(options);
    if (!options_root)
        return ItemError;
    node_readline_set(jube_root_item(options_root), "input",
                      jube_root_item(input_root));
    node_readline_set(jube_root_item(options_root), "output",
                      jube_root_item(output_root));
    if (!node_readline_missing(completer))
        node_readline_set(jube_root_item(options_root), "completer",
                          jube_root_item(completer_root));
    if (!node_readline_missing(terminal))
        node_readline_set(jube_root_item(options_root), "terminal",
                          jube_root_item(terminal_root));
    return node_readline_create_with_profile(jube_root_item(options_root),
                                              promises);
}

static Item node_readline_constructor(Item input, Item output, Item completer,
                                      Item terminal) {
    return node_readline_constructor_with_profile(input, output, completer,
                                                   terminal,
                                                   false);
}

static Item node_readline_promises_constructor(Item input, Item output,
                                               Item completer,
                                               Item terminal) {
    return node_readline_constructor_with_profile(input, output, completer,
                                                   terminal,
                                                   true);
}

static Item node_readline_namespace_build(bool promises) {
    NodeReadlineSessionState* state = node_readline_state();
    if (!state) return ItemNull;
    Item* cache = &state->cache_items[promises ? 1 : 0];
    if (cache->item != 0) return *cache;
    JubeScopedRoots roots(node_readline_host, 3);
    Item namespace_item = node_readline_host->value->new_object();
    uint64_t* namespace_root = roots.slot(namespace_item);
    *cache = jube_root_item(namespace_root);
    int prototype_index = promises ? 3 : 2;
    if (state->cache_items[prototype_index].item == 0) {
        Item prototype = node_readline_host->value->new_object();
        uint64_t* prototype_root = roots.slot(prototype);
        Item events = node_events_namespace();
        Item event_prototype = node_readline_get(events, "prototype");
        node_readline_host->script->set_prototype(jube_root_item(prototype_root),
                                                   event_prototype);
        node_readline_set_method(jube_root_item(prototype_root), "write",
                                 node_readline_method_write, 2);
        node_readline_set_method(jube_root_item(prototype_root), "close",
                                 node_readline_method_close, 0);
        node_readline_set_method(jube_root_item(prototype_root), "setPrompt",
                                 node_readline_method_set_prompt, 1);
        node_readline_set_method(jube_root_item(prototype_root), "getPrompt",
                                 node_readline_method_get_prompt, 0);
        node_readline_set_method(jube_root_item(prototype_root), "getCursorPos",
                                 node_readline_method_get_cursor_pos, 0);
        node_readline_set_method(jube_root_item(prototype_root), "prompt",
                                 node_readline_method_prompt, 1);
        if (promises)
            node_readline_set_method(jube_root_item(prototype_root), "question",
                                     node_readline_method_question_promise, 2);
        else
            node_readline_set_method(jube_root_item(prototype_root), "question",
                                     node_readline_method_question, 3);
        node_readline_set_method(jube_root_item(prototype_root), "pause",
                                 node_readline_method_pause, 0);
        node_readline_set_method(jube_root_item(prototype_root), "resume",
                                 node_readline_method_resume, 0);
        state->cache_items[prototype_index] = jube_root_item(prototype_root);
    }
    Item factory = promises
        ? jube_new_function(node_readline_host->script,
                            node_readline_promises_constructor, 4)
        : jube_new_function(node_readline_host->script,
                            node_readline_constructor, 4);
    node_readline_set(jube_root_item(namespace_root), "createInterface", factory);
    Item constructor = promises
        ? jube_new_constructor(node_readline_host->script,
                               node_readline_promises_constructor, 4)
        : jube_new_constructor(node_readline_host->script,
                               node_readline_constructor, 4);
    node_readline_host->script->function_set_prototype(constructor,
        state->cache_items[prototype_index]);
    node_readline_set(jube_root_item(namespace_root), "Interface", constructor);
    node_readline_set(jube_root_item(namespace_root), "default",
                      jube_root_item(namespace_root));
    return jube_root_item(namespace_root);
}

Item node_readline_namespace(void) {
    return node_readline_namespace_build(false);
}

Item node_readline_promises_namespace(void) {
    return node_readline_namespace_build(true);
}

int node_readline_init(const JubeHostAPI* host) {
    if (!host || host->struct_size < sizeof(JubeHostAPI) ||
            !(host->capabilities & JUBE_HOST_CAP_TEMPLATE_SESSION) ||
            !host->templates ||
            host->templates->struct_size < sizeof(JubeHostTemplateAPI) ||
            !host->templates->render_item || !host->templates->dispatch_item ||
            !host->value || !host->value->native_object_new ||
            !host->value->native_object_data || !host->node ||
            !host->node->roots || !host->script) return -1;
    node_readline_host = host;
    return 0;
}

void node_readline_shutdown(void) {
    node_readline_host = NULL;
}

void node_readline_runtime_attach(void* session) {
    NodeReadlineSessionState* state = (NodeReadlineSessionState*)
        jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_READLINE,
            sizeof(NodeReadlineSessionState));
    if (state) jube_persistent_value_slots_attach(&state->cache_values, session,
        node_readline_host->node->roots, state->cache_items, 4);
}

void node_readline_runtime_reset(void* session) {
    NodeReadlineSessionState* state = node_readline_state();
    if (!state || state->cache_values.session != session) return;
    for (NodeReadlineHandle* handle = state->handles; handle; handle = handle->next)
        node_readline_handle_close(handle);
    jube_persistent_value_slots_reset(&state->cache_values);
}

void node_readline_runtime_detach(void* session) {
    NodeReadlineSessionState* state = node_readline_state();
    if (!state || state->cache_values.session != session) return;
    NodeReadlineHandle* handle = state->handles;
    while (handle) {
        NodeReadlineHandle* next = handle->next;
        node_readline_handle_close(handle);
        mem_free(handle);
        handle = next;
    }
    state->handles = NULL;
    jube_persistent_value_slots_detach(&state->cache_values);
}

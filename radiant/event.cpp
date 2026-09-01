#include "event.hpp"
#include "layout.hpp"
#include "render.hpp"
#include "view.hpp"
#include "radiant.hpp"
#include "../lambda/module/radiant/radiant_history.hpp"
#include "rdt_video.h"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/font/font.h"

#include "../lib/log.h"
#include "../lambda/runtime/side_stack.h"
#include "../lambda/runtime/radiant_event_hook.h"
#include "../lib/utf.h"
#include "../lib/str.h"
#include "../lib/url.h"
// str.h included via view.hpp
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/runtime/template_registry.h"
#include "../lambda/runtime/render_map.h"
#include "../lambda/runtime/edit_bridge.h"
#include "../lambda/lambda.h"         // Context (input_context)
#include "../lambda/lambda-data.hpp"  // EvalContext
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/module/radiant/radiant_dom_bridge.hpp"   // Runtime (heap and name_pool)
#include "../lambda/runtime/runtime-state.h"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/io/mark_builder.hpp" // MarkBuilder for event object construction
#include "../lambda/js/js_dom.h"      // js_dom_set_document for HTML event handlers
#include "../lambda/js/js_dom_events.h" // js_dom_dispatch_event + native event factories
#include "../lambda/js/js_runtime.h"   // js_new_object / js_set_key_default / js_array_new / js_array_push
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/js/js_dom_platform.h"
#include "../lambda/js/js_dom_observers.h"

// CE-3 follow-up: DataTransfer factory from js_clipboard.cpp (no public
// header — js_clipboard installs globals through js_dom_set_document). We
// only need the two-string builder for the paste/drop dispatch path.
extern "C" Item js_data_transfer_new_with_strings(const char* text_plain,
                                                  const char* text_html);
extern Item js_make_number(double value);
extern "C" void js_dom_notify_mutation(DomJsMutationKind kind,
                                        void* target, void* parent);
extern "C" void js_dom_notify_mutation_detail(DomJsMutationKind kind,
                                               void* target, void* parent,
                                               const char* attribute_name,
                                               const char* old_value);
extern "C" DomElement* js_dom_find_form_owner(void* control);
extern "C" bool js_dom_is_submit_button(void* dom_elem);
extern "C" bool js_dom_is_reset_button(void* dom_elem);
extern "C" Item js_dom_form_request_submit_bridge(Item form_item, Item submitter_item);
extern "C" bool js_dom_is_disabled(void* dom_elem);
extern "C" bool js_dom_is_connected(void* dom_elem);
extern "C" Item js_dom_scroll_into_view_bridge(void* dom_elem);
#include "../lib/hashmap.h"           // hashmap utilities used by DocState maps
#include "../lib/memtrack.h"          // mem_free
#include <chrono>       // timing for reactive event dispatch
#include <string.h>

// thread-local eval context used by heap allocation functions
extern __thread EvalContext* context;
extern __thread Context* input_context;
extern "C" Item interp_eval_view_handler(Context* context, Script* module,
                                           AstViewNode* view,
                                           AstEventHandler* handler,
                                           Item model, Item event);
DomDocument* show_html_doc(Url *base, char* doc_filename, int viewport_width, int viewport_height);
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);

// MouseButtonEvent::mods has already been normalized by window/event_sim; JS
// MouseEvent stamping must read RDT flags so synthetic and native inputs agree.
static inline bool event_mod_ctrl(int mods) { return (mods & RDT_MOD_CTRL) != 0; }
static inline bool event_mod_shift(int mods) { return (mods & RDT_MOD_SHIFT) != 0; }
static inline bool event_mod_alt(int mods) { return (mods & RDT_MOD_ALT) != 0; }
static inline bool event_mod_super(int mods) { return (mods & RDT_MOD_SUPER) != 0; }
void to_repaint();
void update_window_title(const char* title);
extern "C" void selection_refresh_presentation(DocState* state);
void rebuild_lambda_doc(UiContext* uicon);
void rebuild_lambda_doc_incremental(UiContext* uicon, RetransformResult* results, int result_count);

struct SelectorMatcher* selector_matcher_create(Pool* pool);
static void clear_cascaded_styles_recursive(DomNode* node);
static void mark_layout_dirty_recursive(DomNode* node);
static bool radiant_dispatch_simple_event(EventContext* evcon, View* target,
                                          const char* type,
                                          bool bubbles, bool cancelable);

static bool event_view_pointer_events_none(View* view) {
    for (DomNode* node = static_cast<DomNode*>(view); node;
         node = node->parent) {
        if (!node->is_element()) continue;
        CssEnum value = layout_specified_keyword(
            lam::dom_require_element(node), CSS_PROPERTY_POINTER_EVENTS,
            CSS_VALUE__UNDEF);
        if (value != CSS_VALUE__UNDEF) return value == CSS_VALUE_NONE;
    }
    return false;
}

static bool event_view_is_float(View* view) {
    if (!view || !view->is_element()) return false;
    DomElement* elem = lam::dom_require_element(view);
    CssEnum float_value = elem->position
        ? elem->positionp()->float_prop
        : layout_specified_keyword(elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
    return float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT;
}

// Forward declarations for event targeting
void target_html_doc(EventContext* evcon, ViewTree* view_tree);
void target_block_view(EventContext* evcon, ViewBlock* block);
void target_inline_view(EventContext* evcon, ViewSpan* view_span);
void target_text_view(EventContext* evcon, ViewText* text);
void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event);
void update_focus_state(EventContext* evcon, View* new_focus, bool from_keyboard);

static WebViewHandle* focused_layer_webview_handle(View* focused) {
    // A focused element can become display:none before script restores focus;
    // only block views can carry an embedded layer during that interval.
    if (!focused || !focused->is_element() || !focused->is_block()) return nullptr;
    ViewBlock* block = lam::view_require_block(focused);
    WebViewProp* webview = block->embed ? block->embedp()->webview : nullptr;
    return webview && webview->mode == WEBVIEW_MODE_LAYER ? webview->handle : nullptr;
}

static const char* rdt_event_type_name(EventType type) {
    switch (type) {
    case RDT_EVENT_NIL: return "nil";
    case RDT_EVENT_MOUSE_DOWN: return "mouse_down";
    case RDT_EVENT_MOUSE_UP: return "mouse_up";
    case RDT_EVENT_MOUSE_MOVE: return "mouse_move";
    case RDT_EVENT_MOUSE_DRAG: return "mouse_drag";
    case RDT_EVENT_SCROLL: return "scroll";
    case RDT_EVENT_KEY_DOWN: return "key_down";
    case RDT_EVENT_KEY_UP: return "key_up";
    case RDT_EVENT_TEXT_INPUT: return "text_input";
    case RDT_EVENT_COMPOSITION_START: return "composition_start";
    case RDT_EVENT_COMPOSITION_UPDATE: return "composition_update";
    case RDT_EVENT_COMPOSITION_END: return "composition_end";
    case RDT_EVENT_FOCUS_IN: return "focus_in";
    case RDT_EVENT_FOCUS_OUT: return "focus_out";
    case RDT_EVENT_CLICK: return "click";
    case RDT_EVENT_DBL_CLICK: return "dbl_click";
    default: return "unknown";
    }
}

static void event_log_raw_input(EventStateLog* log, uint64_t cascade_id,
                                const RdtEvent* event) {
    if (!event_state_log_enabled(log) || !event) return;

    char buf[1024];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf), "input.raw", cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "event", rdt_event_type_name(event->type));
        jw_kv_double(&w, "timestamp", event->timestamp);
        switch (event->type) {
        case RDT_EVENT_MOUSE_DOWN:
        case RDT_EVENT_MOUSE_UP:
            jw_kv_double(&w, "x", event->mouse_button.x);
            jw_kv_double(&w, "y", event->mouse_button.y);
            jw_kv_int(&w, "button", event->mouse_button.button);
            jw_kv_int(&w, "clicks", event->mouse_button.clicks);
            jw_kv_int(&w, "mods", event->mouse_button.mods);
            break;
        case RDT_EVENT_MOUSE_MOVE:
        case RDT_EVENT_MOUSE_DRAG:
            jw_kv_double(&w, "x", event->mouse_position.x);
            jw_kv_double(&w, "y", event->mouse_position.y);
            break;
        case RDT_EVENT_SCROLL:
            jw_kv_double(&w, "x", event->scroll.x);
            jw_kv_double(&w, "y", event->scroll.y);
            jw_kv_double(&w, "xoffset", event->scroll.xoffset);
            jw_kv_double(&w, "yoffset", event->scroll.yoffset);
            break;
        case RDT_EVENT_KEY_DOWN:
        case RDT_EVENT_KEY_UP:
            jw_kv_int(&w, "key", event->key.key);
            jw_kv_int(&w, "scancode", event->key.scancode);
            jw_kv_int(&w, "mods", event->key.mods);
            break;
        case RDT_EVENT_TEXT_INPUT:
            jw_kv_uint(&w, "codepoint", event->text_input.codepoint);
            break;
        case RDT_EVENT_COMPOSITION_START:
        case RDT_EVENT_COMPOSITION_UPDATE:
        case RDT_EVENT_COMPOSITION_END:
            jw_kv_str(&w, "text", event->composition.text);
            jw_kv_uint(&w, "preedit_caret", event->composition.preedit_caret);
            break;
        default:
            break;
        }
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}

static void event_log_hit_target(EventStateLog* log, uint64_t cascade_id,
                                 const EventContext* evcon) {
    if (!event_state_log_enabled(log) || !evcon) return;

    char buf[1536];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf), "hit.target", cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        event_state_log_write_node_ref(&w, "target", (const DomNode*)evcon->target);
        jw_kv_double(&w, "offset_x", evcon->offset_x);
        jw_kv_double(&w, "offset_y", evcon->offset_y);
        if (evcon->target_text_offset_valid) {
            jw_kv_int(&w, "text_offset", evcon->target_text_offset);
        }
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}

static void event_log_focused_target(EventStateLog* log, uint64_t cascade_id,
                                     View* target) {
    if (!event_state_log_enabled(log)) return;

    char buf[1024];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf), "hit.target", cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        event_state_log_write_node_ref(&w, "target", (const DomNode*)target);
        jw_kv_str(&w, "source", "focus");
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}

static uint32_t event_log_text_len(const char* text) {
    return text ? (uint32_t)strlen(text) : 0;
}

static bool event_log_editing_redact(const EditingSurface* surface) {
    return surface && surface->mode == EDIT_MODE_PASSWORD_TEXT;
}

static void event_log_editing_surface(JsonWriter* w,
                                      const EditingSurface* surface) {
    jw_key(w, "surface");
    jw_obj_begin(w);
        event_log_write_surface_core_fields(w, surface, false);
    jw_obj_end(w);
}

static bool event_log_begin_editing_record(DocState* state,
                                           const EditingSurface* surface,
                                           const char* type,
                                           JsonWriter* writer,
                                           char* buffer, size_t buffer_size,
                                           bool* redacted) {
    if (!state || !event_state_log_enabled(state->active_event_log)) return false;
    *redacted = event_log_editing_redact(surface);
    event_state_log_begin_record(state->active_event_log, writer,
                                 buffer, buffer_size, type,
                                 state->active_cascade_id);
    jw_key(writer, "data");
    jw_obj_begin(writer);
    return true;
}

static void event_log_editing_history_named(DocState* state,
                                            const EditingSurface* surface,
                                            const char* input_type_name,
                                            const char* action,
                                            uint32_t depth, // UNUSED_DEPTH_OK: undo-stack depth logged as a JSON field.
                                            uint32_t cursor,
                                            bool did_restore) {
    bool redacted = false;
    char buf[4096];
    JsonWriter w;
    if (!event_log_begin_editing_record(state, surface, "editing.history",
                                        &w, buf, sizeof(buf), &redacted)) return;
        jw_kv_str(&w, "action", action ? action : "restore");
        event_log_editing_surface(&w, surface);
        jw_kv_str(&w, "input_type", input_type_name ? input_type_name : "");
        jw_kv_uint(&w, "depth", redacted ? 0 : depth);
        jw_kv_uint(&w, "cursor", redacted ? 0 : cursor);
        jw_kv_str(&w, "owned_by",
                  editing_surface_is_text_control(surface) ? "radiant" : "consumer");
        jw_kv_bool(&w, "owned_by_form", editing_surface_is_text_control(surface));
        jw_kv_bool(&w, "restored", did_restore);
        jw_kv_bool(&w, "redacted", redacted);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void event_log_editing_history(DocState* state,
                                      const EditingSurface* surface,
                                      const InputIntent* intent,
                                      const char* action,
                                      uint32_t depth, // UNUSED_DEPTH_OK: forwarded to the JSON record below.
                                      uint32_t cursor,
                                      bool did_restore) {
    event_log_editing_history_named(state, surface,
        intent ? input_intent_type_name(intent->type) : "",
        action, depth, cursor, did_restore);
}

extern "C" void radiant_text_edit_history_notify(DomElement* elem,
                                                 const char* action,
                                                 const char* input_type,
                                                 uint32_t depth, // UNUSED_DEPTH_OK: undo-stack depth (output data field via the log).
                                                 uint32_t cursor) {
    if (!elem || !tc_is_text_control(elem)) return;
    FormControlProp* form = elem->form;
    DocState* state = form && form->state_ref
        ? form->state_ref
        : (elem->doc ? (DocState*)elem->doc->state : nullptr);
    if (!state || !event_state_log_enabled(state->active_event_log)) return;

    EditingSurface surface;
    if (!editing_surface_from_target(static_cast<View*>(elem), &surface) ||
        !editing_surface_is_text_control(&surface)) {
        return;
    }
    event_log_editing_history_named(state, &surface, input_type,
                                    action ? action : "push",
                                    depth, cursor, false);
}

static void event_log_editing_mutation(DocState* state,
                                       const EditingSurface* surface,
                                       const InputIntent* intent,
                                       const char* operation,
                                       uint32_t old_len,
                                       uint32_t new_len,
                                       uint32_t selection_start,
                                       uint32_t selection_end) {
    bool redacted = false;
    char buf[4096];
    JsonWriter w;
    if (!event_log_begin_editing_record(state, surface, "editing.mutation",
                                        &w, buf, sizeof(buf), &redacted)) return;
        jw_kv_str(&w, "operation", operation ? operation : "replace");
        event_log_editing_surface(&w, surface);
        jw_kv_str(&w, "input_type",
                  intent ? input_intent_type_name(intent->type) : "");
        jw_kv_uint(&w, "old_len", redacted ? 0 : old_len);
        jw_kv_uint(&w, "new_len", redacted ? 0 : new_len);
        jw_kv_uint(&w, "selection_start", redacted ? 0 : selection_start);
        jw_kv_uint(&w, "selection_end", redacted ? 0 : selection_end);
        jw_kv_bool(&w, "redacted", redacted);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void event_log_editing_selection(DocState* state,
                                        const EditingSurface* surface,
                                        const InputIntent* intent,
                                        const char* operation,
                                        uint32_t anchor,
                                        uint32_t focus) {
    bool redacted = false;
    char buf[4096];
    JsonWriter w;
    if (!event_log_begin_editing_record(state, surface, "editing.selection",
                                        &w, buf, sizeof(buf), &redacted)) return;
        jw_kv_str(&w, "operation", operation ? operation : "select");
        event_log_editing_surface(&w, surface);
        jw_kv_str(&w, "input_type",
                  intent ? input_intent_type_name(intent->type) : "");
        jw_kv_uint(&w, "anchor", redacted ? 0 : anchor);
        jw_kv_uint(&w, "focus", redacted ? 0 : focus);
        jw_kv_bool(&w, "redacted", redacted);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void event_log_editing_clipboard(DocState* state,
                                        const EditingSurface* surface,
                                        const char* operation,
                                        uint32_t text_len,
                                        uint32_t html_len) {
    bool redacted = false;
    char buf[2048];
    JsonWriter w;
    if (!event_log_begin_editing_record(state, surface, "editing.clipboard",
                                        &w, buf, sizeof(buf), &redacted)) return;
        jw_kv_str(&w, "operation", operation ? operation : "");
        event_log_editing_surface(&w, surface);
        jw_kv_uint(&w, "text_len", redacted ? 0 : text_len);
        jw_kv_uint(&w, "html_len", redacted ? 0 : html_len);
        jw_kv_bool(&w, "redacted", redacted);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static bool input_intent_has_clipboard_payload(const InputIntent* intent) {
    if (!intent) return false;
    switch (intent->type) {
        case INPUT_INTENT_INSERT_FROM_PASTE:
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION:
        case INPUT_INTENT_INSERT_FROM_DROP:
            return true;
        default:
            return false;
    }
}

static void event_log_editing_clipboard_intent(DocState* state,
                                               const EditingSurface* surface,
                                               const InputIntent* intent,
                                               const char* operation) {
    if (!input_intent_has_clipboard_payload(intent)) return;
    const char* op = operation;
    if (!op) {
        op = intent->type == INPUT_INTENT_INSERT_FROM_DROP ? "drop" : "paste";
    }
    event_log_editing_clipboard(state, surface,
                                op,
                                event_log_text_len(intent->data),
                                event_log_text_len(intent->html_data));
}

static void event_log_editing_composition(DocState* state,
                                          const EditingSurface* surface,
                                          const InputIntent* intent,
                                          const char* phase,
                                          uint32_t preedit_len,
                                          uint32_t commit_len,
                                          uint32_t caret) {
    bool redacted = false;
    char buf[4096];
    JsonWriter w;
    if (!event_log_begin_editing_record(state, surface, "editing.composition",
                                        &w, buf, sizeof(buf), &redacted)) return;
        jw_kv_str(&w, "phase", phase ? phase : "");
        event_log_editing_surface(&w, surface);
        jw_kv_str(&w, "input_type",
                  intent ? input_intent_type_name(intent->type) : "");
        jw_kv_uint(&w, "preedit_len", redacted ? 0 : preedit_len);
        jw_kv_uint(&w, "commit_len", redacted ? 0 : commit_len);
        jw_kv_uint(&w, "caret", redacted ? 0 : caret);
        jw_kv_bool(&w, "is_composing", intent ? intent->is_composing : false);
        jw_kv_bool(&w, "redacted", redacted);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static bool dispatch_form_selection_extend(EventContext* evcon,
                                           DomElement* elem,
                                           DocState* state,
                                           View* target,
                                           int anchor_offset,
                                           int focus_offset,
                                           const char* operation);
static bool dispatch_rich_selection_snapshot(EventContext* evcon,
                                             DocState* state,
                                             View* target,
                                             const char* operation,
                                             const InputIntent* intent);
static bool dispatch_editing_history_for_controller(EventContext* evcon,
                                                    const EditingSurface* surface,
                                                    InputIntentType input_type,
                                                    void* userdata);
static bool dispatch_editing_composition_for_controller(EventContext* evcon,
                                                        const EditingSurface* surface,
                                                        const CompositionEvent* comp_event,
                                                        const EditingIntent* intent,
                                                        void* userdata);

static void event_log_editing_autoscroll(DocState* state,
                                         const EditingSurface* surface,
                                         const char* operation,
                                         float dx,
                                         float dy,
                                         float velocity_x,
                                         float velocity_y) {
    if (!state || !event_state_log_enabled(state->active_event_log)) return;

    char buf[4096];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
        "editing.autoscroll", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "operation", operation ? operation : "tick");
        event_log_editing_surface(&w, surface);
        jw_kv_double(&w, "dx", dx);
        jw_kv_double(&w, "dy", dy);
        jw_kv_double(&w, "velocity_x", velocity_x);
        jw_kv_double(&w, "velocity_y", velocity_y);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static bool dispatch_form_selection_extend_for_controller(
        EventContext* evcon,
        DomElement* elem,
        DocState* state,
        View* target,
        int anchor_offset,
        int focus_offset,
        const char* operation,
        void* userdata) {
    (void)userdata;
    return dispatch_form_selection_extend(evcon, elem, state, target,
                                          anchor_offset, focus_offset,
                                          operation);
}

static void dispatch_rich_selection_snapshot_for_controller(
        EventContext* evcon,
        DocState* state,
        View* target,
        const char* operation,
        const EditingIntent* intent,
        void* userdata) {
    (void)userdata;
    dispatch_rich_selection_snapshot(evcon, state, target, operation, intent);
}

static void event_log_editing_autoscroll_for_controller(
        DocState* state,
        const EditingSurface* surface,
        const char* operation,
        float dx,
        float dy,
        float velocity_x,
        float velocity_y,
        void* userdata) {
    (void)userdata;
    event_log_editing_autoscroll(state, surface, operation, dx, dy,
                                 velocity_x, velocity_y);
}

static EditingControllerHooks editing_controller_hooks() {
    EditingControllerHooks hooks;
    hooks.selection_snapshot = dispatch_rich_selection_snapshot_for_controller;
    hooks.form_selection_extend = dispatch_form_selection_extend_for_controller;
    hooks.autoscroll_log = event_log_editing_autoscroll_for_controller;
    hooks.history_dispatch = dispatch_editing_history_for_controller;
    hooks.composition_dispatch = dispatch_editing_composition_for_controller;
    hooks.user = nullptr;
    return hooks;
}

static bool sync_viewport_scroll_state(EventContext* evcon) {
    if (!evcon || !evcon->ui_context) return false;

    DomDocument* doc = evcon->target_document
        ? evcon->target_document
        : evcon->ui_context->document;
    DocState* state = (DocState*)doc->state;
    if (!state || !doc->view_tree || !doc->view_tree->root ||
        doc->view_tree->root->view_type != RDT_VIEW_BLOCK) {
        return false;
    }

    ViewBlock* root_block = lam::view_require_block(doc->view_tree->root);
    if (!root_block->scroller || !root_block->scroll()->pane) return false;

    float scroll_x = 0.0f, scroll_y = 0.0f;
    scroll_state_get_position_for_view(state, static_cast<View*>(root_block), root_block->scroll()->pane,
                                       &scroll_x, &scroll_y, NULL, NULL);

    // Keep viewport scroll in the centralized state store and the document
    // reflow target so incremental relayout does not snap back to top.
    bool changed = scroll_x != state->scroll_x || scroll_y != state->scroll_y;
    doc_state_sync_viewport_scroll(state, doc, scroll_x, scroll_y);
    return changed;
}

static DocState* event_context_target_state(EventContext* evcon) {
    if (!evcon) return NULL;
    DomDocument* target_doc = evcon->target_document
        ? evcon->target_document
        : (evcon->ui_context ? evcon->ui_context->document : NULL);
    return target_doc ? target_doc->state : NULL;
}

static DomDocument* event_context_target_document(EventContext* evcon) {
    if (!evcon) return NULL;
    return evcon->target_document
        ? evcon->target_document
        : (evcon->ui_context ? evcon->ui_context->document : NULL);
}

static void restore_embedded_document_scroll_model(DomDocument* doc) {
    if (!doc || !doc->view_tree || !doc->view_tree->root ||
        doc->view_tree->root->view_type != RDT_VIEW_BLOCK) {
        return;
    }

    ViewBlock* root = lam::view_require_block(doc->view_tree->root);
    if (!root->scroller) return;

    if (root->content_height > root->height) {
        root->height = root->content_height;
    }
    root->scroller = NULL;
}

static void layout_event_document_reflow(EventContext* evcon, DomDocument* doc,
                                         View* iframe_container) {
    if (!evcon || !evcon->ui_context || !doc) return;

    UiContext* uicon = evcon->ui_context;
    DomDocument* saved_doc = uicon->document;
    int saved_viewport_width = uicon->viewport_width;
    int saved_viewport_height = uicon->viewport_height;

    uicon->document = doc;

    if (iframe_container &&
        (iframe_container->view_type == RDT_VIEW_BLOCK ||
         iframe_container->view_type == RDT_VIEW_INLINE_BLOCK)) {
        ViewBlock* block = lam::view_require_block(iframe_container);
        if (block->width > 0) {
            uicon->viewport_width = (int)block->width; // INT_CAST_OK: UiContext viewport dimensions are integer CSS pixels.
        }
        if (block->height > 0) {
            uicon->viewport_height = (int)block->height; // INT_CAST_OK: UiContext viewport dimensions are integer CSS pixels.
        }
    }

    layout_html_doc(uicon, doc, true);
    if (iframe_container) {
        restore_embedded_document_scroll_model(doc);
    }

    uicon->document = saved_doc;
    uicon->viewport_width = saved_viewport_width;
    uicon->viewport_height = saved_viewport_height;
}

static bool process_event_target_document_reflow(EventContext* evcon) {
    if (!evcon || !evcon->ui_context || !evcon->target_document ||
        evcon->target_document == evcon->ui_context->document) {
        return false;
    }

    DomDocument* doc = evcon->target_document;
    DocState* state = (DocState*)doc->state;
    if (!state || !state->needs_reflow) return false;

    log_debug("Processing pending iframe reflow before parent repaint");
    reflow_process_pending(state);

    if (!state->needs_reflow) return false;

    layout_event_document_reflow(evcon, doc, evcon->iframe_container);
    doc_state_clear_reflow(state);
    reflow_clear(state);
    doc_state_mark_dirty(state);
    doc_state_request_repaint(state);
    return true;
}

static void clear_document_interaction_state_before_detach(DomDocument* doc) {
    if (!doc || !doc->state) return;

    DocState* state = doc->state;
    log_debug("[IFRAME_DETACH_STATE] clearing transient interaction state for %p", (void*)doc);

    focus_clear(state);
    state_store_caret_clear(state);
    state_store_selection_clear(state);
    selection_press_in_range_clear(state);
    editing_interaction_clear_autoscroll(state);
    editing_interaction_set_active_surface(state, NULL);
    doc_state_set_hover_target(state, NULL);
    doc_state_set_active_target(state, NULL);
    doc_state_set_drag_state(state, NULL, false);
    doc_state_clear_drag_drop(state);

    doc_state_close_dropdown(state, NULL);
    doc_state_close_context_menu(state);
    state->active_text_control = NULL;
    state->last_focused_text_control = NULL;
}

static DocState* event_view_owner_state(View* view) {
    if (!view || !view->is_element()) return NULL;
    DomElement* elem = lam::dom_require_element(view);
    return elem && elem->doc ? (DocState*)elem->doc->state : NULL;
}

static DomDocument* event_context_find_focused_document(DomDocument* doc,
                                                        uint8_t depth,
                                                        View** iframe_container);

static DomDocument* event_context_find_focused_document_in_view(View* view,
                                                                uint8_t depth,
                                                                View** iframe_container) {
    if (!view || depth > 8) return NULL;
    if ((view->view_type == RDT_VIEW_BLOCK ||
         view->view_type == RDT_VIEW_INLINE_BLOCK ||
         view->view_type == RDT_VIEW_LIST_ITEM) &&
        view->is_element()) {
        ViewBlock* block = lam::view_require_block(view);
        if (block->embed && block->embedp()->doc) {
            DomDocument* found = event_context_find_focused_document(
                block->embedp()->doc, (uint8_t)(depth + 1), iframe_container);
            if (found) {
                // Keyboard events need the direct iframe viewport to reflow
                // the focused document at its embedded size rather than the
                // top-level viewport.
                if (iframe_container && !*iframe_container) {
                    *iframe_container = static_cast<View*>(block);
                }
                return found;
            }
        }
    }
    if (!view->is_element()) return NULL;
    DomElement* elem = lam::dom_require_element(view);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        DomDocument* found = event_context_find_focused_document_in_view(
            static_cast<View*>(child), depth, iframe_container);
        if (found) return found;
    }
    return NULL;
}

static DomDocument* event_context_find_focused_document(DomDocument* doc,
                                                        uint8_t depth,
                                                        View** iframe_container) {
    if (!doc) return NULL;
    DocState* state = doc->state;
    if (state && focus_get(state)) return doc;
    if (!doc->view_tree || !doc->view_tree->root) return NULL;
    return event_context_find_focused_document_in_view(doc->view_tree->root,
                                                       depth, iframe_container);
}

static Item call_template_event_handler(TemplateHandlerEntry* entry,
                                        Item model_item, Item event_item) {
    if (!context) {
        log_error("template event: no bound EvalContext");
        return ItemError;
    }
    if (entry && entry->interp_handler) {
        return interp_eval_view_handler((Context*)context, entry->interp_module,
            entry->interp_view, entry->interp_handler, model_item, event_item);
    }
    typedef Item (*TemplateEventHandlerFn)(Context*, Item, Item);
    union {
        fn_ptr raw;
        TemplateEventHandlerFn typed;
    } handler;
    // template_registry stores generated handlers as erased fn_ptr; event
    // handlers receive the host-bound canonical context explicitly.
    handler.raw = entry ? entry->handler_func : NULL;
    return handler.typed((Context*)context, model_item, event_item);
}

static bool pdf_text_run_metrics(ViewText* text, float* out_width, bool* out_copy_space) {
    if (out_width) *out_width = 0.0f;
    if (out_copy_space) *out_copy_space = false;
    if (!text || !text->parent || !text->parent->is_element()) return false;

    DomElement* elem = lam::dom_require_element(text->parent);
    const char* cls = elem->get_attribute("class");
    if (!cls || !strstr(cls, "pdf-text-run")) return false;

    const char* width_attr = elem->get_attribute("data-pdf-width");
    float width = width_attr ? (float)str_to_double_default(width_attr, strlen(width_attr), 0.0) : 0.0f;
    if (width <= 0.0f) return false;

    const char* copy_attr = elem->get_attribute("data-pdf-copy-space");
    if (out_width) *out_width = width;
    if (out_copy_space) *out_copy_space = copy_attr && strcmp(copy_attr, "1") == 0;
    return true;
}

static int pdf_visible_end_offset(ViewText* text, TextRect* rect, bool copy_space) {
    int end_offset = rect ? rect->start_index + max(rect->length, 0) : 0;
    if (!copy_space || !text || !rect || end_offset <= rect->start_index) return end_offset;
    unsigned char* data = text->text_data();
    if (data && data[end_offset - 1] == ' ') return end_offset - 1;
    return end_offset;
}

static float pdf_text_run_visible_natural_width(FontBox* font, TextRect* rect, bool copy_space) {
    float width = rect ? rect->width : 0.0f;
    if (copy_space && font && font->style) {
        width -= font->style->space_width;
    }
    return width > 0.0f ? width : (rect ? rect->width : 0.0f);
}

static float pdf_text_run_visible_natural_width(EventContext* evcon, TextRect* rect, bool copy_space) {
    return pdf_text_run_visible_natural_width(evcon ? &evcon->font : NULL, rect, copy_space);
}

static void target_stacking_view(EventContext* evcon, View* view) {
    if (!evcon || !view || evcon->target) return;

    if (view->is_block()) {
        target_block_view(evcon, lam::view_require_block(view));
    } else if (view->view_type == RDT_VIEW_INLINE) {
        target_inline_view(evcon, lam::view_require_element(view));
    } else if (view->view_type == RDT_VIEW_TEXT) {
        target_text_view(evcon, lam::view_require_text(view));
    }
}

static void target_stacking_list_reverse(EventContext* evcon, ArrayList* views) {
    if (!evcon || !views) return;

    // Hit-testing consumes the same stable paint-order list in reverse so equal
    // z-index siblings target the later painted node instead of drifting from render.
    for (int i = views->length - 1; i >= 0 && !evcon->target; i--) {
        target_stacking_view(evcon, (View*)views->data[i]);
    }
}

static void target_positive_z_descendants(EventContext* evcon, View* first_child) {
    ArrayList* views = radiant_stack_collect_positive_z_descendants(
        first_child, "[RAD_CAP_POSITIONED_HIT]");
    if (!views) return;

    radiant_stack_sort_in_paint_order(views);
    target_stacking_list_reverse(evcon, views);
    arraylist_free(views);
}

static void target_positioned_children(EventContext* evcon, ViewBlock* block) {
    ArrayList* views = radiant_stack_collect_positioned_children(
        block, "[RAD_CAP_POSITIONED_HIT]");
    if (!views) return;

    radiant_stack_sort_in_paint_order(views);
    target_stacking_list_reverse(evcon, views);
    arraylist_free(views);
}

static void target_custom_layout_children(EventContext* evcon, ViewBlock* block) {
    if (!evcon || !block || !block->custom_layout_paint_prop()) return;
    RadiantStackPaintList paint = radiant_stack_collect_custom_layout_paint(block);
    // Generated SVG layers are non-interactive, but their position in this
    // sequence determines which authored child is visually topmost.
    for (int i = paint.count - 1; i >= 0 && !evcon->target; i--) {
        if (!paint.entries[i].is_generated_layer && paint.entries[i].view) {
            target_stacking_view(evcon, paint.entries[i].view);
        }
    }
    radiant_stack_free_custom_layout_paint(&paint);
}

void target_children(EventContext* evcon, View* view) {
    if (!evcon || !view) return;

    bool has_float_child = false;
    for (View* child = view; child; child = child->next()) {
        if (event_view_is_float(child)) {
            has_float_child = true;
            break;
        }
    }

    View* last = view;
    if (has_float_child) {
        while (last->next()) last = last->next();
    }

    // floating siblings can overlap after shrink-to-fit; later floats paint on
    // top of earlier ones, so their normal-flow hit order must be reversed.
    for (View* child = has_float_child ? last : view;
         child && !evcon->target;
         child = has_float_child
             ? (child == view ? nullptr : static_cast<View*>(child->prev_sibling))
             : child->next()) {
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            if (radiant_stack_is_deferred_from_normal_flow(child)) {
                // skip deferred stacking entries; target_block_view walks them in reverse paint order.
            } else {
                target_block_view(evcon, block);
            }
        }
        else if (child->view_type == RDT_VIEW_INLINE) {
            if (radiant_stack_is_deferred_from_normal_flow(child)) {
                continue;
            }
            ViewSpan* span = lam::view_require_element(child);
            target_inline_view(evcon, span);
        }
        else if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(child);
            target_text_view(evcon, text);
        }
    }
}

void target_text_view(EventContext* evcon, ViewText* text) {
    if (event_view_pointer_events_none(static_cast<View*>(text))) return;
    unsigned char* str = text->text_data();
    TextRect *text_rect = text->rect;
    MousePositionEvent* event = &evcon->event.mouse_position;

    NEXT_RECT:
    float x = evcon->block.x + text_rect->x, y = evcon->block.y + text_rect->y;
    float pdf_width = 0.0f;
    bool pdf_copy_space = false;
    float rect_width = text_rect->width;
    if (pdf_text_run_metrics(text, &pdf_width, &pdf_copy_space)) {
        rect_width = pdf_width;
    }
    float rect_right = x + rect_width;
    float rect_bottom = y + text_rect->height;

    log_debug("target text:'%t' start:%d, len:%d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f, blk_x:%.1f",
        str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, evcon->block.x);

    // First check if mouse is within the text rect bounds (use rect height, not char height)
    if (x <= event->x && event->x < rect_right && y <= event->y && event->y < rect_bottom) {
        // Mouse is in this text rect - set target and return
        log_debug("hit on text rect at (%.1f, %.1f)", event->x, event->y);
        evcon->target = text;
        evcon->target_text_rect = text_rect;
        return;
    }

    assert(text_rect->next != text_rect);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
}

typedef struct EditableMarginTextHit {
    ViewText* text;
    TextRect* rect;
    int offset;
    float block_x;
    float block_y;
    float score;
} EditableMarginTextHit;

static bool is_in_rich_editable_subtree(View* view) {
    EditingSurface surface;
    return editing_surface_from_target(view, &surface) &&
        editing_surface_is_rich(&surface);
}

static bool is_rich_editable_host(View* view) {
    if (!view || !view->is_element()) return false;
    DomElement* elem = lam::dom_require_element(view);
    EditingSurface surface;
    if (!editing_surface_from_target(view, &surface)) return false;
    return editing_surface_is_rich(&surface) && surface.owner == elem;
}

static bool text_target_allows_caret(View* target) {
    if (!target) return false;
    DomNode* node = static_cast<DomNode*>(target);
    // Bottom-up: a disabled form control found *before* an editable host
    // forbids caret; an editable host found first allows it. Preserves the
    // legacy walk semantics — see commit history before CE-1.
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            DomElement* elem = lam::dom_require_element(node);
            if (elem->form_control() && form_control_is_disabled(elem->doc ? elem->doc->state : NULL, static_cast<View*>(elem))) {
                return false;
            }
            EditingHost h;
            if (editing_host_lookup(elem, &h) && h.host == elem) return true;
        }
        node = node->parent;
    }
    return true;
}

static bool target_inside_click_control(View* target) {
    DomNode* node = static_cast<DomNode*>(target);
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            DomElement* elem = lam::dom_require_element(node);
            switch (elem->tag()) {
                case MARKUP_NAME_A:
                case MARKUP_NAME_BUTTON:
                case MARKUP_NAME_INPUT:
                case MARKUP_NAME_SELECT:
                case MARKUP_NAME_TEXTAREA:
                    return true;
                default:
                    break;
            }
        }
        node = node->parent;
    }
    return false;
}

static bool mouseup_target_can_finish_text_selection(EventContext* evcon) {
    if (!evcon || !evcon->target) return false;
    View* target = evcon->target;

    // A preserved editor selection must not make toolbar/link/button clicks
    // look like text-selection drags. Only suppress click dispatch when the
    // mouseup actually lands on selectable text/editing surfaces.
    if (target_inside_click_control(target)) {
        return target->is_element() &&
            tc_is_text_control(lam::dom_require_element(target));
    }

    if (target->view_type == RDT_VIEW_TEXT) {
        return text_target_allows_caret(target);
    }

    if (target->is_element()) {
        return is_rich_editable_host(target);
    }

    return false;
}

static bool event_inside_block(EventContext* evcon, ViewBlock* block) {
    if (!evcon || !block) return false;
    MousePositionEvent* event = &evcon->event.mouse_position;
    return evcon->block.x <= event->x && event->x < evcon->block.x + block->width &&
           evcon->block.y <= event->y && event->y < evcon->block.y + block->height;
}

static void find_editable_margin_text_hit(EventContext* evcon, View* view,
                                          float block_x, float block_y,
                                          EditableMarginTextHit* hit,
                                          bool include_vertical_gap) {
    if (!evcon || !view || !hit) return;

    MousePositionEvent* event = &evcon->event.mouse_position;

    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = lam::view_require_text(view);
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            if (rect->height <= 0) continue;
            float rect_x = block_x + rect->x;
            float rect_y = block_y + rect->y;
            float rect_right = rect_x + rect->width;
            float rect_bottom = rect_y + rect->height;

            float score = -1.0f;
            int offset = rect->start_index;
            if (rect_y <= event->y && event->y < rect_bottom && event->x >= rect_right) {
                score = event->x - rect_right;
                offset = rect->start_index + max(rect->length, 0);
            } else if (include_vertical_gap && event->y >= rect_bottom) {
                score = (event->y - rect_bottom) + 10000.0f;
                offset = rect->start_index + max(rect->length, 0);
            } else if (include_vertical_gap && event->y < rect_y) {
                score = (rect_y - event->y) + 20000.0f;
                offset = rect->start_index;
            }

            if (score >= 0.0f && (!hit->text || score < hit->score)) {
                hit->text = text;
                hit->rect = rect;
                hit->offset = offset;
                hit->block_x = block_x;
                hit->block_y = block_y;
                hit->score = score;
            }
        }
        return;
    }

    if (!view->is_element()) return;

    float child_block_x = block_x;
    float child_block_y = block_y;
    if (view->view_type == RDT_VIEW_BLOCK ||
        view->view_type == RDT_VIEW_INLINE_BLOCK ||
        view->view_type == RDT_VIEW_LIST_ITEM) {
        child_block_x += view->x;
        child_block_y += view->y;
    }

    DomElement* elem = lam::dom_require_element(view);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        View* child_view = static_cast<View*>(child);
        if (!child_view->view_type) continue;
        find_editable_margin_text_hit(evcon, child_view, child_block_x, child_block_y, hit,
                          include_vertical_gap);
    }
}

void target_inline_view(EventContext* evcon, ViewSpan* view_span) {
    log_enter();
    FontBox pa_font = evcon->font;
    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(evcon->ui_context, &evcon->font, view_span->font);
        }
        target_children(evcon, view);
    }
    evcon->font = pa_font;
    log_leave();
}

// ESO47: map the hit point into a transformed block's own space.
//
// Hit-testing walks layout space and compares against untransformed boxes, so a
// element moved by `transform` was tested where it was laid out rather than
// where it is painted — CSS says transforms affect hit-testing, and a user
// clicks what they see. Rather than touch all ~38 comparison sites, the point
// itself is mapped once on entering a transformed subtree: every site reads
// evcon->event.mouse_position, so they all follow.
//
// Deliberately limited to pure translations. Their inverse is exact, and they
// commute, so composing them down a nesting chain needs no ordering argument —
// which a general inverse would, since the matrices are built in absolute space
// with absolute origins. Scale and rotation still hit-test untransformed; that
// is the same behaviour as before this change, not a new gap.
static bool event_translate_only_transform(View* view, float* out_dx, float* out_dy) {
    RdtMatrix m;
    if (!view || !view->is_block()) return false;
    if (!view_get_transform_matrix(view, &m)) return false;
    const float eps = 1e-4f;
    if (fabsf(m.e11 - 1.0f) > eps || fabsf(m.e22 - 1.0f) > eps ||
        fabsf(m.e12) > eps || fabsf(m.e21) > eps) {
        return false;   // not a pure translation
    }
    *out_dx = m.e13;
    *out_dy = m.e23;
    return true;
}

void target_block_view(EventContext* evcon, ViewBlock* block) {
    log_enter();
    BlockBlot pa_block = evcon->block;  FontBox pa_font = evcon->font;
    // Undo this block's translation for the duration of the subtree walk.
    float tdx = 0.0f, tdy = 0.0f;
    bool translated = event_translate_only_transform(static_cast<View*>(block), &tdx, &tdy);
    if (translated) {
        evcon->event.mouse_position.x -= tdx;
        evcon->event.mouse_position.y -= tdy;
    }
    evcon->block.x = pa_block.x + block->x;  evcon->block.y = pa_block.y + block->y;
    MousePositionEvent* event = &evcon->event.mouse_position;
    bool pointer_events_none = event_view_pointer_events_none(
        static_cast<View*>(block));
    // target the scrollbars first
    View* view = NULL;
    bool hover = false;
    if (!pointer_events_none && block->scroller && block->scroll_mut()->pane) {
        hover = scrollpane_target(evcon, block);
        if (hover) {
            log_debug("hit on block scroll: %s", block->node_name());
            evcon->target = static_cast<View*>(block);
            evcon->offset_x = event->x - evcon->block.x;
            evcon->offset_y = event->y - evcon->block.y;
            goto RETURN;
        }
        else {
            log_debug("hit not on block scroll");
        }
        // setup scrolling offset
        DocState* state = event_view_owner_state(static_cast<View*>(block));
        if (!state) state = event_context_target_state(evcon);
        float scroll_x = 0.0f, scroll_y = 0.0f;
        // overflow-visible boxes may allocate a pane to record intrinsic overflow,
        // but that pane does not establish a scrolling coordinate space for hits.
        if (block->scroll()->has_hz_scroll || block->scroll()->has_vt_scroll) {
            scroll_state_get_position_for_view(state, static_cast<View*>(block), block->scroll()->pane,
                                               &scroll_x, &scroll_y, NULL, NULL);
        }
        evcon->block.x -= scroll_x;
        evcon->block.y -= scroll_y;
    }

    // Check if this block is a child-window webview — stop hit-testing here.
    // In child-window mode, the OS delivers events directly to the native web view.
    // Radiant should not process events that land inside the webview area.
    if (!pointer_events_none && block->embed && block->embedp()->webview &&
        block->embedp()->webview->mode == WEBVIEW_MODE_WINDOW) {
        float bx = evcon->block.x, by = evcon->block.y;
        MousePositionEvent* mev = &evcon->event.mouse_position;
        if (bx <= mev->x && mev->x < bx + block->width &&
            by <= mev->y && mev->y < by + block->height) {
            log_debug("hit on webview (child-window mode), stopping: %s", block->node_name());
            evcon->target = static_cast<View*>(block);
            evcon->offset_x = mev->x - bx;
            evcon->offset_y = mev->y - by;
            goto RETURN;
        }
    }

    if (block->font) {
        setup_font(evcon->ui_context, &evcon->font, block->font);
    }

    // Positioned content paints after a custom layout's local signed-z sequence.
    // Hit testing must consume those same layers in exact reverse paint order.
    if (block->custom_layout_paint_prop()) {
        target_positioned_children(evcon, block);
        if (evcon->target) goto RETURN;
        target_custom_layout_children(evcon, block);
        if (evcon->target) goto RETURN;
    } else {
        // Positioned content paints above the block's embedded self-content; walking
        // it first prevents iframe/webview hit targets from stealing covered clicks.
        target_positive_z_descendants(evcon, block->first_child);
        if (evcon->target) goto RETURN;
        target_positioned_children(evcon, block);
        if (evcon->target) goto RETURN;
    }

    // Layer-mode webview: Radiant owns events but forwards them to the offscreen web view.
    // Set target to the webview block and inject the mouse event.
    if (!pointer_events_none && block->embed && block->embedp()->webview &&
        block->embedp()->webview->mode == WEBVIEW_MODE_LAYER &&
        block->embedp()->webview->handle) {
        float bx = evcon->block.x, by = evcon->block.y;
        MousePositionEvent* mev = &evcon->event.mouse_position;
        if (bx <= mev->x && mev->x < bx + block->width &&
            by <= mev->y && mev->y < by + block->height) {
            log_debug("hit on webview (layer mode), forwarding event: %s", block->node_name());
            evcon->target = static_cast<View*>(block);
            evcon->offset_x = mev->x - bx;
            evcon->offset_y = mev->y - by;

            // translate to webview-local coordinates and inject
            float local_x = mev->x - bx;
            float local_y = mev->y - by;
            // mouse type: 2=mousemove for hover, 3=click for press (injected on actual click)
            webview_layer_platform_inject_mouse(block->embedp()->webview->handle,
                2, local_x, local_y, 0, 0);
            goto RETURN;
        }
    }

    // Check if this block contains an embedded iframe document
    // If so, target into the iframe's document instead of treating it as a normal block
    if (!pointer_events_none && block->embed && block->embedp()->doc) {
        DomDocument* iframe_doc = block->embedp()->doc;
        if (iframe_doc->view_tree && iframe_doc->view_tree->root) {
            log_debug("targeting into iframe embedded document: %s", block->node_name());

            // Save current state
            View* prev_target = evcon->target;
            DomDocument* prev_target_document = evcon->target_document;

            // Target into the embedded document's view tree
            // The coordinate system is already set up correctly (evcon->block.x/y)
            // since we added block->x and block->y above
            evcon->target_document = iframe_doc;
            target_html_doc(evcon, iframe_doc->view_tree);

            // If we found a target inside the iframe, we're done
            if (evcon->target && evcon->target != prev_target) {
                log_debug("found target inside iframe: %s",
                    evcon->target->is_element() ? (lam::view_require_element(evcon->target))->node_name() : "text");
                // Record the iframe block so events can propagate across
                // the iframe boundary back into the parent document
                evcon->iframe_container = static_cast<View*>(block);
                goto RETURN;
            }

            evcon->target_document = prev_target_document;
            log_debug("no target found inside iframe, will target iframe block itself");
        }
    }

    // target static positioned children
    view = block->custom_layout_paint_prop() ? nullptr : block->first_child;
    if (view) {
        target_children(evcon, view);
        bool rich_host_margin_hit_allowed = event_inside_block(evcon, block);
        bool rich_host = is_rich_editable_host(static_cast<View*>(block));
        if (!rich_host_margin_hit_allowed && rich_host) {
            bool event_inside_later_sibling = false;
            for (View* sibling = static_cast<View*>(block)->next_sibling;
                 sibling; sibling = sibling->next_sibling) {
                if (sibling->view_type != RDT_VIEW_BLOCK &&
                    sibling->view_type != RDT_VIEW_INLINE_BLOCK &&
                    sibling->view_type != RDT_VIEW_LIST_ITEM) {
                    continue;
                }
                ViewBlock* sibling_block = lam::view_require_block(sibling);
                float sibling_x = pa_block.x + sibling_block->x;
                float sibling_y = pa_block.y + sibling_block->y;
                if (sibling_x <= event->x && event->x < sibling_x + sibling_block->width &&
                    sibling_y <= event->y && event->y < sibling_y + sibling_block->height) {
                    event_inside_later_sibling = true;
                    break;
                }
            }
            rich_host_margin_hit_allowed = !event_inside_later_sibling;
        }
        if (!evcon->target && is_in_rich_editable_subtree(static_cast<View*>(block)) &&
            rich_host_margin_hit_allowed) {
            EditableMarginTextHit margin_hit = { NULL, NULL, 0, 0.0f, 0.0f, -1.0f };
            bool include_vertical_gap = rich_host;
            for (View* child = view; child; child = child->next()) {
                if (!child->view_type) continue;
                find_editable_margin_text_hit(evcon, child, evcon->block.x, evcon->block.y,
                                              &margin_hit, include_vertical_gap);
            }
            if (margin_hit.text && margin_hit.rect) {
                evcon->target = static_cast<View*>(margin_hit.text);
                evcon->target_text_rect = margin_hit.rect;
                evcon->target_text_offset_valid = true;
                evcon->target_text_offset = margin_hit.offset;
                evcon->block.x = margin_hit.block_x;
                evcon->block.y = margin_hit.block_y;
                log_debug("editable margin text hit: text=%p start=%d len=%d offset=%d block=(%.1f,%.1f) score=%.1f",
                          margin_hit.text, margin_hit.rect->start_index,
                          margin_hit.rect->length, margin_hit.offset,
                          margin_hit.block_x, margin_hit.block_y, margin_hit.score);
            }
        }
    }

    RETURN:
    if (translated) {
        evcon->event.mouse_position.x += tdx;
        evcon->event.mouse_position.y += tdy;
    }
    // Only restore block position if no target was found
    // When a target is found, keep block at the parent's position for coordinate calculations
    if (!evcon->target) {
        evcon->block = pa_block;
    }
    evcon->font = pa_font;

    // A replaced element (image, etc.) inside a rich editable IS a hit-testable
    // block — clicking it must target the element so the editor can select it.
    // Without this, block hit-testing is skipped for editable content (which is
    // assumed to be all text) and the click snaps to the nearest text via the
    // margin-text-hit above, so the image can never be clicked/selected.
    uintptr_t self_tag = block->tag();
    bool is_replaced_block = self_tag == MARKUP_NAME_IMG || self_tag == MARKUP_NAME_VIDEO ||
        self_tag == MARKUP_NAME_CANVAS || self_tag == MARKUP_NAME_IFRAME ||
        self_tag == MARKUP_NAME_EMBED || self_tag == MARKUP_NAME_OBJECT ||
        self_tag == MARKUP_NAME_HR;
    if (!pointer_events_none && !evcon->target &&
        (is_replaced_block ||
         !(is_in_rich_editable_subtree(static_cast<View*>(block)) && !is_rich_editable_host(static_cast<View*>(block))))) { // check the block itself
        // use the block's own accumulated position (parent + block offset),
        // not the restored parent position
        float x = evcon->block.x + block->x, y = evcon->block.y + block->y;
        if (x <= event->x && event->x < x + block->width &&
            y <= event->y && event->y < y + block->height) {
            // A fixed editor overlay can cover the viewport visually while
            // pointer-events:none makes the underlying control the hit target.
            log_debug("hit on block: %s", block->node_name());
            evcon->target = static_cast<View*>(block);
            evcon->offset_x = event->x - x;
            evcon->offset_y = event->y - y;
        }
        else {
            log_debug("hit not on block: %s, x: %.1f, y: %.1f, ex: %.1f, ey: %.1f, right: %.1f, bottom: %.1f",
                block->node_name(), x, y, event->x, event->y, x + block->width, y + block->height);
        }
    }
    log_leave();
}

void target_html_doc(EventContext* evcon, ViewTree* view_tree) {
    View* root_view = view_tree->root;
    if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
        log_debug("target root view");
        FontBox pa_font = evcon->font;
        FontProp* default_font = view_tree->html_version == HTML5 ? &evcon->ui_context->default_font : &evcon->ui_context->legacy_default_font;
        log_debug("target_html_doc default font: %s, html version: %d", default_font->family, view_tree->html_version);
        setup_font(evcon->ui_context, &evcon->font, default_font);
        target_block_view(evcon, lam::view_require_block(root_view));
        DomNode* root_node = static_cast<DomNode*>(root_view);
        DomDocument* doc = root_node && root_node->is_element()
            ? root_node->as_element()->doc : nullptr;
        MousePositionEvent* mouse = &evcon->event.mouse_position;
        DomElement* svg_hit = doc ? (DomElement*)js_dom_document_svg_element_from_point(
            doc, (float)mouse->x, (float)mouse->y) : nullptr;
        if (svg_hit && evcon->target) {
            bool target_contains_svg = false;
            for (DomNode* node = (DomNode*)svg_hit; node; node = node->parent) {
                if (node == static_cast<DomNode*>(evcon->target)) {
                    target_contains_svg = true;
                    break;
                }
            }
            if (target_contains_svg) {
                // SVG paint geometry has no per-shape CSS boxes. Preserve the
                // normal page-layer winner, then refine only inside that winner
                // with the SVG CTM/bounds hit result used by elementFromPoint().
                evcon->target = static_cast<View*>(svg_hit);
            }
        }
        evcon->font = pa_font;
    }
    else {
        log_error("Invalid root view: %d", root_view ? root_view->view_type : -1);
    }
}

ArrayList* build_view_stack(EventContext* evcon, View* view) {
    ArrayList* list = arraylist_new(100);
    while (view) {
        arraylist_prepend(list, view);
        view = static_cast<View*>(view->parent);
    }
    return list;
}

void fire_text_event(EventContext* evcon, ViewText* text) {
    log_debug("fire text event");
    if (evcon->new_cursor == CSS_VALUE_AUTO) {
        log_debug("set text cursor");
        evcon->new_cursor = CSS_VALUE_TEXT;
    }
    else {
        log_debug("cursor already set as %d", evcon->new_cursor);
    }
}

void fire_inline_event(EventContext* evcon, ViewSpan* span) {
    log_debug("fire inline event");
    if (span->in_line && span->inl()->cursor) {
        evcon->new_cursor = span->inl()->cursor;
    }
    uintptr_t name = span->tag();
    log_debug("fired at view %s", span->node_name());
    if (name == MARKUP_NAME_A) {
        log_debug("fired at anchor tag");
        if (evcon->event.type == RDT_EVENT_MOUSE_DOWN) {
            log_debug("mouse down at anchor tag");
            // ES31: once the package claims linkactivation, click (not
            // mousedown) is the cancelable policy point. Keep the legacy
            // fields only for package-off documents while evaluator ownership
            // remains staged for every static embedded format.
            bool package_claims = radiant_behavior_claims_event(
                evcon, static_cast<View*>(span), "linkactivation");
            if (!evcon->default_prevented && !package_claims) {
                const char* href = span->get_attribute("href");
                if (href) {
                    log_debug("legacy anchor href: %s", href);
                    evcon->new_url = (char*)href;
                    const char* target = span->get_attribute("target");
                    if (target) evcon->new_target = (char*)target;
                }
            }
        }
    }
}

void fire_block_event(EventContext* evcon, ViewBlock* block) {
    log_debug("fire block event");
    // fire as inline view first
    fire_inline_event(evcon, lam::view_require_element(block));
    if (block->scroller && block->scroll_mut()->pane) {
        if (evcon->event.type == RDT_EVENT_SCROLL) {
            if (scrollpane_scroll(evcon, block, block->scroll()->pane)) {
                // Native wheel scrolling mutates the pane outside JS; dispatch
                // the non-bubbling element scroll event that virtualizers observe.
                radiant_dispatch_simple_event(evcon, static_cast<View*>(block),
                                              "scroll", false, false);
            }
        }
        else if (evcon->event.type == RDT_EVENT_MOUSE_DOWN &&
            scroll_state_is_hovered_for_view(event_view_owner_state(static_cast<View*>(block)),
                                             static_cast<View*>(block))) {
            scrollpane_mouse_down(evcon, block);
        }
        else if (evcon->event.type == RDT_EVENT_MOUSE_UP) {
            scrollpane_mouse_up(evcon, block);
        }
        else if (evcon->event.type == RDT_EVENT_MOUSE_DRAG &&
            scroll_state_is_dragging_for_view(event_view_owner_state(static_cast<View*>(block)),
                                              static_cast<View*>(block))) {
            scrollpane_drag(evcon, block);
        }
    }
}

void fire_events(EventContext* evcon, ArrayList* target_list) {
    int stack_size = target_list->length;
    for (int i = 0; i < stack_size; i++) {
        log_debug("fire event to view no. %d", i);
        View* view = static_cast<View*>(target_list->data[i]);
        if (!view || !view->view_type) {
            log_debug("[EVENT_SKIP_NIL_VIEW] skipping uninitialized event stack entry");
            continue;
        }
        if (view->view_type == RDT_VIEW_BLOCK ||
            view->view_type == RDT_VIEW_INLINE_BLOCK ||
            view->view_type == RDT_VIEW_LIST_ITEM ||
            view->view_type == RDT_VIEW_TABLE ||
            view->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
            view->view_type == RDT_VIEW_TABLE_ROW ||
            view->view_type == RDT_VIEW_TABLE_CELL ||
            view->view_type == RDT_VIEW_TABLE_COLUMN_GROUP ||
            view->view_type == RDT_VIEW_TABLE_COLUMN) {
            fire_block_event(evcon, lam::view_require_block(view));
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            fire_inline_event(evcon, lam::view_require_element(view));
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            fire_text_event(evcon, lam::view_require_text(view));
        }
        else {
            log_error("Invalid fire view type: %d", view->view_type);
        }
    }
}

// ============================================================================
// Lambda Template Event Dispatch
// ============================================================================

/**
 * Convert a key code to a human-readable key name string.
 * Returns a static string (no allocation needed).
 */
static const char* key_code_to_name(int key) {
    switch (key) {
        case RDT_KEY_BACKSPACE: return "Backspace";
        case RDT_KEY_DELETE:    return "Delete";
        case RDT_KEY_ENTER:     return "Enter";
        case RDT_KEY_TAB:       return "Tab";
        case RDT_KEY_ESCAPE:    return "Escape";
        case RDT_KEY_LEFT:      return "ArrowLeft";
        case RDT_KEY_RIGHT:     return "ArrowRight";
        case RDT_KEY_UP:        return "ArrowUp";
        case RDT_KEY_DOWN:      return "ArrowDown";
        case RDT_KEY_HOME:      return "Home";
        case RDT_KEY_END:       return "End";
        case RDT_KEY_PAGE_UP:   return "PageUp";
        case RDT_KEY_PAGE_DOWN: return "PageDown";
        default:                return "";
    }
}

static const char* key_code_to_dom_key(int key, int mods) {
    bool shifted = (mods & RDT_MOD_SHIFT) != 0;
    static const char* lower_letters[] = {
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
        "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
    };
    static const char* upper_letters[] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    };
    static const char* unshifted_digits[] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    };
    static const char* shifted_digits[] = {
        ")", "!", "@", "#", "$", "%", "^", "&", "*", "(",
    };
    if (key >= 'A' && key <= 'Z') {
        return shifted ? upper_letters[key - 'A'] : lower_letters[key - 'A'];
    }
    if (key >= '0' && key <= '9') {
        return shifted ? shifted_digits[key - '0'] : unshifted_digits[key - '0'];
    }
    switch (key) {
        case RDT_KEY_SPACE: return " ";
        case '\'': return shifted ? "\"" : "'";
        case ',': return shifted ? "<" : ",";
        case '-': return shifted ? "_" : "-";
        case '.': return shifted ? ">" : ".";
        case '/': return shifted ? "?" : "/";
        case ';': return shifted ? ":" : ";";
        case '=': return shifted ? "+" : "=";
        case '[': return shifted ? "{" : "[";
        case '\\': return shifted ? "|" : "\\";
        case ']': return shifted ? "}" : "]";
        case '`': return shifted ? "~" : "`";
        default: return key_code_to_name(key);
    }
}

static const char* key_code_to_dom_code(int key) {
    static const char* letter_codes[] = {
        "KeyA", "KeyB", "KeyC", "KeyD", "KeyE", "KeyF", "KeyG", "KeyH", "KeyI",
        "KeyJ", "KeyK", "KeyL", "KeyM", "KeyN", "KeyO", "KeyP", "KeyQ", "KeyR",
        "KeyS", "KeyT", "KeyU", "KeyV", "KeyW", "KeyX", "KeyY", "KeyZ",
    };
    static const char* digit_codes[] = {
        "Digit0", "Digit1", "Digit2", "Digit3", "Digit4",
        "Digit5", "Digit6", "Digit7", "Digit8", "Digit9",
    };
    if (key >= 'A' && key <= 'Z') return letter_codes[key - 'A'];
    if (key >= '0' && key <= '9') return digit_codes[key - '0'];
    switch (key) {
        case RDT_KEY_SPACE: return "Space";
        case '\'': return "Quote";
        case ',': return "Comma";
        case '-': return "Minus";
        case '.': return "Period";
        case '/': return "Slash";
        case ';': return "Semicolon";
        case '=': return "Equal";
        case '[': return "BracketLeft";
        case '\\': return "Backslash";
        case ']': return "BracketRight";
        case '`': return "Backquote";
        default: return key_code_to_name(key);
    }
}

static int key_code_to_legacy_code(int key) {
    switch (key) {
        case RDT_KEY_BACKSPACE: return 8;
        case RDT_KEY_TAB:       return 9;
        case RDT_KEY_ENTER:     return 13;
        case RDT_KEY_ESCAPE:    return 27;
        case RDT_KEY_PAGE_UP:   return 33;
        case RDT_KEY_PAGE_DOWN: return 34;
        case RDT_KEY_END:       return 35;
        case RDT_KEY_HOME:      return 36;
        case RDT_KEY_LEFT:      return 37;
        case RDT_KEY_UP:        return 38;
        case RDT_KEY_RIGHT:     return 39;
        case RDT_KEY_DOWN:      return 40;
        case RDT_KEY_DELETE:    return 46;
        // GLFW's printable punctuation values are ASCII, while legacy DOM
        // keyCode uses the browser virtual-key table. In particular, '-' is
        // ASCII 45 but DOM_KEY_INSERT; leaking it as 45 made Editor.js run
        // its Insert-key path and discard the rich selection after one char.
        case '\'':              return 222;
        case ',':               return 188;
        case '-':               return 189;
        case '.':               return 190;
        case '/':               return 191;
        case ';':               return 186;
        case '=':               return 187;
        case '[':               return 219;
        case '\\':             return 220;
        case ']':               return 221;
        case '`':               return 192;
        default:
            // GLFW/Radiant letter, digit, and space codes already match the
            // corresponding legacy DOM virtual-key values.
            return key >= 32 && key <= 90 ? key : 0;
    }
}

static DomElement* rich_editable_from_target(View* target) {
    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface)) return nullptr;
    return editing_surface_is_rich(&surface) ? surface.owner : nullptr;
}

static bool dom_node_is_descendant_of(DomNode* node, DomNode* ancestor) {
    for (DomNode* p = node; p; p = p->parent) {
        if (p == ancestor) return true;
    }
    return false;
}

static void collapse_active_text_control_selection_for_rich_target(DocState* state,
                                                                   View* target) {
    if (!state || !target) return;

    DomElement* elem = tc_get_active_element(state);
    if (!elem) elem = tc_get_last_focused_text_control(state);
    if (!elem || !tc_is_text_control(elem) || !elem->form) return;

    DomNode* target_node = static_cast<DomNode*>(target);
    DomNode* text_control_node = static_cast<DomNode*>(elem);
    if (dom_node_is_descendant_of(target_node, text_control_node)) return;

    tc_ensure_init(elem);
    uint32_t end = elem->form ? elem->form->current_value_u16_len : 0;
    form_control_set_selection(state, static_cast<View*>(elem), end, end, 0);
    tc_set_active_element(state, nullptr);
    log_debug("collapse_active_text_control_selection_for_rich_target: collapsed text control to %u", end);
}

static View* canonical_selection_focus_target(DocState* state) {
    if (!state) return nullptr;
    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
        return static_cast<View*>(state->sel.control);
    }

    state_store_refresh_editing_selection_shadow(state);
    if (state->sel.kind != EDIT_SEL_DOM_RANGE || !state->sel.range) {
        return nullptr;
    }

    DomRange* range = state->sel.range;
    DomBoundary focus = state->sel.direction == DOM_SEL_DIR_BACKWARD
        ? range->start
        : range->end;
    return focus.node ? static_cast<View*>(focus.node) : nullptr;
}

static bool canonical_contenteditable_surface_from_state(DocState* state,
                                                          EditingSurface* out) {
    if (out) editing_surface_clear(out);
    if (!state) return false;

    EditingSurface surface;
    state_store_refresh_editing_selection_shadow(state);
    View* selection_target = canonical_selection_focus_target(state);
    if (selection_target && editing_surface_from_target(selection_target, &surface) &&
        editing_surface_is_rich(&surface) && surface.owner) {
        if (out) *out = surface;
        return true;
    }

    // composition start can have focus without a DOM range; use the current
    // focus surface only after the canonical selection has declined.
    if (editing_surface_from_focus(state, &surface) &&
        editing_surface_is_rich(&surface) && surface.owner) {
        if (out) *out = surface;
        return true;
    }
    return false;
}

static View* rich_keyboard_target_from_selection(DocState* state,
                                                 View* preferred,
                                                 EditingSurface* out_surface) {
    if (out_surface) editing_surface_clear(out_surface);
    EditingSurface surface;
    if (preferred && editing_surface_from_target(preferred, &surface)) {
        if (editing_surface_is_rich(&surface)) {
            if (out_surface) *out_surface = surface;
            return preferred;
        }
        if (editing_surface_is_text_control(&surface)) {
            return nullptr;
        }
    }

    View* selection_target = canonical_selection_focus_target(state);
    if (selection_target && editing_surface_from_target(selection_target, &surface) &&
        editing_surface_is_rich(&surface)) {
        if (out_surface) *out_surface = surface;
        return selection_target;
    }
    return nullptr;
}

static bool copy_current_selection_to_clipboard(DocState* state, const char* prefix) {
    if (!state) return false;
    View* surface_target = canonical_selection_focus_target(state);

    Arena* temp_arena = mem_arena_create(NULL, MEM_ROLE_TEMP, "event.arena");
    char* text = state_store_extract_selection_text(state, temp_arena);
    char* html = state_store_extract_selection_html(state, temp_arena);
    bool copied = false;
    if (html && html[0] && text) {
        clipboard_copy_rich(html, text);
        log_debug("%s: copied rich selection html=%zu text=%zu", prefix ? prefix : "copy selection", strlen(html), strlen(text));
        copied = true;
    } else if (text) {
        clipboard_copy_text(text);
        log_debug("%s: copied plain selection text=%zu", prefix ? prefix : "copy selection", strlen(text));
        copied = true;
    }
    if (copied) {
        EditingSurface surface;
        EditingSurface* surface_ptr = nullptr;
        if (editing_surface_from_target(surface_target, &surface)) {
            surface_ptr = &surface;
        }
        const char* op = (prefix && strstr(prefix, "cut")) ? "cut" : "copy";
        event_log_editing_clipboard(state, surface_ptr, op,
                                    text ? (uint32_t)strlen(text) : 0,
                                    html ? (uint32_t)strlen(html) : 0);
    }
    arena_destroy(temp_arena);
    return copied;
}

// F17/ES24: legacy Lambda payload fields now project through the same event
// wrapper JS receives. Nested Mark values still use the document Input arena;
// only the outer event carrier moved from a one-shot Mark map to the record.
class DomEventPayloadBuilder {
public:
    explicit DomEventPayloadBuilder(Item event) : event_(event) {}

    void put(const char* name, const char* value) {
        set(name, value ? js_name_item(value) : ItemNull);
    }
    void put(const char* name, int64_t value) {
        set(name, (Item){.item = i2it(value)});
    }
    void put(const char* name, double value) {
        set(name, js_make_number(value));
    }
    void put(const char* name, bool value) {
        set(name, (Item){.item = b2it(value)});
    }
    void put(const char* name, Item value) {
        set(name, value);
    }
    void putNull(const char* name) {
        set(name, ItemNull);
    }

private:
    void set(const char* name, Item value) {
        Item result = ItemNull;
        // The Lambda-only handler path has no JS realm. Project payload
        // fields directly to the native carrier instead of entering JS
        // property dispatch, which would lazily initialize intrinsics.
        if (!radiant_dom_event_member_set(event_, name, value, &result)) {
            log_error("dom event record: failed to set payload field '%s'", name);
        }
    }

    Item event_;
};

static Item build_dom_event_record(DomDocument* doc, View* target,
                                   const char* event_name, EventContext* evcon,
                                   const InputIntent* intent = nullptr,
                                   Item existing_event = ItemNull) {
    RootFrame roots(1);
    Rooted<Item> event_root(roots, existing_event);
    if (!radiant_dom_event_is(event_root.get())) {
        event_root.set(radiant_dom_event_create(event_name ? event_name : "", true,
                                                false, false, JS_CLASS_EVENT));
        radiant_dom_event_set_trusted(event_root.get(), evcon != nullptr);
    }
    if (!radiant_dom_event_is(event_root.get()) || !doc || !doc->input) {
        return event_root.get();
    }

    MarkBuilder builder(doc->input);
    DomEventPayloadBuilder mb(event_root.get());
    mb.put("type", event_name);

    // Emitted outside the intent-type guard: an option commit carries no edit
    // intent, only the index the geometry resolved.
    if (intent && intent->option_index >= 0) {
        mb.put("option_index", (int64_t)intent->option_index);
    }

    // F14.1: an execCommand names a legacy command, not a WHATWG input type, so
    // it too sits outside the guard. `value` is the call's third argument.
    if (intent && intent->command) {
        mb.put("command", intent->command);
        if (intent->data) mb.put("value", intent->data);
        else mb.putNull("value");
    }

    // F9: a caret-key notification carries no edit intent either — only the key
    // and modifiers the template maps to an operation.
    if (intent && intent->type == INPUT_INTENT_NONE && intent->key != 0) {
        // key_code_to_dom_key, not key_code_to_name: the latter names only the
        // special keys and returns "" for every letter and digit, so a template
        // matching on "x" or "z" saw an empty string and silently declined.
        // Enter and the arrows worked, which is what made the failure look like
        // a dispatch problem rather than a naming one.
        mb.put("key", key_code_to_dom_key(intent->key, intent->mods));
        mb.put("shift", (intent->mods & RDT_MOD_SHIFT) != 0);
        mb.put("ctrl",  (intent->mods & RDT_MOD_CTRL)  != 0);
        mb.put("alt",   (intent->mods & RDT_MOD_ALT)   != 0);
        mb.put("meta",  (intent->mods & RDT_MOD_SUPER) != 0);
    }

    if (intent && intent->type != INPUT_INTENT_NONE) {
        const char* input_type = input_intent_type_name(intent->type);
        mb.put("input_type", input_type);
        if (intent->data) mb.put("data", intent->data);
        else mb.putNull("data");
        if (intent->data_mime) mb.put("mime", intent->data_mime);
        else mb.putNull("mime");
        // ES17: an undo/redo hands the template the entry to install — the
        // value plus where the selection sat when that state was recorded.
        if (intent->history_value) {
            mb.put("history_value", intent->history_value);
            mb.put("history_sel_start", (int64_t)intent->history_sel_start);
            mb.put("history_sel_end", (int64_t)intent->history_sel_end);
        }
        if (intent->html_data) {
            char* sanitized = clipboard_store_sanitize(builder.arena(), "text/html", intent->html_data);
            if (sanitized) mb.put("html", sanitized);
            else mb.putNull("html");
        } else {
            mb.putNull("html");
        }

        MapBuilder im = builder.map();
        im.put("input_type", input_type);
        if (intent->data) im.put("data", intent->data);
        else im.putNull("data");
        if (intent->data_mime) im.put("mime", intent->data_mime);
        else im.putNull("mime");
        if (intent->html_data) {
            char* sanitized = clipboard_store_sanitize(builder.arena(), "text/html", intent->html_data);
            if (sanitized) im.put("html", sanitized);
            else im.putNull("html");
        } else {
            im.putNull("html");
        }
        im.put("key", key_code_to_name(intent->key));
        im.put("shift", (intent->mods & RDT_MOD_SHIFT) != 0);
        im.put("ctrl",  (intent->mods & RDT_MOD_CTRL)  != 0);
        im.put("alt",   (intent->mods & RDT_MOD_ALT)   != 0);
        im.put("meta",  (intent->mods & RDT_MOD_SUPER) != 0);
        im.put("is_composing", intent->is_composing);
        im.put("composition_caret", (int64_t)intent->composition_caret);
        mb.put("input_intent", im.final());
        mb.put("is_composing", intent->is_composing);
        mb.put("composition_caret", (int64_t)intent->composition_caret);
    }

    // extract target element's class and tag from the innermost DomElement target
    DomNode* tgt_node = static_cast<DomNode*>(target);
    if (tgt_node) {
        // walk up to find the nearest DomElement (target might be a text node)
        while (tgt_node && tgt_node->node_type != DOM_NODE_ELEMENT) {
            tgt_node = tgt_node->parent;
        }
        if (tgt_node && tgt_node->node_type == DOM_NODE_ELEMENT) {
            DomElement* tgt_elem = lam::dom_require_element(tgt_node);
            if (tgt_elem->tag_name) {
                mb.put("target_tag", tgt_elem->tag_name);
            }
            // build space-separated class string from class_names array
            if (tgt_elem->class_count > 0 && tgt_elem->class_names) {
                if (tgt_elem->class_count == 1) {
                    mb.put("target_class", tgt_elem->class_names[0]);
                } else {
                    StrBuf* sb = strbuf_new_cap(64);
                    for (int i = 0; i < tgt_elem->class_count; i++) {
                        if (i > 0) strbuf_append_char(sb, ' ');
                        strbuf_append_str(sb, tgt_elem->class_names[i]);
                    }
                    mb.put("target_class", sb->str);
                    strbuf_free(sb);
                }
            } else {
                mb.put("target_class", "");
            }
        }
    }

    // mouse coordinates (from event context)
    if (evcon) {
        mb.put("x", (double)evcon->event.mouse_button.x);
        mb.put("y", (double)evcon->event.mouse_button.y);
    }

    // for "input" events: add typed character as UTF-8 string
    if (evcon && strcmp(event_name, "input") == 0) {
        uint32_t cp = evcon->event.text_input.codepoint;
        if (cp > 0) {
            char utf8_buf[5];
            utf8_encode_z(cp, utf8_buf);
            mb.put("char", utf8_buf);
        }
    }

    // for "keydown" events: add key name string
    if (evcon && strcmp(event_name, "keydown") == 0) {
        int key = evcon->event.key.key;
        const char* key_name = key_code_to_name(key);
        mb.put("key", key_name);
    }

    // add caret position (as character index) for input, keydown, paste, and cut events
    if (evcon && (strcmp(event_name, "input") == 0 || strcmp(event_name, "keydown") == 0 ||
                  strcmp(event_name, "paste") == 0 || strcmp(event_name, "cut") == 0)) {
        DocState* st = doc->state ? (DocState*)doc->state : nullptr;
        int caret_offset = 0;
        if (caret_get_offset(st, &caret_offset)) {
            int byte_off = evcon->caret_pos_override_valid ?
                evcon->caret_pos_override : caret_offset;
            // use 'target' (the focused element passed to us, valid before retransform)
            // to convert byte offset → character index for Lambda
            int char_idx = byte_off;  // default: same (correct for ASCII)
            const char* val = nullptr;
            int val_len = 0;
            if (target && target->is_element()) {
                DomElement* el = lam::dom_require_element(target);
                if (el->form_control()) {
                    val = el->form->value;
                    val_len = val ? (int)strlen(val) : 0;
                    if (val && byte_off > 0) {
                        int safe_off = byte_off <= val_len ? byte_off : val_len;
                        char_idx = (int)str_utf8_count(val, safe_off);
                    } else {
                        char_idx = 0;
                    }
                }
            }
            mb.put("caret_pos", (int64_t)char_idx);

            // add selection range (as character indices) for form controls.
            // Document selections use source_selection below; flat offsets are
            // only meaningful within a single value buffer.
            if (val && selection_has(st)) {
                int sel_s, sel_e;
                selection_get_range(st, &sel_s, &sel_e);
                int sel_start_char = sel_s;
                int sel_end_char = sel_e;
                if (val) {
                    sel_start_char = (int)str_utf8_count(val, sel_s <= val_len ? sel_s : val_len);
                    sel_end_char = (int)str_utf8_count(val, sel_e <= val_len ? sel_e : val_len);
                }
                mb.put("selection_start", (int64_t)sel_start_char);
                mb.put("selection_end", (int64_t)sel_end_char);
            }
        }
    }

    // for "paste" events: add clipboard text
    if (evcon && strcmp(event_name, "paste") == 0 && evcon->paste_text) {
        mb.put("text", evcon->paste_text);
    }

    bool event_uses_hit_source_pos = evcon &&
        (strcmp(event_name, "mousedown") == 0 || strcmp(event_name, "mousemove") == 0 ||
         strcmp(event_name, "mouseup") == 0 || strcmp(event_name, "click") == 0);

    // R7 step 3b — attach SourcePos / SourceSelection for editor handlers.
    // The editor's `mod_source_pos` shapes are:
    //   pos       = { path: [int...], offset: int }
    //   selection = { kind:'text', anchor: pos, head: pos }   (text)
    //             | { kind:'node', path: [int...] }           (node)
    // Populated from `state->dom_selection` (kept in sync with the legacy
    // caret/selection) whenever the DOM boundary resolves to a recorded
    // source path via render_map. Form inputs already carry their own
    // `caret_pos` / `selection_*` fields above and don't get a SourcePos
    // (their typed value isn't a template-rendered source path).
    {
        DocState* st2 = doc->state ? (DocState*)doc->state : nullptr;
        DomSelection* ds = st2 ? st2->dom_selection : nullptr;
        DomBoundary anchor_boundary = dom_selection_anchor_boundary(ds);
        if (anchor_boundary.node) {
            SourcePosC anchor_pos;
            if (source_pos_from_dom_boundary(&anchor_boundary, &anchor_pos)) {
                if (!event_uses_hit_source_pos) {
                    mb.put("source_pos",
                           source_pos_to_item(builder, &anchor_pos));
                }
                DomBoundary focus_boundary = dom_selection_focus_boundary(ds);
                if (!dom_selection_is_collapsed(ds) && focus_boundary.node) {
                    SourcePosC head_pos;
                    if (source_pos_from_dom_boundary(&focus_boundary, &head_pos)) {
                        mb.put("source_selection",
                               source_text_selection_to_item(
                                   builder, &anchor_pos, &head_pos));
                        source_pos_free(&head_pos);
                    }
                } else {
                    mb.put("source_selection",
                           source_text_selection_to_item(
                               builder, &anchor_pos, &anchor_pos));
                }
                source_pos_free(&anchor_pos);
            }
        }
    }

    DocState* st_press = doc && doc->state ? (DocState*)doc->state : nullptr;
    mb.put("selection_press_in_range",
           event_uses_hit_source_pos && selection_press_in_range_pending(st_press, NULL, NULL));


    if (event_uses_hit_source_pos && doc && doc->view_tree) {
        float event_x = 0.0f, event_y = 0.0f;
        bool has_mouse_pos = false;
        if (evcon->event.type == RDT_EVENT_MOUSE_MOVE) {
            event_x = evcon->event.mouse_position.x;
            event_y = evcon->event.mouse_position.y;
            has_mouse_pos = true;
        } else if (evcon->event.type == RDT_EVENT_MOUSE_DOWN ||
                   evcon->event.type == RDT_EVENT_MOUSE_UP ||
                   evcon->event.type == RDT_EVENT_CLICK) {
            event_x = evcon->event.mouse_button.x;
            event_y = evcon->event.mouse_button.y;
            has_mouse_pos = true;
        }
        if (has_mouse_pos) {
            DomBoundary hit = { NULL, 0 };
            if (evcon->target_text_offset_valid && evcon->target && evcon->target->view_type == RDT_VIEW_TEXT) {
                DomText* hit_text = lam::dom_require_text(evcon->target);
                hit.node = static_cast<DomNode*>(hit_text);
                hit.offset = dom_text_utf8_to_utf16(hit_text, (uint32_t)evcon->target_text_offset);
            } else {
                hit = dom_hit_test_to_boundary(static_cast<View*>(doc->view_tree->root), event_x, event_y);
            }
            SourcePosC hit_pos;
            if (hit.node && source_pos_from_dom_boundary(&hit, &hit_pos)) {
                mb.put("source_pos", source_pos_to_item(builder, &hit_pos));
                source_pos_free(&hit_pos);
            }
        }
    }
    // for drag-and-drop events: add drag_data field
    if (evcon && (strcmp(event_name, "dragstart") == 0 || strcmp(event_name, "dragmove") == 0 ||
                  strcmp(event_name, "drop") == 0 || strcmp(event_name, "dragend") == 0)) {
        DocState* st = doc->state ? (DocState*)doc->state : nullptr;
        if (st && st->drag_drop && st->drag_drop->drag_data) {
            mb.put("drag_data", st->drag_drop->drag_data);
        }
        // add drop target info for drop events
        if (strcmp(event_name, "drop") == 0 && st && st->drag_drop && st->drag_drop->drop_target) {
            View* dt = st->drag_drop->drop_target;
            if (dt->is_element()) {
                DomElement* dte = lam::dom_require_element(dt);
                if (dte->class_count > 0 && dte->class_names) {
                    mb.put("drop_target_class", dte->class_names[0]);
                }
                if (dte->tag_name) {
                    mb.put("drop_target_tag", dte->tag_name);
                }
            }
        }
    }

    return event_root.get();
}

// ============================================================================
// Handler context for emit() support
// ============================================================================

/**
 * Thread-local context tracking the currently executing handler.
 * Used by pn_emit() → dispatch_emit() to walk the DOM ancestry
 * from the current handler's template result upward to find a parent
 * template handler matching the emitted event name.
 */
typedef struct EmitHandlerContext {
    DomDocument* doc;           // current document
    View* target;               // original click target (innermost View)
    Item model_item;            // current handler's model item
    const char* template_ref;   // current handler's template reference
    EventContext* evcon;        // event context for passing to nested handlers
    bool has_pending_selection; // selection to re-apply after reactive rebuild
    Item pending_selection;
} EmitHandlerContext;

static __thread EmitHandlerContext* g_emit_handler_ctx = nullptr;

static bool dom_node_is_within_root(DomNode* node, DomNode* root) {
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur == root) return true;
    }
    return false;
}

static DomNode* source_selection_scope_root(DomDocument* doc, DocState* state) {
    if (!doc || !doc->root || !state) {
        return doc && doc->root ? static_cast<DomNode*>(doc->root) : nullptr;
    }

    const EditingSurface* surface = nullptr;
    if (state->editing.has_active_surface &&
               editing_surface_is_rich(&state->editing.active_surface) &&
               state->editing.active_surface.owner) {
        surface = &state->editing.active_surface;
    }

    DomNode* doc_root = static_cast<DomNode*>(doc->root);
    if (!surface || !surface->owner) return doc_root;

    DomElement* owner = surface->owner;
    if (!dom_node_is_within_root(static_cast<DomNode*>(owner), doc_root) &&
        owner->id && owner->id[0]) {
        DomElement* live_owner = js_dom_find_element_by_id(doc->root, owner->id);
        if (live_owner) owner = live_owner;
    }

    return dom_node_is_within_root(static_cast<DomNode*>(owner), doc_root)
        ? static_cast<DomNode*>(owner)
        : doc_root;
}

static bool apply_source_selection_to_doc(UiContext* uicon, DomDocument* doc, Item selection) {
    if (!doc || !doc->root) return false;
    DocState* state = (DocState*)doc->state;
    if (!state || !state->dom_selection) return false;
    DomNode* root = source_selection_scope_root(doc, state);
    if (!root || !dom_selection_apply_source_selection(state->dom_selection, root, selection)) {
        return false;
    }
    update_caret_visual_position(uicon, state);
    return true;
}

/**
 * dispatch_emit — called from pn_emit() (lambda-proc.cpp).
 * Walks the DOM ancestry from the current handler's result element upward,
 * looking for a parent template with a handler matching the emitted event name.
 * If found, invokes the parent handler with (parent_source_item, event_data).
 */
extern "C" Item dispatch_emit(Item event_name_item, Item event_data) {
    if (!g_emit_handler_ctx || !g_emit_handler_ctx->doc) {
        log_error("dispatch_emit: no handler context — emit() called outside handler");
        return ItemNull;
    }

    const char* event_name = nullptr;
    TypeId name_tid = get_type_id(event_name_item);
    if (name_tid == LMD_TYPE_STRING || name_tid == LMD_TYPE_SYMBOL) {
        event_name = event_name_item.get_chars();
    }
    if (!event_name) {
        log_error("dispatch_emit: event_name must be a string or symbol");
        return ItemNull;
    }

    log_debug("dispatch_emit: emitting '%s' from tmpl=%s", event_name,
             g_emit_handler_ctx->template_ref ? g_emit_handler_ctx->template_ref : "(null)");

    // get the current handler's result node from render map
    Item result_node = render_map_get_result(
        g_emit_handler_ctx->model_item,
        g_emit_handler_ctx->template_ref);
    if (result_node.item == 0 || result_node.item == ITEM_NULL) {
        log_debug("dispatch_emit: no result_node for current handler");
        return ItemNull;
    }

    // find the DomElement for the current result_node, then walk up its parent chain
    // to find a parent template that handles this event
    DomDocument* doc = g_emit_handler_ctx->doc;
    if (!doc->root) return ItemNull;

    // find the DomElement whose embedded Lambda backing matches result_node.element
    // by walking from the original click target upward
    DomNode* node = static_cast<DomNode*>(g_emit_handler_ctx->target);
    bool found_self = false;

    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            DomElement* dom_elem = lam::dom_require_element(node);
            if (!dom_elem->is_synthetic()) {
                Item item;
                item.element = dom_element_render_source(dom_elem);

                // skip the current handler's template (we want PARENT)
                RenderMapLookup lookup;
                if (render_map_reverse_lookup(item, &lookup)) {
                    if (lookup.template_ref == g_emit_handler_ctx->template_ref &&
                        lookup.source_item.item == g_emit_handler_ctx->model_item.item) {
                        found_self = true;
                        node = node->parent;
                        continue;
                    }

                    // found a different template — check for matching handler
                    if (found_self) {
                        TemplateEntry* tmpl = template_registry_find_ref(
                            g_template_registry, lookup.template_ref);

                        if (tmpl && tmpl->handlers) {
                            for (TemplateHandlerEntry* h = tmpl->handlers; h; h = h->next) {
                                if (strcmp(h->event_name, event_name) == 0) {
                                    log_debug("dispatch_emit: found '%s' handler on parent tmpl=%s",
                                             event_name, tmpl->name ? tmpl->name : tmpl->template_ref);

                                    uint64_t mutation_epoch = edit_bridge_mutation_epoch();

                                    // invoke parent handler with (parent_source_item, event_data)
                                    call_template_event_handler(h,
                                        lookup.source_item, event_data);

                                    if (tmpl->is_edit &&
                                        edit_bridge_mutation_epoch() != mutation_epoch) {
                                        render_map_mark_dirty(lookup.source_item, lookup.template_ref);
                                    }

                                    return ItemNull;
                                }
                            }
                        }
                    }
                }
            }
        }
        node = node->parent;
    }

    log_debug("dispatch_emit: no parent handler found for '%s'", event_name);
    return ItemNull;
}

/**
 * dispatch_set_selection — called from pn_set_selection() (lambda-proc.cpp).
 * Push a Lambda SourceSelection back to the live DomSelection so the
 * visual caret/highlight follows after an edit. See
 * Radiant_Rich_Text_Editing.md §7.4 (Source → DOM sync).
 *
 * Resolves the active document via the thread-local handler context, then
 * delegates the parsing + boundary lookup to
 * `dom_selection_apply_source_selection` (source_pos_bridge.cpp).
 */
extern "C" Item dispatch_set_selection(Item selection) {
    if (!g_emit_handler_ctx || !g_emit_handler_ctx->doc) {
        log_error("dispatch_set_selection: no handler context — set_selection() called outside handler");
        return ItemNull;
    }

    g_emit_handler_ctx->pending_selection = selection;
    g_emit_handler_ctx->has_pending_selection = true;

    // Editor handlers update their source model before requesting a caret.
    // Applying that new path to the old DOM can target a shifted sibling, so
    // wait until dispatch finishes rebuilding the live tree.
    return ItemNull;
}

void radiant_register_event_hooks() {
    lambda_radiant_event_register(dispatch_emit, dispatch_set_selection);
}

/**
 * Dispatch a Lambda template event handler for a clicked element.
 * Walks up the DOM ancestry from `target` to find a DomElement whose
 * Lambda backing was produced by a template with a matching handler.
 *
 * @param evcon     Event context
 * @param target    The hit-tested View/DomNode target
 * @param event_name The event name to dispatch (e.g., "click")
 * @return true if a handler was found and invoked
 */
// Handler return protocol (ES5, return-value form). A handler signals intent
// through what it returns rather than through a callable on the event object:
//   'pass'            — decline; dispatch keeps looking for another handler and,
//                       finding none, the event falls through as unhandled
//   'prevent-default' — handled, and the remaining default actions for this
//                       event are suppressed
//   anything else     — handled
// String and Symbol have different layouts, so the type id picks the reader.
static bool handler_verdict_is(Item verdict, const char* word) {
    TypeId tid = get_type_id(verdict);
    const char* text = nullptr;
    size_t len = 0;
    if (tid == LMD_TYPE_SYMBOL) {
        Symbol* sym = (Symbol*)(uintptr_t)verdict.symbol_ptr;
        if (!sym) return false;
        text = sym->chars;  len = sym->len;
    } else if (tid == LMD_TYPE_STRING) {
        String* str = (String*)(uintptr_t)verdict.string_ptr;
        if (!str) return false;
        text = str->chars;  len = str->len;
    } else {
        return false;
    }
    size_t want = strlen(word);
    return len == want && memcmp(text, word, want) == 0;
}

static Item event_context_dom_event(EventContext* evcon, const char* event_name) {
    if (!evcon || !radiant_dom_event_is(evcon->dom_event) ||
        !radiant_dom_event_type_is(evcon->dom_event, event_name)) {
        return ItemNull;
    }
    return evcon->dom_event;
}

static void event_context_set_dom_event(EventContext* evcon, Item event) {
    if (!evcon || !radiant_dom_event_is(event)) return;
    if (evcon->dom_event.item != event.item) {
        evcon->dom_event_ua_handled = false;
        evcon->dom_event_author_dirty = false;
    }
    if (evcon->dom_event_root_lifetime && !evcon->dom_event_root_gc) {
        DomDocument* doc = event_context_target_document(evcon);
        Runtime* runtime = dom_document_script_runtime(doc);
        if (runtime && runtime->heap && runtime->heap->gc) {
            gc_register_root(runtime->heap->gc, &evcon->dom_event.item);
            evcon->dom_event_root_gc = runtime->heap->gc;
        }
    }
    evcon->dom_event = event;
}

// F18 keeps reactive regeneration outside the author cascade. Rebuilding while
// the path still holds ancestor nodes would turn a listener-like template
// handler into a second, mutation-sensitive propagation walk.
typedef struct AuthorTemplateCascade {
    EventContext* evcon;
    Item event;
    bool dirty;
} AuthorTemplateCascade;

static thread_local EventContext* s_active_js_dispatch_event_context = nullptr;
static thread_local AuthorTemplateCascade s_author_template_cascades[8];
static thread_local int s_author_template_cascade_depth = 0;

static AuthorTemplateCascade* author_template_cascade_current(EventContext* evcon) {
    if (s_author_template_cascade_depth <= 0) return nullptr;
    AuthorTemplateCascade* cascade =
        &s_author_template_cascades[s_author_template_cascade_depth - 1];
    return cascade->evcon == evcon ? cascade : nullptr;
}

static bool settle_template_retransform(EventContext* evcon,
                                        bool* out_model_reconciled) {
    if (!render_map_has_dirty()) return false;
    RetransformResult results[16];
    int count = render_map_retransform_with_results(results, 16);
    bool any_changed = false;
    int reported = count <= 16 ? count : 16;
    for (int i = 0; i < reported; i++) {
        if (!item_deep_equal(results[i].old_result, results[i].new_result)) {
            any_changed = true;
            break;
        }
    }
    if (!any_changed) return false;
    if (evcon) {
        rebuild_lambda_doc_incremental(evcon->ui_context, results, reported);
        evcon->need_repaint = true;
    }
    if (out_model_reconciled) *out_model_reconciled = true;
    return true;
}

static bool settle_pending_author_templates(EventContext* evcon,
                                            bool* out_model_reconciled = nullptr) {
    if (!evcon || !evcon->dom_event_author_dirty) return false;
    evcon->dom_event_author_dirty = false;
    return settle_template_retransform(evcon, out_model_reconciled);
}

// An author handler may rebuild the template subtree that owns the hit-tested
// target. The UA tier runs after author dispatch, so retain an all-node DOM
// path and resolve it only after the author settle; a rebuilt checkbox must
// receive its default action rather than leaving state on a retired wrapper.
typedef struct EventTargetPath {
    uint32_t child_indices[64];
    int length;
} EventTargetPath;

static bool capture_event_target_path(DomDocument* doc, View* target,
                                      EventTargetPath* out) {
    if (!doc || !doc->root || !target || !out) return false;
    DomNode* chain[65];
    int depth = 0;
    for (DomNode* node = static_cast<DomNode*>(target); node; node = node->parent) {
        if (depth >= 65) return false;
        chain[depth++] = node;
        if (node == static_cast<DomNode*>(doc->root)) break;
    }
    if (depth == 0 || chain[depth - 1] != static_cast<DomNode*>(doc->root)) {
        return false;
    }
    out->length = 0;
    for (int i = depth - 1; i > 0; i--) {
        uint32_t index = dom_node_child_index(chain[i - 1]);
        if (index == UINT32_MAX || out->length >= 64) return false;
        out->child_indices[out->length++] = index;
    }
    return true;
}

static View* resolve_event_target_path(DomDocument* doc,
                                       const EventTargetPath* path) {
    if (!doc || !doc->root || !path) return nullptr;
    DomNode* node = static_cast<DomNode*>(doc->root);
    for (int i = 0; i < path->length; i++) {
        if (!node->is_element()) return nullptr;
        DomNode* child = node->as_element()->first_child;
        for (uint32_t index = 0; child && index < path->child_indices[i]; index++) {
            child = child->next_sibling;
        }
        if (!child) return nullptr;
        node = child;
    }
    return static_cast<View*>(node);
}

static View* settle_author_templates_for_ua(EventContext* evcon, View* target,
                                             const EventTargetPath* target_path,
                                             bool* out_model_reconciled) {
    if (!settle_pending_author_templates(evcon, out_model_reconciled)) return target;
    if (!target_path) return target;
    DomDocument* doc = event_context_target_document(evcon);
    View* rebuilt_target = resolve_event_target_path(doc, target_path);
    return rebuilt_target;
}

// Bind the document's script runtime, build the event value, invoke one template
// handler, then reconcile any model change back into layout. Shared by both
// dispatch walks: the author-template walk passes the item apply() matched as the
// model, the behavior walk passes the element itself.
static bool invoke_template_handler(EventContext* evcon, View* target,
                                    const char* event_name, const InputIntent* intent,
                                    TemplateEntry* tmpl, TemplateHandlerEntry* h,
                                    Item model_item, const char* template_ref,
                                    bool* out_model_reconciled) {
    log_debug("invoke_template_handler: invoking '%s' handler on tmpl=%s",
              event_name, tmpl->name ? tmpl->name : tmpl->template_ref);

    // Retained handlers borrow the document Runtime's canonical
    // context; no heap-only stack context may outlive dispatch.
    EvalContext* handler_ctx = nullptr;
    // An attach-time dispatch has no EventContext; the document then comes from
    // the element the template governs.
    DomDocument* doc = evcon ? event_context_target_document(evcon) : nullptr;
    if (!doc && target && target->is_element()) {
        DomElement* te = target->as_element();
        doc = te ? te->doc : nullptr;
    }
    // the document's one shared script runtime: a behavior template governs
    // plain HTML pages too, where the Lambda runtime is the JS realm's.
    Runtime* rt = dom_document_script_runtime(doc);
    if (!rt && context && context->runtime) {
        // A synchronous JS bridge may execute before the loader retains the
        // document realm. Use the already-bound caller runtime for the package
        // load; the document must not borrow that stack-owned host pointer.
        rt = context->runtime;
    }
    Context* saved_input_context = input_context;
    if (rt && rt->heap) {
        handler_ctx = runtime_get_eval_context(rt);
        if (!handler_ctx) return true;
        handler_ctx->heap = rt->heap;
        handler_ctx->name_pool = rt->name_pool;
        handler_ctx->pool = rt->heap->pool;
        handler_ctx->type_info = type_info;
        // A retained handler runs on the document's eval thread;
        // nested dispatch must never replace that thread owner.
        if (!eval_context_init(handler_ctx)) {
            log_error("lambda event handler: eval thread belongs to another context");
            return true;
        }
        // Retained handlers outlive Runner's stack context; bind a
        // live side stack before generated code enters its context ABI.
        if (!lambda_side_stack_bind()) {
            log_error("lambda event handler: failed to bind side stack");
            return true;
        }
    }
    // Phase 5: Set ui_mode + arena so retransformed body functions
    // allocate fat DomElements/DomTexts on the result arena.
    if (handler_ctx && rt && rt->ui_mode && rt->result_arena) {
        handler_ctx->ui_mode = true;
        handler_ctx->arena = rt->result_arena;
        input_context = (Context*)handler_ctx;
    } else {
        // Clear input_context to prevent stale arena access
        // during list expansion in retransformed body functions.
        input_context = nullptr;
    }

    // F17: author/UA handlers receive the in-flight host record. When no JS
    // stage created it (a Lambda-only document), create the same record shape
    // here instead of rebuilding a separate Mark map.
    RootFrame event_roots(1);
    Rooted<Item> event_root(event_roots,
        build_dom_event_record(doc, target, event_name, evcon, intent,
            event_context_dom_event(evcon, event_name)));
    Item event_item = event_root.get();
    event_context_set_dom_event(evcon, event_item);

    // set up emit context so handlers can call emit()
    EmitHandlerContext emit_ctx;
    emit_ctx.doc = doc;
    emit_ctx.target = target;
    emit_ctx.model_item = model_item;
    emit_ctx.template_ref = template_ref;
    emit_ctx.evcon = evcon;
    emit_ctx.has_pending_selection = false;
    emit_ctx.pending_selection = ItemNull;
    EmitHandlerContext* saved_emit_ctx = g_emit_handler_ctx;
    g_emit_handler_ctx = &emit_ctx;

    uint64_t mutation_epoch = edit_bridge_mutation_epoch();

    // invoke handler: Item handler(Item model, Item event)
    Item verdict = call_template_event_handler(h, model_item, event_item);
    bool declined = handler_verdict_is(verdict, "pass");
    if (evcon && handler_verdict_is(verdict, "prevent-default")) {
        evcon->default_prevented = true;
        radiant_dom_event_prevent_default(event_root.get());
    }

    // restore emit context
    g_emit_handler_ctx = saved_emit_ctx;

    // A bubbled form-control click can be a no-op; rebuilding its
    // edit template drops the focus just established by mouse down.
    // State writes mark themselves dirty, while inline MarkEditor
    // writes advance this epoch and need this explicit invalidation.
    if (tmpl->is_edit &&
        edit_bridge_mutation_epoch() != mutation_epoch) {
        render_map_mark_dirty(model_item, template_ref);
    }

    AuthorTemplateCascade* cascade = author_template_cascade_current(evcon);
    if (cascade) {
        cascade->dirty = cascade->dirty || render_map_has_dirty();
    } else {
        settle_template_retransform(evcon, out_model_reconciled);
    }

    if (emit_ctx.has_pending_selection) {
        if (evcon && apply_source_selection_to_doc(evcon->ui_context, doc, emit_ctx.pending_selection)) {
            log_debug("dispatch_lambda_handler: applied pending source selection");
            evcon->need_repaint = true;
        } else {
            log_debug("dispatch_lambda_handler: pending source selection did not resolve");
        }
    }

    // input construction is call-scoped; the eval owner is thread-scoped.
    input_context = saved_input_context;

    // a declining handler leaves the event unclaimed so dispatch keeps looking
    return !declined;
}

// EO4: create and bind a document's evaluator at setup, for interactive
// sessions only. A one-shot layout/render run dispatches no events and needs
// none. A `.ls` page and a script-bearing page already own one; a script-less
// HTML page is the only case this adds.
//
// Deciding here rather than at first event is the whole point: at setup the
// parser has already seen whether the page has scripts, whereas at dispatch
// nothing can tell whether JS will start later — which is what stranded
// js_active_runtime_state and crashed an iframe page.
extern "C" bool radiant_document_ensure_evaluator(DomDocument* doc) {
    if (!doc) return false;
    if (dom_document_script_runtime(doc)) return true;   // EO3: already owns one

    // On by default: constraint validation now lives wholly in the dom package,
    // so a script-less HTML page needs an evaluator to validate at all. Set
    // RADIANT_DOM_PKG_CREATE_RUNTIME=0 to opt a session out.
    static int s_enabled = -1;
    if (s_enabled < 0) {
        const char* env = getenv("RADIANT_DOM_PKG_CREATE_RUNTIME");
        s_enabled = (env && env[0] == '0') ? 0 : 1;
    }
    if (!s_enabled) return false;
    if (doc->js_has_dom_realm) return false;             // EO6 owns that case

    Runtime* rt = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_LAYOUT);
    if (!rt) return false;
    runtime_init(rt);
    EvalContext* ctx = runtime_get_eval_context(rt);
    if (!ctx || !eval_context_init(ctx)) {
        log_error("document-evaluator: could not create and bind an evaluator");
        runtime_cleanup(rt);
        mem_free(rt);
        return false;
    }
    doc->lambda_runtime = rt;
    doc->owns_script_runtime = true;
    log_info("document-evaluator: created for a script-less document");
    return true;
}

// ES5 hot-path guard: continuous events must never enter Lambda, and must never
// trigger a package load. Real workloads deliver these per frame, so letting one
// bootstrap the dom package puts script compilation on the pointer path — and
// loading a package mid-mousemove inside a JS page crashed it outright.
static bool event_is_hot_path(const char* event_name) {
    if (!event_name) return false;
    return strcmp(event_name, "mousemove") == 0 ||
           strcmp(event_name, "pointermove") == 0 ||
           strcmp(event_name, "scroll") == 0 ||
           strcmp(event_name, "wheel") == 0 ||
           strcmp(event_name, "dragmove") == 0 ||
           strcmp(event_name, "dragover") == 0;
}

extern "C" bool radiant_author_template_event_live(const char* event_name) {
    if (!s_active_js_dispatch_event_context || !context || !event_name ||
        event_is_hot_path(event_name)) {
        return false;
    }
    return template_registry_may_have_author_handler(g_template_registry,
                                                     event_name);
}

static bool author_template_dispatch_begin(EventContext* evcon, Item event) {
    if (!evcon || !radiant_dom_event_is(event) || event.item != evcon->dom_event.item ||
        s_author_template_cascade_depth >= 8) {
        return false;
    }
    AuthorTemplateCascade* cascade =
        &s_author_template_cascades[s_author_template_cascade_depth++];
    cascade->evcon = evcon;
    cascade->event = event;
    cascade->dirty = false;
    return true;
}

extern "C" bool radiant_author_template_dispatch_begin(Item event) {
    return author_template_dispatch_begin(s_active_js_dispatch_event_context, event);
}

extern "C" void radiant_author_template_dispatch_end(Item event) {
    if (s_author_template_cascade_depth <= 0) return;
    AuthorTemplateCascade* cascade =
        &s_author_template_cascades[s_author_template_cascade_depth - 1];
    if (cascade->event.item != event.item) return;
    if (cascade->dirty) cascade->evcon->dom_event_author_dirty = true;
    cascade->evcon = nullptr;
    cascade->event = ItemNull;
    cascade->dirty = false;
    s_author_template_cascade_depth--;
}

static bool dispatch_author_template_participant(EventContext* evcon,
                                                 void* dom_node, Item event,
                                                 const char* event_name,
                                                 const InputIntent* intent) {
    if (!dom_node || !event_name || !radiant_dom_event_is(event) || !context ||
        !g_template_registry ||
        !author_template_cascade_current(evcon)) {
        return false;
    }
    DomNode* node = static_cast<DomNode*>(dom_node);
    if (!node->is_element()) return false;
    DomElement* dom_elem = node->as_element();
    if (!dom_elem || dom_elem->is_synthetic()) {
        return false;
    }

    Item source_item;
    source_item.element = dom_element_render_source(dom_elem);
    RenderMapLookup lookup;
    if (!render_map_reverse_lookup(source_item, &lookup)) return false;
    TemplateEntry* tmpl = template_registry_find_ref(g_template_registry,
                                                      lookup.template_ref);
    if (!tmpl || tmpl->is_behavior ||
        !template_entry_may_handle_event(tmpl, event_name)) {
        return false;
    }
    TemplateHandlerEntry* handler = template_entry_find_handler(tmpl, event_name);
    if (!handler) return false;
    bool reconciled = false;
    (void)invoke_template_handler(evcon, evcon->target, event_name, intent,
                                  tmpl, handler, lookup.source_item,
                                  lookup.template_ref, &reconciled);
    if (reconciled) evcon->need_repaint = true;
    return true;
}

extern "C" void radiant_dispatch_author_template_participant(void* dom_node,
                                                               Item event,
                                                               const char* event_name) {
    (void)dispatch_author_template_participant(s_active_js_dispatch_event_context,
                                               dom_node, event, event_name,
                                               nullptr);
}

// Load the Lambda dom package into this document's script runtime, once, on the
// first event. Static layout and render runs dispatch no events, so they never
// pay for it. Every template the package declares registers as UA behavior
// rather than as an author template (ES1/ES7).
//
// Loading requires a runtime: a `.ls` page and a script-bearing HTML page both
// have one, but a script-less HTML page owns none yet (ESO25) and is skipped
// until runtime-creation ownership is settled.
// A target inside the disclosure control of a <details>: the nearest <summary>
// ancestor-or-self whose parent is a <details>. Mirrors the `details > summary`
// guard in details.ls exactly, so the evaluator is created for precisely the
// targets that template claims and for nothing else.
// ES21 text drag-and-drop, source half. A mousedown that lands *inside* a text
// control's existing selection begins a drag of that text rather than a new
// selection — the rule every browser uses, and the one that keeps this additive:
// a press anywhere else still starts a selection exactly as before.
// Answers the control and the selection so the caller can arm the drag with the
// range it will move.
static bool radiant_text_drag_source_at(EventContext* evcon, DocState* state,
                                        View* target, float x, float y,
                                        DomElement** out_elem,
                                        uint32_t* out_start, uint32_t* out_end,
                                        uint32_t* out_press) {
    if (!evcon || !evcon->ui_context || !state || !target) return false;
    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_text_control(&surface) || !surface.owner) {
        return false;
    }
    DomElement* elem = surface.owner;
    // A read-only control still allows dragging text out; a disabled one is
    // inert, and an empty selection has nothing to drag.
    if (form_control_is_disabled(state, static_cast<View*>(elem))) return false;
    uint32_t sel_start = 0, sel_end = 0;
    form_control_get_selection(state, static_cast<View*>(elem), &sel_start, &sel_end, nullptr);
    if (sel_end <= sel_start) return false;

    uint32_t hit = 0;
    if (!editing_geometry_text_control_offset_for_point(evcon->ui_context, elem,
                                                        x, y, &hit)) {
        return false;
    }
    // Half-open: pressing exactly at the selection end places a caret instead,
    // matching the browsers and keeping click-past-selection responsive.
    if (hit < sel_start || hit >= sel_end) return false;

    if (out_elem) *out_elem = elem;
    if (out_start) *out_start = sel_start;
    if (out_end) *out_end = sel_end;
    if (out_press) *out_press = hit;
    return true;
}

// ES21, target half. A text control is an implicit drop target for a text drag:
// unlike the element-DnD path it needs no `dropzone` attribute, because dropping
// text into an editable field is UA behavior rather than an author opt-in.
static bool radiant_text_drop_target_at(EventContext* evcon, DocState* state,
                                        View* target, float x, float y,
                                        DomElement** out_elem, uint32_t* out_offset) {
    if (!evcon || !evcon->ui_context || !state || !target) return false;
    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_text_control(&surface) || !surface.owner) {
        return false;
    }
    DomElement* elem = surface.owner;
    if (form_control_is_disabled(state, static_cast<View*>(elem)) ||
        form_control_is_readonly(state, static_cast<View*>(elem))) {
        return false;
    }
    uint32_t offset = 0;
    if (!editing_geometry_text_control_offset_for_point(evcon->ui_context, elem,
                                                        x, y, &offset)) {
        return false;
    }
    if (out_elem) *out_elem = elem;
    if (out_offset) *out_offset = offset;
    return true;
}

static bool radiant_target_is_details_summary(View* target) {
    for (DomNode* n = static_cast<DomNode*>(target); n; n = n->parent) {
        if (n->node_type != DOM_NODE_ELEMENT) continue;
        DomElement* elem = n->as_element();
        if (!elem) continue;
        // stop at the nearest summary: that is the one behavior dispatch will
        // match, so it alone decides whether the package governs this click
        if (elem->tag() != MARKUP_NAME_SUMMARY) continue;
        DomNode* parent = elem->parent;
        DomElement* details = parent && parent->node_type == DOM_NODE_ELEMENT
            ? parent->as_element() : nullptr;
        return details && details->tag() == MARKUP_NAME_DETAILS;
    }
    return false;
}

static bool radiant_target_is_link(View* target) {
    for (DomNode* node = static_cast<DomNode*>(target); node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* elem = node->as_element();
        if (elem && elem->tag() == MARKUP_NAME_A && elem->get_attribute("href")) {
            return true;
        }
    }
    return false;
}

static bool radiant_dom_package_ensure(DomDocument* doc, View* target = nullptr) {
    if (!doc) return false;
    if (doc->dom_package_loaded) {
        return context && template_registry_has_behavior(g_template_registry);
    }

    static int s_enabled = -1;
    if (s_enabled < 0) {
        const char* env = getenv("RADIANT_DOM_PKG");
        s_enabled = (env && env[0] == '0') ? 0 : 1;
    }
    if (!s_enabled) { doc->dom_package_loaded = true; return false; }

    // A document with a live JS DOM realm is deferred (ESO27). ES10/ES12 want
    // the package sharing that realm's one runtime, and doing so no longer
    // crashes — EO5v2's boundary switching fixed that — but it still fails, for
    // a reason now identified: module state ids are handed out from a *Runtime*
    // counter (lambda_module_state_reserve) while the state slabs they index
    // live on the *EvalContext*, and a compiled module carries the id it was
    // assigned at transpile time. Loading the package into a runtime whose JS
    // modules have already sealed those slots trips "sealed layout changed for
    // module 0" and the package registers no templates at all.
    //
    // That is a core-runtime ownership bug (D8), not a Radiant one, so the
    // deferral stands until it is fixed. Consequence, now that no native
    // validator backs it up: a JS page gets no :valid/:invalid.
    // The document's one script runtime, whichever realm established it (ES12).
    //
    // EO4: dispatch never creates. The evaluator, if this document is to have
    // one, was created and bound at document setup by
    // radiant_document_ensure_evaluator() — which runs after the loader has
    // executed the page's scripts, so js_has_dom_realm is already settled.
    Runtime* rt = dom_document_script_runtime(doc);
    if (!rt && context && context->runtime) {
        // A synchronous JS bridge may execute before the loader retains the
        // document realm. The current evaluator can load the package for this
        // call; the document must not borrow that stack-owned host pointer.
        rt = context->runtime;
    }
    if (!rt) {
        // Create one here after all. EO4 moved creation to setup because at
        // dispatch time nothing could tell whether JS would start later — but
        // js_has_dom_realm answers that now, and ensure_evaluator checks it.
        // The attach drain covers documents whose controls render through it;
        // a document reached only by a direct event (an iframe navigated to a
        // form page is the case that exposed this) has no other chance, and
        // since native activation was deleted the package is the only
        // implementation there is.
        // Only for a target the package actually governs. The outer event
        // scope may switch between document-owned evaluators, so link policy
        // can now create its own static-document evaluator without borrowing
        // the runtime of an embedded Lambda/PDF document.
        // F9 widened this from form controls alone to "a target the package
        // governs", which now includes a rich editing surface: the <body>
        // caretkey template owns arrow keys there, so a contenteditable-only
        // page with no form control anywhere would otherwise never load the
        // package and would lose caret navigation entirely once the native rich
        // handler was deleted.
        DomElement* te = target && target->is_element() ? target->as_element() : nullptr;
        bool package_governs = te && te->form_control();
        if (!package_governs && target) {
            EditingSurface governed_surface;
            package_governs = editing_surface_from_target(target, &governed_surface) &&
                              editing_surface_is_rich(&governed_surface);
        }
        // F15 widens it once more, for the same reason F9 did: a <details> in a
        // static document — a markdown README is the common case — has no form
        // control and no script anywhere on the page, so it would never load the
        // package, and there is no native toggle to fall back to.
        if (!package_governs && target) {
            package_governs = radiant_target_is_details_summary(target);
        }
        if (!package_governs && target) {
            package_governs = radiant_target_is_link(target);
        }
        if (package_governs) {
            radiant_document_ensure_evaluator(doc);
            rt = dom_document_script_runtime(doc);
        }
    }
    if (!rt) {
        log_debug("dom-package: document owns no evaluator; keeping native behavior");
        doc->dom_package_loaded = true;
        return false;
    }
    // No heap check here: a freshly created runtime gets its heap when the first
    // script runs, so requiring one up front would block the very load that
    // establishes it.
    // mark before running: a failed load must not be retried on every event
    doc->dom_package_loaded = true;

    // Bind before touching the registry: it is EvalContext-scoped, and on a
    // script-less page nothing has bound an evaluator to this thread yet.
    //
    // Never *change* an existing binding. The thread's evaluator is also what
    // js_active_runtime_state is derived from, so rebinding it here strands JS
    // state for whatever owns the thread — which crashed an iframe page inside
    // js_observer_runtime_state. If another evaluator owns the thread, this
    // document simply keeps its native default actions.
    EvalContext* ctx = runtime_get_eval_context(rt);
    if (!ctx) {
        log_error("dom-package: runtime has no eval context");
        return false;
    }
    if (context && context != ctx) {
        log_debug("dom-package: another evaluator owns this thread; keeping native behavior");
        return false;
    }
    if (!eval_context_init(ctx)) {
        log_error("dom-package: cannot bind the document eval thread");
        return false;
    }
    if (!g_template_registry) g_template_registry = template_registry_new();

    // behavior mode spans only this load, so the page's own templates keep
    // registering as author templates
    template_registry_set_behavior_mode(g_template_registry, true);
    const char* source = "import dom: lambda.package.dom.dom\nnull\n";
    Input* package_result = run_script_mir(rt, source, (char*)"<dom-package>", false);
    // The package result is only a compile/evaluation carrier; its returned
    // value is retained by the runtime, so release the carrier's registries
    // and pool immediately instead of leaking one per document.
    if (package_result && package_result->pool) {
        input_release_auxiliary_resources(package_result);
        pool_destroy(package_result->pool);
    }
    template_registry_set_behavior_mode(g_template_registry, false);

    bool ok = template_registry_has_behavior(g_template_registry);
    log_info("dom-package: %s (%d behavior templates)",
             ok ? "loaded" : "loaded no behavior templates",
             g_template_registry ? g_template_registry->behavior_count : 0);
    return ok;
}

static void select_open_dropdown(UiContext* uicon, DocState* state,
                                 View* select_view);

// Dropdown open/close for the dom package's `<select>` behavior template. The
// policy of *when* to open belongs to the template; the overlay geometry,
// painting and outside-click capture stay native (F2).
extern "C" bool radiant_select_dropdown_is_open(void* dom_node) {
    EmitHandlerContext* ctx = g_emit_handler_ctx;
    if (!ctx || !ctx->evcon || !dom_node) return false;
    DocState* state = event_context_target_state(ctx->evcon);
    return state && state->open_dropdown == static_cast<View*>(static_cast<DomNode*>(dom_node));
}

extern "C" bool radiant_select_set_dropdown_open(void* dom_node, bool open) {
    EmitHandlerContext* ctx = g_emit_handler_ctx;
    if (!ctx || !ctx->evcon || !dom_node) return false;
    DocState* state = event_context_target_state(ctx->evcon);
    if (!state) return false;
    View* view = static_cast<View*>(static_cast<DomNode*>(dom_node));
    if (!open) {
        if (state->open_dropdown == view) doc_state_close_dropdown(state, view);
        return true;
    }
    // one dropdown at a time, matching the native activation path
    if (state->open_dropdown && state->open_dropdown != view) {
        doc_state_close_dropdown(state, state->open_dropdown);
    }
    select_open_dropdown(ctx->evcon->ui_context, state, view);
    return true;
}

// Fire a synthetic DOM event from a behavior handler. Exposed as the dom
// package's `dispatch()` primitive (ES6). The event enters the normal pipeline
// as a fresh event, so anything it triggers is dispatched in a quiescent state
// rather than nested inside the handler that raised it.
extern "C" bool radiant_dispatch_event_from_script(void* dom_node, const char* event_name) {
    if (!dom_node || !event_name || !event_name[0]) return false;
    EmitHandlerContext* ctx = g_emit_handler_ctx;
    if (!ctx || !ctx->evcon) {
        log_error("dispatch(): no active event context — only callable from a handler");
        return false;
    }
    View* view = static_cast<View*>(static_cast<DomNode*>(dom_node));
    // `input` and `change` are the notifications a control emits after its own
    // state settles: they bubble and are not cancelable (HTML 4.10.5).
    bool ok = radiant_dispatch_simple_event(ctx->evcon, view, event_name, true, false);
    log_debug("dispatch-from-script: '%s' -> %s", event_name, ok ? "dispatched" : "no listener");
    return ok;
}

// F4: package submission policy uses this as the cancelable event waist. The
// package handler already runs inside the document's live JS/Lambda batch, so
// dispatch directly into EventTarget; opening a second evaluator here would
// split submit listeners from the activation that raised them.
extern "C" bool radiant_dispatch_submit_event_from_script(void* form_node,
                                                           void* submitter_node) {
    if (!form_node) return false;
    DomNode* node = static_cast<DomNode*>(form_node);
    if (!node || !node->is_element()) return false;
    DomElement* form = node->as_element();
    if (!form || !form->tag_name || strcasecmp(form->tag_name, "form") != 0) {
        return false;
    }
    DomDocument* doc = g_emit_handler_ctx ? g_emit_handler_ctx->doc
                                          : (DomDocument*)js_dom_get_document();
    if (!doc || form->doc != doc) return false;

    // A script-less document has no EventTarget realm or submit listeners.
    // Its Lambda behavior evaluator must not manufacture JS objects: that
    // evaluator deliberately owns no JS Input/shape pool.
    if (!doc->js_has_dom_realm) return true;

    Item event = js_create_event("submit", true, true);
    js_set_key_cstr(event, "isTrusted", (Item){.item = ITEM_TRUE});
    js_set_key_cstr(event, "submitter", submitter_node
        ? js_dom_wrap_element((DomElement*)submitter_node) : ItemNull);
    Item dispatched = js_dom_dispatch_event(js_dom_wrap_element(form), event);
    return dispatched.item != ITEM_FALSE;
}

// Does a behavior template own the default action for this event on this
// target? The engine keeps its native default action as the fallback until the
// package registers a replacement (ES5), so each native activation path asks
// this before running, and exactly one of the two acts.
// Nearest non-synthetic element ancestor governed by a behavior template that
// declares `event_name`. Third user of this walk (claim check, passive probe,
// dispatch), so the shape lives in one place. `out_elem` receives the matched
// element so dispatch can bind `~` and continue past a declined match.
static TemplateEntry* behavior_match_walk(View* target, const char* event_name,
                                          DomElement** out_elem, Item* out_item) {
    for (DomNode* node = static_cast<DomNode*>(target); node; node = node->parent) {
        if (node->node_type != DOM_NODE_ELEMENT) continue;
        DomElement* dom_elem = lam::dom_require_element(node);
        if (dom_elem->is_synthetic()) continue;
        Item elem_item;
        elem_item.element = dom_element_render_source(dom_elem);
        TemplateEntry* tmpl = template_registry_match_behavior(
            g_template_registry, elem_item, event_name);
        if (tmpl) {
            if (out_elem) *out_elem = dom_elem;
            if (out_item) *out_item = elem_item;
            return tmpl;
        }
    }
    return nullptr;
}

bool radiant_behavior_claims_event(EventContext* evcon, View* target,
                                   const char* event_name) {
    if (!target || !event_name) return false;
    if (event_is_hot_path(event_name)) return false;
    // Callers outside dispatch (native validation, for one) have no
    // EventContext; fall back to the element's own document.
    DomDocument* doc = evcon ? event_context_target_document(evcon) : nullptr;
    if (!doc && target->is_element()) {
        DomElement* te = target->as_element();
        doc = te ? te->doc : nullptr;
    }
    // ensure() binds (and if needed creates) the document's evaluator, so a
    // script-less page must not be rejected for having no context yet
    if (!radiant_dom_package_ensure(doc, target)) return false;
    if (!template_registry_may_have_behavior_handler(g_template_registry,
                                                     event_name)) {
        return false;
    }
    return behavior_match_walk(target, event_name, nullptr, nullptr) != nullptr;
}

// UA default behavior: after no author template claimed the event, find the
// behavior template governing the target (or an ancestor) and run its handler.
// Behavior templates attach to elements they did not produce, so this walk
// matches on the element itself rather than through the render map. Inert until
// the dom package registers behavior templates.
static bool dispatch_behavior_handler(EventContext* evcon, View* target,
                                      const char* event_name,
                                      const InputIntent* intent,
                                      bool* out_model_reconciled) {
    // an author handler that returned 'prevent-default' suppresses UA behavior
    if (evcon && evcon->default_prevented) return false;
    // Attach-time dispatch carries no EventContext; fall back to the element's
    // own document, as the claim check and the handler invoke both do.
    DomDocument* doc = evcon ? event_context_target_document(evcon) : nullptr;
    if (!doc && target && target->is_element()) {
        DomElement* te = target->as_element();
        doc = te ? te->doc : nullptr;
    }
    if (event_is_hot_path(event_name)) {
        // a continuous event may only reach an already-loaded package, and even
        // then only a template that explicitly declares it (checked by the match)
        if (!doc || !doc->dom_package_loaded || !context) return false;
        if (!template_registry_has_behavior(g_template_registry)) return false;
    } else if (!radiant_dom_package_ensure(doc, target)) {
        // first discrete event on this document loads the UA behavior package
        return false;
    }
    if (!template_registry_may_have_behavior_handler(g_template_registry,
                                                     event_name)) {
        return false;
    }

    View* cursor = target;
    while (cursor) {
        DomElement* dom_elem = nullptr;
        Item elem_item = ItemNull;
        TemplateEntry* tmpl = behavior_match_walk(cursor, event_name,
                                                  &dom_elem, &elem_item);
        if (!tmpl) break;
        TemplateHandlerEntry* h = template_entry_find_handler(tmpl, event_name);
        if (h) {
            log_debug("dispatch_behavior_handler: '%s' -> behavior tmpl=%s",
                      event_name, tmpl->template_ref ? tmpl->template_ref : "(anon)");
            // The element is its own model. Bind `~` to the module's dom_node
            // wrapper, not the raw Mark element: handlers reach engine state
            // through the radiant primitives, and those speak wrappers.
            // Matching still runs on the Mark element, where attributes live.
            Item model = radiant_dom_wrap_node(dom_elem);
            if (get_type_id(model) == LMD_TYPE_NULL) model = elem_item;
            if (invoke_template_handler(evcon, target, event_name, intent,
                    tmpl, h, model, tmpl->template_ref, out_model_reconciled)) {
                return true;
            }
            // declined with 'pass': keep looking above the matched element,
            // and if nothing else claims it the native default stays in charge
        }
        cursor = static_cast<View*>(static_cast<DomNode*>(dom_elem)->parent);
    }
    return false;
}

// F8/ES19: the behavior init phase.
//
// `init` is the turn a control gets when it becomes live but no event has fired
// yet: it seeds derived state (`:valid`/`:invalid` from the constraint
// attributes, the aria-* mirrors). Without it a `required` field that is empty
// at load only becomes :invalid once the user touches it (ESO31) — this is the
// job native `tc_ensure_init` used to own.
//
// It is a phase rather than a queue drained from render. Three properties fall
// out of that, each of which the old attach queue got wrong:
//   * behavior no longer depends on paint. Every drain used to sit behind
//     `render_html_doc`, so a run that laid out without painting — headless
//     event handling, and `lambda.exe layout` — never inited at all.
//   * nothing outlives the pass that scheduled it, so there is no queue cap, no
//     silent drop, and no raw View* to purge when a document is freed.
//   * batch stays free of handler side effects by construction: the layout
//     command stops before the phase rather than merely failing to reach
//     window.cpp (ESO33's property, now structural).
//
// Iteration is a document-order walk of the view tree, not of the state store.
// The store has no cheap id->view direction, its hash order is unspecified (and
// handler writes carry repaint rects that the .mark goldens count), and a
// handler can insert into the very map an iteration would be holding.
static void behavior_init_visit(DomNode* node, DocState* state, int* out_count) {
    if (!node) return;
    if (node->is_element()) {
        DomElement* elem = static_cast<DomElement*>(node);
        View* view = static_cast<View*>(node);
        if (elem->form_control() && !form_control_behavior_inited(state, view)) {
            // Give the document its evaluator here rather than at load, so only
            // a document that actually owns a control the package governs pays
            // for one. A thread holds a single Runtime, so a document that
            // creates one it does not need denies it to a Lambda-script
            // subdocument — which is how a PDF and a Lambda report rendered into
            // an iframe stopped loading ("eval thread already owns a Runtime").
            // The walk only reaches form controls, which is the same narrowing
            // the queue drain applied (EO4).
            if (elem->doc) radiant_document_ensure_evaluator(elem->doc);
            // The bit is recorded only when a template actually claimed the
            // init, because recording it is what creates the control's durable
            // ViewState. `<button>` is a form control that no template governs;
            // marking it would mint a FORM_CONTROL state entry purely to say
            // "nothing to do", which changes the store's shape (the state-machine
            // tests pin the kind) for no gain. An unclaimed control simply gets
            // re-offered by the next phase, which is a failed match and nothing
            // more.
            if (dispatch_behavior_handler(nullptr, view, "init", nullptr, nullptr)) {
                form_control_set_behavior_inited(state, view, true);
                (*out_count)++;
            }
        }
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            behavior_init_visit(child, state, out_count);
        }
    }
}

void radiant_run_behavior_init(DomDocument* doc) {
    if (!doc || !doc->behavior_init_pending) return;
    // Cleared up front: a handler may create a control (and re-arm the gate),
    // and that control belongs to the next phase, not to this walk.
    doc->behavior_init_pending = false;
    DocState* state = (DocState*)doc->state;
    if (!state || !doc->root) return;
    int count = 0;
    behavior_init_visit(static_cast<DomNode*>(doc->root), state, &count);
    if (count > 0) log_debug("behavior-init: inited %d control(s)", count);
}

// Post-mutation `input` for UA behavior templates. The pre-mutation `input`
// belongs to app templates that own their text; validation and anything else
// that must observe the committed value hooks here instead (F3).
// ESO42: the commit hook. `change` must fire before `blur`, but the decision
// that gates it is made before either — so a template's `on blur` runs too late
// to make it. This dispatches a behavior-only `commit` at the decision point.
//
// Behavior-only is what makes it legal: no DOM event has fired yet, so there
// are no JS listeners to preempt and ES5's after-JS ordering is not in play.
// The template only *decides*; native still dispatches `change` itself, which
// keeps it ahead of `blur` for templates and JS alike and preserves the state
// machine's DISPATCH_CHANGE observation.
//
// Returns whether a template answered at all. When none did, the caller falls
// back to the native comparison (ES5), so a page with no package behaves as it
// always has.
extern "C" bool radiant_dispatch_behavior_commit(EventContext* evcon, View* target) {
    return dispatch_behavior_handler(evcon, target, "commit", nullptr, nullptr);
}

// F2b: the commit half of <select> activation. The template already owns
// opening and closing the dropdown; without this the *choice* stayed native, so
// one interaction was split down the middle — exactly the asymmetry F1b/F2b
// removed everywhere else.
//
// Behavior-only, like `commit` and the composition events: the dropdown overlay
// is not a DOM element, so no DOM event has fired for the option and there are
// no JS listeners to preempt. Native resolves which option the pointer landed on
// (geometry is mechanism) and the template performs the commit.
// F11: keys an open <select> dropdown responds to. Behavior-only like the
// others here — the dropdown overlay is not a DOM element, so no DOM key event
// has fired for it.
extern "C" bool radiant_dispatch_behavior_dropdown_key(EventContext* evcon,
                                                       View* target,
                                                       const InputIntent* intent) {
    return dispatch_behavior_handler(evcon, target, "dropdownkey", intent, nullptr);
}

extern "C" bool radiant_dispatch_behavior_option_commit(EventContext* evcon,
                                                        View* target,
                                                        const InputIntent* intent) {
    return dispatch_behavior_handler(evcon, target, "optioncommit", intent, nullptr);
}

static void dispatch_form_navigation(EventContext* evcon, DomElement* elem,
                                     DocState* state, View* target,
                                     int current_offset, uint32_t destination,
                                     bool extend, const char* extend_operation,
                                     const char* move_operation);

// F9: the caret-operation seam. The template names an operation; native
// computes where that operation lands and performs it. The answer comes back
// through the epoch pattern `request_change` established rather than through new
// verdict vocabulary — a primitive has no EventContext, and dispatching the move
// needs one.
static __thread uint64_t s_caret_op_epoch = 0;
static __thread char s_caret_op_name[32];
static __thread bool s_caret_op_extend = false;

extern "C" uint64_t radiant_caret_operation_epoch(void) { return s_caret_op_epoch; }

// F11: the key-intent epoch, same pattern as the caret one.
static __thread uint64_t s_key_intent_epoch = 0;
static __thread char s_key_intent_name[40];
extern "C" uint64_t radiant_key_intent_epoch(void) { return s_key_intent_epoch; }
extern "C" const char* radiant_key_intent_name(void) { return s_key_intent_name; }
extern "C" void radiant_key_intent_request(const char* name) {
    if (!name) return;
    snprintf(s_key_intent_name, sizeof(s_key_intent_name), "%s", name);
    s_key_intent_epoch++;
}

// Behavior-only, and deliberately context-free — see the note on the seam in
// editing_intent.cpp: a translation must still resolve after preventDefault.
// F13: the DOM edit seam. Behavior-only and context-free, like keyintent.
extern "C" bool radiant_dispatch_behavior_dom_edit(View* target,
                                                   const InputIntent* intent) {
    return dispatch_behavior_handler(nullptr, target, "domedit", intent, nullptr);
}

extern "C" bool radiant_dispatch_behavior_key_intent(View* target,
                                                     const InputIntent* intent) {
    return dispatch_behavior_handler(nullptr, target, "keyintent", intent, nullptr);
}

// F14.1: the legacy command seam. Behavior-only and context-free like the two
// above — `document.execCommand` is a method call, not an event, so there is no
// EventContext and nothing has been preventDefault'd ahead of it.
extern "C" bool radiant_dispatch_behavior_exec_command(View* target,
                                                       const InputIntent* intent) {
    return dispatch_behavior_handler(nullptr, target, "execcommand", intent, nullptr);
}

extern "C" const char* radiant_caret_operation_name(void) { return s_caret_op_name; }
extern "C" bool radiant_caret_operation_extend(void) { return s_caret_op_extend; }

extern "C" void radiant_caret_operation_request(const char* operation, bool extend) {
    if (!operation) return;
    snprintf(s_caret_op_name, sizeof(s_caret_op_name), "%s", operation);
    s_caret_op_extend = extend;
    s_caret_op_epoch++;
}

// Where each named operation lands in a text control. This is the whole of what
// stayed native: the *destination* is geometry over the live buffer, while
// which key asks for which operation is policy and now lives in the package.
// A single-line <input> has no vertical motion and collapses Up/Down to its
// ends; a textarea resolves line and page geometry over the live value.
static int form_caret_line_start(const char* value, int cur) {
    int start = cur;
    while (start > 0 && value && value[start - 1] != '\n') start--;
    return start;
}

static int form_caret_line_end(const char* value, int value_len, int cur) {
    int end = cur;
    while (end < value_len && value && value[end] != '\n') end++;
    return end;
}

static int form_caret_move_line(const char* value, int value_len, int cur,
                                int direction) {
    int current_start = form_caret_line_start(value, cur);
    int column = cur - current_start;
    int target_start = current_start;
    if (direction < 0 && current_start > 0) {
        target_start = form_caret_line_start(value, current_start - 1);
    } else if (direction > 0) {
        int current_end = form_caret_line_end(value, value_len, cur);
        if (current_end < value_len) target_start = current_end + 1;
    }
    int target_end = form_caret_line_end(value, value_len, target_start);
    int target_len = target_end - target_start;
    return target_start + (column < target_len ? column : target_len);
}

static bool form_caret_operation_destination(const char* op, const char* value,
                                             int value_len, int cur, bool multiline,
                                             uint32_t* out_dest) {
    if (!op || !out_dest) return false;
    if (strcmp(op, "moveCharacterBackward") == 0) {
        int off = cur > 0 ? cur - 1 : 0;
        while (off > 0 && value && ((unsigned char)value[off] & 0xC0) == 0x80) off--;
        *out_dest = (uint32_t)off;
    } else if (strcmp(op, "moveCharacterForward") == 0) {
        int off = cur < value_len ? cur + 1 : value_len;
        while (off < value_len && value && ((unsigned char)value[off] & 0xC0) == 0x80) off++;
        *out_dest = (uint32_t)off;
    } else if (strcmp(op, "moveWordBackward") == 0) {
        *out_dest = te_prev_word_byte(value, (uint32_t)value_len, (uint32_t)cur);
    } else if (strcmp(op, "moveWordForward") == 0) {
        *out_dest = te_next_word_byte(value, (uint32_t)value_len, (uint32_t)cur);
    } else if (strcmp(op, "moveLineStart") == 0) {
        *out_dest = (uint32_t)(multiline ? form_caret_line_start(value, cur) : 0);
    } else if (strcmp(op, "moveLineEnd") == 0) {
        *out_dest = (uint32_t)(multiline
            ? form_caret_line_end(value, value_len, cur) : value_len);
    } else if (strcmp(op, "moveDocumentStart") == 0) {
        *out_dest = 0;
    } else if (strcmp(op, "moveDocumentEnd") == 0) {
        *out_dest = (uint32_t)value_len;
    } else if (multiline && (strcmp(op, "moveLineBackward") == 0 ||
                             strcmp(op, "moveLineForward") == 0)) {
        *out_dest = (uint32_t)form_caret_move_line(value, value_len, cur,
            strcmp(op, "moveLineBackward") == 0 ? -1 : 1);
    } else if (multiline && (strcmp(op, "movePageBackward") == 0 ||
                             strcmp(op, "movePageForward") == 0)) {
        int direction = strcmp(op, "movePageBackward") == 0 ? -1 : 1;
        int dest = cur;
        for (int step = 0; step < 10; step++) {
            int next = form_caret_move_line(value, value_len, dest, direction);
            if (next == dest) break;
            dest = next;
        }
        *out_dest = (uint32_t)dest;
    } else {
        return false;
    }
    return true;
}

// Perform the operation the template asked for. The extend/collapse split and
// the WHATWG operation names it reports are unchanged — only who chose the
// operation moved.
static bool form_apply_caret_operation(EventContext* evcon, DomElement* elem,
                                       DocState* state, View* target,
                                       const char* value, int value_len, int cur) {
    uint32_t dest = 0;
    bool multiline = elem && elem->form &&
        elem->form->control_type == FORM_CONTROL_TEXTAREA;
    if (!form_caret_operation_destination(s_caret_op_name, value, value_len,
                                          cur, multiline, &dest)) {
        return false;
    }
    char extend_op[40];
    snprintf(extend_op, sizeof(extend_op), "extend%s", s_caret_op_name + 4);  // move* -> extend*
    dispatch_form_navigation(evcon, elem, state, target, cur, dest,
                             s_caret_op_extend, extend_op, s_caret_op_name);
    return true;
}

// F9: behavior-only key notification. Carries the key and modifiers; the
// template answers by calling `radiant.caret_operation`, which bumps the epoch.
extern "C" bool radiant_dispatch_behavior_caret_key(EventContext* evcon, View* target,
                                                    const InputIntent* intent) {
    return dispatch_behavior_handler(evcon, target, "caretkey", intent, nullptr);
}

// F10: behavior-only, like `commit`, `optioncommit` and the composition hooks —
// no DOM `contextmenu` event has fired, so there are no JS listeners to preempt.
// The template decides whether this target gets a menu and computes the enable
// mask; native supplies only the hit target and the popup position.
extern "C" bool radiant_dispatch_behavior_context_menu(EventContext* evcon,
                                                       View* target) {
    return dispatch_behavior_handler(evcon, target, "contextmenu", nullptr, nullptr);
}

extern "C" bool radiant_dispatch_behavior_input(EventContext* evcon, View* target) {
    return dispatch_behavior_handler(evcon, target, "input", nullptr, nullptr);
}

// F4: submit/reset activation has no separate DOM event to expose to Lambda.
// Keep it behavior-only so the ordinary click dispatch remains available to
// author handlers and cancellation reaches this default-action seam first.
extern "C" bool radiant_dispatch_behavior_submit_activation(EventContext* evcon,
                                                             View* target) {
    return dispatch_behavior_handler(evcon, target, "submitactivation", nullptr, nullptr);
}

extern "C" bool radiant_dispatch_behavior_reset_activation(EventContext* evcon,
                                                            View* target) {
    return dispatch_behavior_handler(evcon, target, "resetactivation", nullptr, nullptr);
}

// F5: the applier seam. A behavior template that handles this `beforeinput`
// applies the edit itself (through replace_range) and returns 'prevent-default',
// which is the signal for the native splice to stand down — the same protocol a
// JS listener uses. A template that returns 'pass', or declares no handler for
// the intent it was given, leaves the native applier in charge, so the flip can
// land one input type at a time.
//
// The intent is passed through so the handler can read `input_type` and `data`
// off the event map rather than re-deriving them from the key.
extern "C" bool radiant_dispatch_behavior_beforeinput(EventContext* evcon,
                                                      View* target,
                                                      const InputIntent* intent) {
    if (!evcon) return false;
    bool prevented_before = evcon->default_prevented;
    dispatch_behavior_handler(evcon, target, "beforeinput", intent, nullptr);
    bool prevented = evcon->default_prevented && !prevented_before;
    // Confine the verdict to this dispatch: `default_prevented` is the whole
    // event's flag, and letting an applied edit set it would also suppress
    // unrelated default actions later in the same event.
    if (prevented) evcon->default_prevented = prevented_before;
    return prevented;
}

static bool dispatch_lambda_handler_legacy_author(EventContext* evcon, View* target,
                                                   const char* event_name,
                                                   const InputIntent* intent,
                                                   bool* out_model_reconciled,
                                                   bool allow_behavior) {
    if (!context || !g_template_registry || g_template_registry->count == 0) {
        return allow_behavior && dispatch_behavior_handler(evcon, target, event_name,
                                                           intent, out_model_reconciled);
    }

    // F20 retains its established first-claim editor contract until the input,
    // IME, and simulator entry families move to the shared author cascade.
    for (DomNode* node = static_cast<DomNode*>(target); node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* dom_elem = lam::dom_require_element(node);
        if (dom_elem->is_synthetic()) continue;
        Item result_item = {.element = dom_element_render_source(dom_elem)};
        RenderMapLookup lookup;
        if (!render_map_reverse_lookup(result_item, &lookup)) continue;
        TemplateEntry* tmpl = template_registry_find_ref(g_template_registry,
                                                         lookup.template_ref);
        TemplateHandlerEntry* handler = template_entry_find_handler(tmpl, event_name);
        if (tmpl && handler && invoke_template_handler(evcon, target, event_name, intent,
                tmpl, handler, lookup.source_item, lookup.template_ref,
                out_model_reconciled)) {
            return true;
        }
    }
    return allow_behavior && dispatch_behavior_handler(evcon, target, event_name,
                                                       intent, out_model_reconciled);
}

static bool dispatch_lambda_handler(EventContext* evcon, View* target, const char* event_name,
                                    const InputIntent* intent = nullptr,
                                    bool* out_model_reconciled = nullptr,
                                    bool allow_behavior = true,
                                    bool legacy_author = false) {
    if (out_model_reconciled) *out_model_reconciled = false;
    if (legacy_author) {
        return dispatch_lambda_handler_legacy_author(evcon, target, event_name, intent,
                                                     out_model_reconciled, allow_behavior);
    }
    // F18 already delivered author templates from the shared JS path. Native
    // callers retained during F20 therefore see only the UA-tier result here,
    // never a second target-to-root author walk for the same record.
    if (event_context_dom_event(evcon, event_name).item != ITEM_NULL) {
        EventTargetPath target_path = {};
        DomDocument* doc = event_context_target_document(evcon);
        bool target_path_valid = capture_event_target_path(doc, target, &target_path);
        if (!allow_behavior || radiant_dom_event_default_prevented(evcon->dom_event)) {
            settle_pending_author_templates(evcon, out_model_reconciled);
            return false;
        }
        if (!evcon->dom_event_ua_handled) {
            View* ua_target = settle_author_templates_for_ua(
                evcon, target, target_path_valid ? &target_path : nullptr,
                out_model_reconciled);
            if (!ua_target) return false;
            evcon->dom_event_ua_handled = dispatch_behavior_handler(
                evcon, ua_target, event_name, intent, out_model_reconciled);
        }
        return evcon->dom_event_ua_handled;
    }
    // Plain HTML documents have no Lambda template Runtime.  Native input must
    // skip template dispatch instead of reading the context-local registry with
    // no document owner bound.
    if (!context || !g_template_registry || g_template_registry->count == 0) {
        // No author templates on this document (a plain HTML page has none),
        // but UA behavior may still govern the target.
        return allow_behavior && dispatch_behavior_handler(evcon, target, event_name,
                                                           intent, out_model_reconciled);
    }

    // Lambda-only documents have no JS dispatch, but their templates are still
    // author participants. Build the same host record and walk every matching
    // template target-to-root before the UA behavior tier; a handler verdict
    // controls only cancellation, never whether an ancestor receives the event.
    DomDocument* doc = event_context_target_document(evcon);
    RootFrame roots(2);
    Rooted<Item> event_root(roots,
        build_dom_event_record(doc, target, event_name, evcon, intent));
    Item event = event_root.get();
    event_context_set_dom_event(evcon, event);

    EventTargetPath target_path = {};
    bool target_path_valid = capture_event_target_path(doc, target, &target_path);

    Item ignored = ItemNull;
    Item target_item = radiant_dom_wrap_node(target);
    radiant_dom_event_member_set(event, "target", target_item, &ignored);
    radiant_dom_event_member_set(event, "srcElement", target_item, &ignored);
    radiant_dom_event_member_set(event, "__dispatch_flag",
                                 (Item){.item = ITEM_TRUE}, &ignored);

    bool author_dispatched = false;
    bool author_live = !event_is_hot_path(event_name) &&
        template_registry_may_have_author_handler(g_template_registry, event_name);
    bool author_cascade = author_live && author_template_dispatch_begin(evcon, event);
    if (author_cascade) {
        for (DomNode* node = static_cast<DomNode*>(target); node; node = node->parent) {
            if (!node->is_element()) continue;
            int phase = node == static_cast<DomNode*>(target) ? 2 : 3;
            radiant_dom_event_set_lambda_dispatch_position(
                event, radiant_dom_wrap_node(node), phase);
            author_dispatched = dispatch_author_template_participant(
                evcon, node, event, event_name, intent) || author_dispatched;
            if (radiant_dom_event_propagation_stopped(event)) break;
        }
        radiant_author_template_dispatch_end(event);
    }

    // Mirror the DOM dispatch cleanup before default actions. Cancellation is
    // deliberately retained, while propagation state cannot leak to the next
    // native dispatch of this reusable host record.
    radiant_dom_event_member_set(event, "eventPhase", ItemNull, &ignored);
    radiant_dom_event_member_set(event, "currentTarget", ItemNull, &ignored);
    radiant_dom_event_clear_lambda_dispatch_position(event);
    radiant_dom_event_member_set(event, "__stop_prop", ItemNull, &ignored);
    radiant_dom_event_member_set(event, "__stop_imm", ItemNull, &ignored);
    radiant_dom_event_member_set(event, "__dispatch_flag", ItemNull, &ignored);

    // The UA tier observes the DOM after all author participants have settled.
    // Rebind through the structural path when regeneration replaced its target.
    View* ua_target = settle_author_templates_for_ua(
        evcon, target, target_path_valid ? &target_path : nullptr,
        out_model_reconciled);
    bool prevented = radiant_dom_event_default_prevented(event);
    evcon->default_prevented = prevented;
    bool ua_handled = false;
    if (allow_behavior && !prevented && ua_target) {
        ua_handled = dispatch_behavior_handler(evcon, ua_target, event_name, intent,
                                               out_model_reconciled);
        evcon->dom_event_ua_handled = ua_handled;
    }
    return author_dispatched || ua_handled;
}

// Forward declaration — CE-3 JS InputEvent dispatcher lives further down,
// alongside the other radiant_dispatch_* JS bridges.
static bool radiant_dispatch_input_event(EventContext* evcon, View* target,
                                         const char* type,
                                         const InputIntent* intent);
static void radiant_dispatch_composition_event(EventContext* evcon,
                                               View* target,
                                               const char* type,
                                               const char* data);
extern "C" bool radiant_dispatch_editing_composition_event(UiContext* uicon,
                                                           EventType event_type,
                                                           const char* text,
                                                           uint32_t caret_cp);

static bool dispatch_editing_input_event(EventContext* evcon, View* target,
                                         const char* type,
                                         const EditingIntent* intent,
                                         void* user) {
    (void)user;
    return radiant_dispatch_input_event(evcon, target, type, intent);
}

static bool event_document_has_js_runtime(EventContext* evcon) {
    DomDocument* document = event_context_target_document(evcon);
    // Lambda template documents retain a Jube support capsule in `js`, but it
    // is not a DOM script realm. Test the realm bit the script runner sets
    // rather than the absence of a Lambda runtime: a document may host page JS
    // and Lambda code at once, so runtime presence cannot classify the page.
    return dom_document_has_js_realm(document);
}

// A Lambda-only document deliberately has no JS-facing EventTarget realm.
// Its trusted native entries still join F18's author/UA pipeline through the
// record-backed template path instead of disappearing at the JS gateway.
static bool dispatch_lambda_handler_without_js(EventContext* evcon, View* target,
                                               const char* event_name,
                                               const InputIntent* intent = nullptr,
                                               bool* out_model_reconciled = nullptr,
                                               bool allow_behavior = true,
                                               bool legacy_author = false) {
    if (event_document_has_js_runtime(evcon)) return false;
    return dispatch_lambda_handler(evcon, target, event_name, intent,
                                   out_model_reconciled, allow_behavior, legacy_author);
}

static bool dispatch_contenteditable_event(EventContext* evcon, View* target,
                                            const InputIntent* intent);
static bool dispatch_contenteditable_composition_event(
        EventContext* evcon, const EditingSurface* surface,
        const EditingIntent* intent);

static EditingFormNotificationHooks form_editing_notification_hooks() {
    EditingFormNotificationHooks hooks = {};
    hooks.dispatch_input_event = dispatch_editing_input_event;
    return hooks;
}

static void rich_select_all_sync_descendant_text_controls(DocState* state,
                                                          DomNode* node) {
    if (!state || !node || !node->is_element()) return;

    DomElement* elem = lam::dom_require_element(node);
    if (tc_is_text_control(elem)) {
        tc_ensure_init(elem);
        FormControlProp* form = elem->form;
        if (!form) return;
        uint32_t len = form->current_value_u16_len;
        form_control_set_selection(state, static_cast<View*>(elem),
                                   0, len, (uint8_t)(len > 0 ? 1 : 0));
        return;
    }

    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        rich_select_all_sync_descendant_text_controls(state, child);
    }
}

static bool dispatch_contenteditable_consumer_event(EventContext* evcon,
                                                    View* target,
                                                    const InputIntent* intent) {
    if (!evcon || !target || !intent || intent->type == INPUT_INTENT_NONE) return false;
    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface)) return false;
    if (!editing_surface_is_rich(&surface)) return false;

    DocState* state = event_context_target_state(evcon);
    event_log_editing_clipboard_intent(state, &surface, intent, nullptr);
    // Clipboard, drag, and physical-key callers share the same action gate;
    // a consumer operation must not regain the retired event-only rich path.
    return dispatch_contenteditable_event(evcon, target, intent);
}

static bool dispatch_rich_selection_snapshot(EventContext* evcon,
                                             DocState* state,
                                             View* target,
                                             const char* operation,
                                             const InputIntent* intent) {
    if (!evcon || !state || !target) return false;

    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_rich(&surface)) {
        return false;
    }

    int start = 0;
    int end = 0;
    if (selection_has(state)) {
        selection_get_range(state, &start, &end);
    } else {
        View* caret_view = nullptr;
        int caret_offset = 0;
        if (caret_get_position(state, &caret_view, &caret_offset)) {
            if (caret_view && caret_view != target) {
                EditingSurface caret_surface;
                if (editing_surface_from_target(caret_view, &caret_surface) &&
                    editing_surface_is_rich(&caret_surface)) {
                    surface = caret_surface;
                }
            }
            start = caret_offset;
            end = caret_offset;
        }
    }

    uint32_t anchor = start < 0 ? 0 : (uint32_t)start;
    uint32_t focus = end < 0 ? 0 : (uint32_t)end;
    event_log_editing_selection(state, &surface, intent,
                                operation ? operation : "selection",
                                anchor, focus);
    return true;
}

static bool dispatch_contenteditable_select_all_default(EventContext* evcon,
                                                        DocState* state,
                                                        View* target,
                                                        const InputIntent* intent) {
    if (!evcon || !state || !target) return false;

    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_rich(&surface) || !surface.owner) {
        return false;
    }

    DomElement* owner = surface.owner;
    if (owner->id && owner->doc && owner->doc->root) {
        DomElement* live_owner = js_dom_find_element_by_id(owner->doc->root, owner->id);
        if (live_owner) {
            owner = live_owner;
            surface.owner = live_owner;
            surface.view = static_cast<View*>(live_owner);
        }
    }

    const char* exc = nullptr;
    DomNode* owner_node = static_cast<DomNode*>(owner);
    DomBoundary start;
    DomBoundary end;
    if (!dom_selection_compute_select_all_boundaries(owner_node, &start, &end)) {
        start = { owner_node, 0 };
        end = { owner_node, dom_node_boundary_length(owner_node) };
    }
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("dispatch_contenteditable_select_all: rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    rich_select_all_sync_descendant_text_controls(state, owner_node);
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("dispatch_contenteditable_select_all: restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    event_log_editing_selection(state, &surface, intent, "selectAll", 0, 0);
    return true;
}

static bool dispatch_contenteditable_select_all(EventContext* evcon,
                                                DocState* state,
                                                View* target,
                                                const InputIntent* intent) {
    if (!evcon || !state || !target || !intent ||
        intent->type != INPUT_INTENT_SELECT_ALL) {
        return false;
    }

    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_rich(&surface)) {
        return false;
    }

    // Select-all is a selection operation, not a beforeinput/default-action
    // edit. Keep it outside the text action gate and do not emit input.
    bool selected = dispatch_contenteditable_select_all_default(evcon, state,
                                                                 target, intent);
    if (selected) evcon->need_repaint = true;
    return selected;
}

static bool dispatch_form_text_replace(EventContext* evcon, DomElement* elem,
                                       DocState* state, View* target,
                                       uint32_t start, uint32_t end,
                                       const char* repl, uint32_t repl_len,
                                       InputIntentType input_type) {
    if (!evcon || !elem || !state || !target) return false;
    if (!tc_is_text_control(elem)) return false;

    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_text_control(&surface)) {
        return te_replace_byte_range(elem, state, target, start, end,
                                     repl, repl_len);
    }

    InputIntent intent;
    intent.type = input_type;
    intent.data = repl ? repl : "";

    EditingFormNotificationHooks hooks = form_editing_notification_hooks();

    uint32_t saved_selection_start = 0;
    uint32_t saved_selection_end = 0;
    uint8_t saved_selection_direction = 0;
    form_control_get_selection(state, target,
                               &saved_selection_start,
                               &saved_selection_end,
                               &saved_selection_direction);

    bool prevented = false;
    bool applied_by_template = false;
    uint64_t splice_epoch_before = radiant_splice_epoch();
    editing_dispatch_form_beforeinput(evcon, &surface, &intent, &hooks,
                                      &prevented, &applied_by_template);
    if (applied_by_template) {
        // F5: a behavior template already performed the splice and left the
        // caret after the text it inserted. Restoring the pre-edit selection
        // here would drag the caret back to where the edit started, so every
        // further keystroke would land at the same offset — typing "hello"
        // produced "olleh" before this branch existed.
        // An applier that claimed the intent but spliced nothing made no edit —
        // a keystroke refused by `maxlength` is exactly that — and an edit that
        // did not happen must not produce an `input` event.
        bool mutated = radiant_splice_epoch() != splice_epoch_before;
        log_debug("dispatch_form_text_replace: applied by behavior template "
                  "inputType=%s mutated=%d",
                  input_intent_type_name(input_type), mutated ? 1 : 0);
        if (mutated) {
            editing_dispatch_form_input(evcon, &surface, &intent, &hooks);
            doc_state_request_repaint(state);
        }
        return true;
    }
    if (prevented) {
        if (input_type == INPUT_INTENT_INSERT_TEXT &&
            saved_selection_start != saved_selection_end) {
            form_control_set_selection(state, target,
                                       saved_selection_end,
                                       saved_selection_end,
                                       0);
        } else {
            form_control_set_selection(state, target,
                                       saved_selection_start,
                                       saved_selection_end,
                                       saved_selection_direction);
        }
        log_debug("dispatch_form_text_replace: beforeinput prevented inputType=%s",
                  input_intent_type_name(input_type));
        return true;
    }

    DomElement* live_elem = elem;
    View* live_target = target;
    bool preserve_dispatch_target =
        input_type == INPUT_INTENT_INSERT_FROM_COMPOSITION ||
        input_type == INPUT_INTENT_DELETE_COMPOSITION_TEXT;
    if (!preserve_dispatch_target) {
        if (elem->id && elem->doc && elem->doc->root) {
            DomElement* live_by_id = js_dom_find_element_by_id(elem->doc->root, elem->id);
            if (live_by_id && tc_is_text_control(live_by_id)) {
                live_elem = live_by_id;
                live_target = static_cast<View*>(live_by_id);
            }
        }
        View* live_focus = focus_get(state);
        if (live_elem == elem && live_focus && live_focus->is_element()) {
            DomElement* focus_elem = lam::dom_require_element(live_focus);
            if (tc_is_text_control(focus_elem)) {
                live_elem = focus_elem;
                live_target = live_focus;
            }
        }
    }
    tc_ensure_init(live_elem);

    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_TEXT,
                               SM_EV_FORM_REPLACE_TEXT, live_target);
    sm_observe_action(state, SM_ACT_DISPATCH_BEFOREINPUT);

    uint32_t old_len = event_log_text_len(live_elem->form ? live_elem->form->value : nullptr);
    const char* previous_history_input_type =
        te_history_input_type_set(state, input_intent_type_name(input_type));
    bool ok = te_replace_byte_range_no_events(live_elem, state, live_target, start, end,
                                              repl, repl_len);
    te_history_input_type_restore(state, previous_history_input_type);
    if (ok) {
        FormControlProp* form = live_elem->form;
        uint32_t new_len = event_log_text_len(form ? form->value : nullptr);
        uint32_t selection_start = form ? form->selection_start : 0;
        uint32_t selection_end = form ? form->selection_end : 0;
        surface.view = live_target;
        surface.owner = live_elem;
        event_log_editing_mutation(state, &surface, &intent, "replace",
                                   old_len, new_len,
                                   selection_start, selection_end);
        uint32_t caret_offset = start + repl_len;
        if (caret_offset > new_len) caret_offset = new_len;
        event_log_editing_selection(state, &surface, &intent,
                                    "replaceCollapse",
                                    caret_offset, caret_offset);
        editing_dispatch_form_input(evcon, &surface, &intent, &hooks);
        sm_observe_action(state, SM_ACT_DISPATCH_INPUT);
        sm_guard.commit();
    }
    return ok;
}

static void restore_form_text_focus_after_input(DocState* state,
                                                DomDocument* doc,
                                                const char* id) {
    if (!state || focus_get(state) || !doc || !doc->root || !id || !id[0]) {
        return;
    }
    DomElement* live_elem = js_dom_find_element_by_id(doc->root, id);
    if (!live_elem || !tc_is_text_control(live_elem)) return;
    focus_set(state, static_cast<View*>(live_elem), false);
}

static uint32_t dispatch_form_text_paste(EventContext* evcon, DomElement* elem,
                                         DocState* state, View* target,
                                         const char* text, uint32_t len) {
    if (!evcon || !elem || !state || !target || !text || len == 0) return 0;
    uint32_t start = 0, end = 0;
    if (!te_prepare_paste_range(elem, state, &start, &end)) {
        return 0;
    }

    EditingSurface surface;
    EditingSurface* surface_ptr = nullptr;
    if (editing_surface_from_target(target, &surface) &&
        editing_surface_is_text_control(&surface)) {
        surface_ptr = &surface;
    }
    event_log_editing_clipboard(state, surface_ptr, "paste", len, 0);

    // The raw clipboard text goes through as the intent data: the applier owns
    // newline normalization and maxlength, so handing it a pre-sanitized copy
    // would leave the policy here after all. The count returned is what was
    // offered, not what the applier chose to keep.
    bool ok = dispatch_form_text_replace(evcon, elem, state, target,
                                         start, end,
                                         text, len,
                                         INPUT_INTENT_INSERT_FROM_PASTE);
    return ok ? len : 0;
}

static bool dispatch_context_menu_cut(void* user, DomElement* elem,
                                      DocState* state,
                                      uint32_t start, uint32_t end) {
    EventContext* evcon = (EventContext*)user;
    if (!evcon || !elem || !state) return false;
    return dispatch_form_text_replace(evcon, elem, state, static_cast<View*>(elem),
                                      start, end, nullptr, 0,
                                      INPUT_INTENT_DELETE_BY_CUT);
}

static bool dispatch_context_menu_delete(void* user, DomElement* elem,
                                         DocState* state,
                                         uint32_t start, uint32_t end) {
    EventContext* evcon = (EventContext*)user;
    if (!evcon || !elem || !state) return false;
    return dispatch_form_text_replace(evcon, elem, state, static_cast<View*>(elem),
                                      start, end, nullptr, 0,
                                      INPUT_INTENT_DELETE_CONTENT_FORWARD);
}

static bool dispatch_context_menu_paste(void* user, DomElement* elem,
                                        DocState* state,
                                        const char* text, uint32_t len) {
    EventContext* evcon = (EventContext*)user;
    if (!evcon || !elem || !state) return false;
    return dispatch_form_text_paste(evcon, elem, state, static_cast<View*>(elem),
                                    text, len) > 0;
}

static void dispatch_selectstart(EventContext* evcon, View* target);

static bool dispatch_form_selection_byte_range(DomElement* elem, DocState* state,
                                               View* target,
                                               uint32_t* out_start,
                                               uint32_t* out_end) {
    if (out_start) *out_start = 0;
    if (out_end) *out_end = 0;
    if (!elem || !state || !target || !tc_is_text_control(elem)) return false;

    tc_ensure_init(elem);
    tc_sync_selection_to_form(elem, state);
    FormControlProp* form = elem->form;
    if (!form || !form->current_value) return false;

    uint32_t start16 = 0;
    uint32_t end16 = 0;
    form_control_get_selection(state, target, &start16, &end16, nullptr);
    if (start16 == end16) return false;
    if (start16 > end16) {
        uint32_t t = start16;
        start16 = end16;
        end16 = t;
    }

    uint32_t start8 = tc_utf16_to_utf8_offset(form->current_value,
                                              form->current_value_len,
                                              start16);
    uint32_t end8 = tc_utf16_to_utf8_offset(form->current_value,
                                            form->current_value_len,
                                            end16);
    if (start8 > end8) {
        uint32_t t = start8;
        start8 = end8;
        end8 = t;
    }
    if (start8 > form->current_value_len) start8 = form->current_value_len;
    if (end8 > form->current_value_len) end8 = form->current_value_len;
    if (start8 == end8) return false;

    if (out_start) *out_start = start8;
    if (out_end) *out_end = end8;
    return true;
}

static bool dispatch_form_copy_selection(EventContext* evcon, DomElement* elem,
                                         DocState* state, View* target,
                                         const char* prefix) {
    (void)evcon;
    uint32_t start = 0;
    uint32_t end = 0;
    if (!dispatch_form_selection_byte_range(elem, state, target,
                                            &start, &end)) {
        return false;
    }
    FormControlProp* form = elem->form;
    char* buf = (char*)mem_alloc((size_t)(end - start) + 1, MEM_CAT_TEMP);
    if (!buf) return false;
    memcpy(buf, form->current_value + start, end - start);
    buf[end - start] = '\0';
    clipboard_copy_text(buf);
    log_debug("%s: copied form selection bytes=%u",
              prefix ? prefix : "form copy", end - start);
    EditingSurface surface;
    EditingSurface* surface_ptr = nullptr;
    if (editing_surface_from_target(target, &surface) &&
        editing_surface_is_text_control(&surface)) {
        surface_ptr = &surface;
    }
    const char* op = (prefix && strstr(prefix, "cut")) ? "cut" : "copy";
    event_log_editing_clipboard(state, surface_ptr, op, end - start, 0);
    mem_free(buf);
    return true;
}

static bool dispatch_form_cut_selection(EventContext* evcon, DomElement* elem,
                                        DocState* state, View* target) {
    if (!evcon || !elem || !state || !target) return false;
    bool editable = !form_control_is_user_readonly(state, static_cast<View*>(elem));
    if (!editable) return false;

    uint32_t start = 0;
    uint32_t end = 0;
    if (!dispatch_form_selection_byte_range(elem, state, target,
                                            &start, &end)) {
        return false;
    }
    if (!dispatch_form_copy_selection(evcon, elem, state, target, "form cut")) {
        return false;
    }
    return dispatch_form_text_replace(evcon, elem, state, target,
                                      start, end, nullptr, 0,
                                      INPUT_INTENT_DELETE_BY_CUT);
}

static const char* form_control_live_value(DomElement* elem, uint32_t* out_len) {
    if (out_len) *out_len = 0;
    if (!elem || !elem->form) return "";

    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    const char* value = form->current_value ? form->current_value : form->value;
    uint32_t value_len = form->current_value
        ? form->current_value_len
        : event_log_text_len(value);
    if (out_len) *out_len = value_len;
    return value ? value : "";
}

static bool dispatch_form_editing_surface(EventContext* evcon, DomElement* elem,
                                          DocState* state, View* target,
                                          EditingSurface* surface) {
    return evcon && elem && state && target && tc_is_text_control(elem) &&
        editing_surface_from_target(target, surface) &&
        editing_surface_is_text_control(surface);
}

static bool dispatch_form_select_all(EventContext* evcon, DomElement* elem,
                                     DocState* state, View* target) {
    EditingSurface surface;
    if (!dispatch_form_editing_surface(evcon, elem, state, target, &surface)) return false;

    InputIntent intent;
    intent.type = INPUT_INTENT_SELECT_ALL;
    uint32_t value_len = 0;
    form_control_live_value(elem, &value_len);
    state_store_selection_start_pointer(state, target, 0);
    state_store_selection_extend_to_offset(state, (int)value_len); // INT_CAST_OK: StateStore selection API uses int offsets.
    selection_transition(state, SELECTION_TRANSITION_END_POINTER_SELECTION, NULL);
    tc_sync_selection_to_form(elem, state);
    event_log_editing_selection(state, &surface, &intent, "selectAll",
                                0, value_len);
    return true;
}

static bool dispatch_context_menu_select_all(void* user, DomElement* elem,
                                             DocState* state) {
    EventContext* evcon = (EventContext*)user;
    if (!evcon || !elem || !state) return false;
    return dispatch_form_select_all(evcon, elem, state, static_cast<View*>(elem));
}

static bool dispatch_form_caret_collapse(EventContext* evcon, DomElement* elem,
                                         DocState* state, View* target,
                                         uint32_t offset,
                                         const char* operation) {
    EditingSurface surface;
    if (!dispatch_form_editing_surface(evcon, elem, state, target, &surface)) return false;

    uint32_t value_len = 0;
    form_control_live_value(elem, &value_len);
    if (offset > value_len) offset = value_len;

    state_store_caret_collapse_to_view_offset(state, target, (int)offset); // INT_CAST_OK: StateStore caret API uses int offsets.
    tc_sync_selection_to_form(elem, state);
    event_log_editing_selection(state, &surface, nullptr,
                                operation ? operation : "collapse",
                                offset, offset);
    return true;
}

static void dispatch_form_keyboard_paste(EventContext* evcon, DocState* state,
                                         View* focused) {
    const char* clip = clipboard_get_text();
    if (clip && *clip) {
        evcon->paste_text = clip;
        dispatch_lambda_handler(evcon, focused, "paste");
        evcon->paste_text = nullptr;
        focused = focus_get(state);
        if (focused && focused->is_element()) {
            DomElement* elem = lam::dom_require_element(focused);
            if (tc_is_text_control(elem)) {
                dispatch_form_text_paste(evcon, elem, state, focused,
                                         clip, (uint32_t)strlen(clip));
            }
        }
    }
    evcon->need_repaint = true;
}

static bool dispatch_form_selection_extend(EventContext* evcon, DomElement* elem,
                                           DocState* state, View* target,
                                           int anchor_offset, int focus_offset,
                                           const char* operation) {
    EditingSurface surface;
    if (!dispatch_form_editing_surface(evcon, elem, state, target, &surface)) return false;

    uint32_t live_value_len = 0;
    form_control_live_value(elem, &live_value_len);
    int value_len = (int)live_value_len; // INT_CAST_OK: text-control byte offsets use StateStore int APIs.
    if (anchor_offset < 0) anchor_offset = 0;
    if (focus_offset < 0) focus_offset = 0;
    if (anchor_offset > value_len) anchor_offset = value_len;
    if (focus_offset > value_len) focus_offset = value_len;

    int log_anchor = anchor_offset;
    View* existing_anchor_view = nullptr;
    int existing_anchor_offset = 0;
    if (selection_get_pointer_anchor(state, &existing_anchor_view,
                                     &existing_anchor_offset) &&
        existing_anchor_view == target) {
        log_anchor = existing_anchor_offset;
    } else if (selection_get_anchor_snapshot(state, &existing_anchor_view,
                                             &existing_anchor_offset, nullptr) &&
               existing_anchor_view == target) {
        anchor_offset = existing_anchor_offset;
        log_anchor = existing_anchor_offset;
    } else {
        state_store_selection_start_pointer(state, target, anchor_offset);
        selection_transition(state, SELECTION_TRANSITION_END_POINTER_SELECTION, NULL);
    }

    state_store_selection_extend_to_offset(state, focus_offset);
    tc_sync_selection_to_form(elem, state);
    event_log_editing_selection(state, &surface, nullptr,
                                operation ? operation : "extend",
                                (uint32_t)log_anchor, (uint32_t)focus_offset);
    return true;
}

static void dispatch_form_navigation(EventContext* evcon, DomElement* elem,
                                     DocState* state, View* target,
                                     int current_offset, uint32_t destination,
                                     bool extend, const char* extend_operation,
                                     const char* move_operation) {
    if (extend) {
        dispatch_form_selection_extend(evcon, elem, state, target,
                                       current_offset, (int)destination, // INT_CAST_OK: StateStore selection API uses int offsets.
                                       extend_operation);
    } else {
        dispatch_form_caret_collapse(evcon, elem, state, target,
                                     destination, move_operation);
    }
}

static bool dispatch_form_selection_start(EventContext* evcon, DomElement* elem,
                                          DocState* state, View* target,
                                          uint32_t offset,
                                          const char* operation) {
    EditingSurface surface;
    if (!dispatch_form_editing_surface(evcon, elem, state, target, &surface)) return false;

    tc_ensure_init(elem);
    uint32_t value_len = elem->form ? elem->form->current_value_len : 0;
    if (offset > value_len) offset = value_len;

    SmTransitionGuard sm_guard(state, SM_FAMILY_SELECTION,
                               SM_EV_UI_START_POINTER_SELECTION, target);
    dispatch_selectstart(evcon, target);
    state_store_selection_start_pointer(state, target, (int)offset); // INT_CAST_OK: StateStore selection API uses int offsets.
    sm_guard.commit();
    tc_sync_selection_to_form(elem, state);
    event_log_editing_selection(state, &surface, nullptr,
                                operation ? operation : "start",
                                offset, offset);
    return true;
}

static bool dispatch_form_selection_range(EventContext* evcon, DomElement* elem,
                                          DocState* state, View* target,
                                          uint32_t start, uint32_t end,
                                          const char* operation) {
    EditingSurface surface;
    if (!dispatch_form_editing_surface(evcon, elem, state, target, &surface)) return false;

    tc_ensure_init(elem);
    uint32_t value_len = elem->form ? elem->form->current_value_len : 0;
    if (start > value_len) start = value_len;
    if (end > value_len) end = value_len;
    if (end < start) {
        uint32_t t = start;
        start = end;
        end = t;
    }

    if (start == end) {
        if (selection_has_projection(state)) state_store_selection_clear(state);
        state_store_caret_collapse_to_view_offset(state, target, (int)start); // INT_CAST_OK: StateStore caret API uses int offsets.
    } else {
        state_store_selection_start_pointer(state, target, (int)start); // INT_CAST_OK: StateStore selection API uses int offsets.
        state_store_selection_extend_to_offset(state, (int)end); // INT_CAST_OK: StateStore selection API uses int offsets.
    }
    selection_finish_active_gesture(state);
    tc_sync_selection_to_form(elem, state);
    event_log_editing_selection(state, &surface, nullptr,
                                operation ? operation : "selectRange",
                                start, end);
    return true;
}

static void dispatch_form_select_word(EventContext* evcon, DomElement* elem,
                                      DocState* state, View* target,
                                      int byte_offset) {
    FormControlProp* form = elem ? elem->form : nullptr;
    const char* value = form ? form->current_value : nullptr;
    uint32_t value_len = form ? form->current_value_len : 0;
    if (!value || value_len == 0) return;

    uint32_t click_offset = byte_offset < 0 ? 0 : (uint32_t)byte_offset;
    uint32_t start = te_word_start(value, value_len, click_offset);
    uint32_t end = te_word_end(value, value_len, click_offset);
    if (start != end) {
        dispatch_form_selection_range(
            evcon, elem, state, target, start, end, "selectWord");
    }
}

static bool dispatch_form_history_restore_selection(DomElement* elem,
                                                    DocState* state,
                                                    View* target,
                                                    EditingSurface* surface,
                                                    InputIntent* intent) {
    if (!elem || !state || !target || !surface) return false;
    if (!tc_is_text_control(elem)) return false;

    FormControlProp* form = elem->form;
    if (!form) return false;
    const char* value = form->current_value ? form->current_value : "";
    uint32_t value_len = form->current_value ? form->current_value_len : 0;
    uint32_t start8 = tc_utf16_to_utf8_offset(value, value_len,
                                              form->selection_start);
    uint32_t end8 = tc_utf16_to_utf8_offset(value, value_len,
                                            form->selection_end);
    if (start8 > value_len) start8 = value_len;
    if (end8 > value_len) end8 = value_len;

    if (start8 == end8) {
        if (selection_has_projection(state)) state_store_selection_clear(state);
        state_store_caret_collapse_to_view_offset(state, target, (int)start8); // INT_CAST_OK: StateStore caret API uses int offsets.
    } else if (form->selection_direction == 2) {
        state_store_selection_start_pointer(state, target, (int)end8); // INT_CAST_OK: StateStore selection API uses int offsets.
        state_store_selection_extend_to_offset(state, (int)start8); // INT_CAST_OK: StateStore selection API uses int offsets.
    } else {
        state_store_selection_start_pointer(state, target, (int)start8); // INT_CAST_OK: StateStore selection API uses int offsets.
        state_store_selection_extend_to_offset(state, (int)end8); // INT_CAST_OK: StateStore selection API uses int offsets.
    }
    tc_sync_selection_to_form(elem, state);
    event_log_editing_selection(state, surface, intent, "historyRestore",
                                start8, end8);
    return true;
}

static bool dispatch_form_history(EventContext* evcon, DomElement* elem,
                                  DocState* state, View* target,
                                  InputIntentType input_type) {
    if (!evcon || !elem || !state || !target) return false;
    if (!tc_is_text_control(elem)) return false;
    if (input_type != INPUT_INTENT_HISTORY_UNDO &&
        input_type != INPUT_INTENT_HISTORY_REDO) {
        return false;
    }

    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_text_control(&surface)) {
        // No editing surface means no beforeinput and therefore no applier.
        // The ring is still native, but installing an entry is the template's
        // job now, so there is nothing to do here.
        return false;
    }

    InputIntent intent;
    intent.type = input_type;
    intent.data = "";

    // ES17: peek the entry this would restore and put it on the event, so a
    // behavior template can install it. The cursor does NOT move here — it
    // moves only once the entry is consumed, below.
    bool redo = input_type == INPUT_INTENT_HISTORY_REDO;
    const char* hist_value = nullptr;
    uint32_t hist_len = 0, hist_start_u16 = 0, hist_end_u16 = 0;
    if (te_history_peek(elem, redo, &hist_value, &hist_len,
                        &hist_start_u16, &hist_end_u16)) {
        intent.history_value = hist_value;
        // The entry's selection is UTF-16 against its own snapshot; Lambda
        // speaks codepoints, so convert against that snapshot, not the value
        // currently in the control.
        uint32_t sb = tc_utf16_to_utf8_offset(hist_value, hist_len, hist_start_u16);
        uint32_t eb = tc_utf16_to_utf8_offset(hist_value, hist_len, hist_end_u16);
        intent.history_sel_start = (uint32_t)str_utf8_byte_to_char(hist_value, hist_len, sb);
        intent.history_sel_end = (uint32_t)str_utf8_byte_to_char(hist_value, hist_len, eb);
    }

    EditingFormNotificationHooks hooks = form_editing_notification_hooks();

    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_TEXT,
                               SM_EV_FORM_HISTORY, target);
    bool prevented = false;
    bool applied_by_template = false;
    // A restore must not re-push. The waist's write path records history on
    // every mutation, so bracket whatever the template does with the same
    // guard the ring's own restore used to use.
    tc_history_guard_enter(state);
    editing_dispatch_form_beforeinput(evcon, &surface, &intent, &hooks,
                                      &prevented, &applied_by_template);
    tc_history_guard_exit(state);
    sm_observe_action(state, SM_ACT_DISPATCH_BEFOREINPUT);
    if (prevented && !applied_by_template) {
        // Cancelled by JS: no restore and no cursor movement, so the same undo
        // is still available next time.
        log_debug("dispatch_form_history: beforeinput prevented inputType=%s",
                  input_intent_type_name(input_type));
        return true;
    }

    uint32_t old_len = event_log_text_len(elem->form ? elem->form->value : nullptr);
    // The template installs the entry; native only moves the cursor onto it.
    // There is no native apply behind this — a control no template governs does
    // not undo, the same way it does not validate (ES17 with no fallback).
    bool did = applied_by_template && te_history_step(elem, redo);
    if (did) {
        FormControlProp* form = elem->form;
        uint32_t new_len = event_log_text_len(form ? form->value : nullptr);
        uint32_t selection_start = form ? form->selection_start : 0;
        uint32_t selection_end = form ? form->selection_end : 0;
        EditHistory* history =
            (EditHistory*)form_control_history_get(state, static_cast<View*>(elem));
        uint32_t depth = history ? history->count : 0;
        uint32_t cursor = history ? history->cursor : 0;
        event_log_editing_history(state, &surface, &intent,
                                  input_type == INPUT_INTENT_HISTORY_UNDO
                                      ? "undo"
                                      : "redo",
                                  depth, cursor, did);
        event_log_editing_mutation(state, &surface, &intent, "history",
                                   old_len, new_len,
                                   selection_start, selection_end);
        dispatch_form_history_restore_selection(elem, state, target,
                                                &surface, &intent);
        editing_dispatch_form_input(evcon, &surface, &intent, &hooks);
        sm_observe_action(state, SM_ACT_DISPATCH_INPUT);
        sm_guard.commit();
    }
    return did;
}

static bool dispatch_editing_history_for_controller(EventContext* evcon,
                                                    const EditingSurface* surface,
                                                    InputIntentType input_type,
                                                    void* userdata) {
    (void)userdata;
    if (!evcon || !surface) return false;
    if (input_type != INPUT_INTENT_HISTORY_UNDO &&
        input_type != INPUT_INTENT_HISTORY_REDO) {
        return false;
    }

    if (editing_surface_is_text_control(surface)) {
        DomDocument* doc = event_context_target_document(evcon);
        DocState* state = doc ? (DocState*)doc->state : nullptr;
        DomElement* elem = surface->owner;
        View* target = surface->view ? surface->view : static_cast<View*>(elem);
        return dispatch_form_history(evcon, elem, state, target, input_type);
    }

    return false;
}

static bool dispatch_form_history_via_controller(EventContext* evcon,
                                                 DomElement* elem,
                                                 DocState* state,
                                                 View* target,
                                                 InputIntentType input_type) {
    if (!evcon || !elem || !state || !target) return false;
    EditingSurface surface;
    if (editing_surface_from_target(target, &surface) &&
        editing_surface_is_text_control(&surface)) {
        EditingControllerHooks hooks = editing_controller_hooks();
        if (input_type == INPUT_INTENT_HISTORY_UNDO) {
            return editing_undo(evcon, &surface, &hooks);
        }
        if (input_type == INPUT_INTENT_HISTORY_REDO) {
            return editing_redo(evcon, &surface, &hooks);
        }
    }
    return dispatch_form_history(evcon, elem, state, target, input_type);
}

// F11: translate the package-owned key intent into the one native mechanism
// that performs it. The package chooses every command; this helper retains
// canonical selection, buffer ownership, beforeinput/input emission, history,
// and clipboard access in the engine.
static bool dispatch_form_key_intent(EventContext* evcon, DomElement* elem,
                                     DocState* state, View* target,
                                     const KeyEvent* key_event, int caret) {
    InputIntent intent;
    if (!input_intent_from_key_event(state, key_event, &intent)) return false;

    bool handled = true;
    switch (intent.type) {
        case INPUT_INTENT_COPY:
            dispatch_form_copy_selection(evcon, elem, state, target, "form input copy");
            break;
        case INPUT_INTENT_SELECT_ALL:
            dispatch_form_select_all(evcon, elem, state, target);
            break;
        case INPUT_INTENT_DELETE_BY_CUT:
            dispatch_form_cut_selection(evcon, elem, state, target);
            break;
        case INPUT_INTENT_INSERT_FROM_PASTE:
            dispatch_form_keyboard_paste(evcon, state, target);
            break;
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
            dispatch_form_history_via_controller(evcon, elem, state, target,
                                                 intent.type);
            break;
        case INPUT_INTENT_INSERT_PARAGRAPH:
        case INPUT_INTENT_INSERT_LINE_BREAK:
            if (elem->form->control_type == FORM_CONTROL_TEXTAREA) {
                dispatch_form_text_replace(evcon, elem, state, target,
                                           (uint32_t)caret, (uint32_t)caret,
                                           "\n", 1, intent.type);
            }
            break;
        case INPUT_INTENT_DELETE_CONTENT_BACKWARD:
        case INPUT_INTENT_DELETE_CONTENT_FORWARD:
        case INPUT_INTENT_DELETE_WORD_BACKWARD:
        case INPUT_INTENT_DELETE_WORD_FORWARD:
        case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
            dispatch_form_text_replace(evcon, elem, state, target,
                                       (uint32_t)caret, (uint32_t)caret,
                                       nullptr, 0, intent.type);
            break;
        default:
            handled = false;
            break;
    }
    if (handled) evcon->need_repaint = true;
    return handled;
}

static View* editing_text_drag_first_text_descendant(View* view) {
    if (!view) return nullptr;
    if (view->is_text()) return view;
    if (view->is_element()) {
        DomElement* elem = lam::dom_require_element(view);
        if (tc_is_text_control(elem)) return view;
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            View* found = editing_text_drag_first_text_descendant(static_cast<View*>(child));
            if (found) return found;
        }
    }
    return nullptr;
}

static View* editing_text_drag_range_view(View* view, const EditingSurface* surface) {
    if (!view || !surface) return view;
    if (editing_surface_is_text_control(surface)) {
        return surface->view ? surface->view : view;
    }
    if (editing_surface_is_rich(surface)) {
        View* text = editing_text_drag_first_text_descendant(view);
        return text ? text : view;
    }
    return view;
}

static uint32_t editing_text_drag_range_len(View* range_view,
                                            const EditingSurface* surface) {
    if (!range_view || !surface) return 0;
    if (editing_surface_is_text_control(surface)) {
        DomElement* elem = surface->owner;
        tc_ensure_init(elem);
        return (elem && elem->form) ? elem->form->current_value_len : 0;
    }
    if (range_view->is_text()) {
        DomText* text = lam::dom_require_text(static_cast<DomNode*>(range_view));
        return text && text->text ? (uint32_t)strlen(text->text) : 0;
    }
    return 0;
}

static void editing_text_drag_clamp_range(View* range_view,
                                          const EditingSurface* surface,
                                          uint32_t* start,
                                          uint32_t* end) {
    if (!start || !end) return;
    uint32_t len = editing_text_drag_range_len(range_view, surface);
    if (*start > len) *start = len;
    if (*end > len) *end = len;
    if (*end < *start) {
        uint32_t tmp = *start;
        *start = *end;
        *end = tmp;
    }
}

static char* editing_text_drag_copy_range_text(View* range_view,
                                               const EditingSurface* surface,
                                               uint32_t start,
                                               uint32_t end) {
    if (!range_view || !surface) return nullptr;
    editing_text_drag_clamp_range(range_view, surface, &start, &end);
    uint32_t len = end > start ? end - start : 0;
    char* out = (char*)mem_alloc((size_t)len + 1, MEM_CAT_TEMP);
    if (!out) return nullptr;
    const char* src = "";
    if (editing_surface_is_text_control(surface)) {
        DomElement* elem = surface->owner;
        tc_ensure_init(elem);
        src = (elem && elem->form && elem->form->current_value)
            ? elem->form->current_value
            : "";
    } else if (range_view->is_text()) {
        DomText* text = lam::dom_require_text(static_cast<DomNode*>(range_view));
        src = (text && text->text) ? text->text : "";
    }
    if (len > 0) memcpy(out, src + start, len);
    out[len] = '\0';
    return out;
}

static uint32_t editing_text_drag_adjust_after_delete(uint32_t pos,
                                                      uint32_t start,
                                                      uint32_t end) {
    if (end <= start) return pos;
    if (pos <= start) return pos;
    if (pos >= end) return pos - (end - start);
    return start;
}

static bool editing_text_drag_set_range(EventContext* evcon,
                                        const EditingSurface* surface,
                                        View* range_view,
                                        uint32_t start,
                                        uint32_t end,
                                        const char* operation) {
    if (!evcon || !surface || !range_view) return false;
    DocState* state = event_context_target_state(evcon);
    if (!state) return false;
    editing_text_drag_clamp_range(range_view, surface, &start, &end);
    if (editing_surface_is_text_control(surface)) {
        return dispatch_form_selection_range(evcon, surface->owner, state,
                                             surface->view, start, end,
                                             operation);
    }
    if (start == end) {
        if (selection_has_projection(state)) state_store_selection_clear(state);
        state_store_caret_collapse_to_view_offset(state, range_view, (int)start); // INT_CAST_OK: StateStore caret API uses int offsets.
    } else {
        state_store_selection_set_view_offsets(state, range_view, (int)start, (int)end); // INT_CAST_OK: StateStore selection API uses int offsets.
    }
    dispatch_rich_selection_snapshot(evcon, state, range_view,
                                     operation ? operation : "dragDropRange",
                                     nullptr);
    return true;
}

static bool editing_text_drag_dispatch_delete(EventContext* evcon,
                                              const EditingSurface* surface,
                                              View* range_view,
                                              uint32_t start,
                                              uint32_t end) {
    if (!evcon || !surface || !range_view) return false;
    if (end <= start) return true;
    DocState* state = event_context_target_state(evcon);
    if (!state) return false;
    editing_text_drag_set_range(evcon, surface, range_view, start, end,
                                "dragSource");
    if (editing_surface_is_text_control(surface)) {
        tc_ensure_init(surface->owner);
        return dispatch_form_text_replace(evcon, surface->owner, state,
                                          surface->view, start, end,
                                          nullptr, 0,
                                          INPUT_INTENT_DELETE_BY_DRAG);
    }
    InputIntent intent;
    intent.type = INPUT_INTENT_DELETE_BY_DRAG;
    intent.data = "";
    return dispatch_contenteditable_consumer_event(evcon, range_view, &intent);
}

static bool editing_text_drag_dispatch_insert(EventContext* evcon,
                                              const EditingSurface* surface,
                                              View* range_view,
                                              uint32_t start,
                                              uint32_t end,
                                              const char* payload,
                                              const char* html_payload) {
    if (!evcon || !surface || !range_view) return false;
    DocState* state = event_context_target_state(evcon);
    if (!state) return false;
    const char* text = payload ? payload : "";
    uint32_t text_len = (uint32_t)strlen(text);
    // A cross-surface drop starts an editing intention in the destination.
    // Transfer focus before setting its range so an embedded source control
    // cannot remain focused while an edit targets its outer host.
    if (surface->owner) {
        update_focus_state(evcon, static_cast<View*>(surface->owner), false);
    }
    editing_text_drag_set_range(evcon, surface, range_view, start, end,
                                "dropTarget");
    if (editing_surface_is_text_control(surface)) {
        tc_ensure_init(surface->owner);
        return dispatch_form_text_replace(evcon, surface->owner, state,
                                          surface->view, start, end,
                                          text, text_len,
                                          INPUT_INTENT_INSERT_FROM_DROP);
    }
    InputIntent intent;
    intent.type = INPUT_INTENT_INSERT_FROM_DROP;
    intent.data = text;
    intent.html_data = html_payload && html_payload[0] ? html_payload : nullptr;
    intent.data_mime = intent.html_data ? "text/html" : "text/plain";
    return dispatch_contenteditable_consumer_event(evcon, range_view, &intent);
}

static bool dispatch_rich_drop_at_range(EventContext* evcon,
                                        View* target,
                                        const DomBoundary* start,
                                        const DomBoundary* end,
                                        const char* payload) {
    if (!evcon || !target || !start || !end ||
        !start->node || !end->node) {
        return false;
    }
    DocState* state = event_context_target_state(evcon);
    if (!state) return false;

    InputIntent intent;
    intent.type = INPUT_INTENT_INSERT_FROM_DROP;
    intent.data = payload ? payload : "";
    intent.data_mime = "text/plain";

    const char* exc = nullptr;
    if (!state_store_set_selection(state, start, end, &exc)) {
        log_debug("dispatch_rich_drop_at_range: drop range rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    selection_transition(state, SELECTION_TRANSITION_END_POINTER_SELECTION,
                         nullptr);
    dispatch_rich_selection_snapshot(evcon, state, target, "dropTarget",
                                     &intent);

    return dispatch_contenteditable_consumer_event(evcon, target, &intent);
}

extern "C" bool radiant_dispatch_editing_text_drag_drop(UiContext* uicon,
                                                         View* source,
                                                         uint32_t source_start,
                                                         uint32_t source_end,
                                                         View* target,
                                                         uint32_t target_start,
                                                         uint32_t target_end,
                                                         const char* payload,
                                                         const char* html_payload,
                                                         bool move) {
    if (!uicon || !uicon->document || !source || !target) return false;
    DocState* state = (DocState*)uicon->document->state;
    if (!state) return false;

    EditingSurface source_surface;
    EditingSurface target_surface;
    if (!editing_surface_from_target(source, &source_surface) ||
        !editing_surface_from_target(target, &target_surface)) {
        log_error("radiant_dispatch_editing_text_drag_drop: missing editing surface");
        return false;
    }
    if ((!editing_surface_is_text_control(&source_surface) &&
         !editing_surface_is_rich(&source_surface)) ||
        (!editing_surface_is_text_control(&target_surface) &&
         !editing_surface_is_rich(&target_surface))) {
        log_error("radiant_dispatch_editing_text_drag_drop: unsupported surface");
        return false;
    }

    View* source_range_view = editing_text_drag_range_view(source, &source_surface);
    View* target_range_view = editing_text_drag_range_view(target, &target_surface);
    editing_text_drag_clamp_range(source_range_view, &source_surface,
                                  &source_start, &source_end);
    editing_text_drag_clamp_range(target_range_view, &target_surface,
                                  &target_start, &target_end);

    char* owned_payload = nullptr;
    const char* drop_text = payload;
    if (!drop_text) {
        owned_payload = editing_text_drag_copy_range_text(source_range_view,
                                                         &source_surface,
                                                         source_start,
                                                         source_end);
        drop_text = owned_payload ? owned_payload : "";
    }

    EventContext evcon;
    memset(&evcon, 0, sizeof(evcon));
    evcon.ui_context = uicon;
    evcon.target = source_range_view;

    bool ok = true;
    if (move && source_end > source_start) {
        ok = editing_text_drag_dispatch_delete(&evcon, &source_surface,
                                               source_range_view,
                                               source_start, source_end);
        if (ok && editing_surface_is_text_control(&source_surface) &&
            editing_surface_is_text_control(&target_surface) &&
            source_surface.owner == target_surface.owner) {
            target_start = editing_text_drag_adjust_after_delete(target_start,
                                                                 source_start,
                                                                 source_end);
            target_end = editing_text_drag_adjust_after_delete(target_end,
                                                               source_start,
                                                               source_end);
        }
    }
    if (ok) {
        evcon.target = target_range_view;
        ok = editing_text_drag_dispatch_insert(&evcon, &target_surface,
                                               target_range_view,
                                               target_start, target_end,
                                               drop_text, html_payload);
    }
    if (owned_payload) mem_free(owned_payload);
    doc_state_request_repaint(state);
    return ok;
}

typedef struct FormImeDispatchContext {
    DocState* state;
    View* target;
    EditingSurface surface;
    EditingSurface* surface_ptr;
    EventContext event;
} FormImeDispatchContext;

static bool prepare_form_ime_dispatch(UiContext* uicon, DomElement* elem,
                                      View* target, bool require_text_control,
                                      bool require_surface,
                                      FormImeDispatchContext* context) {
    if (!uicon || !uicon->document || !elem || !context) return false;
    context->state = (DocState*)uicon->document->state;
    if (!context->state || (require_text_control && !tc_is_text_control(elem))) {
        return false;
    }
    context->target = target ? target : static_cast<View*>(elem);
    editing_surface_clear(&context->surface);
    context->surface_ptr = nullptr;
    if (editing_surface_from_target(context->target, &context->surface) &&
        editing_surface_is_text_control(&context->surface)) {
        context->surface_ptr = &context->surface;
    }
    if (require_surface && !context->surface_ptr) return false;
    memset(&context->event, 0, sizeof(context->event));
    context->event.ui_context = uicon;
    context->event.target = context->target;
    return true;
}

extern "C" bool radiant_dispatch_form_text_ime_begin(UiContext* uicon,
                                                      DomElement* elem,
                                                      View* target) {
    FormImeDispatchContext context;
    if (!prepare_form_ime_dispatch(uicon, elem, target, true, false, &context)) return false;

    InputIntent intent;
    intent.type = INPUT_INTENT_COMPOSITION_START;
    intent.data = "";
    intent.is_composing = true;

    editing_interaction_set_composing(context.state, context.surface_ptr, true);
    radiant_dispatch_composition_event(&context.event, context.target,
                                       "compositionstart", "");
    event_log_editing_composition(context.state, context.surface_ptr, &intent,
                                  "start", 0, 0, 0);
    doc_state_request_repaint(context.state);
    return true;
}

extern "C" bool radiant_dispatch_form_text_ime_update(UiContext* uicon,
                                                       DomElement* elem,
                                                       View* target,
                                                       const char* preedit,
                                                       uint32_t len,
                                                       uint32_t caret_cp) {
    FormImeDispatchContext context;
    if (!prepare_form_ime_dispatch(uicon, elem, target, true, true, &context)) return false;

    InputIntent intent;
    intent.type = INPUT_INTENT_INSERT_COMPOSITION_TEXT;
    intent.data = preedit ? preedit : "";
    intent.composition_caret = caret_cp;
    intent.is_composing = true;

    EditingFormNotificationHooks hooks = form_editing_notification_hooks();

    context.event.composition_caret_hint = caret_cp;
    radiant_dispatch_composition_event(&context.event, context.target,
                                       "compositionupdate",
                                       preedit ? preedit : "");
    context.event.composition_caret_hint = 0;
    bool prevented = false;
    editing_dispatch_form_beforeinput(&context.event, &context.surface, &intent, &hooks,
                                      &prevented);
    if (prevented) {
        log_debug("radiant_dispatch_form_text_ime_update: beforeinput prevented");
        event_log_editing_composition(context.state, context.surface_ptr, &intent,
                                      "update", len, 0, caret_cp);
        return true;
    }
    editing_interaction_set_composing(context.state, context.surface_ptr, true);
    editing_dispatch_form_input(&context.event, &context.surface, &intent, &hooks);
    event_log_editing_composition(context.state, context.surface_ptr, &intent,
                                  "update", len, 0, caret_cp);
    doc_state_request_repaint(context.state);
    return true;
}

extern "C" bool radiant_dispatch_form_text_ime_commit(UiContext* uicon,
                                                       DomElement* elem,
                                                       View* target,
                                                       const char* committed,
                                                       uint32_t len) {
    FormImeDispatchContext context;
    if (!prepare_form_ime_dispatch(uicon, elem, target, false, false, &context)) return false;

    uint32_t start = 0, end = 0;
    bool should_mutate = false;
    if (!te_ime_commit_prepare(elem, context.state, committed, len,
                               &start, &end, &should_mutate)) {
        return false;
    }
    InputIntent intent;
    intent.type = (committed && committed[0])
        ? INPUT_INTENT_INSERT_FROM_COMPOSITION
        : INPUT_INTENT_DELETE_COMPOSITION_TEXT;
    intent.data = committed ? committed : "";
    intent.is_composing = false;

    radiant_dispatch_composition_event(&context.event, context.target,
                                       "compositionend",
                                       committed ? committed : "");

    if (should_mutate) {
        dispatch_form_text_replace(&context.event, elem, context.state, context.target,
                                   start, end, committed, len,
                                   INPUT_INTENT_INSERT_FROM_COMPOSITION);
    } else {
        EditingFormNotificationHooks hooks = form_editing_notification_hooks();

        bool prevented = false;
        if (intent.type == INPUT_INTENT_DELETE_COMPOSITION_TEXT && context.surface_ptr) {
            editing_dispatch_form_beforeinput(&context.event, context.surface_ptr, &intent,
                                              &hooks, &prevented);
        }
        if (!prevented && intent.type == INPUT_INTENT_DELETE_COMPOSITION_TEXT &&
            context.surface_ptr) {
            editing_dispatch_form_input(&context.event, context.surface_ptr, &intent, &hooks);
        }
    }
    editing_interaction_set_composing(context.state, context.surface_ptr, false);
    event_log_editing_composition(context.state, context.surface_ptr, &intent,
                                  (committed && committed[0]) ? "commit" : "cancel",
                                  0, len, 0);
    return true;
}

static void dispatch_selectstart(EventContext* evcon, View* target) {
    if (!evcon || !target) return;
    if (dispatch_lambda_handler(evcon, target, "selectstart")) {
        evcon->need_repaint = true;
    }
    sm_observe_action(event_context_target_state(evcon),
                      SM_ACT_DISPATCH_SELECTSTART);
}

static void dispatch_selectionchange(EventContext* evcon, DocState* state, View* target) {
    if (!evcon || !selection_has(state) || !target) return;
    if (dispatch_lambda_handler(evcon, target, "selectionchange")) {
        evcon->need_repaint = true;
    }
    // The JS `selectionchange` event is queued by the dom_range selection
    // notifier (js_dom_queue_selectionchange) and delivered to page-JS document
    // listeners when the event loop ticks; the headless simulator pumps it via
    // js_event_loop_pump_nowait between events.
}

extern "C" bool radiant_dispatch_editing_composition_event(UiContext* uicon,
                                                           EventType event_type,
                                                           const char* text,
                                                           uint32_t caret_cp) {
    if (!uicon || !uicon->document || !uicon->document->state) return false;
    if (event_type != RDT_EVENT_COMPOSITION_START &&
        event_type != RDT_EVENT_COMPOSITION_UPDATE &&
        event_type != RDT_EVENT_COMPOSITION_END) {
        return false;
    }

    DocState* state = (DocState*)uicon->document->state;
    View* focused = focus_get(state);
    View* target = focused ? focused : caret_get_view(state);
    EditingSurface surface;
    editing_surface_clear(&surface);
    if (state->editing.composition.active &&
        state->editing.composition.surface.kind != EDIT_SURFACE_NONE) {
        surface = state->editing.composition.surface;
    } else if (!target || !editing_surface_from_target(target, &surface)) {
        return false;
    }
    if (!editing_surface_is_text_control(&surface) &&
        !editing_surface_is_rich(&surface)) {
        return false;
    }

    RdtEvent event;
    memset(&event, 0, sizeof(event));
    event.composition.type = event_type;
    event.composition.timestamp = 0;
    event.composition.text = text ? text : "";
    event.composition.preedit_caret = caret_cp;
    handle_event(uicon, uicon->document, &event);
    return true;
}

extern "C" bool radiant_editing_focused_caret_rect(UiContext* uicon,
                                                   float* out_x,
                                                   float* out_y,
                                                   float* out_w,
                                                   float* out_h) {
    if (out_x) *out_x = 0.0f;
    if (out_y) *out_y = 0.0f;
    if (out_w) *out_w = 0.0f;
    if (out_h) *out_h = 0.0f;
    if (!uicon || !uicon->document || !uicon->document->state) return false;

    DocState* state = (DocState*)uicon->document->state;
    View* target = focus_get(state);
    if (!target) target = caret_get_view(state);
    if (!target) return false;

    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface)) return false;

    EditingCaretRect rect;
    editing_caret_rect_clear(&rect);
    if (editing_surface_is_text_control(&surface) && surface.owner &&
        surface.owner->form) {
        tc_ensure_init(surface.owner);
        const char* value = surface.owner->form->current_value
            ? surface.owner->form->current_value : surface.owner->form->value;
        uint32_t value_len = surface.owner->form->current_value_len;
        uint32_t caret_u16 = surface.owner->form->selection_end;
        uint32_t caret_utf8 = tc_utf16_to_utf8_offset(value ? value : "",
                                                      value_len, caret_u16);
        if (!editing_geometry_text_control_caret_rect(uicon, surface.owner,
                caret_utf8, &rect)) {
            return false;
        }
    } else {
        View* caret_view = nullptr;
        int caret_offset = 0;
        if (!caret_get_position(state, &caret_view, &caret_offset) ||
            !caret_view || !caret_view->is_text()) {
            return false;
        }
        uint32_t offset = caret_offset > 0
            ? (uint32_t)caret_offset : 0; // INT_CAST_OK: caret offset is clamped non-negative for byte-offset geometry.
        if (!editing_geometry_dom_text_caret_rect(uicon,
                lam::dom_require_text(caret_view), offset, &rect)) {
            return false;
        }
    }

    if (!rect.valid) return false;
    if (out_x) *out_x = rect.x;
    if (out_y) *out_y = rect.y;
    if (out_w) *out_w = rect.width;
    if (out_h) *out_h = rect.height;
    return true;
}

static bool dispatch_editing_composition_for_controller(EventContext* evcon,
                                                        const EditingSurface* surface,
                                                        const CompositionEvent* comp_event,
                                                        const EditingIntent* intent,
                                                        void* userdata) {
    (void)userdata;
    if (!evcon || !surface || !comp_event || !intent ||
        !evcon->ui_context || !evcon->ui_context->document) {
        return false;
    }

    if (editing_surface_is_text_control(surface) && surface->owner) {
        DomElement* elem = surface->owner;
        View* target = surface->view ? surface->view : static_cast<View*>(elem);
        uint32_t text_len = event_log_text_len(comp_event->text);
        if (comp_event->type == RDT_EVENT_COMPOSITION_START) {
            radiant_dispatch_form_text_ime_begin(evcon->ui_context,
                                                 elem, target);
        } else if (comp_event->type == RDT_EVENT_COMPOSITION_UPDATE) {
            radiant_dispatch_form_text_ime_update(evcon->ui_context,
                                                  elem, target,
                                                  comp_event->text,
                                                  text_len,
                                                  comp_event->preedit_caret);
        } else if (comp_event->type == RDT_EVENT_COMPOSITION_END) {
            radiant_dispatch_form_text_ime_commit(evcon->ui_context,
                                                  elem, target,
                                                  comp_event->text,
                                                  text_len);
        } else {
            return false;
        }
        evcon->need_repaint = true;
        return true;
    }

    if (!editing_surface_is_rich(surface)) return false;

    return dispatch_contenteditable_composition_event(evcon, surface, intent);
}

/**
 * Post-handler rebuild: after JS handler mutates DOM, re-cascade CSS and relayout.
 */
#ifndef NDEBUG
static const char* dom_js_mutation_kind_name(DomJsMutationKind kind) {
    switch (kind) {
        case DOM_JS_MUTATION_CHILD_INSERT: return "child-insert";
        case DOM_JS_MUTATION_CHILD_REMOVE: return "child-remove";
        case DOM_JS_MUTATION_TEXT: return "text";
        case DOM_JS_MUTATION_ATTRIBUTE: return "attribute";
        case DOM_JS_MUTATION_STYLE: return "style";
        case DOM_JS_MUTATION_TREE_REPLACE: return "tree-replace";
        case DOM_JS_MUTATION_STYLE_REPAINT: return "style-repaint";
        case DOM_JS_MUTATION_CONTROL_VALUE: return "control-value";
        case DOM_JS_MUTATION_UNKNOWN:
        default: return "unknown";
    }
}
#endif

static void dom_js_mutation_reset_records(DomDocument* doc) {
    dom_js_mutation_records_reset(doc);
}

static void dom_js_mutation_log_records(DomDocument* doc) {
#ifndef NDEBUG
    if (!doc) return;

    log_info("html handler mutations: count=%d records=%d overflow=%d kind_mask=0x%08x",
             doc->js.mutation_count,
             doc->js.mutation_record_count,
             doc->js.mutation_record_overflow,
             doc->js.mutation_kind_mask);

    int limit = doc->js.mutation_record_count < 8 ? doc->js.mutation_record_count : 8;
    for (int i = 0; i < limit; i++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[i];
        log_debug("html handler mutation record: seq=%u kind=%s target=%u parent=%u",
                  record->sequence,
                  dom_js_mutation_kind_name(record->kind),
                  record->target_id,
                  record->parent_id);
    }
#else
    (void)doc;
#endif
}

static void dom_js_record_reconcile(DomDocument* doc,
                                    DomReconcileMode mode,
                                    const char* reason,
                                    int mutations,
                                    int records,
                                    int overflow,
                                    const char* recascade_scope,
                                    const char* layout_scope,
                                    const char* state_action,
                                    int state_pruned) {
    if (!doc) return;
    doc->reconcile.mode = mode;
    doc->reconcile.reason = reason ? reason : "none";
    doc->reconcile.mutations = mutations;
    doc->reconcile.records = records;
    doc->reconcile.record_overflow = overflow;

    // Keep the reconcile decision self-contained in logs: mutation details are
    // otherwise separated from the layout/state-retention decision.
    log_info("dom mutation reconcile: mode=%s reason=%s recascade=%s layout=%s state=%s pruned=%d "
             "mutations=%d records=%d overflow=%d kind_mask=0x%08x",
             dom_reconcile_mode_name(mode),
             doc->reconcile.reason,
             recascade_scope ? recascade_scope : "unknown",
             layout_scope ? layout_scope : "unknown",
             state_action ? state_action : "unknown",
             state_pruned,
             mutations,
             records,
             overflow,
             doc->js.mutation_kind_mask);

    DocState* state = (DocState*)doc->state;
    if (!state || !event_state_log_enabled(state->active_event_log)) return;

    char buf[1024];
    JsonWriter w;
    event_state_log_begin_record(state->active_event_log, &w, buf, sizeof(buf),
                                 "dom.reconcile", state->active_cascade_id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "mode", dom_reconcile_mode_name(mode));
        jw_kv_str(&w, "reason", doc->reconcile.reason);
        jw_kv_int(&w, "mutations", mutations);
        jw_kv_int(&w, "records", records);
        jw_kv_int(&w, "record_overflow", overflow);
        jw_kv_str(&w, "recascade_scope", recascade_scope ? recascade_scope : "unknown");
        jw_kv_str(&w, "layout_scope", layout_scope ? layout_scope : "unknown");
        jw_kv_str(&w, "state_action", state_action ? state_action : "unknown");
        jw_kv_int(&w, "state_pruned", state_pruned);
    jw_obj_end(&w);
    event_state_log_finish_record(state->active_event_log, &w);
}

static void dom_js_clear_layout_dirty_recursive(DomNode* node) {
    if (!node) return;
    node->layout_dirty = false;
    if (node->is_element()) {
        DomElement* elem = lam::dom_require_element(node);
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            dom_js_clear_layout_dirty_recursive(child);
        }
    }
}

static bool dom_js_is_connected_to_document(DomDocument* doc, DomNode* node) {
    if (!doc || !node) return false;
    DomNode* root = static_cast<DomNode*>(doc->root);
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur == root) return true;
    }
    return false;
}

static DomElement* dom_js_record_cascade_root(DomDocument* doc,
                                              DomJsMutationRecord* record) {
    if (!doc || !record) return nullptr;

    DomNode* node = nullptr;
    if (record->kind == DOM_JS_MUTATION_CHILD_REMOVE) {
        node = record->parent;
    } else {
        node = record->target ? record->target : record->parent;
    }
    if (!node || !dom_js_is_connected_to_document(doc, node)) {
        node = record->parent;
    }
    if (!node || !dom_js_is_connected_to_document(doc, node)) return nullptr;
    if (node->is_element()) return lam::dom_require_element(node);
    if (node->parent && node->parent->is_element()) {
        return lam::dom_require_element(node->parent);
    }
    return nullptr;
}

static bool dom_js_record_has_connected_endpoint(DomDocument* doc,
                                                 DomJsMutationRecord* record) {
    if (!doc || !record) return false;
    DomNode* root = static_cast<DomNode*>(doc->root);
    if (record->target == root) return true;
    return dom_js_is_connected_to_document(doc, record->parent);
}

static bool dom_js_node_is_stylesheet_related(DomNode* node) {
    if (!node) return false;
    DomElement* elem = nullptr;
    if (node->is_element()) {
        elem = lam::dom_require_element(node);
    } else if (node->parent && node->parent->is_element()) {
        elem = lam::dom_require_element(node->parent);
    }
    if (!elem || !elem->tag_name) return false;
    return strcasecmp(elem->tag_name, "style") == 0 ||
           strcasecmp(elem->tag_name, "link") == 0;
}

static bool dom_js_simple_selector_has_structural_dependency(CssSimpleSelector* simple) {
    if (!simple) return false;

    switch (simple->type) {
        case CSS_SELECTOR_PSEUDO_EMPTY:
        case CSS_SELECTOR_PSEUDO_FIRST_CHILD:
        case CSS_SELECTOR_PSEUDO_LAST_CHILD:
        case CSS_SELECTOR_PSEUDO_ONLY_CHILD:
        case CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_LAST_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_ONLY_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_NTH_CHILD:
        case CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD:
        case CSS_SELECTOR_PSEUDO_NTH_OF_TYPE:
        case CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE:
            return true;
        case CSS_SELECTOR_PSEUDO_HAS:
            return true;
        default:
            break;
    }

    if (simple->function_selectors && simple->function_selector_count > 0) {
        for (size_t i = 0; i < simple->function_selector_count; i++) {
            CssSelector* selector = simple->function_selectors[i];
            if (!selector) continue;
            for (size_t part = 0; part + 1 < selector->compound_selector_count; part++) {
                CssCombinator combinator = selector->combinators[part];
                if (combinator == CSS_COMBINATOR_NEXT_SIBLING ||
                    combinator == CSS_COMBINATOR_SUBSEQUENT_SIBLING) {
                    return true;
                }
            }
            for (size_t part = 0; part < selector->compound_selector_count; part++) {
                CssCompoundSelector* compound = selector->compound_selectors[part];
                if (!compound) continue;
                for (size_t s = 0; s < compound->simple_selector_count; s++) {
                    if (dom_js_simple_selector_has_structural_dependency(
                            compound->simple_selectors[s])) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static bool dom_js_selector_has_structural_dependency(CssSelector* selector) {
    if (!selector) return false;

    for (size_t i = 0; i + 1 < selector->compound_selector_count; i++) {
        CssCombinator combinator = selector->combinators[i];
        if (combinator == CSS_COMBINATOR_NEXT_SIBLING ||
            combinator == CSS_COMBINATOR_SUBSEQUENT_SIBLING) {
            return true;
        }
    }

    for (size_t i = 0; i < selector->compound_selector_count; i++) {
        CssCompoundSelector* compound = selector->compound_selectors[i];
        if (!compound) continue;
        for (size_t s = 0; s < compound->simple_selector_count; s++) {
            if (dom_js_simple_selector_has_structural_dependency(
                    compound->simple_selectors[s])) {
                return true;
            }
        }
    }
    return false;
}

static bool dom_js_selector_group_has_structural_dependency(CssSelectorGroup* group) {
    if (!group) return false;
    for (size_t i = 0; i < group->selector_count; i++) {
        if (dom_js_selector_has_structural_dependency(group->selectors[i])) {
            return true;
        }
    }
    return false;
}

static bool dom_js_rule_has_structural_dependency(CssRule* rule) {
    if (!rule) return false;

    if (rule->type == CSS_RULE_STYLE ||
        rule->type == CSS_RULE_NESTING ||
        rule->type == CSS_RULE_NESTED_DECLARATIONS) {
        if (dom_js_selector_group_has_structural_dependency(
                rule->data.style_rule.selector_group) ||
            dom_js_selector_has_structural_dependency(
                rule->data.style_rule.selector)) {
            return true;
        }
        for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
            if (dom_js_rule_has_structural_dependency(
                    rule->data.style_rule.nested_rules[i])) {
                return true;
            }
        }
        return false;
    }

    if (rule->type == CSS_RULE_MEDIA ||
        rule->type == CSS_RULE_SUPPORTS ||
        rule->type == CSS_RULE_CONTAINER ||
        rule->type == CSS_RULE_SCOPE ||
        rule->type == CSS_RULE_LAYER) {
        for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            if (dom_js_rule_has_structural_dependency(
                    rule->data.conditional_rule.rules[i])) {
                return true;
            }
        }
    }
    return false;
}

static bool dom_js_stylesheet_has_structural_dependency(CssStylesheet* stylesheet) {
    if (!stylesheet || stylesheet->disabled) return false;

    for (size_t i = 0; i < stylesheet->rule_count; i++) {
        if (dom_js_rule_has_structural_dependency(stylesheet->rules[i])) {
            return true;
        }
    }
    for (size_t i = 0; i < stylesheet->imported_count; i++) {
        if (dom_js_stylesheet_has_structural_dependency(
                stylesheet->imported_stylesheets[i])) {
            return true;
        }
    }
    return false;
}

static bool dom_js_document_has_structural_css_dependency(DomDocument* doc) {
    if (!doc || !doc->stylesheets || doc->stylesheet_count <= 0) return false;
    for (int i = 0; i < doc->stylesheet_count; i++) {
        if (dom_js_stylesheet_has_structural_dependency(doc->stylesheets[i])) {
            return true;
        }
    }
    return false;
}

static bool dom_js_mutation_can_incremental(DomDocument* doc, const char** reason) {
    if (reason) *reason = "eligible";
    if (!doc || !doc->root || !doc->view_tree) {
        if (reason) *reason = "missing-layout-state";
        return false;
    }
    if (doc->js.mutation_record_overflow || doc->js.mutation_record_count <= 0) {
        if (reason) *reason = doc->js.mutation_record_overflow ? "record-overflow" : "no-records";
        return false;
    }

    for (int i = 0; i < doc->js.mutation_record_count; i++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[i];
        if (!dom_js_record_has_connected_endpoint(doc, record)) {
            continue;
        }
        if (record->kind == DOM_JS_MUTATION_UNKNOWN ||
            record->kind == DOM_JS_MUTATION_TREE_REPLACE) {
            if (reason) *reason = "broad-mutation";
            return false;
        }
        if (dom_js_node_is_stylesheet_related(record->target) ||
            dom_js_node_is_stylesheet_related(record->parent)) {
            if (reason) *reason = "stylesheet-mutation";
            return false;
        }
        if ((record->kind == DOM_JS_MUTATION_CHILD_INSERT ||
             record->kind == DOM_JS_MUTATION_CHILD_REMOVE) &&
            dom_js_document_has_structural_css_dependency(doc)) {
            // Stylesheet presence alone is safe for retained incremental layout;
            // only sibling/child-position-dependent selectors need broad recascade.
            if (reason) *reason = "structural-css-risk";
            return false;
        }
    }
    return true;
}

static void dom_js_recascade_subtree(DomDocument* doc, DomElement* root,
                                     DomJsMutationKind kind,
                                     SelectorMatcher* matcher) {
    if (!doc || !root) return;

    if (kind == DOM_JS_MUTATION_CONTROL_VALUE) return;
    if (kind == DOM_JS_MUTATION_STYLE ||
        kind == DOM_JS_MUTATION_STYLE_REPAINT ||
        kind == DOM_JS_MUTATION_TEXT) {
        root->set_styles_resolved(false);
        return;
    }

    clear_cascaded_styles_recursive(static_cast<DomNode*>(root));

    Pool* pool = doc->document_pool;
    CssEngine* css_engine = (CssEngine*)doc->services.cached_css_engine;
    radiant_apply_css_stylesheets_to_tree(
        doc, root, doc->stylesheets, doc->stylesheet_count,
        pool, css_engine, matcher);
}

static bool dom_js_node_contains(DomNode* ancestor, DomNode* node) {
    for (DomNode* current = node; current; current = current->parent) {
        if (current == ancestor) return true;
    }
    return false;
}

static bool dom_js_node_has_table_fixup_context(DomNode* node) {
    if (node && node->is_element() &&
        layout_element_contains_table_internal(node->as_element())) {
        // CSS Tables 3 §2.2: a display mutation can expose table-internal
        // descendants through a boxless wrapper, so reset their flow parent.
        return true;
    }
    for (DomNode* current = node; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* element = lam::dom_require_element(current);
        CssEnum display = resolve_display_value((void*)element).inner;
        if (element->tag() == MARKUP_NAME_TABLE ||
            layout_element_is_anonymous_table_fixup(element) ||
            is_table_internal_display(display)) {
            return true;
        }
    }
    return false;
}

static DomElement* dom_js_table_layout_root(DomNode* node) {
    DomElement* fixup_parent = nullptr;
    DomElement* flow_parent = nullptr;
    for (DomNode* current = node; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* element = lam::dom_require_element(current);
        DisplayValue display = resolve_display_value((void*)element);
        if (!element->is_table_fixup() &&
            (element->tag() == MARKUP_NAME_TABLE ||
             display.inner == CSS_VALUE_TABLE)) {
            return element;
        }
        if (!fixup_parent && element->is_table_fixup() && element->parent &&
            element->parent->is_element()) {
            fixup_parent = element->parent->as_element();
        }
        if (!flow_parent && current != node &&
            display.outer != CSS_VALUE_CONTENTS &&
            (display.inner == CSS_VALUE_FLOW ||
             display.inner == CSS_VALUE_FLOW_ROOT)) {
            flow_parent = element;
        }
    }
    return fixup_parent ? fixup_parent : flow_parent;
}

static void dom_js_reset_mutated_layout_subtrees(DomDocument* doc,
                                                 SelectorMatcher* matcher) {
    if (!doc || !doc->view_tree) return;

    DomElement* roots[DOM_JS_MUTATION_RECORD_CAP] = {};
    int root_count = 0;
    for (int i = 0; i < doc->js.mutation_record_count; i++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[i];
        if (record->kind != DOM_JS_MUTATION_STYLE &&
            record->kind != DOM_JS_MUTATION_STYLE_REPAINT &&
            record->kind != DOM_JS_MUTATION_ATTRIBUTE) {
            continue;
        }
        DomElement* candidate = dom_js_record_cascade_root(doc, record);
        if (!candidate) continue;
        if (dom_js_node_has_table_fixup_context(
                static_cast<DomNode*>(candidate))) {
            DomElement* table_root = nullptr;
            if (record->kind == DOM_JS_MUTATION_STYLE ||
                record->kind == DOM_JS_MUTATION_STYLE_REPAINT ||
                record->kind == DOM_JS_MUTATION_ATTRIBUTE) {
                table_root = dom_js_table_layout_root(static_cast<DomNode*>(candidate));
            }
            if (!table_root) continue;
            bool already_reset = false;
            for (int j = 0; j < root_count; j++) {
                if (roots[j] == table_root) {
                    already_reset = true;
                    break;
                }
            }
            if (already_reset) continue;
            // Table-internal style changes can change anonymous-box fixup; a
            // retained row/cell subtree otherwise preserves the old structure.
            layout_unwrap_all_anonymous_table_fixups_for_dom_mutation(table_root);
            view_pool_reset_retained_subtree(
                doc->view_tree, static_cast<DomNode*>(table_root));
            // Reset releases inherited view properties; recascading only the
            // target would measure descendants against the reset parent font.
            dom_js_recascade_subtree(doc, table_root,
                                     DOM_JS_MUTATION_ATTRIBUTE, matcher);
            if (root_count < DOM_JS_MUTATION_RECORD_CAP) {
                roots[root_count++] = table_root;
            }
            continue;
        }

        bool covered = false;
        for (int j = 0; j < root_count; j++) {
            if (dom_js_node_contains(static_cast<DomNode*>(roots[j]),
                                     static_cast<DomNode*>(candidate))) {
                covered = true;
                break;
            }
        }
        if (covered) continue;

        for (int j = 0; j < root_count;) {
            if (dom_js_node_contains(static_cast<DomNode*>(candidate),
                                     static_cast<DomNode*>(roots[j]))) {
                roots[j] = roots[--root_count];
            } else {
                j++;
            }
        }
        roots[root_count++] = candidate;
    }

    for (int i = 0; i < root_count; i++) {
        // Computed style is stored in retained view properties; merely clearing
        // styles_resolved leaves removed borders/sizes live across the reflow.
        view_pool_reset_retained_subtree(
            doc->view_tree, static_cast<DomNode*>(roots[i]));
    }
}

typedef struct DomJsDirtyBound {
    DomNode* node;
    float x;
    float y;
    float width;
    float height;
    bool valid;
} DomJsDirtyBound;

static bool dom_js_compute_absolute_bound(DomNode* node, DomJsDirtyBound* bound) {
    if (!node || !bound) return false;

    bound->node = node;
    bound->x = node->x;
    bound->y = node->y;
    bound->width = node->width;
    bound->height = node->height;
    for (DomNode* parent = node->parent; parent; parent = parent->parent) {
        bound->x += parent->x;
        bound->y += parent->y;
    }
    bound->valid = bound->width > 0.0f && bound->height > 0.0f;
    return bound->valid;
}

static bool dom_js_bounds_equal(DomJsDirtyBound* a, DomJsDirtyBound* b) {
    if (!a || !b || !a->valid || !b->valid) return false;
    const float epsilon = 0.5f;
    float dx = a->x - b->x; if (dx < 0.0f) dx = -dx;
    float dy = a->y - b->y; if (dy < 0.0f) dy = -dy;
    float dw = a->width - b->width; if (dw < 0.0f) dw = -dw;
    float dh = a->height - b->height; if (dh < 0.0f) dh = -dh;
    return dx <= epsilon && dy <= epsilon && dw <= epsilon && dh <= epsilon;
}

static bool dom_js_add_unique_repaint_root(DomNode* node,
                                           DomNode** roots,
                                           int capacity,
                                           int* count) {
    if (!node || !roots || !count || capacity <= 0) return true;
    for (int i = 0; i < *count; i++) {
        if (roots[i] == node) return true;
    }
    if (*count >= capacity) return false;
    roots[(*count)++] = node;
    return true;
}

static int dom_js_collect_repaint_roots(DomDocument* doc,
                                        DomNode** roots,
                                        int capacity,
                                        bool* overflow) {
    if (overflow) *overflow = false;
    if (!doc || !roots || capacity <= 0) return 0;

    int count = 0;
    for (int i = 0; i < doc->js.mutation_record_count; i++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[i];
        if (!dom_js_record_has_connected_endpoint(doc, record)) {
            continue;
        }
        DomElement* root = dom_js_record_cascade_root(doc, record);
        if (!root) continue;

        for (DomNode* node = static_cast<DomNode*>(root); node; node = node->parent) {
            if (!dom_js_add_unique_repaint_root(node, roots, capacity, &count)) {
                if (overflow) *overflow = true;
                return count;
            }
        }
    }
    return count;
}

static bool dom_js_mark_selective_dirty(DocState* state,
                                        DomJsDirtyBound* old_bounds,
                                        int bound_count,
                                        int* dirty_rect_count,
                                        const char** reason,
                                        bool allow_geometry_change) {
    if (dirty_rect_count) *dirty_rect_count = 0;
    if (reason) *reason = "dirty-rects";
    if (!state) {
        if (reason) *reason = "no-state";
        return false;
    }
    if (!old_bounds || bound_count <= 0) {
        if (reason) *reason = "no-bounds";
        return false;
    }

    for (int i = 0; i < bound_count; i++) {
        DomJsDirtyBound* old_bound = &old_bounds[i];
        DomJsDirtyBound new_bound = {};
        if (!old_bound->valid) {
            if (reason) *reason = "old-bound-invalid";
            return false;
        }
        if (!dom_js_compute_absolute_bound(old_bound->node, &new_bound)) {
            if (reason) *reason = "new-bound-invalid";
            return false;
        }
        if (!allow_geometry_change && !dom_js_bounds_equal(old_bound, &new_bound)) {
            if (reason) *reason = "geometry-changed";
            return false;
        }
    }

    for (int i = 0; i < bound_count; i++) {
        DomJsDirtyBound* old_bound = &old_bounds[i];
        dirty_mark_rect(&state->dirty_tracker,
                        old_bound->x, old_bound->y,
                        old_bound->width, old_bound->height);
        if (dirty_rect_count) (*dirty_rect_count)++;
        DomJsDirtyBound new_bound = {};
        if (allow_geometry_change &&
            dom_js_compute_absolute_bound(old_bound->node, &new_bound) &&
            !dom_js_bounds_equal(old_bound, &new_bound)) {
            dirty_mark_rect(&state->dirty_tracker,
                            new_bound.x, new_bound.y,
                            new_bound.width, new_bound.height);
            if (dirty_rect_count) (*dirty_rect_count)++;
        }
    }
    bool has_regions = dirty_has_regions(&state->dirty_tracker);
    if (!has_regions && reason) *reason = "no-regions";
    return has_regions;
}

static bool post_html_handler_incremental_rebuild(
        EventContext* evcon, DomDocument* doc,
        std::chrono::high_resolution_clock::time_point t_start,
        std::chrono::high_resolution_clock::time_point t0,
        int mutations,
        const char** fallback_reason_out) {
    using namespace std::chrono;

    const char* reason = nullptr;
    if (!dom_js_mutation_can_incremental(doc, &reason)) {
        log_info("html handler incremental: fallback=%s", reason ? reason : "unknown");
        if (fallback_reason_out) *fallback_reason_out = reason ? reason : "unknown";
        return false;
    }
    if (fallback_reason_out) *fallback_reason_out = "eligible";

    Pool* pool = doc->document_pool;
    SelectorMatcher* matcher = selector_matcher_create(pool);
    state_configure_selector_matcher((DocState*)doc->state, matcher);

    DomNode* repaint_roots[DOM_JS_MUTATION_RECORD_CAP] = {};
    bool repaint_root_overflow = false;
    int repaint_root_count = dom_js_collect_repaint_roots(
        doc, repaint_roots, DOM_JS_MUTATION_RECORD_CAP, &repaint_root_overflow);
    DomJsDirtyBound old_bounds[DOM_JS_MUTATION_RECORD_CAP] = {};
    int old_bound_count = 0;
    for (int i = 0; i < repaint_root_count; i++) {
        DomJsDirtyBound bound = {};
        if (dom_js_compute_absolute_bound(repaint_roots[i], &bound)) {
            old_bounds[old_bound_count++] = bound;
        }
    }

    dom_js_reset_mutated_layout_subtrees(doc, matcher);

    for (int i = 0; i < doc->js.mutation_record_count; i++) {
        DomJsMutationRecord* record = &doc->js.mutation_records[i];
        if (!dom_js_record_has_connected_endpoint(doc, record)) {
            continue;
        }
        DomElement* root = dom_js_record_cascade_root(doc, record);
        if (root) {
            dom_js_recascade_subtree(doc, root, record->kind, matcher);
        }
    }

    auto t1 = high_resolution_clock::now();

    DocState* state = (DocState*)doc->state;
    if (state) {
        doc_state_close_dropdown(state, NULL);
        doc_state_close_context_menu(state);
    }

    DomDocument* saved_doc = evcon->ui_context ? evcon->ui_context->document : nullptr;
    if (evcon->ui_context) evcon->ui_context->document = doc;
    doc->incremental_layout = true;
    doc->skip_style_reset = true;
    layout_html_doc(evcon->ui_context, doc, true);
    doc->skip_style_reset = false;
    doc->incremental_layout = false;
    if (evcon->ui_context) evcon->ui_context->document = saved_doc;

    if (doc->root) {
        dom_js_clear_layout_dirty_recursive(static_cast<DomNode*>(doc->root));
    }

    auto t2 = high_resolution_clock::now();

    int dirty_rect_count = 0;
    bool selective_dirty = false;
    const char* repaint_reason = nullptr;
    bool allow_geometry_dirty = !repaint_root_overflow && old_bound_count > 0;
    if (state) {
        dirty_clear(&state->dirty_tracker);
        if (repaint_root_overflow) {
            repaint_reason = "repaint-root-overflow";
        } else {
            selective_dirty = dom_js_mark_selective_dirty(state, old_bounds,
                                                          old_bound_count,
                                                          &dirty_rect_count,
                                                          &repaint_reason,
                                                          allow_geometry_dirty);
        }
        if (!selective_dirty) {
            state->dirty_tracker.full_repaint = true;
            doc_state_mark_dirty(state);
        }
        doc_state_request_repaint(state);
        doc_state_clear_reflow(state);
        reflow_clear(state);
    }

    evcon->need_repaint = true;
    to_repaint();

    auto t3 = high_resolution_clock::now();
    log_info("[TIMING] html handler rebuild: mode=incremental cascade=%.2fms layout=%.2fms repaint_req=%.2fms "
             "total=%.2fms (mutations=%d records=%d repaint=%s dirty_rects=%d repaint_reason=%s)",
             duration<double, std::milli>(t1 - t0).count(),
             duration<double, std::milli>(t2 - t1).count(),
             duration<double, std::milli>(t3 - t2).count(),
             duration<double, std::milli>(t3 - t_start).count(),
             mutations,
             doc->js.mutation_record_count,
             selective_dirty ? "dirty-rects" : "full",
             dirty_rect_count,
             repaint_reason ? repaint_reason : "none");
    // Tests read this structured result; timing logs alone cannot distinguish
    // incremental mutation handling from a broad fallback path.
    dom_js_record_reconcile(doc, DOM_RECONCILE_INCREMENTAL, "eligible",
                            mutations, doc->js.mutation_record_count,
                            doc->js.mutation_record_overflow,
                            "mutation-subtrees", "incremental-layout",
                            "retained", 0);
    return true;
}

static DomElement* radiant_view_to_dom_element(View* v);

static void post_html_handler_rebuild(EventContext* evcon,
                                       std::chrono::high_resolution_clock::time_point t_start,
                                       std::chrono::high_resolution_clock::time_point t_handler) {
    using namespace std::chrono;
    DomDocument* doc = event_context_target_document(evcon);
    if (!doc) return;
    int mutations = doc->js.mutation_count;

    if (mutations == 0) {
        log_info("[TIMING] html event handler: %.2fms (no DOM changes)",
                 duration<double, std::milli>(t_handler - t_start).count());
        return;
    }

    auto t0 = high_resolution_clock::now();
    dom_js_mutation_log_records(doc);

    const char* fallback_reason = "none";
    if (post_html_handler_incremental_rebuild(evcon, doc, t_start, t0,
                                              mutations, &fallback_reason)) {
        dom_js_mutation_reset_records(doc);
        return;
    }
    if (fallback_reason && strcmp(fallback_reason, "stylesheet-mutation") == 0) {
        // A <style>/<link> structural edit changes global selector inputs even
        // when no CSSOM wrapper initiated the mutation.
        style_epoch_mark_global_change(doc);
    }

    // Re-cascade CSS on the full tree (handles broad className changes, style writes, etc.)
    // Re-collect inline stylesheets in case JS added/removed/disabled <style> elements
    Pool* pool = doc->document_pool;
    CssEngine* css_engine = (CssEngine*)doc->services.cached_css_engine;

    // Clear previously cascaded declarations so removed classes/attributes cannot
    // keep stale winning CSS declarations in specified_style.
    clear_cascaded_styles_recursive(static_cast<DomNode*>(doc->root));
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (matcher) {
        state_configure_selector_matcher((DocState*)doc->state, matcher);
        radiant_apply_css_stylesheets_to_tree(
            doc, doc->root, doc->stylesheets, doc->stylesheet_count, pool, css_engine, matcher);
    }

    auto t1 = high_resolution_clock::now();

    DocState* state = (DocState*)doc->state;

    // The fallback drops the layout epoch, not the DOM identity epoch. Keep
    // StateStore owners for connected DOM nodes; after relayout we prune only
    // state whose node was actually removed by the mutation.
    if (state) {
        doc_state_close_dropdown(state, NULL);
        doc_state_close_context_menu(state);
    }

    // Broad DOM fallback is a layout-resource epoch change, not a DOM/view-node
    // identity change; keep the ViewTree shell and retained nodes for StateStore.
    if (!doc->view_tree) {
        doc->view_tree = (ViewTree*)mem_calloc(1, sizeof(ViewTree), MEM_CAT_LAYOUT); // OBJ_HEAP_OK: DomDocument owns the ViewTree shell across retained layout resets.
        view_pool_reset_retained(doc->view_tree);
    } else {
        view_pool_reset_retained(doc->view_tree);
    }

    // Clear stale layout-pool targets. DOM-backed StateStore entries survive
    // and are pruned/rebound after relayout below.
    if (state) {
        // Drop CSS animations/transitions whose View* targets were just freed; relayout
        // below re-creates them for elements that still have them. Without this, the next
        // animation_scheduler_tick dereferences a dangling View* (use-after-free).
        animation_scheduler_remove_views(state->animation_scheduler);
    }

    DomDocument* saved_doc = evcon->ui_context ? evcon->ui_context->document : nullptr;
    if (evcon->ui_context) evcon->ui_context->document = doc;
    layout_html_doc(evcon->ui_context, doc, true);
    if (evcon->ui_context) evcon->ui_context->document = saved_doc;

    int state_pruned = 0;
    if (state) {
        state_pruned = (int)state_store_prune_after_reflow(state); // INT_CAST_OK: log/test telemetry count
        selection_refresh_presentation(state);
    }

    auto t2 = high_resolution_clock::now();

    // Request repaint
    evcon->need_repaint = true;
    to_repaint();

    auto t3 = high_resolution_clock::now();

    log_info("[TIMING] html handler rebuild: mode=retained_full_layout cascade=%.2fms layout=%.2fms repaint_req=%.2fms "
             "total=%.2fms (mutations=%d)",
             duration<double, std::milli>(t1 - t0).count(),
             duration<double, std::milli>(t2 - t1).count(),
             duration<double, std::milli>(t3 - t2).count(),
             duration<double, std::milli>(t3 - t_start).count(),
             mutations);
    // Retained full layout is still a broad reconcile; keep the reason
    // test-visible so state-retention fixtures do not have to scrape log.txt.
    dom_js_record_reconcile(doc, DOM_RECONCILE_RETAINED_FULL_LAYOUT,
                            fallback_reason ? fallback_reason : "unknown",
                            mutations, doc->js.mutation_record_count,
                            doc->js.mutation_record_overflow,
                            "full-document", "full-flow-retained",
                            "retained-pruned-after-reflow", state_pruned);

    // Reset mutation count for next event
    dom_js_mutation_reset_records(doc);
}

void radiant_reconcile_js_dom_mutations(UiContext* uicon, DomDocument* doc) {
    if (!uicon || !doc || doc->js.mutation_count == 0) return;
    EventContext evcon = {};
    evcon.ui_context = uicon;
    evcon.target_document = doc;
    auto now = std::chrono::high_resolution_clock::now();
    // Timers, promises, and observer callbacks run outside native dispatch but
    // their DOM changes require the identical recascade and retained relayout.
    post_html_handler_rebuild(&evcon, now, now);
}

/**
 * §7 unification (U-0): walk a layout View up to the nearest DOM element node.
 * Layout views are themselves DomNode-derived, but text/anonymous views map
 * to their containing element for event-target purposes.
 */
static DomElement* radiant_view_to_dom_element(View* v) {
    DomNode* node = static_cast<DomNode*>(v);
    int depth = 0;
    while (node && depth < 200) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            return lam::dom_require_element(node);
        }
        node = node->parent;
        depth++;
    }
    return nullptr;
}

// Internal: enter/exit the JS EvalContext that DOM event callbacks run under.
// Both factories AND dispatch must run between enter/exit because Item creation
// allocates from the active JS runtime heap and number-stack base frame.
typedef struct {
    EvalContext* handler_ctx;
    Context*     saved_input_ctx;
    DomDocument* doc;
    bool         active;
} JsCtxScope;

// Retained JS handlers enter the document's runtime context before allocating
// event values or invoking a generated context-ABI function.
static bool radiant_js_ctx_enter(JsCtxScope* s, EventContext* evcon) {
    s->active = false;
    s->handler_ctx = nullptr;
    s->doc = event_context_target_document(evcon);
    if (!s->doc || !s->doc->js.mir_ctx || !s->doc->js.runtime) return false;
    Runtime* runtime = s->doc->js.runtime;
    s->handler_ctx = runtime_get_eval_context(runtime);
    if (!s->handler_ctx || !runtime->heap || !runtime->name_pool) return false;
    s->handler_ctx->heap = runtime->heap;
    s->handler_ctx->name_pool = runtime->name_pool;
    s->handler_ctx->type_list = runtime->type_list;
    s->handler_ctx->pool = runtime->heap->pool;
    s->saved_input_ctx = input_context;
    if (!eval_context_init(s->handler_ctx) ||
            (s->handler_ctx->js_state &&
             !js_runtime_state_init(s->handler_ctx))) {
        return false;
    }
    input_context = nullptr;
    js_dom_set_document(s->doc);
    dom_js_mutation_reset_records(s->doc);
    s->active = true;
    return true;
}

static void radiant_js_ctx_exit(JsCtxScope* s, EventContext* evcon,
                                std::chrono::high_resolution_clock::time_point t_start)
{
    if (!s->active) return;
    auto t_handler = std::chrono::high_resolution_clock::now();
    input_context = s->saved_input_ctx;
    post_html_handler_rebuild(evcon, t_start, t_handler);
    s->active = false;
}

static thread_local uint32_t js_dispatch_batch_depth = 0;
static thread_local DomDocument* js_dispatch_batch_document = nullptr;

struct JsDispatchScope {
    EventContext* evcon;
    JsCtxScope scope;
    std::chrono::high_resolution_clock::time_point t_start;
    bool active;
    bool owns_batch;
    bool reuses_batch;
    bool no_js_passthrough;
    uint32_t previous_batch_depth;
    DomDocument* previous_batch_document;
    EventContext* previous_active_event_context;

    JsDispatchScope(EventContext* event_context, bool allow_without_js = false) {
        evcon = event_context;
        active = false;
        owns_batch = false;
        reuses_batch = false;
        no_js_passthrough = false;
        previous_batch_depth = js_dispatch_batch_depth;
        previous_batch_document = js_dispatch_batch_document;
        previous_active_event_context = s_active_js_dispatch_event_context;
        DomDocument* target_document = event_context_target_document(evcon);
        if (js_dispatch_batch_depth != 0 &&
            js_dispatch_batch_document == target_document) {
            // nested ordinary events share the active document batch so
            // reconcile cannot switch the evaluator underneath dispatch.
            js_dispatch_batch_depth++;
            active = true;
            reuses_batch = true;
            s_active_js_dispatch_event_context = evcon;
            return;
        }
        if (allow_without_js && !event_document_has_js_runtime(evcon)) {
            // a Lambda page has no JS event realm; its ordinary author handler
            // still runs without opening a second evaluator scope.
            active = true;
            no_js_passthrough = true;
            s_active_js_dispatch_event_context = evcon;
            return;
        }
        if (radiant_js_ctx_enter(&scope, evcon)) {
            t_start = std::chrono::high_resolution_clock::now();
            active = true;
            owns_batch = true;
            js_dispatch_batch_depth = 1;
            js_dispatch_batch_document = target_document;
            s_active_js_dispatch_event_context = evcon;
        }
    }

    ~JsDispatchScope() {
        if (!active) return;
        if (reuses_batch) {
            if (js_dispatch_batch_depth > 0) js_dispatch_batch_depth--;
            s_active_js_dispatch_event_context = previous_active_event_context;
            return;
        }
        if (no_js_passthrough) {
            s_active_js_dispatch_event_context = previous_active_event_context;
            return;
        }
        radiant_js_ctx_exit(&scope, evcon, t_start);
        // A nested event can target an iframe/second document. Restore the
        // enclosing batch instead of clearing it, otherwise its remaining
        // beforeinput/action work runs with the nested document global.
        js_dispatch_batch_depth = previous_batch_depth;
        js_dispatch_batch_document = previous_batch_document;
        if (previous_batch_depth != 0 && previous_batch_document) {
            js_dom_set_document(previous_batch_document);
        }
        s_active_js_dispatch_event_context = previous_active_event_context;
    }
};

static bool dispatch_contenteditable_plain_event(EventContext* evcon,
                                                 View* target,
                                                 const InputIntent* intent,
                                                 const EditingSurface* surface) {
    if (!evcon || !target || !intent || !surface ||
        !editing_surface_is_rich(surface)) {
        return false;
    }
    DocState* state = event_context_target_state(evcon);
    if (!state || !surface->owner) return false;

    // All rich editors use the same synchronous ordering: an ordinary
    // beforeinput handler gets first refusal, the package supplies the
    // unclaimed default, and input follows a committed edit.
    JsDispatchScope dispatch_scope(evcon, true);
    if (!dispatch_scope.active) return false;
    editing_interaction_set_active_surface(state, surface);

    bool has_js_runtime = event_document_has_js_runtime(evcon);
    bool beforeinput_prevented = false;
    bool author_handled = false;
    bool author_model_reconciled = false;
    if (input_intent_is_dispatchable(intent->type)) {
        if (has_js_runtime) {
            beforeinput_prevented = radiant_dispatch_input_event(
                evcon, target, "beforeinput", intent);
        } else {
            // Lambda edit templates are ordinary author handlers now. Keeping
            // behavior dispatch disabled here prevents the UA default from
            // being selected before the author handler has declined.
            author_handled = dispatch_lambda_handler(
                evcon, target, "beforeinput", intent,
                &author_model_reconciled, false, true);
        }
    }

    // author code may replace the original host or move the selection. The
    // canonical DOM selection/focus now decides where the default can apply.
    EditingSurface canonical_surface;
    if (!canonical_contenteditable_surface_from_state(state,
                                                      &canonical_surface)) {
        return true;
    }
    DomElement* canonical_host = canonical_surface.owner;
    editing_interaction_set_active_surface(state, &canonical_surface);
    if (beforeinput_prevented) return true;
    if (author_handled) {
        if (author_model_reconciled) {
            bool input_model_reconciled = false;
            dispatch_lambda_handler(
                evcon, static_cast<View*>(canonical_host), "input", intent,
                &input_model_reconciled, false, true);
            evcon->need_repaint = true;
        }
        return true;
    }

    EditingTargetRange target_ranges[4] = {};
    uint32_t target_range_count = editing_compute_target_ranges(
        state, &canonical_surface, intent, target_ranges, 4);
    if (target_range_count > 4) return true;
    for (uint32_t i = 0; i < target_range_count; i++) {
        if (!editing_geometry_surface_contains_target_range(
                &canonical_surface, &target_ranges[i])) {
            return true;
        }
    }

    DomSelection* selection = state->dom_selection;
    DomBoundary start = {};
    DomBoundary end = {};
    bool have_range = target_range_count > 0;
    if (have_range) {
        start = target_ranges[0].start;
        end = target_ranges[0].end;
    } else if (selection && selection->range_count == 1 &&
               selection->ranges[0]) {
        // some intents intentionally have no StaticRange target (formatting,
        // replacement, history). Their live Selection is still the package's
        // current mechanism input.
        start = selection->ranges[0]->start;
        end = selection->ranges[0]->end;
        have_range = true;
    }

    if (have_range) {
        bool prepared = dom_edit_prepare_pending_range(
            state, canonical_host, start, end);
        if (!prepared) {
            return true;
        }
    } else if (intent->type == INPUT_INTENT_COMPOSITION_START) {
        // starting composition reserves state but has no replacement range yet.
        dom_edit_set_pending_range(state, canonical_host, nullptr, 0, 0,
                                   nullptr, 0);
    } else {
        return true;
    }

    uint64_t apply_epoch_before = dom_edit_apply_epoch();
    bool claimed = radiant_dispatch_behavior_dom_edit(
        static_cast<View*>(canonical_host), intent);
    bool applied = dom_edit_apply_epoch() != apply_epoch_before;
    DomNode* caret_node = dom_edit_caret_node();
    uint32_t caret_offset = dom_edit_caret_offset_u16();
    dom_edit_clear_pending_range(state);

    if (applied && caret_node && selection) {
        const char* exception = nullptr;
        if (!dom_selection_collapse(selection, caret_node, caret_offset,
                                    &exception)) {
            log_error("F14.3: failed to collapse package editing caret: %s",
                      exception ? exception : "unknown");
        }
    }

    if (applied && intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT &&
        caret_node && caret_node->is_text()) {
        // the next IME update replaces the provisional run, not the caret that
        // the package just left behind. Keep its start in UTF-8 byte space,
        // which is the composition anchor's native representation.
        DomText* caret_text = lam::dom_require_text(caret_node);
        uint32_t data_u16 = tc_utf8_to_utf16_length(
            intent->data ? intent->data : "",
            intent->data ? (uint32_t)strlen(intent->data) : 0);
        // The package caret may sit inside the new run, so derive the anchor
        // from the range it replaced whenever that start is still the caret's
        // text node. Subtracting the preedit length only works when the caret
        // is at the run's end; a second composition at offset four would
        // otherwise anchor at one.
        uint32_t anchor_u16 = 0;
        if (start.node == caret_node && start.node->is_text()) {
            anchor_u16 = start.offset;
        } else if (caret_offset >= data_u16) {
            anchor_u16 = caret_offset - data_u16;
        }
        state->editing.composition.surface = canonical_surface;
        state->editing.composition.anchor_view = static_cast<View*>(caret_text);
        state->editing.composition.anchor_offset = (int)dom_text_utf16_to_utf8(
            caret_text, anchor_u16); // INT_CAST_OK: DOM offsets fit the composition anchor.
        state->editing.composition.dom_preedit_len = data_u16;
    }

    if (claimed || applied) {
        bool composition_cancel =
            intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT;
        if (input_intent_is_dispatchable(intent->type) && !composition_cancel) {
            if (has_js_runtime) {
                radiant_dispatch_input_event(evcon, static_cast<View*>(canonical_host),
                                             "input", intent);
            } else {
                bool input_model_reconciled = false;
                dispatch_lambda_handler(
                    evcon, static_cast<View*>(canonical_host), "input", intent,
                    &input_model_reconciled, false, true);
            }
        }
        evcon->need_repaint = true;
    }
    return true;
}

static bool dispatch_contenteditable_event(EventContext* evcon, View* target,
                                            const InputIntent* intent) {
    if (!evcon || !target || !intent || intent->type == INPUT_INTENT_NONE) {
        return false;
    }
    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface) ||
        !editing_surface_is_rich(&surface) || !surface.owner) {
        return false;
    }
    return dispatch_contenteditable_plain_event(evcon, target, intent,
                                                &surface);
}

static bool dispatch_contenteditable_composition_event(
        EventContext* evcon, const EditingSurface* surface,
        const EditingIntent* intent) {
    if (!evcon || !surface || !intent || !editing_surface_is_rich(surface)) {
        return false;
    }
    View* target = surface->view ? surface->view : static_cast<View*>(surface->owner);
    if (!target) return false;

    // Composition event listeners, beforeinput, the selected action, and input
    // share one JavaScript batch so an editor cannot observe a preedit event
    // without being able to synchronously handle its following edit.
    JsDispatchScope dispatch_scope(evcon, true);
    if (!dispatch_scope.active) return false;

    const char* event_name = "compositionupdate";
    const char* phase = "update";
    uint32_t preedit_len = 0;
    uint32_t commit_len = 0;
    if (intent->type == INPUT_INTENT_COMPOSITION_START) {
        event_name = "compositionstart";
        phase = "start";
    } else if (intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT) {
        preedit_len = event_log_text_len(intent->data);
    } else if (intent->type == INPUT_INTENT_INSERT_FROM_COMPOSITION) {
        event_name = "compositionend";
        phase = "commit";
        commit_len = event_log_text_len(intent->data);
    } else if (intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT) {
        event_name = "compositionend";
        phase = "cancel";
    }

    if (event_document_has_js_runtime(evcon)) {
        radiant_dispatch_composition_event(evcon, target, event_name, intent->data);
    }
    DocState* state = event_context_target_state(evcon);
    event_log_editing_composition(state, surface, intent, phase, preedit_len,
                                  commit_len, intent->composition_caret);
    bool composing = intent->type == INPUT_INTENT_COMPOSITION_START ||
        intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT;
    editing_interaction_set_composing(state, surface, composing);
    bool handled = dispatch_contenteditable_consumer_event(evcon, target, intent);
    if (handled) evcon->need_repaint = true;
    return true;
}

void radiant_dispatch_window_event(UiContext* uicon, DomDocument* doc, const char* type) {
    if (!uicon || !doc || !type || !type[0]) return;
    EventContext evcon = {};
    evcon.ui_context = uicon;
    evcon.target_document = doc;
    JsDispatchScope dispatch_scope(&evcon);
    if (!dispatch_scope.active) return;
    // Native window notifications use the canonical global EventTarget, which
    // is also the key used by window.addEventListener in the module bridge.
    Item event_item = js_create_event(type, false, false);
    js_dom_dispatch_event(js_get_global_this(), event_item);
    if (strcmp(type, "resize") == 0) js_match_media_notify_resize();
    if (strcmp(type, "resize") == 0 || strcmp(type, "scroll") == 0) {
        js_dom_observers_post_layout();
    }
}

void radiant_dispatch_css_event(UiContext* uicon, DomElement* target,
                                const char* type, const char* detail_name,
                                const char* detail_value, double elapsed_time) {
    if (!uicon || !target || !target->doc || !type || !type[0]) return;
    EventContext evcon = {};
    evcon.ui_context = uicon;
    evcon.target_document = target->doc;
    JsCtxScope scope = {};
    bool entered_scope = radiant_js_ctx_enter(&scope, &evcon);
    // Batch DOM execution still owns the live JIT context but does not retain
    // it on the document; CSS completion must dispatch through that active frame.
    if (!entered_scope && (!context || js_dom_get_document() != target->doc)) return;

    Item event_item = js_create_native_css_event(type, detail_name,
        detail_value, elapsed_time);
    js_dom_dispatch_event(js_dom_wrap_element(target), event_item);

    // CSS events run inside the animation scheduler. Rebuilding immediately
    // would invalidate its current View pointers; the mutation ledger requests
    // the safe event-loop reflow after this scheduler tick completes.
    if (entered_scope) {
        input_context = scope.saved_input_ctx;
        scope.active = false;
    }
}

typedef Item (*RadiantJsEventBuilder)(void* userdata);

static bool radiant_dispatch_built_event(EventContext* evcon, View* target,
                                         RadiantJsEventBuilder build_event,
                                         void* userdata,
                                         bool read_prevented,
                                         bool* dispatched = nullptr,
                                         bool run_ua_tier = true) {
    if (dispatched) *dispatched = false;
    DomElement* dom_target = radiant_view_to_dom_element(target);
    if (!dom_target || !build_event) return false;
    JsDispatchScope dispatch_scope(evcon);
    DomDocument* target_doc = event_context_target_document(evcon);
    bool active_batch_context = context && js_dom_get_document() == target_doc;
    // Synthetic input can synchronously re-enter native dispatch while the
    // page's batch context is already active. Such documents
    // do not yet retain js_mir_ctx, so requiring a fresh scope silently drops
    // beforeinput/input instead of reusing the live allocation context.
    if (!dispatch_scope.active && !active_batch_context) return false;
    if (dispatched) *dispatched = true;
    RootFrame roots(2);
    Rooted<Item> event_root(roots, build_event(userdata));
    event_context_set_dom_event(evcon, event_root.get());
    EventTargetPath target_path = {};
    bool target_path_valid = capture_event_target_path(target_doc, target, &target_path);
    Rooted<Item> target_root(roots, js_dom_wrap_element(dom_target));
    js_dom_dispatch_event(target_root.get(), event_root.get());
    bool prevented = radiant_dom_event_default_prevented(event_root.get());
    evcon->default_prevented = prevented;
    const char* event_name = fn_to_cstr(js_get_name_key(event_root.get(), "type"));
    if (run_ua_tier) {
        View* ua_target = settle_author_templates_for_ua(
            evcon, target, target_path_valid ? &target_path : nullptr, nullptr);
        if (!prevented && event_name && ua_target) {
            evcon->dom_event_ua_handled = dispatch_behavior_handler(
                evcon, ua_target, event_name, nullptr, nullptr);
            prevented = radiant_dom_event_default_prevented(event_root.get());
            evcon->default_prevented = prevented;
        }
    }
    return read_prevented ? prevented : false;
}

/**
 * §7 unification (U-1): dispatch a "mouseover" / "mouseout" / generic mouse
 * event through the JS EventTarget pipeline at the given target view. Returns
 * true if default action should be prevented.
 */
typedef struct {
    const char* type;
    double client_x;
    double client_y;
    int button;
    int buttons;
    bool ctrl;
    bool shift;
    bool alt;
    bool meta;
    int detail;
    double timestamp_ms;
} MouseEventBuildArgs;

static Item build_mouse_event_item(void* userdata) {
    MouseEventBuildArgs* args = (MouseEventBuildArgs*)userdata;
    Item event = js_create_native_mouse_event(args->type, args->client_x, args->client_y,
        args->button, args->buttons, args->ctrl, args->shift, args->alt,
        args->meta, args->detail, ItemNull);
    if (args->timestamp_ms >= 0.0) {
        js_event_set_timestamp(event, args->timestamp_ms);
    }
    return event;
}

static bool radiant_dispatch_mouse_event(EventContext* evcon, View* target,
                                         const char* type, double client_x, double client_y,
                                         int button, int buttons,
                                         bool ctrl, bool shift, bool alt, bool meta,
                                         int detail,
                                         bool* dispatched = nullptr)
{
    MouseEventBuildArgs args = {
        type, client_x, client_y, button, buttons,
        ctrl, shift, alt, meta, detail, -1.0
    };
    bool is_click = type && strcmp(type, "click") == 0;
    bool prevented = radiant_dispatch_built_event(evcon, target, build_mouse_event_item,
        &args, true, dispatched, !is_click);
    return prevented;
}

extern "C" bool radiant_dispatch_event_sim_mouse(UiContext* uicon, View* target,
    const char* type, double client_x, double client_y, int button, int buttons,
    int mods, int detail, double timestamp_ms)
{
    if (!uicon || !uicon->document || !target || !type) return false;
    EventContext evcon = {};
    evcon.ui_context = uicon;
    evcon.target_document = uicon->document;
    MouseEventBuildArgs args = {
        type, client_x, client_y, button, buttons,
        (mods & RDT_MOD_CTRL) != 0, (mods & RDT_MOD_SHIFT) != 0,
        (mods & RDT_MOD_ALT) != 0, (mods & RDT_MOD_SUPER) != 0,
        detail, timestamp_ms
    };
    return radiant_dispatch_built_event(&evcon, target, build_mouse_event_item,
        &args, true);
}

typedef struct {
    const char* type;
    double client_x;
    double client_y;
    int button;
    int buttons;
    bool ctrl;
    bool shift;
    bool alt;
    bool meta;
    const char* pointer_type;
} PointerEventBuildArgs;

static Item build_pointer_event_item(void* userdata) {
    PointerEventBuildArgs* args = (PointerEventBuildArgs*)userdata;
    return js_create_native_pointer_event(args->type, args->client_x, args->client_y,
        args->button, args->buttons, args->ctrl, args->shift, args->alt,
        args->meta, args->pointer_type, 1, true);
}

static bool radiant_dispatch_pointer_event(EventContext* evcon, View* target,
                                           const char* type, double client_x,
                                           double client_y, int button, int buttons,
                                           bool ctrl, bool shift, bool alt,
                                           bool meta, const char* pointer_type,
                                           bool* dispatched = nullptr) {
    PointerEventBuildArgs args = {
        type, client_x, client_y, button, buttons,
        ctrl, shift, alt, meta, pointer_type ? pointer_type : "mouse"
    };
    return radiant_dispatch_built_event(evcon, target, build_pointer_event_item,
        &args, true, dispatched);
}

extern "C" bool radiant_dispatch_event_sim_pointer(UiContext* uicon, View* target,
    const char* type, double client_x, double client_y, int button, int buttons,
    int mods, const char* pointer_type)
{
    if (!uicon || !uicon->document || !target || !type) return false;
    EventContext evcon = {};
    evcon.ui_context = uicon;
    evcon.target_document = uicon->document;
    return radiant_dispatch_pointer_event(&evcon, target, type,
        client_x, client_y, button, buttons,
        (mods & RDT_MOD_CTRL) != 0, (mods & RDT_MOD_SHIFT) != 0,
        (mods & RDT_MOD_ALT) != 0, (mods & RDT_MOD_SUPER) != 0,
        pointer_type ? pointer_type : "touch");
}

/**
 * §7 unification (U-3): dispatch a "keydown"/"keyup" event through the JS
 * EventTarget pipeline at the given target view. Returns true if default
 * action should be prevented.
 */
typedef struct {
    const char* type;
    const char* key_name;
    const char* code_name;
    int legacy_key_code;
    bool ctrl;
    bool shift;
    bool alt;
    bool meta;
    bool repeat;
} KeyboardEventBuildArgs;

static Item build_keyboard_event_item(void* userdata) {
    KeyboardEventBuildArgs* args = (KeyboardEventBuildArgs*)userdata;
    return js_create_native_keyboard_event(args->type, args->key_name, args->code_name,
        args->legacy_key_code,
        args->ctrl, args->shift, args->alt, args->meta, args->repeat);
}

static bool radiant_dispatch_keyboard_event(EventContext* evcon, View* target,
                                            const char* type, int key_code,
                                            int mods, bool repeat)
{
    const char* key_name = key_code_to_dom_key(key_code, mods);
    KeyboardEventBuildArgs args = {
        type,
        key_name,
        key_code_to_dom_code(key_code),
        key_code_to_legacy_code(key_code),
        (mods & RDT_MOD_CTRL) != 0,
        (mods & RDT_MOD_SHIFT) != 0,
        (mods & RDT_MOD_ALT) != 0,
        (mods & RDT_MOD_SUPER) != 0,
        repeat
    };
    // Editable libraries inspect `key`/`code` before legacy keyCode. Preserve
    // the physical key identity here so ordinary typing does not look like an
    // empty-key shortcut to script-owned editors.
    return radiant_dispatch_built_event(evcon, target, build_keyboard_event_item,
        &args, true);
}

// Build a JS StaticRange-shaped Map for one EditingTargetRange. Matches the
// shape produced by js_ctor_static_range_fn so that JS code consuming
// `getTargetRanges()` sees the same property surface as `new StaticRange(...)`.
static Item ce_build_static_range_item(const EditingTargetRange* r) {
    Item obj = js_new_object();
    Item start = r->start.node ? js_dom_wrap_element(r->start.node) : ItemNull;
    Item end   = r->end.node   ? js_dom_wrap_element(r->end.node)   : ItemNull;
    Item start_key = (Item){.item = s2it(heap_create_name("startContainer"))};
    Item end_key   = (Item){.item = s2it(heap_create_name("endContainer"))};
    Item so_key    = (Item){.item = s2it(heap_create_name("startOffset"))};
    Item eo_key    = (Item){.item = s2it(heap_create_name("endOffset"))};
    Item col_key   = (Item){.item = s2it(heap_create_name("collapsed"))};
    js_set_key_default(obj, start_key, start);
    js_set_key_default(obj, end_key,   end);
    js_set_key_default(obj, so_key,    (Item){.item = i2it((long)r->start.offset)});
    js_set_key_default(obj, eo_key,    (Item){.item = i2it((long)r->end.offset)});
    bool collapsed = (r->start.node == r->end.node) &&
                     (r->start.offset == r->end.offset);
    js_set_key_default(obj, col_key, (Item){.item = b2it(collapsed)});
    return obj;
}

static bool input_intent_uses_transfer_payload(InputIntentType type) {
    switch (type) {
        case INPUT_INTENT_INSERT_FROM_PASTE:
        case INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION:
        case INPUT_INTENT_INSERT_FROM_DROP:
        case INPUT_INTENT_DELETE_BY_DRAG:
        case INPUT_INTENT_DELETE_BY_CUT:
            return true;
        default:
            return false;
    }
}

static const char* input_event_data_for_surface(const EditingSurface* surface,
                                                bool has_surface,
                                                const InputIntent* intent) {
    if (!intent) return nullptr;
    if (has_surface && editing_surface_is_rich(surface) &&
        input_intent_uses_transfer_payload(intent->type)) {
        return nullptr;
    }
    return intent->data;
}

/**
 * CE-3 (Radiant_Design_Content_Editable.md §6): dispatch a `beforeinput` or
 * `input` event via the JS EventTarget pipeline. `beforeinput` is cancelable;
 * a JS handler that calls preventDefault() causes us to return true so the
 * caller can skip the model mutation. `input` is informational only.
 *
 * Returns false when there is no JS context (headless / non-JS embedding),
 * which the caller treats as "no JS opinion" — Lambda-template paths still
 * fire through dispatch_lambda_handler in that case.
 */
typedef struct {
    EventContext* evcon;
    View* target;
    const char* type;
    const InputIntent* intent;
    EditingSurface surface;
    bool has_surface;
} InputEventBuildArgs;

static Item build_input_event_item(void* userdata) {
    InputEventBuildArgs* args = (InputEventBuildArgs*)userdata;
    const char* input_type = input_intent_type_name(args->intent->type);
    const char* data = input_event_data_for_surface(&args->surface,
                                                    args->has_surface,
                                                    args->intent);

    EditingTargetRange ranges[1];
    const EditingTargetRange* range_snapshot = nullptr;
    uint32_t n_ranges = 0;
    bool wants_target_ranges =
        strcmp(args->type, "beforeinput") == 0 || strcmp(args->type, "input") == 0;
    if (!wants_target_ranges) {
        range_snapshot = nullptr;
        n_ranges = 0;
    } else if (args->evcon->editing_target_ranges_active) {
        range_snapshot = args->evcon->editing_target_ranges;
        n_ranges = args->evcon->editing_target_range_count;
    } else {
        // Without a saved range, compute the StaticRange[] snapshot before
        // dispatch so handlers see the current pre-dispatch ranges.
        DocState* state = event_context_target_state(args->evcon);
        n_ranges = args->has_surface
            ? editing_compute_target_ranges(state, &args->surface, args->intent, ranges, 1)
            : 0;
        range_snapshot = ranges;
    }
    if (!range_snapshot) {
        n_ranges = 0;
    }
    Item ranges_arr = js_array_new(0);
    for (uint32_t i = 0; i < n_ranges; i++) {
        js_array_push(ranges_arr, ce_build_static_range_item(&range_snapshot[i]));
    }

    // CE-3 follow-up (§6.1, §8): rich paste/drop/cut/drag intents attach a
    // DataTransfer carrying the text/plain (and text/html if present) payload.
    // Text controls expose the plain text through `data` and keep dataTransfer
    // null, matching the Input Events cut/paste WPT surface.
    Item data_transfer = ItemNull;
    if (args->has_surface && editing_surface_is_rich(&args->surface) &&
        input_intent_uses_transfer_payload(args->intent->type)) {
        data_transfer = js_data_transfer_new_with_strings(args->intent->data,
                                                          args->intent->html_data);
    }

    return js_create_native_input_event(args->type, input_type, data,
                                        args->intent->is_composing, data_transfer,
                                        ranges_arr);
}

static bool radiant_dispatch_input_event(EventContext* evcon, View* target,
                                         const char* type,
                                         const InputIntent* intent)
{
    if (!evcon || !target || !type || !intent) return false;
    EditingSurface surface;
    bool has_surface = editing_surface_from_target(target, &surface);
    InputEventBuildArgs args = {evcon, target, type, intent, surface, has_surface};
    return radiant_dispatch_built_event(evcon, target, build_input_event_item,
        &args, true, nullptr, false);
}

typedef struct {
    const char* type;
    const char* data;
} CompositionEventBuildArgs;

static Item build_composition_event_item(void* userdata) {
    CompositionEventBuildArgs* args = (CompositionEventBuildArgs*)userdata;
    return js_create_native_composition_event(args->type,
                                              args->data ? args->data : "");
}

static void radiant_dispatch_composition_event(EventContext* evcon,
                                               View* target,
                                               const char* type,
                                               const char* data)
{
    if (!evcon || !target || !type) return;
    CompositionEventBuildArgs args = {type, data};
    radiant_dispatch_built_event(evcon, target, build_composition_event_item,
        &args, false, nullptr, false);
    // ESO45: composition events reached JS only. A behavior template owns the
    // session now (ES18/F7), and the ancestor walk from the focused control
    // reaches <body>, which is where that template matches. After the JS
    // dispatch, as ES5 requires.
    //
    // The payload rides an InputIntent because that is what the behavior event
    // map already knows how to expose — `data` and `composition_caret` — and
    // the JS-side event object is not a Mark map a template can read.
    InputIntent comp_intent;
    comp_intent.type = INPUT_INTENT_INSERT_COMPOSITION_TEXT;
    comp_intent.data = data ? data : "";
    comp_intent.is_composing = true;
    comp_intent.composition_caret = evcon->composition_caret_hint;
    dispatch_behavior_handler(evcon, target, type, &comp_intent, nullptr);
}

/**
 * §7 unification (U-4): dispatch focus/blur/focusin/focusout via the JS
 * EventTarget pipeline. Per spec, focus and blur do not bubble; focusin
 * and focusout do — `js_create_native_focus_event` sets bubbles accordingly.
 */
typedef struct {
    const char* type;
    View* related;
} FocusEventBuildArgs;

static Item build_focus_event_item(void* userdata) {
    FocusEventBuildArgs* args = (FocusEventBuildArgs*)userdata;
    Item rel = ItemNull;
    if (args->related) {
        DomElement* rel_el = radiant_view_to_dom_element(args->related);
        if (rel_el) rel = js_dom_wrap_element(rel_el);
    }
    return js_create_native_focus_event(args->type, rel);
}

static void radiant_dispatch_focus_event(EventContext* evcon, View* target,
                                         const char* type, View* related)
{
    FocusEventBuildArgs args = {type, related};
    radiant_dispatch_built_event(evcon, target, build_focus_event_item,
        &args, false);
}

/**
 * Dispatch a plain Event through the JS EventTarget pipeline.
 */
typedef struct {
    const char* type;
    bool bubbles;
    bool cancelable;
} SimpleEventBuildArgs;

static Item build_simple_event_item(void* userdata) {
    SimpleEventBuildArgs* args = (SimpleEventBuildArgs*)userdata;
    return js_create_event(args->type, args->bubbles, args->cancelable);
}

static bool radiant_dispatch_simple_event(EventContext* evcon, View* target,
                                          const char* type,
                                          bool bubbles, bool cancelable)
{
    SimpleEventBuildArgs args = {type, bubbles, cancelable};
    return radiant_dispatch_built_event(evcon, target, build_simple_event_item,
        &args, true);
}

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event);
void event_context_cleanup(EventContext* evcon);
extern "C" void js_dom_select_set_selected_index_bridge(void* dom_elem, Item value);

extern "C" bool radiant_dispatch_event_sim_select_change(UiContext* uicon,
                                                         View* target,
                                                         int selected_index)
{
    if (!uicon || !target) return false;
    RdtEvent event;
    memset(&event, 0, sizeof(event));
    event.type = RDT_EVENT_NIL;
    event.timestamp = 0;
    EventContext evcon;
    event_context_init(&evcon, uicon, &event);
    evcon.target = target;
    DomElement* dom_target = radiant_view_to_dom_element(target);
    if (!dom_target) {
        event_context_cleanup(&evcon);
        return false;
    }
    // F2c: the template commits, here as on the pointer and keyboard paths. The
    // sim used to write selectedness itself, which left a second copy of the
    // commit policy — including radio-style exclusivity concerns — that no test
    // could observe diverging from the template's, because the only tests that
    // exercised it were the ones bypassing the template. Commit before the JS
    // mirror below, which needs the new value already in place.
    {
        InputIntent commit_intent;
        commit_intent.option_index = selected_index;
        radiant_dispatch_behavior_option_commit(&evcon, target, &commit_intent);
    }
    bool prevented = false;
    {
        JsDispatchScope dispatch_scope(&evcon);
        if (!dispatch_scope.active) {
            event_context_cleanup(&evcon);
            return false;
        }
        // the template has committed selectedness; mirror it into the JS DOM
        // before firing change so handlers reading target.value see it.
        js_dom_select_set_selected_index_bridge((void*)dom_target,
                                                (Item){.item = i2it(selected_index)});
        Item target_item = js_dom_wrap_element(dom_target);
        Item input_ev = js_create_event("input", true, false);
        js_dom_dispatch_event(target_item, input_ev);
        Item change_ev = js_create_event("change", true, false);
        js_dom_dispatch_event(target_item, change_ev);
        prevented = radiant_dom_event_default_prevented(change_ev);
    }
    event_context_cleanup(&evcon);
    return prevented;
}

// Stage 4C Phase B: dispatch a JS clipboard event (paste/copy/cut) with a
// store-backed clipboardData to a rich/contenteditable surface, so script-owned
// editors that use addEventListener('paste'|'copy') work under `lambda.exe view`
// (there is no OS clipboard-event delivery in headless view). Returns true if
// the handler called preventDefault().
extern "C" bool js_dispatch_clipboard_event_to_element(Item target_item, const char* type);
static bool radiant_dispatch_clipboard_event(EventContext* evcon, View* target,
                                             const char* type)
{
    DomElement* dom_target = radiant_view_to_dom_element(target);
    if (!dom_target) return false;
    JsDispatchScope dispatch_scope(evcon);
    if (!dispatch_scope.active) return false;
    Item target_item = js_dom_wrap_element(dom_target);
    bool prevented = js_dispatch_clipboard_event_to_element(target_item, type);
    return prevented;
}

// Stage 4C Phase B: HTML5 drag-and-drop JS event dispatch. The native drag
// machinery (DragDropState) only invokes Lambda-template handlers and only on
// `dropzone`-attributed elements; script editors that reorder blocks via
// addEventListener('dragstart'|'dragover'|'drop') with a DataTransfer never see
// those. These helpers dispatch real JS DragEvents to the element under the
// cursor with a session-persistent DataTransfer, independent of `dropzone`.
extern "C" void js_drag_session_begin(void);
extern "C" void js_drag_session_end(void);
extern "C" bool js_dispatch_drag_event_to_element(Item target_item,
        const char* type, double client_x, double client_y);

static bool radiant_dispatch_drag_event(EventContext* evcon, View* target,
                                        const char* type, double cx, double cy)
{
    DomElement* dom_target = radiant_view_to_dom_element(target);
    if (!dom_target) return false;
    JsDispatchScope dispatch_scope(evcon);
    if (!dispatch_scope.active) return false;
    Item target_item = js_dom_wrap_element(dom_target);
    bool prevented = js_dispatch_drag_event_to_element(target_item, type, cx, cy);
    log_debug("JSDND: dispatched '%s' at (%.1f,%.1f) prevented=%d", type, cx, cy, prevented);
    return prevented;
}

/**
 * §7 unification (U-5): dispatch a "wheel" event via the JS EventTarget
 * pipeline. Returns true if default action (native scroll) should be
 * suppressed (event.preventDefault()).
 */
typedef struct {
    double client_x;
    double client_y;
    double delta_x;
    double delta_y;
    int mods;
} WheelEventBuildArgs;

static Item build_wheel_event_item(void* userdata) {
    WheelEventBuildArgs* args = (WheelEventBuildArgs*)userdata;
    return js_create_native_wheel_event("wheel", args->client_x, args->client_y,
        args->delta_x, args->delta_y, 0,
        (args->mods & RDT_MOD_CTRL) != 0,
        (args->mods & RDT_MOD_SHIFT) != 0,
        (args->mods & RDT_MOD_ALT) != 0,
        (args->mods & RDT_MOD_SUPER) != 0);
}

static bool radiant_dispatch_wheel_event(EventContext* evcon, View* target,
                                         double client_x, double client_y,
                                         double delta_x, double delta_y,
                                         int mods)
{
    WheelEventBuildArgs args = {client_x, client_y, delta_x, delta_y, mods};
    return radiant_dispatch_built_event(evcon, target, build_wheel_event_item,
        &args, true);
}

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event) {
    memset(evcon, 0, sizeof(EventContext));
    evcon->dom_event = ItemNull;
    evcon->dom_event_root_lifetime = true;
    evcon->ui_context = uicon;
    evcon->event = *event;
    evcon->target_document = uicon
        ? event_context_find_focused_document(uicon->document, 0,
                                              &evcon->iframe_container)
        : NULL;
    if (!evcon->target_document && uicon) evcon->target_document = uicon->document;
    // load default font Arial, size 16 px
    setup_font(uicon, &evcon->font, &uicon->default_font);
    evcon->new_cursor = CSS_VALUE_AUTO;
    radiant_document_ensure_state(uicon->document, "event_context_init");
}

void event_context_cleanup(EventContext* evcon) {
    if (!evcon) return;
    if (evcon->dom_event_root_gc) {
        gc_unregister_root((gc_heap_t*)evcon->dom_event_root_gc,
                           &evcon->dom_event.item);
    }
    evcon->dom_event = ItemNull;
    evcon->dom_event_root_gc = nullptr;
    evcon->dom_event_root_lifetime = false;
}

bool radiant_editing_animation_active(DocState* state) {
    return editing_controller_animation_active(state);
}

bool radiant_editing_animation_tick(UiContext* uicon, double timestamp) {
    EditingControllerHooks hooks = editing_controller_hooks();
    bool changed = editing_controller_animation_tick(uicon, timestamp, &hooks);
    if (changed) to_repaint();
    return changed;
}

// ============================================================================
// Interaction State Updates
// ============================================================================

/**
 * Recursively clear stylesheet declarations and styles_resolved on every
 * element in the subtree. Live inline declarations remain attached while
 * previously matching selector declarations (e.g. :hover) are removed.
 */
static void clear_cascaded_styles_recursive(DomNode* node) {
    if (!node) return;
    if (node->is_element()) {
        DomElement* e = lam::dom_require_element(node);
        if (!layout_element_is_anonymous_table_fixup(e)) {
            dom_element_clear_cascaded_styles(e);
            // Pseudo declarations share the base cascade epoch; otherwise a :hover
            // recascade reads declarations that no longer match.
            dom_element_clear_pseudo_styles(e);
            e->set_styles_resolved(false);
        }
        for (DomNode* c = e->first_child; c; c = c->next_sibling) {
            if ((uintptr_t)c < 4096) {
                log_error("drawing recascade invalid child link: parent=%p tag=%s child=%p",
                          (void*)e, e->tag_name ? e->tag_name : "?", (void*)c);
                return;
            }
            clear_cascaded_styles_recursive(c);
        }
    }
}

static void mark_layout_dirty_recursive(DomNode* node) {
    if (!node) return;
    node->layout_dirty = true;
    if (!node->is_element()) return;
    DomElement* element = lam::dom_require_element(node);
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        mark_layout_dirty_recursive(child);
    }
}

static bool css_simple_selector_uses_hover(CssSimpleSelector* simple) {
    if (!simple) return false;
    if (simple->type == CSS_SELECTOR_PSEUDO_HOVER) return true;
    for (size_t i = 0; i < simple->function_selector_count; i++) {
        CssSelector* nested = simple->function_selectors ? simple->function_selectors[i] : NULL;
        if (!nested) continue;
        for (size_t c = 0; c < nested->compound_selector_count; c++) {
            CssCompoundSelector* compound = nested->compound_selectors ? nested->compound_selectors[c] : NULL;
            if (!compound) continue;
            for (size_t s = 0; s < compound->simple_selector_count; s++) {
                if (css_simple_selector_uses_hover(compound->simple_selectors ? compound->simple_selectors[s] : NULL)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool css_selector_uses_hover(CssSelector* selector) {
    if (!selector) return false;
    for (size_t c = 0; c < selector->compound_selector_count; c++) {
        CssCompoundSelector* compound = selector->compound_selectors ? selector->compound_selectors[c] : NULL;
        if (!compound) continue;
        for (size_t s = 0; s < compound->simple_selector_count; s++) {
            if (css_simple_selector_uses_hover(compound->simple_selectors ? compound->simple_selectors[s] : NULL)) {
                return true;
            }
        }
    }
    return false;
}

static bool css_rule_uses_hover(CssRule* rule) {
    if (!rule) return false;
    if (rule->type == CSS_RULE_STYLE || rule->type == CSS_RULE_NESTING) {
        CssSelectorGroup* group = rule->data.style_rule.selector_group;
        if (group) {
            for (size_t i = 0; i < group->selector_count; i++) {
                if (css_selector_uses_hover(group->selectors ? group->selectors[i] : NULL)) return true;
            }
        } else if (css_selector_uses_hover(rule->data.style_rule.selector)) {
            return true;
        }
        for (size_t i = 0; i < rule->data.style_rule.nested_rule_count; i++) {
            if (css_rule_uses_hover(rule->data.style_rule.nested_rules ? rule->data.style_rule.nested_rules[i] : NULL)) {
                return true;
            }
        }
    } else if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
               rule->type == CSS_RULE_CONTAINER) {
        for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            if (css_rule_uses_hover(rule->data.conditional_rule.rules ? rule->data.conditional_rule.rules[i] : NULL)) {
                return true;
            }
        }
    }
    return false;
}

static bool document_has_hover_rules(DomDocument* doc) {
    if (!doc) return false;
    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* stylesheet = doc->stylesheets ? doc->stylesheets[i] : NULL;
        if (!stylesheet || stylesheet->disabled) continue;
        for (size_t r = 0; r < stylesheet->rule_count; r++) {
            if (css_rule_uses_hover(stylesheet->rules ? stylesheet->rules[r] : NULL)) {
                return true;
            }
        }
    }
    return false;
}

static void recascade_document_for_pseudo_state(DomDocument* doc, DocState* state) {
    if (!doc || !state) return;

    Pool* pool = doc->document_pool;
    CssEngine* css_engine = (CssEngine*)doc->services.cached_css_engine;
    if (pool && css_engine && doc->root) {
        // Pseudo-state changes can affect descendants through selectors like
        // `.parent:hover .child`, so clear and re-apply the full cascade once
        // after the StateStore pseudo bits have been updated.
        clear_cascaded_styles_recursive(static_cast<DomNode*>(doc->root));
        SelectorMatcher* matcher = selector_matcher_create(pool);
        if (matcher) {
            state_configure_selector_matcher(state, matcher);
            radiant_apply_css_stylesheets_to_tree(
                doc, doc->root, doc->stylesheets, doc->stylesheet_count,
                pool, css_engine, matcher);
        }
    }
}

/**
 * Schedule style/layout work after StateStore pseudo-state changes.
 */
static void sync_pseudo_state(View* view, uint32_t pseudo_flag, bool set) {
    (void)set;
    if (!view || !view->is_element()) return;

    DomElement* element = lam::dom_require_element(view);
    if (element->doc && element->doc->state) {
        DocState* state = (DocState*)element->doc->state;
        DomDocument* doc = element->doc;

        if (pseudo_flag == PSEUDO_STATE_HOVER && !document_has_hover_rules(doc)) {
            return;
        }

        recascade_document_for_pseudo_state(doc, state);

        // Selector combinators can make a pseudo-state change affect siblings
        // and ancestors, so a subtree-only request leaves `:checked + label`
        // with stale computed style after a JS IDL write.
        // The mutation reconciler may run an incremental layout before the
        // queued reflow. Marking the retained tree prevents its clean subtree
        // fast path from reusing styles resolved before this state transition.
        mark_layout_dirty_recursive(static_cast<DomNode*>(doc->root));
        reflow_schedule(state, doc->root, REFLOW_SUBTREE, CHANGE_PSEUDO_STATE);

        // Always mark for repaint
        dirty_mark_element(state, doc->root);
        doc_state_mark_dirty(state);
    }
}

void radiant_sync_pseudo_state(View* view, uint32_t pseudo_flag, bool set) {
    sync_pseudo_state(view, pseudo_flag, set);
}

// Resolve the owning document from any view, including non-element views
// (e.g. a text run). The hover hit-test target for inline content is the
// leaf text view, which is not an element, so walk up to the nearest
// element ancestor to find the document.
static DomDocument* hover_resolve_document(View* view) {
    DomNode* node = static_cast<DomNode*>(view);
    while (node) {
        if (node->is_element()) {
            DomElement* elem = lam::dom_require_element(node);
            if (elem->doc) return elem->doc;
        }
        node = node->parent;
    }
    return nullptr;
}

static void sync_hover_pseudo_state_after_transition(DocState* state,
                                                     View* prev_hover,
                                                     View* new_target) {
    if (!state) return;

    DomDocument* doc = hover_resolve_document(new_target);
    if (!doc) doc = hover_resolve_document(prev_hover);
    if (!doc) return;

    if (document_has_hover_rules(doc)) {
        recascade_document_for_pseudo_state(doc, state);
        if (doc->root) {
            reflow_schedule(state, doc->root, REFLOW_SUBTREE, CHANGE_PSEUDO_STATE);
            dirty_mark_element(state, doc->root);
        }
    }

    View* node = prev_hover;
    while (node) {
        dirty_mark_element(state, node);
        node = static_cast<View*>(node->parent);
    }

    node = new_target;
    while (node) {
        dirty_mark_element(state, node);
        node = static_cast<View*>(node->parent);
    }

    doc_state_mark_dirty(state);
}

/**
 * Update hover state when mouse moves to a new target
 * Sets :hover on target and all ancestors, clears :hover on previous target
 */
void update_hover_state(EventContext* evcon, View* new_target) {
    DocState* state = event_context_target_state(evcon);
    if (!state) return;

    View* prev_hover = static_cast<View*>(state->hover_target);

    if (prev_hover == new_target) return;  // no change

    HoverTransitionArgs hover_args = { .target = new_target };
    hover_transition(state, HOVER_TRANSITION_SET_TARGET, &hover_args);

    sync_hover_pseudo_state_after_transition(state, prev_hover, new_target);
    evcon->need_repaint = true;

    if (prev_hover) {
        log_debug("update_hover_state: cleared hover on %p", prev_hover);
        // Hover transitions previously emitted only mouseover, leaving
        // mouseenter-driven tooltip libraries unable to observe real input.
        radiant_dispatch_mouse_event(evcon, prev_hover, "mouseout",
            evcon->event.mouse_position.x, evcon->event.mouse_position.y,
            0, 0, false, false, false, false, 0);
        radiant_dispatch_mouse_event(evcon, prev_hover, "mouseleave",
            evcon->event.mouse_position.x, evcon->event.mouse_position.y,
            0, 0, false, false, false, false, 0);
    }

    if (new_target) {
        log_debug("update_hover_state: set hover on %p", new_target);

        // Dispatch through the unified EventTarget path. Static inline
        // attributes have already been installed as IDL `onmouseover` slots.
        radiant_dispatch_mouse_event(evcon, new_target, "mouseover",
            evcon->event.mouse_position.x, evcon->event.mouse_position.y,
            0, 0, false, false, false, false, 0);
        radiant_dispatch_mouse_event(evcon, new_target, "mouseenter",
            evcon->event.mouse_position.x, evcon->event.mouse_position.y,
            0, 0, false, false, false, false, 0);
    }
}

/**
 * Update active state on mouse down/up
 */
void update_active_state(EventContext* evcon, View* target, bool is_active) {
    DocState* state = event_context_target_state(evcon);
    if (!state) return;

    if (is_active) {
        ActiveTransitionArgs active_args = { .target = target };
        active_transition(state, ACTIVE_TRANSITION_SET_TARGET, &active_args);
        View* node = target;
        while (node) {
            sync_pseudo_state(node, PSEUDO_STATE_ACTIVE, true);
            node = static_cast<View*>(node->parent);
        }
        log_debug("update_active_state: set active on %p", target);
    } else {
        View* prev_active = static_cast<View*>(state->active_target);
        ActiveTransitionArgs active_args = { .target = NULL };
        active_transition(state, ACTIVE_TRANSITION_SET_TARGET, &active_args);
        if (prev_active) {
            View* node = prev_active;
            while (node) {
                sync_pseudo_state(node, PSEUDO_STATE_ACTIVE, false);
                node = static_cast<View*>(node->parent);
            }
        }
        log_debug("update_active_state: cleared active");
    }
}

// ============================================================================
// Checkbox and Radio Button State Handling
// ============================================================================

/**
 * Check if an element is a checkbox input
 */
static bool is_input_type(View* view, const char* expected_type) {
    if (!view || !view->is_element()) return false;
    ViewElement* elem = lam::view_require_element(view);
    if (elem->tag() != MARKUP_NAME_INPUT) return false;
    const char* type = elem->get_attribute("type");
    return type && strcmp(type, expected_type) == 0;
}

static bool is_checkbox(View* view) {
    return is_input_type(view, "checkbox");
}

/**
 * Check if an element is a radio button input
 */
static bool is_radio(View* view) {
    return is_input_type(view, "radio");
}


/**
 * Find the associated checkbox/radio input for a target element.
 * If target is already a checkbox/radio, returns it.
 * If target is inside a label, finds the checkbox/radio:
 *   - By "for" attribute matching input id
 *   - By finding an input child inside the label
 * @return The checkbox/radio input View, or NULL if not found
 */
static View* find_checkbox_radio_input(View* target) {
    if (!target) return nullptr;

    log_debug("find_checkbox_radio_input: starting search from target=%p", target);

    // Check if target itself is a checkbox/radio
    if (is_checkbox(target) || is_radio(target)) {
        log_debug("find_checkbox_radio_input: target is checkbox/radio");
        return target;
    }

    // Walk up the tree looking for a label element
    View* current = target;
    View* label_element = nullptr;
    while (current) {
        if (current->is_element()) {
            ViewElement* elem = lam::view_require_element(current);
            log_debug("find_checkbox_radio_input: checking element tag=%d (%s)", elem->tag(), elem->node_name());
            if (elem->tag() == MARKUP_NAME_LABEL) {
                label_element = current;
                log_debug("find_checkbox_radio_input: found label element");
                break;
            }
            // If we hit a checkbox/radio directly, use it
            if (is_checkbox(current) || is_radio(current)) {
                log_debug("find_checkbox_radio_input: found checkbox/radio in ancestor chain");
                return current;
            }
        }
        current = current->parent;
    }

    if (!label_element) {
        log_debug("find_checkbox_radio_input: no label found in ancestor chain");
        return nullptr;
    }

    ViewElement* label = lam::view_require_element(label_element);

    // Check for "for" attribute pointing to an input id
    const char* for_attr = label->get_attribute("for");
    if (for_attr && for_attr[0]) {
        log_debug("find_checkbox_radio_input: label has for='%s'", for_attr);
        // Need to find input with matching id in the document
        // Walk from document root to find matching id
        View* root = label_element;
        while (root->parent) root = root->parent;

        // Simple DFS to find element with matching id
        View* search = root;
        while (search) {
            if (search->is_element()) {
                ViewElement* elem = lam::view_require_element(search);
                const char* id = elem->get_attribute("id");
                if (id && strcmp(id, for_attr) == 0) {
                    if (is_checkbox(search) || is_radio(search)) {
                        return search;
                    }
                }
            }
            // Depth-first traversal
            if (search->is_element()) {
                ViewElement* search_elem = lam::view_require_element(search);
                // Form controls can be visually hidden by widget CSS, so this
                // must follow DOM children rather than only laid-out blocks.
                if (search_elem->first_child) {
                    search = static_cast<View*>(search_elem->first_child);
                    continue;
                }
            }
            if (search->next()) {
                search = search->next();
                continue;
            }
            search = search->parent;
            while (search && !search->next()) {
                search = search->parent;
            }
            if (search) search = search->next();
        }
    }

    // No "for" attribute or not found - look for input child inside label
    View* child = label->first_child;
    while (child) {
        if (is_checkbox(child) || is_radio(child)) {
            return child;
        }
        // Check children recursively
        if (child->is_element()) {
            ViewElement* child_elem = lam::view_require_element(child);
            View* nested = static_cast<View*>(child_elem->first_child);
            while (nested) {
                if (is_checkbox(nested) || is_radio(nested)) {
                    return nested;
                }
                nested = nested->next();
            }
        }
        child = child->next();
    }

    return nullptr;
}

// Resolve the activation control from a hit inside a button/input, then use
// the form subtree's document order for implicit submission. The tree walk is
// mechanism; submitter selection stays in the form behavior.
static View* find_form_activation_button(View* target, bool reset) {
    for (DomNode* node = static_cast<DomNode*>(target); node; node = node->parent) {
        if (!node->is_element()) continue;
        bool match = reset
            ? js_dom_is_reset_button((void*)node)
            : js_dom_is_submit_button((void*)node);
        if (match) return static_cast<View*>(node);
    }
    return nullptr;
}

static View* find_first_form_submitter_in_tree(DomNode* node, DomElement* form) {
    for (DomNode* current = node; current; current = current->next_sibling) {
        if (!current->is_element()) continue;
        DomElement* current_elem = current->as_element();
        if (js_dom_is_submit_button((void*)current) &&
            js_dom_find_form_owner((void*)current) == form &&
            !js_dom_is_disabled((void*)current)) {
            return static_cast<View*>(current);
        }
        View* nested = find_first_form_submitter_in_tree(current_elem->first_child, form);
        if (nested) return nested;
    }
    return nullptr;
}

static View* find_first_form_submitter(DomElement* form) {
    if (!form) return nullptr;
    DomDocument* doc = form->doc;
    bool connected = false;
    if (doc && doc->root) {
        for (DomNode* node = (DomNode*)form; node; node = node->parent) {
            if (node == (DomNode*)doc->root) {
                connected = true;
                break;
            }
        }
    }
    return find_first_form_submitter_in_tree(
        connected ? (DomNode*)doc->root : form->first_child, form);
}

// Run the package policy for a submitter or an implicit form target. A direct
// bridge remains only for package-off documents, preserving script-less HTML
// behavior while the migrated class is still being staged.
static bool run_form_submit_activation(EventContext* evcon, View* target) {
    if (!target || !target->is_element()) return false;
    DomElement* elem = lam::dom_require_element(target);
    DomElement* owner = nullptr;
    bool has_submitter = js_dom_is_submit_button((void*)elem);
    if (has_submitter) {
        if (js_dom_is_disabled((void*)elem) || !js_dom_is_connected((void*)elem)) {
            return false;
        }
        owner = js_dom_find_form_owner((void*)elem);
    } else if (elem->tag_name && strcasecmp(elem->tag_name, "form") == 0) {
        owner = elem;
    }
    if (!owner) return false;

    bool claimed = radiant_behavior_claims_event(
        evcon, target, "submitactivation");
    if (claimed) {
        radiant_dispatch_behavior_submit_activation(evcon, target);
    } else {
        Item submitter = has_submitter ? js_dom_wrap_element(elem)
                                       : make_js_undefined();
        js_dom_form_request_submit_bridge(js_dom_wrap_element(owner), submitter);
    }
    return true;
}

static bool run_form_reset_activation(EventContext* evcon, View* target) {
    if (!target || !target->is_element()) return false;
    DomElement* elem = lam::dom_require_element(target);
    if (!js_dom_is_reset_button((void*)elem) ||
        js_dom_is_disabled((void*)elem) || !js_dom_is_connected((void*)elem)) {
        return false;
    }
    DomElement* owner = js_dom_find_form_owner((void*)elem);
    if (!owner) return false;

    bool claimed = radiant_behavior_claims_event(
        evcon, target, "resetactivation");
    if (claimed) {
        radiant_dispatch_behavior_reset_activation(evcon, target);
    } else {
        radiant_dom_element_operation(js_dom_wrap_element(owner), JUBE_DOM_RESET,
                                      nullptr, 0);
    }
    return true;
}

static void run_form_button_default(EventContext* evcon, View* target) {
    if (!evcon || !target || evcon->default_prevented) return;
    if (find_form_activation_button(target, true)) {
        run_form_reset_activation(evcon, target);
    } else if (find_form_activation_button(target, false)) {
        run_form_submit_activation(evcon, target);
    }
}

static View* find_link_activation_target(View* target) {
    for (View* current = target; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* elem = lam::dom_require_element(current);
        if (elem->tag() == MARKUP_NAME_A && elem->get_attribute("href")) {
            return current;
        }
    }
    return nullptr;
}

// The ordinary click is the author event. This behavior-only follow-up runs
// only once click cancellation is settled, then the package submits its
// resolved request for native execution (ES31).
static bool run_link_activation(EventContext* evcon, View* target) {
    if (!evcon || !evcon->ui_context || evcon->default_prevented) return false;
    View* anchor = find_link_activation_target(target);
    if (!anchor) return false;
    if (!radiant_behavior_claims_event(evcon, anchor, "linkactivation")) {
        return false;
    }
    if (!dispatch_behavior_handler(evcon, anchor, "linkactivation", nullptr, nullptr)) {
        return false;
    }
    DomDocument* document = event_context_target_document(evcon);
    return radiant_execute_pending_navigation(evcon->ui_context, document);
}

// ES25/ES26: trusted and script-created clicks settle one UA default tier.
// Checkbox/radio association, form activation, and navigation are mechanisms;
// their policy remains the behavior package selected by the shared record.
static bool dispatch_click_default_actions(EventContext* evcon, View* target) {
    if (!evcon || !target || evcon->default_prevented) return false;

    bool handled = false;
    View* activation_target = find_checkbox_radio_input(target);
    if (!activation_target) activation_target = target;
    if (dispatch_lambda_handler(evcon, activation_target, "click")) {
        evcon->need_repaint = true;
        handled = true;
    }

    View* submit_target = find_form_activation_button(target, false);
    if (submit_target && !evcon->default_prevented &&
        !js_dom_is_disabled((void*)submit_target) &&
        js_dom_is_connected((void*)submit_target)) {
        if (run_form_submit_activation(evcon, submit_target)) {
            evcon->need_repaint = true;
            handled = true;
        }
    }

    View* reset_target = find_form_activation_button(target, true);
    if (reset_target && !evcon->default_prevented &&
        !js_dom_is_disabled((void*)reset_target) &&
        js_dom_is_connected((void*)reset_target)) {
        if (run_form_reset_activation(evcon, reset_target)) {
            evcon->need_repaint = true;
            handled = true;
        }
    }

    if (!evcon->default_prevented && run_link_activation(evcon, target)) {
        evcon->need_repaint = true;
        handled = true;
    }
    return handled;
}

// Only the exact bridge re-entry bypasses the wrapper below. A synthetic event
// raised from a trusted listener is a distinct logical dispatch and must enter
// the shared tier too.
static thread_local Item s_synthetic_dom_dispatch_raw_event = ItemNull;

extern "C" bool radiant_synthetic_dom_dispatch_is_reentry(Item event_item) {
    return event_item.item != ITEM_NULL &&
        event_item.item == s_synthetic_dom_dispatch_raw_event.item;
}

extern "C" Item radiant_dispatch_synthetic_dom_event(Item target_item,
                                                      Item event_item) {
    if (!radiant_dom_event_is(event_item)) return ItemNull;
    DomElement* target = (DomElement*)js_dom_unwrap_element(target_item);
    if (!target || !target->doc) return ItemNull;

    EventContext evcon = {};
    evcon.target = static_cast<View*>(target);
    evcon.target_document = target->doc;
    evcon.ui_context = static_cast<UiContext*>(target->doc->js.host_ui_context);
    JsDispatchScope dispatch_scope(&evcon);
    bool borrows_script_context = !dispatch_scope.active && context &&
        js_dom_get_document() == target->doc;
    if (!dispatch_scope.active && !borrows_script_context) return ItemNull;
    EventContext* previous_active_event_context = s_active_js_dispatch_event_context;
    if (borrows_script_context) {
        // Bootstrap scripts already own the document evaluator but run before
        // the first native input batch. Publish this event only for the call so
        // the author cascade and its nested synthetic events retain one owner.
        s_active_js_dispatch_event_context = &evcon;
    }

    RootFrame roots(2);
    Rooted<Item> target_root(roots, target_item);
    Rooted<Item> event_root(roots, event_item);
    event_context_set_dom_event(&evcon, event_root.get());
    Item previous_raw_event = s_synthetic_dom_dispatch_raw_event;
    s_synthetic_dom_dispatch_raw_event = event_root.get();
    Item result = js_dom_dispatch_event(target_root.get(), event_root.get());
    s_synthetic_dom_dispatch_raw_event = previous_raw_event;
    if (!item_is_error(result)) {
        const char* event_name = fn_to_cstr(js_get_name_key(event_root.get(), "type"));
        if (event_name && !radiant_dom_event_default_prevented(event_root.get())) {
            if (strcmp(event_name, "click") == 0) {
                dispatch_click_default_actions(&evcon, evcon.target);
            } else {
                dispatch_lambda_handler(&evcon, evcon.target, event_name);
            }
        }
    }
    if (borrows_script_context) {
        s_active_js_dispatch_event_context = previous_active_event_context;
    }
    return result;
}


static bool click_target_is_disabled_control(DocState* state, View* target) {
    for (View* current = target; current; current = current->parent) {
        if (!current->is_element()) continue;
        DomElement* elem = lam::dom_require_element(current);
        if (elem->form_control() && form_control_is_disabled(state, current)) {
            return true;
        }
    }
    return false;
}


// ============================================================================
// Select Dropdown Handling
// ============================================================================



/**
 * Calculate dropdown popup dimensions
 */
static void calculate_dropdown_dimensions(ViewBlock* select, DocState* state,
                                           float logical_width) {
    if (!select || !state || !select->form) return;

    int option_count = select->form->option_count;
    if (option_count <= 0) option_count = 1;

    // Maximum visible options
    int max_visible = 10;
    int visible_count = (option_count < max_visible) ? option_count : max_visible;

    // the popup uses the native option row, not an author-sized closed control
    float option_height = form_select_dropdown_row_height(select->form);

    // Calculate popup dimensions
    doc_state_set_dropdown_geometry(state, state->dropdown_x, state->dropdown_y,
        logical_width, visible_count * option_height);
}

/**
 * Handle click on select to toggle dropdown
 */
// Opening a dropdown is mechanism: overlay placement and sizing come from
// layout geometry. Both the native activation path and the dom package's
// `open_dropdown` primitive go through here so the geometry is computed once.
static void select_open_dropdown(UiContext* uicon, DocState* state,
                                 View* select_view) {
    if (!uicon || !state || !select_view) return;
    ViewBlock* select = lam::view_require_block(select_view);
    if (!select || !select->form) return;
    log_debug("select_open_dropdown: opening with %d options", select->form->option_count);
    doc_state_open_dropdown(state, select_view);

    float visual_x = 0.0f, visual_y = 0.0f;
    float visual_width = 0.0f, visual_height = 0.0f;
    view_get_visual_bounds(select_view, &visual_x, &visual_y,
                           &visual_width, &visual_height);
    float doc_x = 0.0f, doc_y = 0.0f;
    radiant_document_viewport_offset(uicon, select->doc, &doc_x, &doc_y);

    // Popup state stays in the top-level logical viewport. Rendering performs
    // the sole logical-to-surface conversion, so event hit-testing never needs
    // to know the monitor scale.
    doc_state_set_dropdown_geometry(state, doc_x + visual_x,
        doc_y + visual_y + visual_height, state->dropdown_width,
        state->dropdown_height);
    calculate_dropdown_dimensions(select, state, visual_width);
}


static ViewBlock* event_open_dropdown_select(EventContext* evcon,
                                             DocState** out_state) {
    DocState* state = event_context_target_state(evcon);
    if (out_state) *out_state = state;
    if (!state || !state->open_dropdown) return nullptr;
    ViewBlock* select = lam::view_require_block(state->open_dropdown);
    return select->form ? select : nullptr;
}

/**
 * Handle click on a dropdown option
 * @param mouse_x/mouse_y Pointer position in logical window pixels
 * @return true if an option was selected
 */
static bool handle_dropdown_option_click(EventContext* evcon, float mouse_x, float mouse_y) {
    DocState* state = nullptr;
    ViewBlock* select = event_open_dropdown_select(evcon, &state);
    if (!select) return false;

    log_debug("handle_dropdown_option_click: pointer=(%.1f, %.1f), dropdown=(%.1f, %.1f, %.1f, %.1f)",
             mouse_x, mouse_y, state->dropdown_x, state->dropdown_y,
             state->dropdown_width, state->dropdown_height);

    // Check if click is within dropdown popup
    if (mouse_x < state->dropdown_x || mouse_x > state->dropdown_x + state->dropdown_width) {
        log_debug("handle_dropdown_option_click: click outside X bounds");
        return false;
    }
    if (mouse_y < state->dropdown_y || mouse_y > state->dropdown_y + state->dropdown_height) {
        log_debug("handle_dropdown_option_click: click outside Y bounds");
        return false;
    }

    // Calculate which option was clicked
    float option_height = form_select_dropdown_row_height(select->form);
    int clicked_index = (int)((mouse_y - state->dropdown_y) / option_height); // INT_CAST_OK: option array index

    log_debug("handle_dropdown_option_click: option_height=%.1f, clicked_index=%d, option_count=%d",
             option_height, clicked_index, select->form->option_count);

    if (clicked_index >= 0 && clicked_index < select->form->option_count) {
        log_debug("handle_dropdown_option_click: option %d resolved, dispatching commit", clicked_index);
        // Geometry resolved the option; the template commits it and closes the
        // dropdown. No native fallback, matching activation, validation and
        // undo — an unclaimed <select> simply does not commit.
        InputIntent intent;
        intent.option_index = clicked_index;
        return radiant_dispatch_behavior_option_commit(evcon, static_cast<View*>(select),
                                                       &intent);
    }

    log_debug("handle_dropdown_option_click: clicked_index out of range");
    return false;
}

/**
 * Handle mouse move to update hover state in dropdown
 */
static void update_dropdown_hover(EventContext* evcon, float mouse_x, float mouse_y) {
    DocState* state = nullptr;
    ViewBlock* select = event_open_dropdown_select(evcon, &state);
    if (!select) return;

    // Check if mouse is within dropdown popup
    if (mouse_x < state->dropdown_x || mouse_x > state->dropdown_x + state->dropdown_width ||
        mouse_y < state->dropdown_y || mouse_y > state->dropdown_y + state->dropdown_height) {
        if (form_control_get_hover_index(state, static_cast<View*>(select)) != -1) {
            form_control_set_hover_index(state, static_cast<View*>(select), -1);
        }
        return;
    }

    // Calculate which option is hovered
    float option_height = form_select_dropdown_row_height(select->form);
    int hover_index = (int)((mouse_y - state->dropdown_y) / option_height); // INT_CAST_OK: option array index

    if (hover_index >= 0 && hover_index < select->form->option_count) {
        if (form_control_get_hover_index(state, static_cast<View*>(select)) != hover_index) {
            form_control_set_hover_index(state, static_cast<View*>(select), hover_index);
        }
    }
}

/**
 * Handle keyboard navigation in dropdown
 */
static bool editing_key_may_emit_text(const KeyEvent* key_event) {
    if (!key_event) return false;
    if (key_event->mods & (RDT_MOD_CTRL | RDT_MOD_SUPER | RDT_MOD_ALT)) {
        return false;
    }
    switch (key_event->key) {
    case RDT_KEY_BACKSPACE:
    case RDT_KEY_DELETE:
    case RDT_KEY_ENTER:
    case RDT_KEY_TAB:
    case RDT_KEY_ESCAPE:
    case RDT_KEY_LEFT:
    case RDT_KEY_RIGHT:
    case RDT_KEY_UP:
    case RDT_KEY_DOWN:
    case RDT_KEY_HOME:
    case RDT_KEY_END:
    case RDT_KEY_PAGE_UP:
    case RDT_KEY_PAGE_DOWN:
        return false;
    default:
        // Platform IMEs may report an unidentified physical key before their
        // committed Unicode callback, which still has keydown cancellation.
        return true;
    }
}

// F11: every key an open dropdown responds to now goes to the template. Enter
// already committed through `optioncommit`, but Up/Down and Escape did not —
// one interaction split by key, which is the third time this shape has appeared
// in this function (mouse commit, then Enter, now its siblings).
static bool handle_dropdown_key(EventContext* evcon, int key, int mods) {
    DocState* state = nullptr;
    ViewBlock* select = event_open_dropdown_select(evcon, &state);
    if (!select) return false;
    InputIntent intent;
    intent.key = key;
    intent.mods = mods;
    return radiant_dispatch_behavior_dropdown_key(evcon, static_cast<View*>(select),
                                                  &intent);
}

/**
 * Close dropdown if clicking outside
 */
static void close_dropdown_if_outside(EventContext* evcon, float mouse_x, float mouse_y) {
    DocState* state = nullptr;
    ViewBlock* select = event_open_dropdown_select(evcon, &state);
    if (!select) return;

    float select_abs_x = 0.0f, select_abs_y = 0.0f;
    float select_w = 0.0f, select_h = 0.0f;
    view_get_visual_bounds(static_cast<View*>(select), &select_abs_x,
                           &select_abs_y, &select_w, &select_h);
    float doc_x = 0.0f, doc_y = 0.0f;
    radiant_document_viewport_offset(evcon->ui_context, select->doc,
                                     &doc_x, &doc_y);
    select_abs_x += doc_x;
    select_abs_y += doc_y;

    // Check if click is on the select itself (toggle handled elsewhere)
    if (mouse_x >= select_abs_x && mouse_x <= select_abs_x + select_w &&
        mouse_y >= select_abs_y && mouse_y <= select_abs_y + select_h) {
        return;  // Click on the select box itself; the <select> behavior template owns opening/closing it
    }

    // Check if click is on dropdown popup
    if (mouse_x >= state->dropdown_x && mouse_x <= state->dropdown_x + state->dropdown_width &&
        mouse_y >= state->dropdown_y && mouse_y <= state->dropdown_y + state->dropdown_height) {
        return;  // Click on dropdown, let handle_dropdown_option_click deal with it
    }

    // Click outside - close dropdown
    log_debug("close_dropdown_if_outside: closing dropdown");
    doc_state_close_dropdown(state, static_cast<View*>(select));
}

/**
 * Check if an element is focusable
 */
bool is_view_focusable(View* view) {
    if (!view) return false;

    // Elements that are focusable by default:
    // - <a> with href
    // - <button>
    // - <input> (except hidden)
    // - <select>
    // - <textarea>
    // - elements with tabindex >= 0

    if (view->is_element()) {
        ViewElement* elem = lam::view_require_element(view);
        uint32_t tag = elem->tag();

        // F8 (Radiant_Design_Form_Input.md §4): a disabled form control
        // is not part of the tabbing order. The HTML/ARIA spec says
        // disabled form elements are inert.
        DomElement* delem = lam::dom_require_element(view);
        DocState* state = delem->doc ? (DocState*)delem->doc->state : NULL;
        if (delem->form_control() && form_control_is_disabled(state, static_cast<View*>(delem))) {
            return false;
        }

        switch (tag) {
        case MARKUP_NAME_A:
            // <a> is focusable if it has href
            return elem->get_attribute("href") != NULL;
        case MARKUP_NAME_BUTTON:
        case MARKUP_NAME_SELECT:
        case MARKUP_NAME_TEXTAREA:
            return true;
        case MARKUP_NAME_INPUT: {
            // Input is focusable unless type="hidden"
            const char* type = elem->get_attribute("type");
            return !type || strcmp(type, "hidden") != 0;
        }
        default:
            // Check for tabindex attribute
            const char* tabindex = elem->get_attribute("tabindex");
            if (tabindex) {
                int ti = (int)str_to_int64_default(tabindex, strlen(tabindex), 0);
                return ti >= 0;
            }
            // CE-2 (Radiant_Design_Content_Editable.md §5): a contenteditable
            // editing host is implicitly focusable (treated as tabindex=0)
            // when no explicit tabindex is set.
            EditingHost h;
            if (editing_host_lookup(elem, &h) && h.host == elem) return true;
            break;
        }
    }

    return false;
}

bool is_view_programmatically_focusable(View* view) {
    if (is_view_focusable(view)) return true;
    if (!view || !view->is_element()) return false;
    ViewElement* elem = lam::view_require_element(view);
    // A negative tabindex excludes sequential focus only; HTMLElement.focus()
    // must still accept the target so keyboard events reach modal-style widgets.
    return elem->get_attribute("tabindex") != NULL;
}

static View* mouse_focus_target(View* hit) {
    for (View* view = hit; view; view = view->parent) {
        if (is_view_programmatically_focusable(view)) return view;
    }
    // Generated widget internals can be hit-tested through a visual child
    // whose View parent is not its DOM parent; follow the DOM chain so a
    // tabindex handle still receives the browser's mouse-focus default.
    for (DomNode* node = hit ? static_cast<DomNode*>(hit) : nullptr;
         node; node = node->parent) {
        if (node->is_element() &&
            is_view_programmatically_focusable(static_cast<View*>(node))) {
            return static_cast<View*>(node);
        }
    }
    return nullptr;
}

static bool prepare_previous_focus_blur(EventContext* evcon,
                                        DocState* state,
                                        View* prev_focus) {
    if (!evcon || !state || !prev_focus || !prev_focus->is_element()) {
        return false;
    }
    DomElement* prev_elem = lam::dom_require_element(prev_focus);
    if (te_password_reveal_clear(prev_elem)) {
        doc_state_request_repaint(state);
        evcon->need_repaint = true;
    }
    // ESO42: give a behavior template the commit decision, here — before
    // `change` and `blur` are dispatched below, which is the only point where
    // answering it can still preserve their order. A template that answers
    // requests the event through radiant.request_change; one that decides the
    // value was not committed simply requests nothing.
    // The commit decision is the package's, with no native fallback behind it:
    // a control no template governs commits nothing, exactly as a control no
    // template validates has no :valid/:invalid.
    (void)prev_elem;
    uint64_t change_epoch_before = radiant_change_request_epoch();
    radiant_dispatch_behavior_commit(evcon, prev_focus);
    return radiant_change_request_epoch() != change_epoch_before;
}

static void dispatch_focus_change_observed(EventContext* evcon, View* target) {
    if (!evcon || !target) return;
    radiant_dispatch_simple_event(evcon, target, "change", true, false);
    dispatch_lambda_handler_without_js(evcon, target, "change");
    sm_observe_action(event_context_target_state(evcon),
                      SM_ACT_DISPATCH_CHANGE);
}

static void dispatch_focus_blur_observed(EventContext* evcon,
                                         View* target,
                                         View* related_target) {
    if (!evcon || !target) return;
    radiant_dispatch_focus_event(evcon, target, "blur", related_target);
    dispatch_lambda_handler_without_js(evcon, target, "blur");
    sm_observe_action(event_context_target_state(evcon),
                      SM_ACT_DISPATCH_BLUR);
    radiant_dispatch_focus_event(evcon, target, "focusout", related_target);
    dispatch_lambda_handler_without_js(evcon, target, "focusout");
}

/**
 * Update focus state when an element gains/loses focus
 * @param from_keyboard true if focus change was triggered by keyboard (Tab key, etc.)
 */
static void blur_parent_document_focus_for_iframe_target(EventContext* evcon) {
    if (!evcon || !evcon->ui_context || !evcon->target_document ||
        evcon->target_document == evcon->ui_context->document) {
        return;
    }

    DomDocument* parent_doc = evcon->ui_context->document;
    DocState* parent_state = parent_doc ? (DocState*)parent_doc->state : NULL;
    if (!parent_state || !focus_get(parent_state)) return;

    // An iframe click moves keyboard ownership into its child document; leaving
    // the parent's old link focused would route later text input back to it.
    DomDocument* target_doc = evcon->target_document;
    evcon->target_document = parent_doc;
    update_focus_state(evcon, NULL, false);
    evcon->target_document = target_doc;
}

void update_focus_state(EventContext* evcon, View* new_focus, bool from_keyboard) {
    if (new_focus) {
        blur_parent_document_focus_for_iframe_target(evcon);
    }
    DocState* state = event_context_target_state(evcon);
    if (!state) return;

    View* prev_focus = focus_get(state);

    if (prev_focus == new_focus) return;  // no change
    bool should_dispatch_change =
        prepare_previous_focus_blur(evcon, state, prev_focus);

    // Use the focus API to handle all state updates
    if (new_focus) {
        if (prev_focus) {
            SmTransitionGuard sm_guard(state, SM_FAMILY_FOCUS,
                should_dispatch_change ? SM_EV_UI_FOCUS_WITH_CHANGE :
                                         SM_EV_UI_FOCUS_WITH_BLUR,
                new_focus);
            focus_set(state, new_focus, from_keyboard);
            if (should_dispatch_change) {
                dispatch_focus_change_observed(evcon, prev_focus);
            }
            dispatch_focus_blur_observed(evcon, prev_focus, new_focus);
            sm_guard.commit();
        } else {
            focus_set(state, new_focus, from_keyboard);
        }

        radiant_dispatch_focus_event(evcon, new_focus, "focus", prev_focus);
        dispatch_lambda_handler_without_js(evcon, new_focus, "focus");
        radiant_dispatch_focus_event(evcon, new_focus, "focusin", prev_focus);
        dispatch_lambda_handler_without_js(evcon, new_focus, "focusin");

        // F1 (Radiant_Design_Form_Input.md §3.1): snapshot the value at
        // focus time so a later blur can decide whether to fire `change`.
        if (new_focus->is_element()) {
            te_focus_capture_value(lam::dom_require_element(new_focus));
        }

        // CE-4 (Radiant_Design_Content_Editable.md §7): on focus of any
        // element carrying `inputmode` / `enterkeyhint`, read the hints so
        // the platform IME / on-screen keyboard backend can apply them.
        // Actual forwarding to NSTextInputClient / TSF / IBus is reserved
        // for editor3 §3.9 `RdTextInputClient`; for now we log so the focus
        // path is traceable in `log.txt` and tests can observe activation.
        if (new_focus->is_element()) {
            DomElement* focus_elem = lam::dom_require_element(new_focus);
            const char* im = focus_elem->get_attribute("inputmode");
            const char* ek = focus_elem->get_attribute("enterkeyhint");
            if (im || ek) {
                log_debug("CE-4 ime_hint_forward: target=%p inputmode='%s' enterkeyhint='%s'",
                          new_focus, im ? im : "", ek ? ek : "");
            }
        }

        log_debug("update_focus_state: set focus on %p (keyboard=%d, focus-visible=%d)",
                  new_focus, from_keyboard, from_keyboard);
    } else {
        if (prev_focus) {
            SmTransitionGuard sm_guard(state, SM_FAMILY_FOCUS,
                should_dispatch_change ? SM_EV_UI_BLUR_WITH_CHANGE :
                                         SM_EV_UI_BLUR_WITH_BLUR,
                prev_focus);
            focus_clear(state);
            if (should_dispatch_change) {
                dispatch_focus_change_observed(evcon, prev_focus);
            }
            dispatch_focus_blur_observed(evcon, prev_focus, nullptr);
            sm_guard.commit();
        } else {
            focus_clear(state);
        }

        log_debug("update_focus_state: cleared focus");
    }
}

/**
 * Update drag state
 */
void update_drag_state(EventContext* evcon, View* target, bool is_dragging) {
    DocState* state = event_context_target_state(evcon);
    if (!state) return;

    DragTransitionArgs drag_args = { .target = target, .dragging = is_dragging };
    drag_transition(state, DRAG_TRANSITION_SET_STATE, &drag_args);

    log_debug("update_drag_state: dragging=%d, target=%p", is_dragging, target);
}

// The legacy package-off target path only needs a named iframe lookup; parsing
// a one-use CSS selector made the fallback depend on tokenizer and matcher state.
static DomElement* find_iframe_by_name(DomNode* node, const char* target_name) {
    if (!node || !target_name) return nullptr;
    if (node->is_element()) {
        DomElement* elem = node->as_element();
        const char* name = elem->get_attribute("name");
        if (elem->tag() == MARKUP_NAME_IFRAME && name && strcmp(name, target_name) == 0) {
            return elem;
        }
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            DomElement* found = find_iframe_by_name(child, target_name);
            if (found) return found;
        }
    }
    return nullptr;
}

// find the sub-view that matches the given node
View* find_view(View* view, DomNode* node) {
    // Compare if the view's node matches the target node directly
    if (view == node) { return view; }

    if (view->is_group()) {
        ViewElement* group = lam::view_require_element(view);
        View* child = group->first_child;
        while (child) {
            View* found = find_view(child, node);
            if (found) { return found; }
            child = child->next_sibling;
        }
    }
    return NULL;
}

// The request is document-owned, not EventContext-owned: a behavior handler
// runs below the event dispatcher and must not retain a stack EventContext.
// Node references are pinned across that return boundary (D4.5.1v3).
typedef struct RadiantNavigationRequest {
    DomDocument* source_document;
    DomNodeRef source_ref;
    DomDocument* target_document;
    DomNodeRef target_ref;
    DomDocument* fragment_document;
    DomNodeRef fragment_ref;
    char* url;
    char* target_name;
    RadiantNavigationTargetKind target_kind;
    struct RadiantNavigationRequest* next;
} RadiantNavigationRequest;

typedef struct RadiantNavigationQueue {
    RadiantNavigationRequest* first;
    RadiantNavigationRequest* last;
} RadiantNavigationQueue;

static void navigation_request_destroy(RadiantNavigationRequest* request) {
    if (!request) return;
    if (request->source_document && request->source_ref.address) {
        dom_node_unpin(request->source_document, request->source_ref,
                       DOM_NODE_PIN_EVENT_QUEUE);
    }
    if (request->target_document && request->target_ref.address) {
        dom_node_unpin(request->target_document, request->target_ref,
                       DOM_NODE_PIN_EVENT_QUEUE);
    }
    if (request->fragment_document && request->fragment_ref.address) {
        dom_node_unpin(request->fragment_document, request->fragment_ref,
                       DOM_NODE_PIN_EVENT_QUEUE);
    }
    if (request->url) mem_free(request->url);
    if (request->target_name) mem_free(request->target_name);
    mem_free(request);
}

static void navigation_queue_destroy(void* data) {
    RadiantNavigationQueue* queue = (RadiantNavigationQueue*)data;
    if (!queue) return;
    RadiantNavigationRequest* request = queue->first;
    while (request) {
        RadiantNavigationRequest* next = request->next;
        navigation_request_destroy(request);
        request = next;
    }
    mem_free(queue);
}

static RadiantNavigationQueue* navigation_queue_for_document(DomDocument* doc,
                                                              bool create) {
    if (!doc) return nullptr;
    for (DomDocumentResource* resource = doc->resources; resource;
         resource = resource->next) {
        if (resource->destroy == navigation_queue_destroy) {
            return (RadiantNavigationQueue*)resource->data;
        }
    }
    if (!create) return nullptr;
    RadiantNavigationQueue* queue = (RadiantNavigationQueue*)mem_calloc(
        1, sizeof(RadiantNavigationQueue), MEM_CAT_LAYOUT);
    if (!queue || !dom_document_add_resource(doc, queue, navigation_queue_destroy)) {
        if (queue) mem_free(queue);
        return nullptr;
    }
    return queue;
}

static bool navigation_request_pin(DomDocument* document, DomElement* element,
                                   DomNodeRef* out_ref) {
    if (!document || !element || !out_ref || element->doc != document) return false;
    *out_ref = dom_node_ref((DomNode*)element);
    return dom_node_ref_validate(document, *out_ref) &&
           dom_node_pin(document, *out_ref, DOM_NODE_PIN_EVENT_QUEUE);
}

bool radiant_queue_navigation_request(DomElement* source, const char* url,
                                      DomElement* target,
                                      RadiantNavigationTargetKind target_kind,
                                      const char* target_name,
                                      DomElement* fragment_target) {
    if (!source || !source->doc || !url || !url[0]) return false;
    if ((target_kind == RADIANT_NAVIGATION_TARGET_EXISTING && !target) ||
        (target_kind == RADIANT_NAVIGATION_TARGET_NEW && target)) {
        return false;
    }

    RadiantNavigationRequest* request = (RadiantNavigationRequest*)mem_calloc(
        1, sizeof(RadiantNavigationRequest), MEM_CAT_LAYOUT);
    if (!request) return false;
    request->source_document = source->doc;
    request->target_kind = target_kind;
    request->url = mem_strdup(url, MEM_CAT_LAYOUT);
    if (target_name && target_name[0]) {
        request->target_name = mem_strdup(target_name, MEM_CAT_LAYOUT);
    }
    if (!request->url || (target_name && target_name[0] && !request->target_name) ||
        !navigation_request_pin(request->source_document, source,
                                &request->source_ref)) {
        navigation_request_destroy(request);
        return false;
    }
    if (target && !navigation_request_pin(target->doc, target, &request->target_ref)) {
        navigation_request_destroy(request);
        return false;
    }
    request->target_document = target ? target->doc : nullptr;
    if (fragment_target &&
        !navigation_request_pin(fragment_target->doc, fragment_target,
                                &request->fragment_ref)) {
        navigation_request_destroy(request);
        return false;
    }
    request->fragment_document = fragment_target ? fragment_target->doc : nullptr;

    RadiantNavigationQueue* queue = navigation_queue_for_document(
        request->source_document, true);
    if (!queue) {
        navigation_request_destroy(request);
        return false;
    }
    if (queue->last) queue->last->next = request;
    else queue->first = request;
    queue->last = request;
    return true;
}

static RadiantNavigationRequest* navigation_queue_take(DomDocument* doc) {
    RadiantNavigationQueue* queue = navigation_queue_for_document(doc, false);
    if (!queue || !queue->first) return nullptr;
    RadiantNavigationRequest* request = queue->first;
    queue->first = request->next;
    if (!queue->first) queue->last = nullptr;
    request->next = nullptr;
    return request;
}

static bool navigation_request_is_live(const RadiantNavigationRequest* request,
                                       DomElement** out_source,
                                       DomElement** out_target,
                                       DomElement** out_fragment) {
    if (out_source) *out_source = nullptr;
    if (out_target) *out_target = nullptr;
    if (out_fragment) *out_fragment = nullptr;
    if (!request) return false;
    DomNode* source = dom_node_ref_validate(request->source_document,
                                            request->source_ref);
    if (!source || !source->is_element()) return false;
    if (out_source) *out_source = source->as_element();
    if (request->target_kind == RADIANT_NAVIGATION_TARGET_EXISTING) {
        DomNode* target = dom_node_ref_validate(request->target_document,
                                                request->target_ref);
        if (!target || !target->is_element()) return false;
        if (out_target) *out_target = target->as_element();
    }
    if (request->fragment_ref.address) {
        DomNode* fragment = dom_node_ref_validate(request->fragment_document,
                                                  request->fragment_ref);
        if (!fragment || !fragment->is_element()) return false;
        if (out_fragment) *out_fragment = fragment->as_element();
    }
    return true;
}

static bool navigation_url_resolve(DomDocument* source, const char* raw,
                                   Url** out_url) {
    if (out_url) *out_url = nullptr;
    if (!source || !raw || !raw[0] || !out_url) return false;
    Url* resolved = source->url ? url_parse_with_base(raw, source->url)
                                : url_parse(raw);
    if (!resolved || !url_is_valid(resolved)) {
        if (resolved) url_destroy(resolved);
        return false;
    }
    *out_url = resolved;
    return true;
}

bool radiant_urls_match_without_fragment(const Url* first, const Url* second) {
    if (!first || !second) return false;
    const char* first_parts[] = {
        url_get_protocol(first), url_get_username(first), url_get_password(first),
        url_get_host(first), url_get_pathname(first), url_get_search(first),
    };
    const char* second_parts[] = {
        url_get_protocol(second), url_get_username(second), url_get_password(second),
        url_get_host(second), url_get_pathname(second), url_get_search(second),
    };
    for (size_t i = 0; i < sizeof(first_parts) / sizeof(first_parts[0]); i++) {
        if (strcmp(first_parts[i] ? first_parts[i] : "",
                   second_parts[i] ? second_parts[i] : "") != 0) {
            return false;
        }
    }
    return true;
}

static void navigation_clear_target_state(DomNode* node, DocState* state) {
    for (DomNode* current = node; current; current = current->next_sibling) {
        if (!current->is_element()) continue;
        DomElement* elem = current->as_element();
        if (state_get_bool(state, elem, STATE_TARGET)) {
            state_set_bool(state, elem, STATE_TARGET, false);
            radiant_sync_pseudo_state((View*)elem, PSEUDO_STATE_TARGET, false);
        }
        navigation_clear_target_state(elem->first_child, state);
    }
}

static bool navigation_apply_fragment(DomDocument* document,
                                      DomElement* fragment,
                                      const char* resolved_url) {
    if (!document || !document->root || !resolved_url) return false;
    RadiantHistoryTraversal traversal = {};
    radiant_history_set_location(document, resolved_url, &traversal);
    DocState* state = (DocState*)document->state;
    if (!state) return false;
    navigation_clear_target_state((DomNode*)document->root, state);
    if (fragment) {
        state_set_bool(state, fragment, STATE_TARGET, true);
        radiant_sync_pseudo_state((View*)fragment, PSEUDO_STATE_TARGET, true);
        js_dom_scroll_into_view_bridge(fragment);
    }
    doc_state_request_repaint(state);
    return true;
}

static bool navigation_execute_iframe_target(UiContext* uicon,
                                             DomElement* iframe,
                                             const char* url) {
    if (!uicon || !iframe || iframe->tag() != MARKUP_NAME_IFRAME ||
        !iframe->doc || !iframe->doc->view_tree || !url || !url[0]) {
        return false;
    }
    DomDocument* owner = iframe->doc;
    View* iframe_view = find_view(owner->view_tree->root, (DomNode*)iframe);
    if (!iframe_view || (iframe_view->view_type != RDT_VIEW_BLOCK &&
                         iframe_view->view_type != RDT_VIEW_INLINE_BLOCK)) {
        return false;
    }
    ViewBlock* block = lam::view_require_block(iframe_view);
    if (!block || !block->embed) return false;
    if (!iframe->set_attribute("src", url)) return false;
    if (block->scroller && block->scroll_mut()->pane) {
        block->scroll()->pane->reset();
        block->content_width = 0.0f;
        block->content_height = 0.0f;
    }

    int css_vw = (int)block->width; // INT_CAST_OK: loader viewport is integer CSS pixels.
    int css_vh = (int)block->height; // INT_CAST_OK: loader viewport is integer CSS pixels.
    DomDocument* old_doc = block->embedp()->doc;
    if (uicon->font_ctx) {
        // Glyph-cache keys retain raw document FontHandle addresses; clear
        // them before iframe navigation can free and reuse those addresses.
        font_context_reset_glyph_caches(uicon->font_ctx);
    }
    DomDocument* new_doc = load_html_doc(owner->url, (char*)url, css_vw, css_vh);
    if (!new_doc) return false;
    block->embed->doc = new_doc;
    dom_document_set_embedding(new_doc, owner, iframe);
    radiant_document_ensure_state(new_doc, "navigation_iframe_target");
    new_doc->viewport.output_scale = 1.0f;
    ui_context_sync_document_raster_scale(uicon, new_doc);

    if (new_doc->html_root) {
        DomDocument* saved_doc = uicon->document;
        float saved_viewport_width = uicon->viewport_width;
        float saved_viewport_height = uicon->viewport_height;
        uicon->document = new_doc;
        uicon->viewport_width = (float)css_vw;
        uicon->viewport_height = (float)css_vh;
        process_document_font_faces(uicon, new_doc);
        layout_html_doc(uicon, new_doc, false);
        uicon->document = saved_doc;
        uicon->viewport_width = saved_viewport_width;
        uicon->viewport_height = saved_viewport_height;
    }
    if (new_doc->view_tree && new_doc->view_tree->root) {
        ViewBlock* root = lam::view_require_block(new_doc->view_tree->root);
        if (root->scroller) {
            if (root->content_height > root->height) root->height = root->content_height;
            root->scroller = nullptr;
        }
        block->content_width = root->content_width > 0.0f ? root->content_width : root->width;
        block->content_height = root->content_height > 0.0f ? root->content_height : root->height;
        update_scroller(block, block->content_width, block->content_height);
    }
    clear_document_interaction_state_before_detach(old_doc);
    dom_document_clear_embedding(old_doc);
    free_document(old_doc);
    if (owner->state) doc_state_mark_dirty(owner->state);
    return true;
}

static bool navigation_execute_top_target(UiContext* uicon, DomDocument* document,
                                          const char* url) {
    if (!uicon || !document || document != uicon->document || !url || !url[0]) {
        return false;
    }
    int css_vw = (int)uicon->viewport_width; // INT_CAST_OK: loader viewport is integer CSS pixels.
    int css_vh = (int)uicon->viewport_height; // INT_CAST_OK: loader viewport is integer CSS pixels.
    BrowsingSession* session = uicon->browsing_session;
    DomDocument* new_doc = nullptr;
    if (session) {
        ViewBlock* root_block = document->view_tree
            ? lam::view_require_block(document->view_tree->root) : nullptr;
        DocState* state = (DocState*)document->state;
        if (root_block && root_block->scroller && root_block->scroll_mut()->pane) {
            float scroll_y = 0.0f;
            scroll_state_get_position_for_view(state, static_cast<View*>(root_block),
                                               root_block->scroll()->pane,
                                               nullptr, &scroll_y, nullptr, nullptr);
            session_save_scroll_position(session, scroll_y);
        }
        log_info("navigation-exec: navigating via session to %s", url);
        new_doc = session_navigate(session, uicon, url, css_vw, css_vh);
    } else {
        log_info("navigation-exec: navigating directly to %s", url);
        new_doc = show_html_doc(document->url, (char*)url, css_vw, css_vh);
        free_document(document);
    }
    if (!new_doc) return false;
    const char* page_title = session ? session_current_title(session) : nullptr;
    if (!page_title) page_title = session_extract_title(new_doc);
    if (page_title) {
        char title_buf[512];
        snprintf(title_buf, sizeof(title_buf), "Lambda - %s", page_title);
        update_window_title(title_buf);
    }
    return true;
}

bool radiant_execute_pending_navigation(UiContext* uicon, DomDocument* source) {
    RadiantNavigationRequest* request = navigation_queue_take(source);
    if (!request) return false;

    DomElement* source_elem = nullptr;
    DomElement* target_elem = nullptr;
    DomElement* fragment_elem = nullptr;
    bool live = navigation_request_is_live(request, &source_elem, &target_elem,
                                           &fragment_elem);
    Url* resolved = nullptr;
    bool resolved_ok = live && navigation_url_resolve(source_elem->doc, request->url,
                                                       &resolved);
    const char* href = resolved ? url_get_href(resolved) : nullptr;
    char* owned_href = href ? mem_strdup(href, MEM_CAT_LAYOUT) : nullptr;
    RadiantNavigationTargetKind target_kind = request->target_kind;
    DomDocument* target_document = target_elem ? target_elem->doc : nullptr;
    bool target_is_iframe = target_elem && target_elem->tag() == MARKUP_NAME_IFRAME;
    bool target_is_root = target_elem && target_document &&
                          target_elem == target_document->root;
    DomDocument* destination_document = target_is_iframe && target_elem->embed
        ? target_elem->embedp()->doc : target_document;
    bool fragment_is_target_document = !fragment_elem ||
        (destination_document == fragment_elem->doc);
    const char* hash = resolved ? url_get_hash(resolved) : nullptr;
    bool fragment_destination = destination_document && destination_document->url &&
        hash && hash[0] == '#' && radiant_urls_match_without_fragment(
            destination_document->url, resolved);
    navigation_request_destroy(request);
    if (!resolved_ok || !owned_href) {
        if (resolved) url_destroy(resolved);
        if (owned_href) mem_free(owned_href);
        return false;
    }
    if (resolved) url_destroy(resolved);

    bool executed = false;
    if (target_kind == RADIANT_NAVIGATION_TARGET_NEW) {
        // A named/new browsing context needs a host-owned window factory. No
        // current UiContext exposes one, so do not degrade it to _self.
        log_warn("navigation-exec: new browsing context is not available");
    } else if (!target_elem || (!target_is_iframe && !target_is_root) ||
               !fragment_is_target_document) {
        log_warn("navigation-exec: package supplied an invalid resolved target");
    } else if (fragment_destination) {
        executed = navigation_apply_fragment(destination_document, fragment_elem,
                                             owned_href);
    } else if (target_is_iframe) {
        executed = navigation_execute_iframe_target(uicon, target_elem, owned_href);
    } else if (target_document == uicon->document) {
        executed = navigation_execute_top_target(uicon, target_document, owned_href);
    } else {
        DomElement* iframe = dom_document_embedding_element(target_document);
        executed = navigation_execute_iframe_target(uicon, iframe, owned_href);
    }
    mem_free(owned_href);
    if (executed) to_repaint();
    return executed;
}

/**
 * Calculate absolute window position from view-relative coordinates.
 * Walks up the parent chain accumulating block positions.
 * @param view The view whose coordinate system the position is relative to
 * @param rel_x X coordinate relative to view's parent block
 * @param rel_y Y coordinate relative to view's parent block
 * @param iframe_offset_x Additional X offset for iframe content
 * @param iframe_offset_y Additional Y offset for iframe content
 * @param out_abs_x Output: absolute X in window coordinates
 * @param out_abs_y Output: absolute Y in window coordinates
 */
void view_to_absolute_position(View* view, float rel_x, float rel_y,
    float iframe_offset_x, float iframe_offset_y,
    float* out_abs_x, float* out_abs_y) {

    float abs_x = rel_x;
    float abs_y = rel_y;

    // Walk up from view's parent to accumulate block positions
    View* parent = view->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK ||
            parent->view_type == RDT_VIEW_INLINE_BLOCK ||
            parent->view_type == RDT_VIEW_LIST_ITEM) {
            abs_x += (lam::view_require_block(parent))->x;
            abs_y += (lam::view_require_block(parent))->y;
        }
        parent = parent->parent;
    }

    // Add iframe offset
    abs_x += iframe_offset_x;
    abs_y += iframe_offset_y;

    *out_abs_x = abs_x;
    *out_abs_y = abs_y;
}

struct EventTextRun {
    unsigned char* end;
    int visible_end_offset;
    float pdf_width;
    bool pdf_copy_space;
    bool is_pdf;
};

static EventTextRun event_text_run(ViewText* text, TextRect* rect) {
    EventTextRun run = {0};
    run.is_pdf = pdf_text_run_metrics(text, &run.pdf_width, &run.pdf_copy_space);
    run.visible_end_offset = pdf_visible_end_offset(text, rect, run.pdf_copy_space);
    int end_offset = run.is_pdf
        ? run.visible_end_offset : rect->start_index + max(rect->length, 0);
    run.end = text->text_data() + end_offset;
    return run;
}

static bool event_text_glyph_advance(FontBox* font, unsigned char* p, unsigned char* end,
                                     bool* has_space, int* out_bytes, float* out_advance) {
    *out_bytes = 1;
    *out_advance = 0.0f;
    if (is_space(*p)) {
        if (*has_space) return false;
        *has_space = true;
        *out_advance = font->style->space_width;
        return true;
    }

    *has_space = false;
    uint32_t codepoint;
    *out_bytes = str_utf8_decode((const char*)p, (size_t)(end - p), &codepoint);
    if (*out_bytes <= 0) {
        *out_bytes = 1;
        codepoint = *p;
    }
    GlyphInfo glyph = font_get_glyph(font_box_handle(font), codepoint);
    if (glyph.id == 0) return false;
    *out_advance = glyph.advance_x;
    return true;
}

/**
 * Calculate character offset from mouse click position within a text rect
 * Returns the byte offset closest to the click position, aligned to UTF-8 character boundaries
 */
int calculate_char_offset_from_position(EventContext* evcon, ViewText* text,
    TextRect* rect, float mouse_x, float mouse_y) {
    unsigned char* str = text->text_data();
    float x = evcon->block.x + rect->x;

    unsigned char* p = str + rect->start_index;
    EventTextRun run = event_text_run(text, rect);
    unsigned char* end = run.end;
    int byte_offset = rect->start_index;  // track byte offset for return value

    float raster_scale = ui_context_raster_scale(evcon->ui_context);

    // Get letter-spacing and word-spacing from font style (same as used in layout)
    float letter_spacing = evcon->font.style ? evcon->font.style->letter_spacing : 0.0f;
    float word_spacing = evcon->font.style ? evcon->font.style->word_spacing : 0.0f;

    bool has_space = false;

    if (run.is_pdf && run.pdf_width > 0.0f) {
        float visible_width = pdf_text_run_visible_natural_width(evcon, rect, run.pdf_copy_space);
        if (visible_width > 0.0f) {
            float local_pdf_x = mouse_x - x;
            if (local_pdf_x <= 0.0f) return rect->start_index;
            if (local_pdf_x >= run.pdf_width) return run.visible_end_offset;
            mouse_x = x + local_pdf_x * visible_width / run.pdf_width;
        }
    }

    log_debug("calculate_char_offset: mouse_x=%.1f, start x=%.1f, rect.width=%.1f, rect.length=%d, block.x=%.1f, rect.x=%.1f",
              mouse_x, x, rect->width, rect->length, evcon->block.x, rect->x);

    // Skip leading collapsed whitespace (spaces, tabs, newlines at the start)
    // These characters don't contribute to visual width but are part of the text
    while (p < end && (is_space(*p) || *p == '\n' || *p == '\r' || *p == '\t')) {
        p++;
        byte_offset++;
    }

    while (p < end) {
        float wd = 0;
        int bytes = 1;  // number of bytes for current character

        // Skip newlines and carriage returns - they don't have visual width
        if (*p == '\n' || *p == '\r') {
            // At end of visual content - treat rest as trailing whitespace
            break;
        }
        if (is_space(*p)) {
            if (has_space) {
                // Consecutive spaces are collapsed - skip without adding width
                p++;
                byte_offset++;
                continue;
            }
            has_space = true;
            wd = evcon->font.style->space_width + word_spacing;
            bytes = 1;  // spaces are always single byte
        } else {
            has_space = false;
            // Decode UTF-8 codepoint to handle multi-byte characters
            uint32_t codepoint;
            bytes = str_utf8_decode((const char*)p, (size_t)(end - p), &codepoint);
            if (bytes <= 0) {
                // Invalid UTF-8 sequence, skip single byte
                bytes = 1;
                codepoint = *p;
            }
            // Use font_load_glyph to match layout calculation
            FontStyleDesc _sd = font_style_desc_from_prop(evcon->font.style);
            LoadedGlyph* glyph = font_load_glyph(font_box_handle(&evcon->font), &_sd, codepoint, false);
            if (!glyph) {
                log_error("Could not load codepoint U+%04X", codepoint);
                p += bytes;
                byte_offset += bytes;
                continue;
            }
            wd = glyph->advance_x / raster_scale;
        }

        // Add letter-spacing (applied after each character except the last)
        unsigned char* next_p = p + bytes;
        if (next_p < end && *next_p != '\n' && *next_p != '\r') {
            wd += letter_spacing;
        }

        float char_mid = x + wd / 2.0f;

        // If mouse is before the midpoint of this character, return current byte offset
        // (caret should be placed before this character)
        if (mouse_x < char_mid) {
            log_debug("calculate_char_offset: matched at byte_offset %d", byte_offset);
            return byte_offset;
        }

        x += wd;
        p += bytes;
        byte_offset += bytes;
    }

    log_debug("calculate_char_offset: end of text, returning byte_offset=%d", byte_offset);
    // Mouse is after all characters - return end offset
    return byte_offset;
}

/**
 * Calculate visual position (x, y, height) from byte offset within a text rect
 * The target_offset is a byte offset aligned to UTF-8 character boundaries
 * Returns the x position relative to the text rect's origin
 */
void calculate_position_from_char_offset(EventContext* evcon, ViewText* text,
    TextRect* rect, int target_offset, float* out_x, float* out_y, float* out_height) {

    unsigned char* str = text->text_data();
    float x = rect->x;  // relative to block
    float y = rect->y;

    unsigned char* p = str + rect->start_index;
    EventTextRun run = event_text_run(text, rect);
    unsigned char* end = run.end;
    int byte_offset = rect->start_index;  // track byte offset
    float raster_scale = ui_context_raster_scale(evcon->ui_context);
    bool has_space = false;

    // Debug: log initial state
    log_debug("[CALC-POS] target_offset=%d, rect->x=%.1f, rect->start_index=%d, raster_scale=%.1f, y_ppem=%d",
        target_offset, rect->x, rect->start_index, raster_scale,
        font_box_handle(&evcon->font) ? (int)font_handle_get_physical_size_px(font_box_handle(&evcon->font)) : -1);

    while (p < end && byte_offset < target_offset) {
        float wd = 0;
        int bytes;
        bool has_advance = event_text_glyph_advance(
            &evcon->font, p, end, &has_space, &bytes, &wd);
        if (!has_advance) { p += bytes; byte_offset += bytes; continue; }
        x += wd;
        p += bytes;
        byte_offset += bytes;
    }
    if (run.is_pdf && run.pdf_width > 0.0f) {
        float visible_width = pdf_text_run_visible_natural_width(evcon, rect, run.pdf_copy_space);
        if (visible_width > 0.0f) {
            if (target_offset >= run.visible_end_offset) {
                x = rect->x + run.pdf_width;
            } else {
                x = rect->x + (x - rect->x) * run.pdf_width / visible_width;
            }
        }
    }
    log_debug("[CALC-POS] final x=%.1f for target_offset=%d", x, target_offset);

    *out_x = x;
    *out_y = y;
    *out_height = rect->height;  // use rect height as caret height
}

static void event_text_caret_rect(EventContext* evcon, ViewText* text,
                                  TextRect* fallback_rect, int char_offset,
                                  float* x, float* y, float* height) {
    EditingCaretRect caret_rect;
    if (editing_geometry_dom_text_caret_rect(
            evcon->ui_context, text,
            char_offset < 0 ? 0 : (uint32_t)char_offset, &caret_rect)) {
        *x = caret_rect.x;
        *y = caret_rect.y;
        *height = caret_rect.height;
        return;
    }
    calculate_position_from_char_offset(
        evcon, text, fallback_rect, char_offset, x, y, height);
}

// Glyph-precise X resolver registered with the dom_range resolver so that
// `dom_range_for_each_rect()` (used to paint selection rectangles) computes
// rect-relative x using the SAME glyph walk as the caret painter
// (`calculate_position_from_char_offset`). Without this, the resolver falls
// back to linear interpolation and the right edge of the selection ends up
// off by ~1 character width from where the caret is drawn (since real fonts
// are proportional, not monospaced).
static float event_glyph_x_resolver(UiContext* uicon, ViewText* text,
                                    TextRect* rect, int byte_offset) {
    if (!text || !rect) return rect ? rect->x : 0.0f;
    if (byte_offset <= rect->start_index) return rect->x;
    if (rect->length <= 0) return rect->x;
    EventTextRun run = event_text_run(text, rect);
    int rect_end_offset = run.is_pdf ? run.visible_end_offset : rect->start_index + rect->length;
    if (byte_offset >= rect_end_offset) {
        return rect->x + (run.is_pdf ? run.pdf_width : rect->width);
    }

    // Mirror calculate_position_from_char_offset, but build a temporary
    // FontBox from text->font (no EventContext available here).
    FontBox fbox;
    memset(&fbox, 0, sizeof(fbox));
    if (text->font) setup_font(uicon, &fbox, text->font);
    if (!font_box_handle(&fbox) || !fbox.style) return rect->x;

    unsigned char* str = text->text_data();
    unsigned char* p = str + rect->start_index;
    unsigned char* end = str + rect_end_offset;
    int byte_off = rect->start_index;
    float x = rect->x;
    bool has_space = false;

    while (p < end && byte_off < byte_offset) {
        float wd = 0;
        int bytes;
        bool has_advance = event_text_glyph_advance(
            &fbox, p, end, &has_space, &bytes, &wd);
        if (!has_advance) { p += bytes; byte_off += bytes; continue; }
        x += wd;
        p += bytes;
        byte_off += bytes;
    }
    if (run.is_pdf && run.pdf_width > 0.0f) {
        float visible_width = pdf_text_run_visible_natural_width(&fbox, rect, run.pdf_copy_space);
        if (visible_width > 0.0f) {
            if (byte_offset >= run.visible_end_offset) {
                return rect->x + run.pdf_width;
            }
            return rect->x + (x - rect->x) * run.pdf_width / visible_width;
        }
    }
    return x;
}

// Static registration: hooks the resolver into dom_range_resolver.cpp at
// program start so selection painting always uses glyph-precise widths.
__attribute__((constructor))
static void register_event_glyph_x_resolver() { // UNUSED_FUNCTION_OK: process constructor installs the DOM range resolver
    dom_range_set_glyph_x_resolver(event_glyph_x_resolver);
}

// Inverse resolver: given a rect-relative target X, return the byte offset
// in `rect` whose visual X is closest. Used by Up/Down arrow vertical
// caret navigation so the caret lands at the same visual column on the
// new line. Mirrors the glyph walk used elsewhere.
static int event_byte_offset_for_x_resolver(UiContext* uicon, ViewText* text,
                                            TextRect* rect, float target_local_x) {
    if (!text || !rect) return rect ? rect->start_index : 0;
    if (rect->length <= 0) return rect->start_index;
    if (target_local_x <= rect->x) return rect->start_index;
    EventTextRun run = event_text_run(text, rect);
    float target_x = target_local_x;

    if (run.is_pdf && run.pdf_width > 0.0f) {
        if (target_x >= rect->x + run.pdf_width) return run.visible_end_offset;
    } else if (target_x >= rect->x + rect->width) {
        return rect->start_index + rect->length;
    }

    FontBox fbox;
    memset(&fbox, 0, sizeof(fbox));
    if (text->font) setup_font(uicon, &fbox, text->font);
    if (!font_box_handle(&fbox) || !fbox.style) return rect->start_index;

    unsigned char* str = text->text_data();
    unsigned char* p = str + rect->start_index;
    unsigned char* end = run.end;
    int byte_off = rect->start_index;
    float x = rect->x;
    bool has_space = false;

    if (run.is_pdf && run.pdf_width > 0.0f) {
        float visible_width = pdf_text_run_visible_natural_width(&fbox, rect, run.pdf_copy_space);
        if (visible_width > 0.0f) {
            target_x = rect->x + (target_x - rect->x) * visible_width / run.pdf_width;
        }
    }

    while (p < end) {
        float wd = 0;
        int bytes;
        bool has_advance = event_text_glyph_advance(
            &fbox, p, end, &has_space, &bytes, &wd);
        if (!has_advance) { p += bytes; byte_off += bytes; continue; }
        // Caret goes BEFORE this glyph if target_local_x is left of the midpoint.
        if (target_x < x + wd / 2.0f) return byte_off;
        x += wd;
        p += bytes;
        byte_off += bytes;
    }
    return run.is_pdf ? run.visible_end_offset : rect->start_index + rect->length;
}

__attribute__((constructor))
static void register_event_byte_offset_for_x_resolver() { // UNUSED_FUNCTION_OK: process constructor installs the DOM range resolver
    dom_range_set_byte_offset_for_x_resolver(event_byte_offset_for_x_resolver);
}

/**
 * Find the TextRect containing a given character offset
 * Returns the TextRect that contains the offset, or the last rect if offset is beyond all rects
 */
TextRect* find_text_rect_for_offset(ViewText* text, int char_offset) {
    if (!text || !text->rect) return nullptr;

    TextRect* rect = text->rect;
    TextRect* prev_rect = rect;

    while (rect) {
        int rect_start = rect->start_index;
        int rect_end = rect->start_index + rect->length;

        // Check if offset is within this rect
        if (char_offset >= rect_start && char_offset <= rect_end) {
            return rect;
        }

        prev_rect = rect;
        rect = rect->next;
    }

    // If offset is beyond all rects, return the last one
    return prev_rect;
}

static bool text_point_inside_existing_selection(DocState* state, View* view, int char_offset) {
    if (!state || !state->dom_selection || !view || view->view_type != RDT_VIEW_TEXT) return false;
    DomSelection* selection = state->dom_selection;
    if (selection->range_count == 0 || dom_selection_is_collapsed(selection) || !selection->ranges[0]) return false;

    DomText* text = lam::dom_require_text(view);
    uint32_t offset = char_offset < 0 ? 0 : dom_text_utf8_to_utf16(text, (uint32_t)char_offset);
    DomNode* node = static_cast<DomNode*>(text);
    if (dom_range_is_point_in_range(selection->ranges[0], node, offset)) return true;

    uint32_t boundary_len = dom_node_boundary_length(node);
    if (offset < boundary_len && dom_range_is_point_in_range(selection->ranges[0], node, offset + 1)) return true;
    if (offset > 0 && dom_range_is_point_in_range(selection->ranges[0], node, offset - 1)) return true;

    SourcePosC click_pos = {};
    SourcePosC start_pos = {};
    SourcePosC end_pos = {};
    DomBoundary click_boundary = { node, offset };
    bool source_inside = false;
    if (source_pos_from_dom_boundary(&click_boundary, &click_pos) &&
        source_pos_from_dom_range(selection->ranges[0], &start_pos, &end_pos) &&
        click_pos.kind == SOURCE_POS_TEXT && start_pos.kind == SOURCE_POS_TEXT && end_pos.kind == SOURCE_POS_TEXT &&
        source_path_equal(&click_pos.path, &start_pos.path) &&
        source_path_equal(&click_pos.path, &end_pos.path)) {
        uint32_t sel_start = start_pos.offset <= end_pos.offset ? start_pos.offset : end_pos.offset;
        uint32_t sel_end = start_pos.offset <= end_pos.offset ? end_pos.offset : start_pos.offset;
        source_inside = click_pos.offset + 1 >= sel_start && click_pos.offset <= sel_end + 1;
    }
    source_pos_free(&click_pos);
    source_pos_free(&start_pos);
    source_pos_free(&end_pos);
    if (source_inside) return true;
    return false;
}

/**
 * Update caret visual position after movement operations
 * Must be called after caret_move, caret_move_line, caret_move_to
 * Handles text views, images, and other navigable views
 */
void update_caret_visual_position(UiContext* uicon, DocState* state) {
    View* view = NULL;
    int caret_offset = 0;
    if (!uicon || !caret_get_position(state, &view, &caret_offset)) return;

    float caret_x = 0, caret_y = 0, caret_height = 16;

    // Handle different view types
    if (view->is_text()) {
        ViewText* text = lam::view_require_text(view);
        if (!text->rect) {
            log_debug("[CARET-VISUAL] Text view has no rect");
            return;
        }

        // Find the TextRect containing the current offset
        TextRect* rect = find_text_rect_for_offset(text, caret_offset);
        if (!rect) {
            log_debug("[CARET-VISUAL] Could not find rect for offset %d", caret_offset);
            return;
        }

        EditingCaretRect caret_rect;
        if (editing_geometry_dom_text_caret_rect(uicon, text,
                caret_offset < 0 ? 0 : (uint32_t)caret_offset,
                &caret_rect)) {
            caret_x = caret_rect.x;
            caret_y = caret_rect.y;
            caret_height = caret_rect.height;
        } else {
            // Setup event context for legacy fallback font access.
            EventContext evcon;
            memset(&evcon, 0, sizeof(EventContext));
            evcon.ui_context = uicon;

            if (text->font) {
                setup_font(uicon, &evcon.font, text->font);
            } else {
                DomDocument* doc = uicon->document;
                if (doc && doc->view_tree) {
                    FontProp* default_font = doc->view_tree->html_version == HTML5
                        ? &uicon->default_font : &uicon->legacy_default_font;
                    setup_font(uicon, &evcon.font, default_font);
                }
            }

            calculate_position_from_char_offset(&evcon, text, rect, caret_offset,
                &caret_x, &caret_y, &caret_height);
        }

    } else if (view->view_type == RDT_VIEW_MARKER) {
        // For markers: caret is at left edge (offset 0) or right edge (offset 1)
        ViewMarker* marker = lam::view_require<RDT_VIEW_MARKER>(view);
        MarkerProp* marker_prop = marker && marker->blk ? (MarkerProp*)marker->blk : nullptr;
        float marker_width = marker_prop ? marker_prop->width : view->width;
        float marker_height = marker_prop ? marker_prop->height : view->height;
        if (caret_offset == 0) {
            caret_x = view->x;
        } else {
            caret_x = view->x + marker_width;
        }
        caret_y = view->y;
        caret_height = marker_height;
        log_debug("[CARET-VISUAL] Marker view: x=%.1f y=%.1f height=%.1f",
            caret_x, caret_y, caret_height);

    } else {
        // Unsupported view type
        log_debug("[CARET-VISUAL] Unsupported view type %d", view->view_type);
        return;
    }

    caret_project_visual(state, caret_x, caret_y, caret_height);

    // Preserve the existing iframe offset - it was correctly calculated when
    // the caret was initially placed via mouse click. During keyboard navigation,
    // we stay in the same iframe context, so the offset remains valid.
    // Note: chain_x/chain_y calculation above is for debugging only

    float iframe_offset_x = 0, iframe_offset_y = 0;
    caret_get_visual_snapshot(state, NULL, NULL, NULL, &iframe_offset_x, &iframe_offset_y);
    log_debug("[CARET-VISUAL] Updated caret: view_type=%d offset=%d x=%.1f y=%.1f height=%.1f iframe_offset=(%.1f,%.1f)",
        view->view_type, caret_offset, caret_x, caret_y, caret_height,
        iframe_offset_x, iframe_offset_y);
}

// ============================================================================
// Main Event Handler
// ============================================================================

// Native input delivery starts after the loader restored its caller context.
// Keep the complete event turn inside the target document owner so template
// lookup, DOM wrappers, and nested JS dispatch cannot borrow a null or
// unrelated runtime capsule.
// EO5v2: make `target` the thread's bound EvalContext, releasing whatever is
// bound now. A document — including an iframe's — manages its own Runtime and
// EvalContext, and a thread holds one at a time, so the binding moves between
// documents here.
//
// Legal only at a quiescent point: no handler, nested dispatch or guest code
// may be running, because the outgoing context's frames would still be live.
// The side-stack roots the GC walks are per-Context, so an unbound context
// keeps its own roots; what must not happen is a switch while frames are open.
// This is not the replace-and-restore that `runtime-state.h` forbids: there is
// no restore, the new owner simply holds the thread until the next boundary.
extern "C" bool radiant_eval_context_switch(EvalContext* target) {
    if (!target) return false;
    if (context == target) return true;
    if (context) {
        EvalContext* prev = context;
        // EO2: the JS cache is released with the binding it derives from
        if (js_runtime_state_thread_matches(prev)) js_runtime_state_shutdown(prev);
        if (!eval_context_shutdown(prev)) {
            log_error("eval-switch: outgoing context refused release");
            return false;
        }
    }
    if (!eval_context_init(target)) {
        log_error("eval-switch: incoming context refused binding");
        return false;
    }
    return true;
}

// Depth of active event scopes on this thread. A switch is only taken at the
// outermost one; a nested dispatch runs on whatever is already bound.
static __thread int s_event_scope_depth = 0;

struct EventDocumentScope {
    bool active;

    EventDocumentScope(UiContext* uicon, DomDocument* doc)
        : active(false) {
        Runtime* runtime = dom_document_script_runtime(doc);
        if (!runtime) return;
        EvalContext* owner = runtime_get_eval_context(runtime);
        if (!owner || !runtime->heap || !runtime->name_pool) return;
        owner->heap = runtime->heap;
        owner->name_pool = runtime->name_pool;
        owner->type_list = runtime->type_list;
        owner->pool = runtime->heap->pool;
        // EO5v2: at the outermost dispatch this may hand the thread from
        // another document's evaluator to this one. Nested dispatch never
        // switches — the outer document's frames are still live.
        if (context && context != owner) {
            if (s_event_scope_depth > 0) return;
            if (!radiant_eval_context_switch(owner)) return;
        } else if (!eval_context_init(owner)) {
            return;
        }
        // Lambda templates initialize a JS support capsule for Jube helpers,
        // but it is not a DOM script realm. Publish the retained JavaScript
        // runtime only when the script runner actually established a realm; the
        // shared runtime must still be the one JS bound to.
        bool has_document_js_runtime = dom_document_has_js_realm(doc) &&
            runtime == doc->js.runtime;
        if (has_document_js_runtime) {
            if (!js_runtime_state_init(owner)) return;
            js_dom_set_ui_context(uicon);
            js_dom_set_document(doc);
        }
        s_event_scope_depth++;
        active = true;
    }

    ~EventDocumentScope() {
        if (active) s_event_scope_depth--;
        // No restore: the document that just ran keeps the thread until some
        // other document's dispatch switches it away (EO5v2).
    }
};

void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event) {
    EventContext evcon;
    log_debug("HANDLE_EVENT: type=%d", event->type);
    log_debug("Handling event %d", event->type);
    // PDF documents don't have html_root - they only have view_tree
    // For PDFs, we can still handle basic events using the view_tree
    if (!doc) {
        log_error("No document to handle event");
        return;
    }
    // Controls that initialized during the last layout get their `init` turn
    // here, before this event is processed: layout has finished, so the state
    // they write lands in a quiescent pass rather than inside one. This is also
    // what covers a headless event run that never paints (ES19).
    radiant_run_behavior_init(doc);
    if (!doc->html_root && !doc->view_tree) {
        log_error("No document content to handle event");
        return;
    }
    // For PDF documents (no html_root), skip complex event handling for now
    // PDF is a static document format, so we only need basic scrolling/navigation
    if (!doc->html_root) {
        log_debug("PDF document - skipping DOM event handling");
        return;
    }
    EventDocumentScope document_scope(uicon, doc);
    event_context_init(&evcon, uicon, event);
    DocState* cascade_state = (DocState*)doc->state;
    EventStateLog* cascade_log = cascade_state && cascade_state->active_event_log
        ? cascade_state->active_event_log : evcon.ui_context->event_log;
    uint64_t cascade_id = state_begin_event_cascade(cascade_state, cascade_log, "input");
    event_log_raw_input(cascade_log, cascade_id, event);

    // ------------------------------------------------------------------
    // Phase 6 (single source of truth): view/offset selection helpers route
    // through DomSelection plus state-machine transitions. Event code may
    // compute glyph-precise visual geometry, but new rich DOM mutations should
    // use canonical StateStore selection boundaries or editing operations.
    // ------------------------------------------------------------------

    // find target view based on mouse position
    float mouse_x = 0.0f, mouse_y = 0.0f;
    switch (event->type) {
    case RDT_EVENT_MOUSE_MOVE: {
        MousePositionEvent* motion = &event->mouse_position;
        log_debug("Mouse event at (%.1f, %.1f)", motion->x, motion->y);
        mouse_x = motion->x;  mouse_y = motion->y;
        target_html_doc(&evcon, doc->view_tree);
        event_log_hit_target(cascade_log, cascade_id, &evcon);

        // Update hover state based on new target
        update_hover_state(&evcon, evcon.target);

        // Update dropdown hover if open
        update_dropdown_hover(&evcon, mouse_x, mouse_y);

        if (evcon.target) {
            log_debug("Target view found at position (%.1f, %.1f)", mouse_x, mouse_y);
            int buttons = uicon->mouse_state.is_mouse_down ? 1 : 0;
            // Native mouse input has a compatibility PointerEvent stream. JS
            // drag libraries select that stream when PointerEvent exists, so
            // omitting it made real mouse motion invisible after pointerdown.
            radiant_dispatch_pointer_event(&evcon, evcon.target,
                "pointermove", mouse_x, mouse_y, 0, buttons,
                false, false, false, false, "mouse");
            radiant_dispatch_mouse_event(&evcon, evcon.target,
                "mousemove", mouse_x, mouse_y, 0, buttons,
                false, false, false, false, 0);
            dispatch_lambda_handler(&evcon, evcon.target, "mousemove");
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            log_debug("No target view found at position (%.1f, %.1f)", mouse_x, mouse_y);
        }

        // fire drag event if dragging in progress
        DocState* state = event_context_target_state(&evcon);

        // Handle element drag-and-drop
        if (state && state->drag_drop && (state->drag_drop->pending || state->drag_drop->active)) {
            DragDropState* dd = state->drag_drop;
            DragTransitionArgs motion_args = { .x = (float)motion->x, .y = (float)motion->y };
            drag_transition(state, DRAG_TRANSITION_UPDATE_DROP_MOTION, &motion_args);

            if (dd->pending && !dd->active) {
                // check movement threshold in logical pixels
                float dx = dd->current_x - dd->start_x;
                float dy = dd->current_y - dd->start_y;
                if (dx * dx + dy * dy > 25.0f) {
                    DragTransitionArgs active_args = { .active = true };
                    drag_transition(state, DRAG_TRANSITION_SET_DROP_ACTIVE, &active_args);
                    log_debug("DRAG START: source=%p distance=%.1f", dd->source_view, sqrtf(dx*dx + dy*dy));
                    // dispatch "dragstart" to source element
                    dispatch_lambda_handler(&evcon, dd->source_view, "dragstart");
                    // Stage 4C: also fire a real JS DragEvent so script editors
                    // (addEventListener) get a DataTransfer. The session is
                    // opened inside the dispatch (needs the JS ctx) on "dragstart".
                    // Coord space matches the JS mouse events. The native
                    // DragDropState (source_view is a DOM element; active/pending
                    // flags) now survives handler-driven DOM mutation via
                    // fallback retention, so JS DnD rides on it directly.
                    radiant_dispatch_drag_event(&evcon, dd->source_view, "dragstart",
                                                dd->current_x, dd->current_y);
                }
            }

            if (dd->active) {
                // find drop target: walk up from hit-test target to find element with dropzone attr
                View* new_drop_target = nullptr;
                if (evcon.target) {
                    DomNode* node = static_cast<DomNode*>(evcon.target);
                    while (node) {
                        if (node->node_type == DOM_NODE_ELEMENT) {
                            DomElement* elem = lam::dom_require_element(node);
                            const char* dropzone = elem->get_attribute("dropzone");
                            if (dropzone && *dropzone) {
                                new_drop_target = static_cast<View*>(elem);
                                break;
                            }
                        }
                        node = node->parent;
                    }
                }

                // ES21: for a text drag the drop target is the text control
                // under the cursor, with no `dropzone` opt-in — that attribute
                // gates the element-DnD system, which this is not part of.
                if (!new_drop_target && dd->has_source_range && evcon.target) {
                    DomElement* text_drop_elem = nullptr;
                    uint32_t text_drop_offset = 0;
                    if (radiant_text_drop_target_at(&evcon, state, evcon.target,
                                                    (float)motion->x, (float)motion->y,
                                                    &text_drop_elem, &text_drop_offset)) {
                        new_drop_target = static_cast<View*>(text_drop_elem);
                    }
                }

                bool has_drop_range = false;
                DomBoundary drop_start = {};
                DomBoundary drop_end = {};
                if (new_drop_target) {
                    EditingSurface drop_surface;
                    EditingBoundary hit_boundary;
                    DomDocument* drag_doc = event_context_target_document(&evcon);
                    bool has_surface = editing_surface_from_target(new_drop_target,
                        &drop_surface) && editing_surface_is_rich(&drop_surface);
                    bool has_boundary = has_surface && drag_doc &&
                        drag_doc->view_tree && drag_doc->view_tree->root &&
                        editing_geometry_hit_test_boundary(evcon.ui_context,
                            static_cast<View*>(drag_doc->view_tree->root),
                            &drop_surface, (float)motion->x, (float)motion->y,
                            EDITING_CLAMP_SKIP_TEXT_CONTROLS, &hit_boundary);
                    if (has_boundary && hit_boundary.dom.node) {
                        drop_start = hit_boundary.dom;
                        drop_end = hit_boundary.dom;
                        has_drop_range = true;
                    }
                }

                // dispatch dragover/dragleave on drop target changes
                bool drop_target_changed = new_drop_target != dd->drop_target;
                if (drop_target_changed && dd->drop_target) {
                    dispatch_lambda_handler(&evcon, dd->drop_target, "dragleave");
                }
                DragTransitionArgs target_args = {};
                target_args.drop_target = new_drop_target;
                target_args.has_drop_range = has_drop_range;
                target_args.drop_start = drop_start;
                target_args.drop_end = drop_end;
                drag_transition(state, DRAG_TRANSITION_SET_DROP_TARGET,
                                &target_args);
                if (drop_target_changed) {
                    if (dd->drop_target) {
                        dispatch_lambda_handler(&evcon, dd->drop_target, "dragover");
                    }
                }

                // dispatch "dragmove" to source (throttled by frame rate inherently)
                dispatch_lambda_handler(&evcon, dd->source_view, "dragmove");

                // Stage 4C: JS dragover to the element under the cursor,
                // independent of the dropzone-based native drop_target. The
                // handler may mutate the DOM (drop-line) → fallback relayout,
                // but dd->active is retained so this keeps firing each move.
                if (evcon.target) {
                    radiant_dispatch_drag_event(&evcon,
                        static_cast<View*>(evcon.target), "dragover",
                        motion->x, motion->y);
                }

                // set cursor to grabbing
                evcon.new_cursor = CSS_VALUE_POINTER;
                evcon.need_repaint = true;
            }
        }

        // Handle text selection drag (supports cross-view selection)
        View* anchor_view = NULL;
        int anchor_offset = 0;
        if (selection_get_pointer_anchor(state, &anchor_view, &anchor_offset)) {
            View* current_target = evcon.target;

            log_debug("[SELECTION DRAG] is_selecting=true, anchor_view=%p, current_target=%p (type=%d)",
                anchor_view, current_target, current_target ? current_target->view_type : -1);

            // Handle textarea form control drag selection
            if (anchor_view && anchor_view->is_element()) {
                DomElement* anchor_elem = lam::dom_require_element(anchor_view);
                if (anchor_elem->form_control() &&
                    anchor_elem->form->control_type == FORM_CONTROL_TEXTAREA) {
                    uint32_t hit_offset = 0;
                    editing_geometry_text_control_offset_for_point(evcon.ui_context,
                        anchor_elem, (float)motion->x, (float)motion->y,
                        &hit_offset);
                    int char_offset = (int)hit_offset; // INT_CAST_OK: StateStore selection API uses int offsets.
                    log_debug("[TA DRAG] motion=(%.1f,%.1f) char_offset=%d",
                              motion->x, motion->y, char_offset);

                    dispatch_form_selection_extend(&evcon, anchor_elem, state,
                        anchor_view, anchor_offset, char_offset, "dragExtend");
                    EditingControllerHooks hooks = editing_controller_hooks();
                    editing_controller_drag_autoscroll(&evcon, state, anchor_view,
                                                       (float)motion->x,
                                                       (float)motion->y,
                                                       &hooks);
                    evcon.need_repaint = true;
                    // skip text selection drag below
                    goto textarea_drag_done;
                }

                // Single-line <input type="text"> drag selection
                if (anchor_elem->form_control() &&
                    anchor_elem->form->control_type == FORM_CONTROL_TEXT) {
                    uint32_t hit_offset = 0;
                    editing_geometry_text_control_offset_for_point(evcon.ui_context,
                        anchor_elem, (float)motion->x, (float)motion->y,
                        &hit_offset);
                    int char_offset = (int)hit_offset; // INT_CAST_OK: StateStore selection API uses int offsets.

                    dispatch_form_selection_extend(&evcon, anchor_elem, state,
                        anchor_view, anchor_offset, char_offset, "dragExtend");
                    EditingControllerHooks hooks = editing_controller_hooks();
                    editing_controller_drag_autoscroll(&evcon, state, anchor_view,
                                                       (float)motion->x,
                                                       (float)motion->y,
                                                       &hooks);
                    // Refresh StateStore text-control selection projection so
                    // render_form shows the live drag highlight.
                    uint32_t sel_start = 0, sel_end = 0;
                    form_control_get_selection(state, static_cast<View*>(anchor_elem), &sel_start, &sel_end, NULL);
                    log_debug("[INPUT DRAG SEL] char_offset=%d sel_u16=[%u..%u] tc_init=%d",
                              char_offset,
                              sel_start,
                              sel_end,
                              anchor_elem->form->tc_initialized ? 1 : 0);
                    evcon.need_repaint = true;
                    goto textarea_drag_done;
                }
            }

            // Check if we're dragging over a text view (could be the same or different)
            View* drag_target_view = nullptr;
            int drag_hit_offset = -1;
            View* selection_focus_view = NULL;
            int selection_focus_offset = 0;
            float selection_iframe_offset_x = 0;
            float selection_iframe_offset_y = 0;
            bool selection_collapsed = true;
            selection_get_focus_snapshot(state, &selection_focus_view,
                &selection_focus_offset, &selection_iframe_offset_x,
                &selection_iframe_offset_y, &selection_collapsed);
            DomDocument* selection_doc = event_context_target_document(&evcon);
            if (current_target && current_target->view_type == RDT_VIEW_TEXT) {
                drag_target_view = current_target;
            } else if (anchor_view && anchor_view->view_type == RDT_VIEW_TEXT &&
                       selection_doc && selection_doc->view_tree &&
                       selection_doc->view_tree->root) {
                EditingSurface anchor_surface;
                EditingBoundary hit_boundary;
                bool has_anchor_surface = editing_surface_from_target(anchor_view, &anchor_surface) &&
                    editing_surface_is_rich(&anchor_surface);
                bool has_hit_boundary = has_anchor_surface &&
                    editing_geometry_hit_test_boundary(evcon.ui_context,
                        static_cast<View*>(selection_doc->view_tree->root), &anchor_surface,
                        (float)motion->x, (float)motion->y,
                        EDITING_CLAMP_SKIP_TEXT_CONTROLS, &hit_boundary);
                if (has_hit_boundary && hit_boundary.dom.node &&
                    hit_boundary.dom.node->node_type == DOM_NODE_TEXT) {
                    DomText* hit_text = lam::dom_require_text(hit_boundary.dom.node);
                    drag_target_view = static_cast<View*>(hit_text);
                    drag_hit_offset = (int)hit_boundary.offset; // INT_CAST_OK: editor selection offsets are byte-index ints
                }
            } else if (selection_focus_view &&
                       selection_focus_view->view_type == RDT_VIEW_TEXT &&
                       !selection_collapsed) {
                // Mouse is not over a text view (e.g., in the gap between
                // adjacent block-level elements like <li>s). If we already
                // have an extended selection, keep its focus_view — falling
                // back to anchor_view here would RESET focus_view to anchor
                // and visually collapse the selection back into the anchor's
                // text node, making the highlight appear to disappear.
                drag_target_view = selection_focus_view;
            } else if (anchor_view && anchor_view->view_type == RDT_VIEW_TEXT) {
                // Initial drag (still collapsed): stay with the anchor view.
                drag_target_view = anchor_view;
            }

            if (drag_target_view && drag_target_view->view_type == RDT_VIEW_TEXT) {
                ViewText* text = lam::view_require_text(drag_target_view);
                TextRect* rect = text->rect;

                // Setup font from text view (critical for correct glyph advance calculation)
                FontBox saved_font = evcon.font;
                if (text->font) {
                    setup_font(evcon.ui_context, &evcon.font, text->font);
                }

                // Calculate the correct block position for the drag target view
                // by walking up ITS parent chain
                float sel_block_x = 0, sel_block_y = 0;
                View* parent = text->parent;
                while (parent) {
                    if (parent->view_type == RDT_VIEW_BLOCK ||
                        parent->view_type == RDT_VIEW_INLINE_BLOCK ||
                        parent->view_type == RDT_VIEW_LIST_ITEM) {
                        sel_block_x += (lam::view_require_block(parent))->x;
                        sel_block_y += (lam::view_require_block(parent))->y;
                    }
                    parent = parent->parent;
                }

                // Add the iframe offset that was stored when selection started
                sel_block_x += selection_iframe_offset_x;
                sel_block_y += selection_iframe_offset_y;

                // Save evcon.block and temporarily set it to the selection view's block position
                BlockBlot saved_block = evcon.block;
                evcon.block.x = sel_block_x;
                evcon.block.y = sel_block_y;

                // Pick the TextRect whose vertical band best matches mouse_y. For
                // multi-line wrapped text the chain `text->rect -> next -> next ...`
                // is one rect per visual line; using only `text->rect` (line 1)
                // makes the selection collapse whenever the mouse passes through
                // the gap between lines (or onto later lines) at an x near the
                // anchor's x. Convert mouse_y to layout-relative y, then pick
                // the rect that contains it.
                //
                // Special case (in_gap): when rel_y falls in the inter-line GAP
                // between two rects of the SAME wrapped text node (line-height
                // > the rect's own height), snap the focus offset to the
                // line-break boundary (end of the rect above) instead of
                // recomputing from mouse_x. Otherwise, when dragging back up
                // from a lower line to the anchor's line, the mouse passes
                // through that gap directly below the anchor x and the
                // recomputed offset would equal the anchor offset -> selection
                // visually disappears for one frame.
                float rel_y = motion->y - sel_block_y;
                TextRect* picked = rect;
                bool in_gap = false;
                int gap_offset = -1;
                for (TextRect* r = rect; r; r = r->next) {
                    if (rel_y < r->y) {
                        if (r == rect) {
                            picked = r;  // above the very first line
                        } else {
                            // In the gap between previous rect (picked) and r:
                            // snap focus to the line-break boundary.
                            in_gap = true;
                            gap_offset = picked->start_index + max(picked->length, 0);
                        }
                        break;
                    }
                    picked = r;
                    if (rel_y <= r->y + r->height) break;  // mouse inside this rect
                    // else: keep walking; if no later rect contains rel_y we'll
                    // end up with the last rect (mouse below all lines).
                }
                rect = picked;

                bool use_margin_offset = evcon.target_text_offset_valid &&
                    evcon.target == drag_target_view && evcon.target_text_rect;
                if (use_margin_offset) {
                    rect = evcon.target_text_rect;
                }

                // Calculate character offset from mouse position using target text rect
                int char_offset;
                if (use_margin_offset) {
                    char_offset = evcon.target_text_offset;
                } else if (drag_hit_offset >= 0) {
                    char_offset = drag_hit_offset;
                } else if (in_gap && gap_offset >= 0) {
                    char_offset = gap_offset;
                } else {
                    EditingBoundary hit_boundary;
                    if (editing_geometry_dom_text_boundary_from_point(evcon.ui_context,
                            text, rect,
                            (float)motion->x, (float)motion->y,
                            &hit_boundary)) {
                        char_offset = (int)hit_boundary.offset; // INT_CAST_OK: editor selection offsets are byte-index ints
                    } else {
                        char_offset = calculate_char_offset_from_position(
                            &evcon, text, rect,
                            motion->x, motion->y);
                    }
                }

                log_debug("[SELECTION DRAG] target_view=%p (same as anchor: %d), char_offset=%d, anchor=%d, picked_rect=(%.1f,%.1f,%.1fx%.1f start=%d len=%d) rel_y=%.1f in_gap=%d margin=%d",
                    drag_target_view, drag_target_view == anchor_view, char_offset, anchor_offset,
                    rect->x, rect->y, rect->width, rect->height, rect->start_index, rect->length, rel_y, in_gap,
                    use_margin_offset);

                // Always use selection_extend_to_view so that focus_view is
                // refreshed to the current drag target. Using the same-view
                // state_store_selection_extend_to_offset() leaves focus_view at whatever it was last
                // set to — which is wrong if the user previously dragged across
                // a different text view and now drags back: focus_view stays
                // pointing at the OTHER view while focus_offset becomes a byte
                // offset valid only in this (anchor) view, producing a broken
                // DomSelection range that renders as collapsed.
                state_store_selection_extend_to_view(state, drag_target_view, char_offset);
                if (drag_target_view != anchor_view) {
                    log_debug("[CROSS-VIEW SEL] Extending from anchor_view=%p to focus_view=%p",
                        anchor_view, drag_target_view);
                }
                state_store_caret_collapse_to_view_offset(state, drag_target_view, char_offset);
                dispatch_rich_selection_snapshot(&evcon, state, drag_target_view,
                    "dragExtend", nullptr);

                // Calculate and set visual position for the caret
                float caret_x, caret_y, caret_height;
                event_text_caret_rect(&evcon, text, rect, char_offset,
                                      &caret_x, &caret_y, &caret_height);

                log_debug("[CARET DRAG] char_offset=%d, calc pos: (%.1f, %.1f) height=%.1f, sel_block: (%.1f, %.1f)",
                    char_offset, caret_x, caret_y, caret_height, sel_block_x, sel_block_y);

                // Restore evcon.block and evcon.font
                evcon.block = saved_block;
                evcon.font = saved_font;

                caret_project_visual_from_selection(state, caret_x, caret_y, caret_height);

                // Update selection end visual coordinates for rendering
                selection_project_focus_visual(state, caret_x, caret_y, caret_height);
                float selection_end_x = 0, selection_end_y = 0;
                float caret_visual_x = 0, caret_visual_y = 0;
                selection_get_focus_visual_snapshot(state, &selection_end_x, &selection_end_y, NULL);
                caret_get_visual_snapshot(state, &caret_visual_x, &caret_visual_y, NULL, NULL, NULL);
                log_debug("[SEL-END] Setting selection end: (%.1f, %.1f), caret at (%.1f, %.1f)",
                    selection_end_x, selection_end_y, caret_visual_x, caret_visual_y);

                selection_get_focus_visual_snapshot(state, NULL, NULL, &selection_collapsed);
                log_debug("Dragging selection to offset %d, collapsed=%d", char_offset, selection_collapsed);
                evcon.need_repaint = true;
            }
            EditingControllerHooks hooks = editing_controller_hooks();
            editing_controller_drag_autoscroll(&evcon, state, anchor_view,
                                               (float)motion->x,
                                               (float)motion->y,
                                               &hooks);
        }
        textarea_drag_done:

        if (state && state->drag_target) {
            log_debug("Dragging in progress");
            ArrayList* target_list = build_view_stack(&evcon, static_cast<View*>(state->drag_target));
            evcon.event.type = RDT_EVENT_MOUSE_DRAG;  // deliver as drag event
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        }

        if (uicon->mouse_state.cursor != evcon.new_cursor) {
            log_debug("Change cursor to %d", evcon.new_cursor);
            uicon->mouse_state.cursor = evcon.new_cursor; // update the mouse state
            int cursor_type;
            switch (evcon.new_cursor) {
            case CSS_VALUE_TEXT: cursor_type = GLFW_IBEAM_CURSOR; break;
            case CSS_VALUE_POINTER: cursor_type = GLFW_HAND_CURSOR; break;
            default: cursor_type = GLFW_ARROW_CURSOR; break;
            }
            GLFWcursor* cursor = glfwCreateStandardCursor(cursor_type);
            if (cursor) {
                if (uicon->mouse_state.sys_cursor) {
                    glfwDestroyCursor(uicon->mouse_state.sys_cursor);
                }
                uicon->mouse_state.sys_cursor = cursor;
                glfwSetCursor(uicon->window, cursor);
            }
        }
        break;
    }
    case RDT_EVENT_MOUSE_DOWN:   case RDT_EVENT_MOUSE_UP: {
        MouseButtonEvent* btn_event = &event->mouse_button;
        log_debug("Mouse button event (%.1f, %.1f)", btn_event->x, btn_event->y);
        mouse_x = btn_event->x;  mouse_y = btn_event->y; // changed to use btn_event's y
        target_html_doc(&evcon, doc->view_tree);
        event_log_hit_target(cascade_log, cascade_id, &evcon);

        // Forward mouse button events to layer-mode webview
        if (evcon.target && evcon.target->is_element() && evcon.target->is_block()) {
            // SVG paint hits target leaf geometry without a CSS block box;
            // only block views can own an embedded layer webview.
            ViewBlock* tblock = lam::view_require_block(evcon.target);
            if (tblock->embed && tblock->embedp()->webview &&
                tblock->embedp()->webview->mode == WEBVIEW_MODE_LAYER &&
                tblock->embedp()->webview->handle) {
                int mouse_type = (event->type == RDT_EVENT_MOUSE_DOWN) ? 0 : 1;
                webview_layer_platform_inject_mouse(tblock->embedp()->webview->handle,
                    mouse_type, evcon.offset_x, evcon.offset_y,
                    btn_event->button, btn_event->mods);
                if (event->type == RDT_EVENT_MOUSE_UP) {
                    webview_layer_platform_inject_mouse(tblock->embedp()->webview->handle,
                        3, evcon.offset_x, evcon.offset_y,
                        btn_event->button, btn_event->mods);
                }
            }
        }

        DocState* state = event_context_target_state(&evcon);

        if (btn_event->button == GLFW_MOUSE_BUTTON_LEFT) {
            uicon->mouse_state.is_mouse_down = event->type == RDT_EVENT_MOUSE_DOWN;
        }

        // F8 (Radiant_Design_Form_Input.md §3.10): native context menu
        // hit-testing. Runs before any focus / drag work so a click inside
        // the popup or its dismissal doesn't reach underlying views.
        if (event->type == RDT_EVENT_MOUSE_DOWN && state && state->context_menu_target) {
            float mxp = (float)btn_event->x;
            float myp = (float)btn_event->y;
            if (context_menu_contains(state, mxp, myp)) {
                if (btn_event->button == GLFW_MOUSE_BUTTON_LEFT) {
                    ContextMenuEditHooks hooks;
                    hooks.cut_selection = dispatch_context_menu_cut;
                    hooks.delete_selection = dispatch_context_menu_delete;
                    hooks.paste_text = dispatch_context_menu_paste;
                    hooks.select_all = dispatch_context_menu_select_all;
                    hooks.user = &evcon;
                    context_menu_click_with_hooks(state, mxp, myp, &hooks);
                }
                break;
            }
            // Click outside the open menu always dismisses it; we then
            // continue with normal handling so the new click still works.
            context_menu_close(state);
        }
        // F10: a right click asks the `<body>` behavior template whether a menu
        // belongs here and which items are live. Native still resolves the hit
        // target and the popup position — logical geometry is mechanism and does
        // not cross into policy — and records both so `radiant.open_context_menu`
        // can place the popup without the template ever handling coordinates.
        if (event->type == RDT_EVENT_MOUSE_DOWN &&
            btn_event->button == GLFW_MOUSE_BUTTON_RIGHT &&
            state && evcon.target && evcon.target->is_element()) {
            state->pending_context_menu_target = evcon.target;
            state->pending_context_menu_x = (float)btn_event->x;
            state->pending_context_menu_y = (float)btn_event->y;
            bool opened = radiant_dispatch_behavior_context_menu(&evcon, evcon.target);
            state->pending_context_menu_target = nullptr;
            if (opened && state->context_menu_target) break;
        }

        // Update active and focus states
        if (event->type == RDT_EVENT_MOUSE_DOWN && evcon.target) {
            selection_press_in_range_clear(state);
            log_debug("MOUSE_DOWN: target=%p view_type=%d", evcon.target, evcon.target->view_type);
            if (evcon.target->view_type == RDT_VIEW_TEXT) {
                log_debug("Target is ViewText, target_text_rect=%p", evcon.target_text_rect);
            }

            // Set :active state
            update_active_state(&evcon, evcon.target, true);

            if (dispatch_lambda_handler_without_js(&evcon, evcon.target,
                                                   "mousedown")) {
                evcon.need_repaint = true;
            }

            bool pointer_prevented = radiant_dispatch_pointer_event(
                &evcon, evcon.target, "pointerdown",
                btn_event->x, btn_event->y, btn_event->button,
                1 << btn_event->button,
                event_mod_ctrl(btn_event->mods),
                event_mod_shift(btn_event->mods),
                event_mod_alt(btn_event->mods),
                event_mod_super(btn_event->mods), "mouse");
            if (pointer_prevented) evcon.default_prevented = true;
            // Dispatch through JS EventTarget before native defaults so
            // preventDefault() can suppress focus/caret default actions.
            {
                bool prevented = radiant_dispatch_mouse_event(&evcon, evcon.target,
                    "mousedown", btn_event->x, btn_event->y,
                    btn_event->button, 1 << btn_event->button,
                    event_mod_ctrl(btn_event->mods),
                    event_mod_shift(btn_event->mods),
                    event_mod_alt(btn_event->mods),
                    event_mod_super(btn_event->mods),
                    1);
                if (prevented) evcon.default_prevented = true;
            }

            // Update focus if target is focusable (mouse-triggered focus).
            // A canceled mousedown suppresses the browser focus default action;
            // toolbar controls use this to keep text-control selection active.
            // Hit testing commonly lands on a button's text child; browser
            // mouse focus belongs to the nearest focusable ancestor instead.
            View* mouse_focus = mouse_focus_target(evcon.target);
            if (!evcon.default_prevented && mouse_focus) {
                update_focus_state(&evcon, mouse_focus, false);  // from_keyboard=false
            } else if (!evcon.default_prevented) {
                DomElement* rich_host = rich_editable_from_target(evcon.target);
                if (rich_host && is_view_focusable(static_cast<View*>(rich_host))) {
                    update_focus_state(&evcon, static_cast<View*>(rich_host), false);
                }
            }

            // A click that lands in an empty / non-text editable element (an
            // empty <li>, an empty <p>, a block holding only an image) resolves
            // to an *element* boundary rather than text. Place the caret there
            // so the element is focusable/typable, before the text-snapping and
            // host-last-text fallbacks below (which would jump to a neighbour).
            bool placed_element_caret = false;
            if (!evcon.default_prevented) {
                DomDocument* mdoc = event_context_target_document(&evcon);
                View* mroot = (mdoc && mdoc->view_tree)
                    ? static_cast<View*>(mdoc->view_tree->root) : nullptr;
                if (mroot) {
                    DomBoundary eb = dom_hit_test_to_boundary(
                        mroot, (float)btn_event->x, (float)btn_event->y);
                    if (eb.node && eb.node->node_type == DOM_NODE_ELEMENT) {
                        EditingSurface esurf;
                        if (editing_surface_from_target(static_cast<View*>(eb.node),
                                &esurf) && editing_surface_is_rich(&esurf)) {
                            const char* exc = nullptr;
                            if (state_store_set_selection(state, &eb, &eb, &exc)) {
                                editing_interaction_set_active_surface(state, &esurf);
                                placed_element_caret = true;
                                evcon.need_repaint = true;
                                log_debug("rich_mouse_empty_element_caret: node=%p offset=%u",
                                          (void*)eb.node, eb.offset);
                            }
                        }
                    }
                }
            }

            if (!placed_element_caret && !evcon.default_prevented &&
                evcon.target->view_type != RDT_VIEW_TEXT &&
                !is_view_focusable(evcon.target)) {
                DomElement* rich_host = rich_editable_from_target(evcon.target);
                DomText* fallback_text = editing_find_text_descendant(
                    rich_host ? static_cast<DomNode*>(rich_host) : nullptr, true);
                if (fallback_text) {
                    uint32_t fallback_len = fallback_text->length > 0
                        ? (uint32_t)fallback_text->length
                        : (uint32_t)strlen(fallback_text->text ? fallback_text->text : "");
                    state_store_caret_collapse_to_view_offset(state, static_cast<View*>(fallback_text),
                              (int)fallback_len); // INT_CAST_OK: StateStore caret API uses int offsets.
                    EditingSurface surface;
                    if (editing_surface_from_target(static_cast<View*>(fallback_text), &surface) &&
                        editing_surface_is_rich(&surface)) {
                        editing_interaction_set_active_surface(state, &surface);
                    }
                    log_debug("rich_mouse_blank_caret: host=%p text=%p offset=%u",
                              (void*)rich_host, (void*)fallback_text, fallback_len);
                    evcon.need_repaint = true;
                }
            }

            // ES21: a press inside a text control's selection arms a text drag.
            // Checked before the `draggable` walk so a selection inside a draggable
            // ancestor drags its text, as browsers do; the walk still wins for a
            // press outside the selection.
            // Single, unmodified press only — the same gate the document-text
            // path uses. A double/triple click inside a selection must still
            // reach word/line selection, and a shift-click must still extend.
            bool text_drag_armed = false;
            if (event->type == RDT_EVENT_MOUSE_DOWN &&
                btn_event->button == GLFW_MOUSE_BUTTON_LEFT &&
                event->mouse_button.clicks == 1 &&
                !(event->mouse_button.mods & RDT_MOD_SHIFT) &&
                state && evcon.target && !evcon.default_prevented) {
                DomElement* drag_src_elem = nullptr;
                uint32_t drag_sel_start = 0, drag_sel_end = 0, drag_press = 0;
                if (radiant_text_drag_source_at(&evcon, state, evcon.target,
                                                (float)btn_event->x, (float)btn_event->y,
                                                &drag_src_elem, &drag_sel_start,
                                                &drag_sel_end, &drag_press)) {
                    DragTransitionArgs text_drag = {};
                    text_drag.source = static_cast<View*>(drag_src_elem);
                    text_drag.x = (float)btn_event->x;
                    text_drag.y = (float)btn_event->y;
                    text_drag.has_source_range = true;
                    text_drag.source_start = drag_sel_start;
                    text_drag.source_end = drag_sel_end;
                    text_drag.press_offset = drag_press;
                    drag_transition(state, DRAG_TRANSITION_BEGIN_DROP, &text_drag);
                    text_drag_armed = true;
                    log_debug("TEXT DRAG ARM: elem=%p sel=[%u..%u]",
                              (void*)drag_src_elem, drag_sel_start, drag_sel_end);
                }
            }

            // Handle click in text - position caret or start selection.
            // This is a mousedown default action, so a canceled mousedown must
            // leave the existing text-control selection intact.
            if (!placed_element_caret &&
                !evcon.default_prevented && evcon.target->view_type == RDT_VIEW_TEXT &&
                evcon.target_text_rect && text_target_allows_caret(evcon.target)) {
                ViewText* text = lam::view_require_text(evcon.target);
                TextRect* rect = evcon.target_text_rect;
                // Setup font from text view (critical for correct glyph advance calculation)
                FontBox saved_font = evcon.font;
                if (text->font) {
                    setup_font(evcon.ui_context, &evcon.font, text->font);
                }

                // Calculate character offset from click position
                int char_offset = evcon.target_text_offset_valid
                    ? evcon.target_text_offset
                    : 0;
                if (!evcon.target_text_offset_valid) {
                    EditingBoundary hit_boundary;
                    if (editing_geometry_dom_text_boundary_from_point(evcon.ui_context,
                            text, rect,
                            (float)btn_event->x, (float)btn_event->y,
                            &hit_boundary)) {
                        char_offset = (int)hit_boundary.offset; // INT_CAST_OK: editor selection offsets are byte-index ints
                    } else {
                        char_offset = calculate_char_offset_from_position(
                            &evcon, text, rect, btn_event->x, btn_event->y);
                    }
                }

                log_debug("CLICK IN TEXT at offset %d (target=%p)", char_offset, evcon.target);

                bool mouse_down_in_selection = btn_event->button == GLFW_MOUSE_BUTTON_LEFT &&
                    event->mouse_button.clicks == 1 &&
                    !(event->mouse_button.mods & RDT_MOD_SHIFT) &&
                    text_point_inside_existing_selection(state, evcon.target, char_offset);

                if (mouse_down_in_selection) {
                    selection_transition(state, SELECTION_TRANSITION_END_POINTER_SELECTION, NULL);
                    selection_press_in_range_begin(state, evcon.target, char_offset);
                    log_debug("[TEXT SEL PRESS] preserving existing selection on mouse down");
                    evcon.need_repaint = true;
                } else {

                bool shift_extending = (event->mouse_button.mods & RDT_MOD_SHIFT) &&
                    selection_has_projection(state);
                if (!shift_extending) {
                    // Set caret at clicked position for a fresh placement. A
                    // shift-click must preserve the existing collapsed
                    // selection anchor so state_store_selection_extend_to_offset() can use it.
                    View* focused = focus_get(state);
                    if (focused && focused->is_element()) {
                        DomElement* focused_elem = lam::dom_require_element(focused);
                        DomNode* target_node = static_cast<DomNode*>(evcon.target);
                        DomNode* focused_node = static_cast<DomNode*>(focused);
                        if (tc_is_text_control(focused_elem) &&
                            !dom_node_is_descendant_of(target_node, focused_node)) {
                            // plain document text clicks must transfer caret ownership
                            // away from the focused text control before StateStore
                            // refresh preserves that control's selection shadow.
                            update_focus_state(&evcon, NULL, false);
                        }
                    }
                    collapse_active_text_control_selection_for_rich_target(state, evcon.target);
                    state_store_caret_collapse_to_view_offset(state, evcon.target, char_offset);
                }

                // Calculate visual position for the caret
                float caret_x, caret_y, caret_height;
                event_text_caret_rect(&evcon, text, rect, char_offset,
                                      &caret_x, &caret_y, &caret_height);

                caret_project_visual_from_block(state, static_cast<View*>(text), caret_x, caret_y, caret_height,
                                                evcon.block.x, evcon.block.y);
#ifndef NDEBUG
                float caret_iframe_offset_x = 0, caret_iframe_offset_y = 0;
                if (caret_get_visual_snapshot(state, NULL, NULL, NULL,
                        &caret_iframe_offset_x, &caret_iframe_offset_y)) {
                    log_debug("CARET VISUAL: x=%.1f y=%.1f height=%.1f iframe_offset=(%.1f,%.1f)",
                        caret_x, caret_y, caret_height,
                        caret_iframe_offset_x, caret_iframe_offset_y);
                    float render_x = caret_x;
                    float render_y = caret_y;
                    for (View* render_parent = text->parent; render_parent; render_parent = render_parent->parent) {
                        if (render_parent->view_type == RDT_VIEW_BLOCK ||
                            render_parent->view_type == RDT_VIEW_INLINE_BLOCK ||
                            render_parent->view_type == RDT_VIEW_LIST_ITEM) {
                            render_x += render_parent->x;
                            render_y += render_parent->y;
                        }
                    }
                    render_x += caret_iframe_offset_x;
                    render_y += caret_iframe_offset_y;
                    log_info("[CARET FINAL] mouse=(%.1f,%.1f) local=(%.1f,%.1f) render=(%.1f,%.1f) offset=%d block=(%.1f,%.1f) rect=(%.1f,%.1f %.1fx%.1f)",
                        btn_event->x, btn_event->y, caret_x, caret_y,
                        render_x, render_y, char_offset, evcon.block.x, evcon.block.y,
                        rect->x, rect->y, rect->width, rect->height);
                }
#endif

                // Start new selection if shift not pressed, otherwise extend
                if (!(event->mouse_button.mods & RDT_MOD_SHIFT)) {
                    SmTransitionGuard sm_guard(state, SM_FAMILY_SELECTION,
                        SM_EV_UI_START_POINTER_SELECTION, evcon.target);
                    dispatch_selectstart(&evcon, evcon.target);
                    state_store_selection_start_pointer(state, evcon.target, char_offset);
                    sm_guard.commit();
                    dispatch_rich_selection_snapshot(&evcon, state, evcon.target,
                        "mouseDown", nullptr);

                    // Set visual coordinates for selection (same point for start)
                    selection_project_anchor_visual_from_caret(state, caret_x, caret_y, caret_height);
                } else if (shift_extending) {
                    // Shift-click extends selection
                    state_store_selection_extend_to_offset(state, char_offset);
                    dispatch_rich_selection_snapshot(&evcon, state, evcon.target,
                        "extendMouse", nullptr);

                    // Update end visual coordinates
                    selection_project_focus_visual(state, caret_x, caret_y, caret_height);
                }

                if (!(event->mouse_button.mods & RDT_MOD_SHIFT)) {
                    const char* text_buf = (const char*)text->text_data();
                    uint32_t text_len = text_buf ? (uint32_t)strlen(text_buf) : 0;
                    uint32_t click_off = char_offset < 0 ? 0 : (uint32_t)char_offset;
                    if (click_off > text_len) click_off = text_len;
                    if (event->mouse_button.clicks >= 3) {
                        uint32_t start = te_line_start(text_buf, text_len, click_off);
                        uint32_t end = te_line_end(text_buf, text_len, click_off);
                        te_apply_byte_range(state, evcon.target, start, end);
                        dispatch_rich_selection_snapshot(&evcon, state, evcon.target,
                            "selectLine", nullptr);
                    } else if (event->mouse_button.clicks == 2) {
                        uint32_t start = te_word_start(text_buf, text_len, click_off);
                        uint32_t end = te_word_end(text_buf, text_len, click_off);
                        if (start != end) {
                            te_apply_byte_range(state, evcon.target, start, end);
                            dispatch_rich_selection_snapshot(&evcon, state, evcon.target,
                                "selectWord", nullptr);
                        }
                    }
                }

                }

                // Restore font
                evcon.font = saved_font;
                evcon.need_repaint = true;
            } else if (!evcon.default_prevented && evcon.target->is_element()) {
                DomElement* target_elem = lam::dom_require_element(evcon.target);

                // Text input form controls: place caret inside the input
                // An armed text drag must not also place a caret: the press is
                // a drag, and starting a pointer selection here leaves an anchor
                // at the press offset that the move then invalidates — dropping
                // text out of the source shortens it below that offset and trips
                // "editing drag anchor offset exceeds target length". Preserving
                // the selection is also what the drag needs to move.
                if (!text_drag_armed && target_elem->form_control() &&
                    target_elem->form->control_type == FORM_CONTROL_TEXT &&
                    !form_control_is_disabled(state, static_cast<View*>(target_elem))) {

                    EditingBoundary click_boundary;
                    editing_geometry_text_control_boundary_from_point(evcon.ui_context,
                        target_elem, (float)event->mouse_button.x,
                        (float)event->mouse_button.y, &click_boundary);
                    int char_offset = (int)click_boundary.offset; // INT_CAST_OK: StateStore selection API uses int offsets.

                    EditingCaretRect caret_rect;
                    if (editing_geometry_caret_rect(evcon.ui_context, &click_boundary, &caret_rect)) {
                        caret_project_visual_from_block(state, evcon.target,
                            caret_rect.x, caret_rect.y, caret_rect.height,
                            evcon.block.x, evcon.block.y);
                        log_debug("INPUT CARET: offset=%d x=%.1f y=%.1f height=%.1f",
                            char_offset, caret_rect.x, caret_rect.y, caret_rect.height);
                    }

                    // Start/extend selection so a subsequent mouse drag
                    // (RDT_EVENT_MOUSE_MOVE with is_selecting=true) hits
                    // the single-line input drag-selection branch and
                    // mirrors the result back into form->selection_*.
                    if (!(event->mouse_button.mods & RDT_MOD_SHIFT)) {
                        dispatch_form_selection_start(&evcon, target_elem, state,
                            evcon.target, (uint32_t)char_offset, "mouseDown");
                    } else if (selection_has_projection(state)) {
                        dispatch_form_selection_extend(&evcon, target_elem, state,
                            evcon.target, char_offset, char_offset, "extendMouse");
                    }

                    // F2 (Radiant_Design_Form_Input.md §3.4): dblclick =>
                    // word selection, tripleclick (or higher) => select-all
                    // for single-line <input>.
                    if (event->mouse_button.clicks >= 3) {
                        dispatch_form_select_all(&evcon, target_elem, state, evcon.target);
                    } else if (event->mouse_button.clicks == 2) {
                        dispatch_form_select_word(
                            &evcon, target_elem, state, evcon.target, char_offset);
                    }
                    evcon.need_repaint = true;

                } else if (!text_drag_armed && target_elem->form_control() &&
                           target_elem->form->control_type == FORM_CONTROL_TEXTAREA &&
                           !form_control_is_disabled(state, static_cast<View*>(target_elem))) {
                    // Textarea form controls: click-to-position caret
                    EditingBoundary click_boundary;
                    editing_geometry_text_control_boundary_from_point(evcon.ui_context,
                        target_elem, (float)event->mouse_button.x,
                        (float)event->mouse_button.y, &click_boundary);
                    int char_offset = (int)click_boundary.offset; // INT_CAST_OK: StateStore selection API uses int offsets.

                    // Start/extend textarea selection
                    if (!(event->mouse_button.mods & RDT_MOD_SHIFT)) {
                        dispatch_form_selection_start(&evcon, target_elem, state,
                            evcon.target, (uint32_t)char_offset, "mouseDown");
                    } else if (selection_has_projection(state)) {
                        dispatch_form_selection_extend(&evcon, target_elem, state,
                            evcon.target, char_offset, char_offset, "extendMouse");
                    }

                    log_debug("TEXTAREA CARET: offset=%d", char_offset);
                    // F2: dblclick selects the word, tripleclick selects the
                    // logical line in <textarea>.
                    if (event->mouse_button.clicks >= 3) {
                        const char* select_value = target_elem->form
                            ? target_elem->form->current_value : nullptr;
                        uint32_t select_len = target_elem->form
                            ? target_elem->form->current_value_len : 0;
                        uint32_t click_off = char_offset < 0 ? 0 : (uint32_t)char_offset;
                        uint32_t start = te_line_start(select_value, select_len, click_off);
                        uint32_t end = te_line_end(select_value, select_len, click_off);
                        dispatch_form_selection_range(&evcon, target_elem, state,
                            evcon.target, start, end, "selectLine");
                    } else if (event->mouse_button.clicks == 2) {
                        dispatch_form_select_word(
                            &evcon, target_elem, state, evcon.target, char_offset);
                    }
                    evcon.need_repaint = true;

                } else if (target_elem->display.inner == RDT_DISPLAY_REPLACED) {
                    bool disabled_form_control =
                        target_elem->form_control() &&
                        form_control_is_disabled(state, static_cast<View*>(target_elem));
                    if (disabled_form_control) {
                        if (state && state->sel.kind == EDIT_SEL_TEXT_CONTROL) {
                            selection_refresh_presentation(state);
                        }
                    } else {
                        // Non-text replaced elements: clear caret
                        state_store_caret_clear(state);
                        state_store_selection_clear(state);
                    }
                    evcon.need_repaint = true;
                }
            }
        } else if (event->type == RDT_EVENT_MOUSE_DOWN && !evcon.target) {
            // Click outside all content (e.g., below body) — clear caret and selection
            // In browsers, clicking outside the document body clears the text caret
            if (state) {
                state_store_caret_clear(state);
                state_store_selection_clear(state);
                evcon.need_repaint = true;
            }
        }

        // Check for draggable element on MOUSE_DOWN — initiate pending drag
        if (event->type == RDT_EVENT_MOUSE_DOWN && evcon.target && state) {
            // walk up from target to find element with draggable="true"
            DomNode* node = static_cast<DomNode*>(evcon.target);
            DomElement* draggable_elem = nullptr;
            while (node) {
                if (node->node_type == DOM_NODE_ELEMENT) {
                    DomElement* elem = lam::dom_require_element(node);
                    const char* draggable = elem->get_attribute("draggable");
                    bool is_draggable = draggable && strcmp(draggable, "true") == 0;
                    // Browser-faithful: <img> and <a href> are draggable by
                    // default (no draggable attr) unless draggable="false".
                    if (!is_draggable &&
                        !(draggable && strcmp(draggable, "false") == 0)) {
                        const char* tag = elem->tag_name;
                        if (tag && (strcasecmp(tag, "img") == 0 ||
                                    (strcasecmp(tag, "a") == 0 &&
                                     elem->get_attribute("href")))) {
                            is_draggable = true;
                        }
                    }
                    if (is_draggable) {
                        draggable_elem = elem;
                        break;
                    }
                }
                node = node->parent;
            }
            if (draggable_elem) {
                const char* drag_data = draggable_elem->get_attribute("dragdata");
                DragTransitionArgs drag_args = {
                    .source = static_cast<View*>(draggable_elem),
                    .x = (float)btn_event->x,
                    .y = (float)btn_event->y,
                    .drag_data = drag_data
                };
                drag_transition(state, DRAG_TRANSITION_BEGIN_DROP, &drag_args);
                DragDropState* drag_drop = state->drag_drop;
                if (drag_drop) {
                    log_debug("DRAG PENDING: source=%p start=(%.0f,%.0f) data=%s",
                        draggable_elem, drag_drop->start_x, drag_drop->start_y,
                        drag_data ? drag_data : "(none)");
                }
            }
        }

        if (event->type == RDT_EVENT_MOUSE_UP) {
            // Snapshot before any handler runs: a behavior template may open a
            // dropdown during dispatch, and the overlay block below must only
            // act on one that was already open when the click arrived.
            View* dropdown_open_at_press = state ? state->open_dropdown : nullptr;
            if (evcon.target) {
                bool pointer_up_prevented = radiant_dispatch_pointer_event(
                    &evcon, evcon.target, "pointerup",
                    btn_event->x, btn_event->y, btn_event->button, 0,
                    event_mod_ctrl(btn_event->mods),
                    event_mod_shift(btn_event->mods),
                    event_mod_alt(btn_event->mods),
                    event_mod_super(btn_event->mods), "mouse");
                if (pointer_up_prevented) evcon.default_prevented = true;
            }
            // Dispatch the JS 'mouseup' event through the EventTarget pipeline
            // (browsers fire mouseup before click). Only mousedown + click were
            // dispatched before, so window/document-level drag listeners that
            // finish on mouseup — e.g. an editor's image-resize / block
            // drag-reorder using window.addEventListener('mouseup') — never ran
            // under `view`. It bubbles to document/window like mousedown does.
            if (evcon.target) {
                bool up_prevented = radiant_dispatch_mouse_event(&evcon, evcon.target,
                    "mouseup", btn_event->x, btn_event->y,
                    btn_event->button, 1 << btn_event->button,
                    event_mod_ctrl(btn_event->mods),
                    event_mod_shift(btn_event->mods),
                    event_mod_alt(btn_event->mods),
                    event_mod_super(btn_event->mods),
                    1);
                if (up_prevented) evcon.default_prevented = true;
                if (dispatch_lambda_handler_without_js(&evcon, evcon.target,
                                                       "mouseup")) {
                    evcon.need_repaint = true;
                }
            }

            // Stage 4C: JS drop + dragend for script editors, gated on the
            // (now retention-safe) native drag. Fire a final dragover at the
            // release point; a drop follows only if that last dragover was
            // canceled — HTML5 gates drop on preventDefault. Then dragend to the
            // source DOM element (survives any fallback relayout).
            if (state && state->drag_drop && state->drag_drop->active) {
                View* drag_src = state->drag_drop->source_view;
                if (evcon.target) {
                    bool ov = radiant_dispatch_drag_event(&evcon,
                        static_cast<View*>(evcon.target), "dragover",
                        btn_event->x, btn_event->y);
                    if (ov) {
                        radiant_dispatch_drag_event(&evcon,
                            static_cast<View*>(evcon.target), "drop",
                            btn_event->x, btn_event->y);
                    }
                }
                if (drag_src) {
                    radiant_dispatch_drag_event(&evcon, drag_src,
                        "dragend", btn_event->x, btn_event->y);
                }
                js_drag_session_end();
            }

            // Handle drag-and-drop completion first
            bool drag_handled = false;
            if (state && state->drag_drop) {
                DragDropState* dd = state->drag_drop;
                if (dd->active) {
                    // drag was active — dispatch drop or dragend
                    if (dd->drop_target) {
                        // dispatch "drop" to the drop target element
                        log_debug("DRAG DROP: source=%p target=%p", dd->source_view, dd->drop_target);
                        dispatch_lambda_handler(&evcon, dd->drop_target, "drop");

                        // ES21: a text drag drops through the same entry point
                        // the harness drives, so the applier, the maxlength and
                        // newline policy in editing.ls, and the same-control
                        // offset adjustment after the delete are shared rather
                        // than reimplemented here. The source range recorded at
                        // drag start is what closes the "guess at what to
                        // remove" gap the rich path below still has.
                        DomElement* text_drop_elem = nullptr;
                        uint32_t text_drop_offset = 0;
                        if (dd->has_source_range && dd->source_view &&
                            radiant_text_drop_target_at(&evcon, state, dd->drop_target,
                                                        (float)btn_event->x,
                                                        (float)btn_event->y,
                                                        &text_drop_elem,
                                                        &text_drop_offset)) {
                            // Ctrl/Cmd holds a copy; a plain drag moves, which is
                            // the browser convention on both platforms.
                            bool copy = event_mod_ctrl(btn_event->mods) ||
                                        event_mod_super(btn_event->mods);
                            radiant_dispatch_editing_text_drag_drop(
                                evcon.ui_context, dd->source_view,
                                dd->source_start, dd->source_end,
                                static_cast<View*>(text_drop_elem),
                                text_drop_offset, text_drop_offset,
                                nullptr, nullptr, !copy);
                            evcon.need_repaint = true;
                        }
                        // CE-5 / ED2-2: lower drop-on-editable to
                        // beforeinput {insertFromDrop}. When dragover stored
                        // a DOM target range, run the same defaultable rich
                        // edit path used by simulated text drop.
                        else if (editing_host_lookup(static_cast<DomNode*>(dd->drop_target),
                                                nullptr)) {
                            // Source deletion for element drag remains a
                            // consumer intent until live drag state carries a
                            // source text range; defaulting without one would
                            // guess at what to remove.
                            if (dd->source_view &&
                                editing_host_lookup(static_cast<DomNode*>(dd->source_view),
                                                    nullptr)) {
                                InputIntent del = {};
                                del.type = INPUT_INTENT_DELETE_BY_DRAG;
                                dispatch_contenteditable_consumer_event(&evcon, dd->source_view, &del);
                            }
                            bool inserted = dd->has_drop_range &&
                                dispatch_rich_drop_at_range(&evcon,
                                    dd->drop_target, &dd->drop_start,
                                    &dd->drop_end,
                                    dd->drag_data ? dd->drag_data : "");
                            if (!inserted) {
                                InputIntent ins = {};
                                ins.type = INPUT_INTENT_INSERT_FROM_DROP;
                                // §8: pass drag_data as the textual payload.
                                // radiant_dispatch_input_event builds the
                                // InputEvent DataTransfer from this intent data.
                                // Files/custom drag item stores are still
                                // deferred.
                                ins.data = dd->drag_data ? dd->drag_data : "";
                                dispatch_contenteditable_consumer_event(&evcon, dd->drop_target, &ins);
                            }
                        }
                    }
                    // dispatch "dragend" to source
                    dispatch_lambda_handler(&evcon, dd->source_view, "dragend");
                    // clear any dragover highlight on previous drop target
                    if (dd->drop_target) {
                        dispatch_lambda_handler(&evcon, dd->drop_target, "dragleave");
                    }
                    drag_handled = true;
                    evcon.need_repaint = true;
                }
                else if (dd->pending && dd->has_source_range && dd->source_view &&
                         dd->source_view->is_element()) {
                    // ES21: the press armed a text drag that never crossed the
                    // movement threshold, so it was a plain click inside the
                    // selection. Browsers collapse to the press offset on
                    // release, and suppressing the mousedown caret placement
                    // without this leaves the old selection standing — which is
                    // how rsc_scale_editing_geometry_matrix caught it.
                    // Same pairing the document-text path makes with
                    // selection_press_in_range_begin / _pending.
                    DomElement* press_elem = lam::dom_require_element(dd->source_view);
                    dispatch_form_selection_start(&evcon, press_elem, state,
                                                  dd->source_view, dd->press_offset,
                                                  "textDragClick");
                    evcon.need_repaint = true;
                }
                drag_transition(state, DRAG_TRANSITION_CLEAR_DROP, NULL);
            }

            // Clear :active state
            update_active_state(&evcon, NULL, false);

            View* collapse_view = NULL;
            int collapse_offset = 0;
            if (selection_press_in_range_pending(state, &collapse_view, &collapse_offset)) {
                if (evcon.target && evcon.target->view_type == RDT_VIEW_TEXT && evcon.target_text_rect) {
                    ViewText* text = lam::view_require_text(evcon.target);
                    FontBox saved_font = evcon.font;
                    if (text->font) {
                        setup_font(evcon.ui_context, &evcon.font, text->font);
                    }
                    collapse_view = evcon.target;
                    if (evcon.target_text_offset_valid) {
                        collapse_offset = evcon.target_text_offset;
                    } else {
                        EditingBoundary hit_boundary;
                        if (editing_geometry_dom_text_boundary_from_point(evcon.ui_context,
                                text, evcon.target_text_rect,
                                (float)btn_event->x, (float)btn_event->y,
                                &hit_boundary)) {
                            collapse_offset = (int)hit_boundary.offset; // INT_CAST_OK: editor selection offsets are byte-index ints
                        } else {
                            collapse_offset = calculate_char_offset_from_position(
                                &evcon, text, evcon.target_text_rect, btn_event->x, btn_event->y);
                        }
                    }
                    evcon.font = saved_font;
                }
                state_store_selection_start_pointer(state, collapse_view, collapse_offset);
                selection_transition(state, SELECTION_TRANSITION_END_POINTER_SELECTION, NULL);
                selection_press_in_range_clear(state);
                log_debug("[TEXT SEL PRESS] collapsed preserved selection on mouse up");
                evcon.need_repaint = true;
            }

            bool text_selection_drag_handled = selection_is_pointer_range_active(state) &&
                mouseup_target_can_finish_text_selection(&evcon);

            // Handle select dropdown click FIRST (before other click handling)
            // If a dropdown is open, handle clicks on it before anything else
            bool dropdown_handled = false;
            // Only a dropdown that was already open when this click arrived can
            // receive it. A click that *opens* one — which a behavior template
            // does during dispatch, earlier in this handler than the native
            // path did — must not then be treated as a click into it.
            if (state && state->open_dropdown &&
                state->open_dropdown == dropdown_open_at_press) {
                // Check if clicking on dropdown option
                if (handle_dropdown_option_click(&evcon, (float)mouse_x, (float)mouse_y)) {
                    // Option was selected, done - skip other click handlers
                    dropdown_handled = true;
                } else {
                    // Check if clicking outside dropdown - close it
                    close_dropdown_if_outside(&evcon, (float)mouse_x, (float)mouse_y);
                    dropdown_handled = true;  // Still handled - don't re-open dropdown
                }
            }

            // A secondary-button release emits mouseup/contextmenu, not the
            // primary click activation that can toggle controls or submit forms.
            bool primary_activation = btn_event->button == GLFW_MOUSE_BUTTON_LEFT;
            // Only process other click handlers if dropdown wasn't involved and not a drag
            if (primary_activation && !dropdown_handled && !drag_handled &&
                !text_selection_drag_handled) {
                // Dispatch click through JS EventTarget before built-in default
                // actions so listeners or IDL handlers can call preventDefault().
                // Native disabled controls suppress click dispatch even when
                // hit testing lands on a child created by a widget library.
                bool click_on_disabled_control = click_target_is_disabled_control(
                    state, evcon.target);
                if (evcon.target && !click_on_disabled_control) {
                    bool prevented = radiant_dispatch_mouse_event(&evcon, evcon.target,
                        "click", mouse_x, mouse_y,
                        btn_event->button, 0,
                        event_mod_ctrl(btn_event->mods),
                        event_mod_shift(btn_event->mods),
                        event_mod_alt(btn_event->mods),
                        event_mod_super(btn_event->mods),
                        1);
                    if (prevented) evcon.default_prevented = true;
                }

                // Handle click on <video> element — play/pause toggle + seek bar
                if (evcon.target && state && !evcon.default_prevented) {
                    View* v = evcon.target;
                    // walk up to find a block with embed->video
                    while (v) {
                        if (v->view_type == RDT_VIEW_BLOCK) {
                            ViewBlock* blk = lam::view_require_block(v);
                            if (blk->embed && blk->embedp()->video) {
                                RdtVideo* video = (RdtVideo*)blk->embedp()->video;
                                bool has_controls = blk->embedp()->has_controls;

                                // compute absolute viewport position by walking parent chain
                                float vid_x = 0, vid_y = 0;
                                View* walk = static_cast<View*>(blk);
                                while (walk) {
                                    if (walk->view_type == RDT_VIEW_BLOCK) {
                                        ViewBlock* wb = lam::view_require_block(walk);
                                        vid_x += wb->x;
                                        vid_y += wb->y;
                                        if (wb->scroller && wb->scroll_mut()->pane) {
                                            DocState* scroll_state = wb->doc ? wb->doc->state : NULL;
                                            float scroll_x = 0.0f, scroll_y = 0.0f;
                                            scroll_state_get_position_for_view(scroll_state, static_cast<View*>(wb),
                                                wb->scroll()->pane, &scroll_x, &scroll_y, NULL, NULL);
                                            vid_x -= scroll_x;
                                            vid_y -= scroll_y;
                                        }
                                    }
                                    walk = static_cast<View*>(walk->parent);
                                }

                                float vid_w = blk->width;
                                float vid_h = blk->height;
                                float mx = (float)mouse_x;
                                float my = (float)mouse_y;

                                log_debug("[VIDEO CLICK] mx=%.0f my=%.0f vid=(%0.f,%0.f %0.fx%0.f) controls=%d",
                                          mx, my, vid_x, vid_y, vid_w, vid_h, has_controls);

                                if (has_controls && my >= vid_y + vid_h - 40.0f) {
                                    // click in controls bar
                                    float bar_x = vid_x + 8.0f;  // CONTROLS_PADDING
                                    float btn_end = bar_x + 24.0f + 8.0f;  // play btn + margin

                                    // volume slider region (from right edge)
                                    float vol_end = vid_x + vid_w - 8.0f;   // CONTROLS_PADDING from right
                                    float vol_start = vol_end - 60.0f;      // VOLUME_WIDTH
                                    float speaker_start = vol_start - 4.0f - 16.0f;  // ICON_MARGIN/2 + icon

                                    if (mx >= vol_start && mx <= vol_end) {
                                        // volume slider click
                                        float frac = (mx - vol_start) / 60.0f;
                                        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
                                        rdt_video_set_volume(video, frac);
                                        log_debug("[VIDEO CLICK] volume set to %.0f%%", frac * 100);
                                    } else if (mx >= speaker_start && mx < vol_start) {
                                        // speaker icon click — toggle mute
                                        // TODO: track muted state properly
                                        log_debug("[VIDEO CLICK] speaker icon clicked (mute toggle)");
                                    } else if (mx < btn_end) {
                                        // play/pause button
                                        RdtVideoState vs = rdt_video_get_state(video);
                                        if (vs == RDT_VIDEO_STATE_PLAYING) {
                                            rdt_video_pause(video);
                                        } else {
                                            rdt_video_play(video);
                                        }
                                        log_debug("[VIDEO CLICK] play/pause toggled");
                                    } else {
                                        // estimate seek bar region — seek on click
                                        // compute seek bar bounds (approximate)
                                        float seek_start = btn_end + 50.0f;  // after time text
                                        float seek_end = speaker_start - 8.0f - 50.0f - 8.0f;  // before dur text + volume
                                        if (mx >= seek_start && mx <= seek_end && seek_end > seek_start) {
                                            float frac = (mx - seek_start) / (seek_end - seek_start);
                                            if (frac < 0) frac = 0; if (frac > 1) frac = 1;
                                            double dur = rdt_video_get_duration(video);
                                            if (dur > 0) {
                                                rdt_video_seek(video, dur * frac);
                                                log_debug("[VIDEO CLICK] seek to %.1f%%", frac * 100);
                                            }
                                        }
                                    }
                                    evcon.need_repaint = true;
                                } else {
                                    // click on video body — toggle play/pause
                                    RdtVideoState vs = rdt_video_get_state(video);
                                    if (vs == RDT_VIDEO_STATE_PLAYING) {
                                        rdt_video_pause(video);
                                    } else {
                                        rdt_video_play(video);
                                    }
                                    log_debug("[VIDEO CLICK] body click — play/pause toggled");
                                    evcon.need_repaint = true;
                                }
                                break;
                            }
                        }
                        v = static_cast<View*>(v->parent);
                    }
                }

                if (evcon.target) {
                    dispatch_click_default_actions(&evcon, evcon.target);
                }
            }

            // End selection mode
            if (selection_has_projection(state)) {
                dispatch_selectionchange(&evcon, state, evcon.target);
                selection_transition(state, SELECTION_TRANSITION_END_POINTER_SELECTION, NULL);
                EditingControllerHooks hooks = editing_controller_hooks();
                editing_controller_drag_autoscroll_stop(state, &hooks);
            }
            EditingControllerHooks hooks = editing_controller_hooks();
            editing_controller_drag_autoscroll_stop(state, &hooks);
        }

        if (evcon.target) {
            log_debug("Target view found at position (%.1f, %.1f)", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            log_debug("No target view found at position (%.1f, %.1f)", mouse_x, mouse_y);
        }

        // fire drag event if dragging in progress
        if (evcon.event.type == RDT_EVENT_MOUSE_UP && state && state->drag_target) {
            log_debug("mouse up in dragging");
            ArrayList* target_list = build_view_stack(&evcon, static_cast<View*>(state->drag_target));
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
            update_drag_state(&evcon, NULL, false);
        }

        if (evcon.new_url) {
            log_debug("opening_url:%s", evcon.new_url);
            const char* new_url = evcon.new_url;

            // -- Fragment-only navigation: scroll to #id without loading a new page --
            if (new_url[0] == '#' && doc->root) {
                const char* fragment_id = new_url + 1;  // skip '#'
                log_info("browse_nav: fragment navigation to #%s", fragment_id);
                DomElement* target_elem = js_dom_find_element_by_id(doc->root, fragment_id);
                if (target_elem) {
                    View* target_view = find_view(doc->view_tree->root, static_cast<DomNode*>(target_elem));
                    if (target_view) {
                        // get root scroller and scroll to element's y position
                        ViewBlock* root_block = lam::view_require_block(doc->view_tree->root);
                        if (root_block && root_block->scroller && root_block->scroll_mut()->pane) {
                            ScrollPane* pane = root_block->scroll()->pane;
                            float target_y = target_view->y;
                            DocState* scroll_state = (DocState*)uicon->document->state;
                            float scroll_x = 0.0f, scroll_y = 0.0f;
                            scroll_state_get_position_for_view(scroll_state, static_cast<View*>(root_block), pane,
                                                               &scroll_x, &scroll_y, NULL, NULL);
                            scroll_state_set_position_for_view(scroll_state, static_cast<View*>(root_block),
                                                               pane, scroll_x, target_y, true);
                            scroll_state_get_position_for_view(scroll_state, static_cast<View*>(root_block), pane,
                                                               NULL, &scroll_y, NULL, NULL);
                            log_info("browse_nav: scrolled to #%s at y=%.0f", fragment_id,
                                     scroll_y);
                            doc_state_mark_dirty(uicon->document->state);
                        }
                    } else {
                        log_warn("browse_nav: element #%s found but no view for it", fragment_id);
                    }
                } else {
                    log_warn("browse_nav: element #%s not found in document", fragment_id);
                }
                to_repaint();
                break;
            }

            if (evcon.new_target) {
                log_debug("setting new src to target: %s", evcon.new_target);
                // Legacy package-off navigation resolves the target by name;
                // execution stays shared with the package-pinned path.
                DomElement* iframe = find_iframe_by_name(doc->root, evcon.new_target);
                if (!iframe || !navigation_execute_iframe_target(evcon.ui_context, iframe, new_url)) {
                    log_debug("failed to find iframe view");
                }
            }
            else {
                // Legacy package-off navigation uses the same native executor
                // after its mousedown policy determines the current document.
                navigation_execute_top_target(evcon.ui_context, doc, new_url);
            }
            to_repaint();
        }
        // Phase 6E: sync canonical selection/projection caches into
        // form->selection_* after mouse-driven focus / hit-test / drag ops.
        {
            DocState* tc_state = event_context_target_state(&evcon);
            View* tc_focused = tc_state ? focus_get(tc_state) : nullptr;
            if (tc_focused && tc_focused->is_element()) {
                DomElement* tc_elem = lam::dom_require_element(tc_focused);
                if (tc_is_text_control(tc_elem)) {
                    tc_sync_selection_to_form(tc_elem, tc_state);
                    tc_set_active_element(tc_state, tc_elem);
                    tc_set_last_focused_text_control(tc_state, tc_elem);
                }
            }
        }
        break;
    }
    case RDT_EVENT_SCROLL: {
        ScrollEvent* scroll = &event->scroll;
        log_debug("Mouse scroll event");
        mouse_x = scroll->x;  mouse_y = scroll->y; // updated to use scroll's x and y
        target_html_doc(&evcon, doc->view_tree);
        event_log_hit_target(cascade_log, cascade_id, &evcon);

        // Forward scroll to layer-mode webview
        if (evcon.target && evcon.target->is_element() && evcon.target->is_block()) {
            // SVG paint hits target leaf geometry without a CSS block box;
            // only block views can own an embedded layer webview.
            ViewBlock* tblock = lam::view_require_block(evcon.target);
            if (tblock->embed && tblock->embedp()->webview &&
                tblock->embedp()->webview->mode == WEBVIEW_MODE_LAYER &&
                tblock->embedp()->webview->handle) {
                webview_layer_platform_inject_scroll(tblock->embedp()->webview->handle,
                    scroll->xoffset, scroll->yoffset, evcon.offset_x, evcon.offset_y);
                break;  // consumed by webview
            }
        }

        if (evcon.target) {
            log_debug("Target view found at position (%.1f, %.1f)", mouse_x, mouse_y);
            // Dispatch "wheel" through JS EventTarget before native scroll.
            bool wheel_prevented = radiant_dispatch_wheel_event(&evcon, evcon.target,
                mouse_x, mouse_y,
                -(double)scroll->xoffset * 100.0,
                -(double)scroll->yoffset * 100.0,
                0);
            if (wheel_prevented) {
                log_debug("wheel default suppressed by preventDefault()");
                break;
            }
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack (inside iframe if applicable)
            fire_events(&evcon, target_list);
            arraylist_free(target_list);

            // Propagate scroll to iframe container (the outer iframe block handles scrolling)
            if (evcon.iframe_container) {
                ArrayList* parent_list = build_view_stack(&evcon, evcon.iframe_container);
                fire_events(&evcon, parent_list);
                arraylist_free(parent_list);
            }
        } else {
            log_debug("No target view found at position (%.1f, %.1f)", mouse_x, mouse_y);
        }
        break;
    }
    case RDT_EVENT_KEY_DOWN: {
        KeyEvent* key_event = &event->key;
        DocState* state = event_context_target_state(&evcon);
        if (!state) break;

        // F8: Esc closes the native context menu before any other handler.
        if (state->context_menu_target && key_event->key == RDT_KEY_ESCAPE) {
            context_menu_close(state);
            evcon.need_repaint = true;
            break;
        }

        // Handle dropdown keyboard navigation first (if dropdown is open)
        if (state->open_dropdown) {
            if (handle_dropdown_key(&evcon, key_event->key, key_event->mods)) {
                evcon.need_repaint = true;
                break;
            }
        }

        View* focused = focus_get(state);
        event_log_focused_target(cascade_log, cascade_id, focused);
        log_debug("Key down: key=%d, mods=0x%x, focused=%p", key_event->key, key_event->mods, focused);
        View* caret_intent_view = caret_get_view(state);
        View* debug_caret_view = nullptr;
        int debug_caret_offset = 0;
        if ((!caret_intent_view || !rich_editable_from_target(caret_intent_view)) &&
            caret_get_debug_snapshot(state, &debug_caret_view, &debug_caret_offset,
                                     nullptr, nullptr, nullptr, nullptr, nullptr,
                                     nullptr) &&
            debug_caret_view && rich_editable_from_target(debug_caret_view)) {
            caret_intent_view = debug_caret_view;
        }
        View* intent_target = caret_intent_view &&
            rich_editable_from_target(caret_intent_view)
                ? caret_intent_view
                : (focused ? focused : caret_intent_view);
        if (!intent_target && state->editing.has_active_surface &&
            editing_surface_is_rich(&state->editing.active_surface)) {
            intent_target = state->editing.active_surface.view
                ? state->editing.active_surface.view
                : static_cast<View*>(state->editing.active_surface.owner);
        }
        if (!intent_target) {
            intent_target = rich_keyboard_target_from_selection(state, nullptr,
                                                                nullptr);
        }

        // Forward key events to layer-mode webview if it has focus
        WebViewHandle* focused_webview = focused_layer_webview_handle(focused);
        if (focused_webview) {
            int key_type = (event->type == RDT_EVENT_KEY_DOWN) ? 0 : 1;
            webview_layer_platform_inject_key(
                focused_webview, key_type, key_event->key, key_event->mods);
            break;
        }

        EditingSurface rich_keyboard_surface;
        bool rich_keydown_dispatched = intent_target &&
            editing_surface_from_target(intent_target, &rich_keyboard_surface) &&
            editing_surface_is_rich(&rich_keyboard_surface);
        bool rich_clipboard_shortcut = (key_event->mods &
            (RDT_MOD_SUPER | RDT_MOD_CTRL)) &&
            (key_event->key == RDT_KEY_V || key_event->key == RDT_KEY_C ||
             key_event->key == RDT_KEY_X);
        if (rich_keydown_dispatched && key_event->key != RDT_KEY_TAB) {
            // Selection can still identify the rich surface after a template
            // rebuild clears focus; do not drop its beforeinput edit.
            // Contenteditable keymaps own structural commands. Dispatch the
            // public keydown first so preventDefault suppresses this key's
            // mapped beforeinput edit instead of racing it afterward.
            bool prevented = radiant_dispatch_keyboard_event(&evcon, intent_target,
                "keydown", key_event->key, key_event->mods, false);
            if (editing_key_may_emit_text(key_event)) {
                state->editing.pending_text_input = true;
                state->editing.pending_text_input_prevented = prevented;
                state->editing.pending_text_input_key = key_event->key;
            }
            if (prevented) {
                evcon.default_prevented = true;
                break;
            }
            if (!rich_clipboard_shortcut) {
                InputIntent intent;
                if (input_intent_from_key_event(state, key_event, &intent)) {
                    if (intent.type == INPUT_INTENT_SELECT_ALL) {
                        View* select_target = rich_keyboard_target_from_selection(
                            state, intent_target, nullptr);
                        if (select_target) {
                            dispatch_contenteditable_select_all(&evcon, state,
                                select_target, &intent);
                        }
                    } else {
                        dispatch_contenteditable_event(&evcon, intent_target,
                            &intent);
                    }
                    evcon.need_repaint = true;
                    break;
                }
            }
        }

        // Tab is a keydown-only interaction in browsers — no beforeinput is
        // fired for it. A JS keydown listener on the focused element (e.g. a
        // contenteditable editor that indents/outdents list items on Tab) must
        // get first crack and be able to preventDefault. Dispatch keydown here
        // (for BOTH plain and rich/script-owned surfaces — native rich editing
        // is retired, so all editing is script-owned), and only fall back to
        // focus navigation when the default was not prevented. Handling Tab here
        // preempts the rich-intent beforeinput path below (which would otherwise
        // translate Tab into a formatIndent beforeinput the editor never asked
        // for) and the generic keydown dispatch (avoiding a double keydown).
        if (key_event->key == RDT_KEY_TAB) {
            bool tab_prevented = false;
            if (focused) {
                tab_prevented = radiant_dispatch_keyboard_event(&evcon, focused,
                    "keydown", key_event->key, key_event->mods, false);
                if (tab_prevented) evcon.default_prevented = true;
                focused = focus_get(state);
            }
            if (!tab_prevented) {
                bool forward = !(key_event->mods & RDT_MOD_SHIFT);
                DomDocument* focus_doc = evcon.target_document ? evcon.target_document : doc;
                if (focus_doc && focus_doc->view_tree && focus_doc->view_tree->root) {
                    View* previous_focus = focus_get(state);
                    focus_move(state, focus_doc->view_tree->root, forward);
                    View* next_focus = focus_get(state);
                    if (next_focus && next_focus != previous_focus) {
                        // Sequential focus navigation must emit focusin so
                        // script focus traps can redirect an escaped Tab.
                        dispatch_focus_blur_observed(&evcon, previous_focus, next_focus);
                        radiant_dispatch_focus_event(&evcon, next_focus,
                                                     "focus", previous_focus);
                        radiant_dispatch_focus_event(&evcon, next_focus,
                                                     "focusin", previous_focus);
                    }
                }
            }
            evcon.need_repaint = true;
            break;
        }

        // Clipboard on a rich/contenteditable surface: fire the JS
        // paste/copy/cut ClipboardEvent with store-backed clipboardData so a
        // script-owned editor receives the same shortcut notification as a
        // browser. Cut must reach this branch before its deleteByCut intent:
        // CodeMirror claims cut in its ClipboardEvent listener and mutates its
        // own model, while the DOM fallback deliberately stays unsupported.
        if ((key_event->mods & (RDT_MOD_SUPER | RDT_MOD_CTRL)) &&
            (key_event->key == RDT_KEY_V || key_event->key == RDT_KEY_C ||
             key_event->key == RDT_KEY_X)) {
            EditingSurface clip_surface;
            if (intent_target &&
                editing_surface_from_target(intent_target, &clip_surface) &&
                editing_surface_is_rich(&clip_surface)) {
                const char* clip_type = key_event->key == RDT_KEY_V ? "paste" :
                    (key_event->key == RDT_KEY_X ? "cut" : "copy");
                if (radiant_dispatch_clipboard_event(&evcon, intent_target, clip_type)) {
                    evcon.default_prevented = true;
                    evcon.need_repaint = true;
                    break;
                }
                if (key_event->key == RDT_KEY_V) {
                    InputIntent paste_intent;
                    if (input_intent_from_key_event(state, key_event, &paste_intent)) {
                        dispatch_contenteditable_event(&evcon, intent_target,
                            &paste_intent);
                        evcon.need_repaint = true;
                        break;
                    }
                } else if (key_event->key == RDT_KEY_X) {
                    copy_current_selection_to_clipboard(state, "rich cut");
                    InputIntent cut_intent;
                    if (input_intent_from_key_event(state, key_event, &cut_intent)) {
                        dispatch_contenteditable_event(&evcon, intent_target,
                            &cut_intent);
                        evcon.need_repaint = true;
                        break;
                    }
                } else {
                    copy_current_selection_to_clipboard(state, "rich copy");
                    evcon.need_repaint = true;
                    break;
                }
            }
        }

        // Space toggles a focused checkbox / radio (matches native browser
        // and ARIA keyboard behavior). Space and Enter both "activate" a
        // focused <button> (browsers fire click on key-up for Space and
        // key-down for Enter; we fire on key-down for both for simplicity
        // so HTML form submission works without a mouse).
        if (focused && focused->is_element()
            && (key_event->key == RDT_KEY_SPACE || key_event->key == RDT_KEY_ENTER)
            && !(key_event->mods & (RDT_MOD_CTRL | RDT_MOD_SUPER | RDT_MOD_ALT))) {
            ViewElement* fe = lam::view_require_element(focused);
            uint32_t tag = fe->tag();
            bool handled = false;
            if (tag == MARKUP_NAME_INPUT && key_event->key == RDT_KEY_SPACE) {
                if (is_checkbox(focused) || is_radio(focused)) {
                    bool js_click_dispatched = false;
                    radiant_dispatch_mouse_event(&evcon, focused, "click",
                        0, 0, 0, 0, false, false, false, false, 1,
                        &js_click_dispatched);
                    // Keyboard activation goes through the same dispatch the
                    // mouse path uses, so the behavior template owns it too.
                    // It used to call the native activation directly and
                    // without consulting the claim, so a checkbox activated by
                    // Space ran native while the same checkbox clicked by mouse
                    // ran the template — benign only because the two agreed.
                    if (!js_click_dispatched && !evcon.default_prevented) {
                        dispatch_lambda_handler(&evcon, focused, "click");
                    }
                    handled = true;
                }
            } else if (tag == MARKUP_NAME_BUTTON) {
                // Disabled buttons are inert.
                DomElement* delem = lam::dom_require_element(focused);
                bool disabled = delem->form_control() && form_control_is_disabled(state, static_cast<View*>(delem));
                if (!disabled) {
                    radiant_dispatch_mouse_event(&evcon, focused, "click",
                        0, 0, 0, 0, false, false, false, false, 1);
                    run_form_button_default(&evcon, focused);
                    handled = true;
                }
            } else if (tag == MARKUP_NAME_INPUT &&
                       key_event->key == RDT_KEY_ENTER &&
                       (js_dom_is_submit_button((void*)focused) ||
                        js_dom_is_reset_button((void*)focused))) {
                DomElement* delem = lam::dom_require_element(focused);
                bool disabled = delem->form_control() &&
                    form_control_is_disabled(state, static_cast<View*>(delem));
                if (!disabled) {
                    radiant_dispatch_mouse_event(&evcon, focused, "click",
                        0, 0, 0, 0, false, false, false, false, 1);
                    run_form_button_default(&evcon, focused);
                    handled = true;
                }
            } else if (tag == MARKUP_NAME_A && key_event->key == RDT_KEY_ENTER) {
                DomElement* anchor = lam::dom_require_element(focused);
                if (anchor->get_attribute("href")) {
                    bool js_click_dispatched = false;
                    bool prevented = radiant_dispatch_mouse_event(&evcon, focused, "click",
                        0, 0, 0, 0, false, false, false, false, 1,
                        &js_click_dispatched);
                    if (prevented) evcon.default_prevented = true;
                    if (!js_click_dispatched && !evcon.default_prevented) {
                        dispatch_lambda_handler(&evcon, focused, "click");
                    }
                    if (!evcon.default_prevented) {
                        run_link_activation(&evcon, focused);
                    }
                    handled = true;
                }
            } else if (tag == MARKUP_NAME_SELECT) {
                // Space / Enter on a focused <select> opens (or toggles)
                // the dropdown popup, matching native browser behavior.
                DomElement* delem = lam::dom_require_element(focused);
                bool disabled = delem->form_control() && form_control_is_disabled(state, static_cast<View*>(delem));
                if (!disabled) {
                    handled = dispatch_lambda_handler(&evcon, focused, "click");
                }
            }
            if (handled) {
                evcon.need_repaint = true;
                break;
            }
        }

        // Rich-text editing path (Phase R4): translate platform key events
        // into browser-like beforeinput intents for contenteditable
        // template output. Native form controls continue down the existing
        // text-control path.
        if (!rich_keydown_dispatched) {
            InputIntent intent;
            if (intent_target && input_intent_from_key_event(state, key_event, &intent)) {
                if (intent.type == INPUT_INTENT_SELECT_ALL) {
                    EditingSurface surface;
                    View* rich_select_all_target =
                        rich_keyboard_target_from_selection(state, intent_target,
                                                            &surface);
                    if (rich_select_all_target) {
                        dispatch_contenteditable_select_all(&evcon, state,
                            rich_select_all_target, &intent);
                        evcon.need_repaint = true;
                        break;
                    }
                }
                bool handled = false;
                handled = dispatch_contenteditable_consumer_event(
                    &evcon, intent_target, &intent);
                if (handled) {
                    evcon.need_repaint = true;
                    break;
                }
            }
        }

        // Dispatch keydown through JS EventTarget for inline, IDL, and
        // addEventListener handlers.
        if (focused && !rich_keydown_dispatched) {
            bool prevented = radiant_dispatch_keyboard_event(&evcon, focused,
                "keydown", key_event->key, key_event->mods, false);
            if (prevented) evcon.default_prevented = true;
            focused = focus_get(state);
        }
        if (!rich_keydown_dispatched && focused &&
            (key_event->key == RDT_KEY_BACKSPACE ||
             key_event->key == RDT_KEY_DELETE ||
             key_event->key == RDT_KEY_ENTER ||
             key_event->key == RDT_KEY_ESCAPE)) {
            if (dispatch_lambda_handler_without_js(
                    &evcon, focused, "keydown", nullptr, nullptr, true, true)) {
                evcon.need_repaint = true;
                focused = focus_get(state);
            }
        }

        // F4: a single-line text control's Enter is implicit submission, not
        // text insertion. JS keydown cancellation remains authoritative; the
        // package then chooses the first enabled submitter or the form itself.
        if (focused && !evcon.default_prevented &&
            key_event->key == RDT_KEY_ENTER &&
            !(key_event->mods & (RDT_MOD_SHIFT | RDT_MOD_CTRL |
                                 RDT_MOD_ALT | RDT_MOD_SUPER)) &&
            focused->is_element()) {
            DomElement* focus_elem = lam::dom_require_element(focused);
            if (focus_elem->form_control() &&
                focus_elem->form->control_type == FORM_CONTROL_TEXT) {
                DomElement* owner = js_dom_find_form_owner((void*)focus_elem);
                if (owner) {
                    View* submitter = find_first_form_submitter(owner);
                    View* activation = submitter ? submitter : static_cast<View*>(owner);
                    if (run_form_submit_activation(&evcon, activation)) {
                        evcon.need_repaint = true;
                        break;
                    }
                }
            }
        }

        // The old input and textarea key tables drifted by modifier and by
        // operation vocabulary. Keymap/caret now choose all commands once in
        // the package; native receives only the named command to apply.
        int form_caret_offset = 0;
        if (!evcon.default_prevented && focused && focused->is_element() &&
            caret_get_offset(state, &form_caret_offset)) {
            DomElement* focus_elem = lam::dom_require_element(focused);
            if (tc_is_text_control(focus_elem)) {
                uint32_t live_value_len = 0;
                const char* value = form_control_live_value(focus_elem, &live_value_len);
                int value_len = (int)live_value_len; // INT_CAST_OK: text-control byte offsets use StateStore int APIs.
                int cur = form_caret_offset;
                if (cur < 0) cur = 0;
                if (cur > value_len) cur = value_len;

                uint64_t caret_epoch_before = radiant_caret_operation_epoch();
                InputIntent caret_key_intent;
                caret_key_intent.key = key_event->key;
                caret_key_intent.mods = key_event->mods;
                radiant_dispatch_behavior_caret_key(&evcon, focused, &caret_key_intent);
                if (radiant_caret_operation_epoch() != caret_epoch_before &&
                    form_apply_caret_operation(&evcon, focus_elem, state, focused,
                                               value, value_len, cur)) {
                    evcon.need_repaint = true;
                    break;
                }
                if (dispatch_form_key_intent(&evcon, focus_elem, state, focused,
                                             key_event, cur)) {
                    break;
                }
            }
        }

        // Handle caret/selection navigation when we have a caret with a view
        // The caret view is set when clicking on text, which may not be a focusable element
        View* caret_view = NULL;
        int caret_offset = 0;
        if (caret_get_position(state, &caret_view, &caret_offset)) {
            bool ctrl = (key_event->mods & RDT_MOD_CTRL) != 0;
            bool cmd = (key_event->mods & RDT_MOD_SUPER) != 0;

            EditingControllerHooks controller_hooks = editing_controller_hooks();
            EditingSurface caret_surface;
            bool caret_in_rich_surface =
                editing_surface_from_target(caret_view, &caret_surface) &&
                editing_surface_is_rich(&caret_surface);

            // F9: the same `caretkey` seam the text-control path uses. One
            // template now maps keys to operations for both surfaces; only the
            // geometry that resolves an operation stays per-surface.
            bool rich_nav_handled = false;
            // Gate on "the caret is not in a text control", which is what the
            // retired handle_rich_navigation actually tested — it ran for any
            // caret outside a text control, including a plain document
            // selection, not only inside an editing host. Gating on
            // caret_in_rich_surface instead silently dropped arrow keys on
            // ordinary selected text (four caret/selection tests caught it).
            if (editing_controller_caret_surface_kind(state) == 2) {
                uint64_t caret_epoch_before = radiant_caret_operation_epoch();
                InputIntent caret_key_intent;
                caret_key_intent.key = key_event->key;
                caret_key_intent.mods = key_event->mods;
                // The caret usually sits in a text node, and the behavior walk
                // matches elements — dispatching the text node itself finds no
                // template and silently declines, which is why arrow keys on a
                // rich surface reached the "rich key fallback fenced" branch and
                // did nothing.
                View* caret_dispatch_target = caret_view;
                while (caret_dispatch_target && !caret_dispatch_target->is_element()) {
                    caret_dispatch_target =
                        static_cast<View*>(static_cast<DomNode*>(caret_dispatch_target)->parent);
                }
                if (caret_dispatch_target) {
                    radiant_dispatch_behavior_caret_key(&evcon, caret_dispatch_target,
                                                        &caret_key_intent);
                }
                if (radiant_caret_operation_epoch() != caret_epoch_before) {
                    rich_nav_handled = editing_controller_apply_caret_operation(
                        &evcon, state, &controller_hooks,
                        radiant_caret_operation_name(),
                        radiant_caret_operation_extend());
                }
            }
            if (!rich_nav_handled && !caret_in_rich_surface) {
                // Non-rich compatibility branch only. Rich/editable mutation
                // and selection ownership belongs to the intent edit
                // path above plus the caretkey template dispatch.
                //
                // F11b: deliberately NOT migrated to the <body> template, unlike
                // every other key table. Reaching a template here would require
                // the document to own an evaluator, and the only condition that
                // covers "an ordinary document selection" is "this page has a
                // caret" — which is nearly every page. The thread holds one
                // Runtime, so that gate would let a PDF viewer claim it: the
                // pdf_text_selection_copy fixture drags a selection and presses
                // Ctrl+C, so it would qualify. EO4 exists to stop exactly that.
                // These five cases stay native because the cost of reaching
                // policy is a correctness risk, not because they are mechanism.
                switch (key_event->key) {
                    case RDT_KEY_A:
                        // Select all (Ctrl+A / Cmd+A)
                        if (ctrl || cmd) {
                            state_store_selection_select_all(state);
                            evcon.need_repaint = true;
                        }
                        break;

                    case RDT_KEY_C:
                        // Copy selection (Ctrl+C / Cmd+C)
                        if (ctrl || cmd) {
                            copy_current_selection_to_clipboard(state, "legacy copy");
                        }
                        break;

                    case RDT_KEY_X:
                        // Cut over non-editable text does nothing at all, which
                        // is what a browser does: cut is a *mutation*, and there
                        // is nothing here to mutate. Ctrl+C above is the one
                        // that copies.
                        //
                        // This used to write the selection to the clipboard and
                        // then drop the highlight, so a cut on a static page
                        // both claimed to have removed the text and quietly
                        // overwrote whatever the user had copied earlier —
                        // losing their clipboard to a keystroke that changed
                        // nothing on the page.
                        break;

                    // Backspace and Delete do nothing here, and that is the
                    // finished behaviour rather than a gap. The only content
                    // that reaches this branch is non-editable document text: a
                    // caret inside a text control is consumed by the
                    // text-control block above, which breaks out of the keydown
                    // entirely, and a caret inside an editing host is excluded
                    // by `!caret_in_rich_surface`. There is nothing to delete,
                    // which is also what a browser does with Backspace on a
                    // static page.
                    //
                    // They stay listed rather than falling into `default:` so
                    // the next reader does not re-add the TODO that stood here.
                    // What that TODO did do was request a repaint on every
                    // press — work for a keystroke that changes nothing.
                    case RDT_KEY_BACKSPACE:
                    case RDT_KEY_DELETE:
                        break;

                    default:
                        break;
                }
            } else if (caret_in_rich_surface) {
                log_debug("event: rich key fallback fenced; key=%d", key_event->key);
            }
        }
        // Mirror StateStore selection projection into form->selection_* so JS
        // reads (selectionStart/End/value) observe text edits immediately.
        {
            DocState* tc_state = event_context_target_state(&evcon);
            View* tc_focused = tc_state ? focus_get(tc_state) : nullptr;
            if (tc_focused && tc_focused->is_element()) {
                DomElement* tc_elem = lam::dom_require_element(tc_focused);
                if (tc_is_text_control(tc_elem)) {
                    tc_sync_selection_to_form(tc_elem, tc_state);
                }
            }
        }
        break;
    }
    case RDT_EVENT_KEY_UP: {
        // Key release - forward to layer-mode webview if focused
        log_debug("Key up: key=%d", event->key.key);
        {
            DocState* state = event_context_target_state(&evcon);
            if (state) {
                if (state->editing.pending_text_input &&
                    state->editing.pending_text_input_key == event->key.key) {
                    // Keyup closes a sequence whose platform produced no text
                    // callback, so a later IME or injected character cannot
                    // inherit this key's preventDefault decision.
                    state->editing.pending_text_input = false;
                    state->editing.pending_text_input_prevented = false;
                    state->editing.pending_text_input_key = RDT_KEY_UNKNOWN;
                }
                View* focused = focus_get(state);
                event_log_focused_target(cascade_log, cascade_id, focused);
                WebViewHandle* focused_webview = focused_layer_webview_handle(focused);
                if (focused_webview) {
                    webview_layer_platform_inject_key(
                        focused_webview, 1, event->key.key, event->key.mods);
                    break;
                }
                if (focused) {
                    radiant_dispatch_keyboard_event(&evcon, focused,
                        "keyup", event->key.key, event->key.mods, false);
                }
            }
        }
        break;
    }
    case RDT_EVENT_COMPOSITION_START:
    case RDT_EVENT_COMPOSITION_UPDATE:
    case RDT_EVENT_COMPOSITION_END: {
        CompositionEvent* comp_event = &event->composition;
        DocState* state = event_context_target_state(&evcon);
        if (!state) break;

        // Composition starts a distinct text stream, not the delayed character
        // callback of the last physical keydown.
        state->editing.pending_text_input = false;
        state->editing.pending_text_input_prevented = false;
        state->editing.pending_text_input_key = RDT_KEY_UNKNOWN;

        View* focused = focus_get(state);
        event_log_focused_target(cascade_log, cascade_id, focused);
        EditingControllerHooks controller_hooks = editing_controller_hooks();
        editing_controller_handle_composition(&evcon, state, comp_event,
                                              &controller_hooks);
        break;
    }
    case RDT_EVENT_TEXT_INPUT: {
        TextInputEvent* text_event = &event->text_input;
        DocState* state = event_context_target_state(&evcon);
        if (!state) break;

        if (state->editing.pending_text_input) {
            bool keydown_prevented = state->editing.pending_text_input_prevented;
            state->editing.pending_text_input = false;
            state->editing.pending_text_input_prevented = false;
            state->editing.pending_text_input_key = RDT_KEY_UNKNOWN;
            if (keydown_prevented) {
                // The matching keydown canceled default handling; do not let
                // its later character event bypass the public cancellation.
                break;
            }
        }

        View* focused = focus_get(state);
        event_log_focused_target(cascade_log, cascade_id, focused);
        log_debug("Text input: codepoint=U+%04X, focused=%p", text_event->codepoint, focused);

        // Forward text input to layer-mode webview if focused
        WebViewHandle* focused_webview = focused_layer_webview_handle(focused);
        if (focused_webview) {
            webview_layer_platform_inject_text(focused_webview, text_event->codepoint);
            break;
        }


        // capture selection state before dispatch for correct caret adjustment
        bool had_input_selection = false;
        int input_sel_start = 0;
        int input_sel_end = 0;
        if (selection_has(state)) {
            had_input_selection = true;
            selection_get_range(state, &input_sel_start, &input_sel_end);
        }
        // Rich-text text insertion is driven through beforeinput/insertText.
        // This avoids the legacy contenteditable TODO path and lets Lambda
        // commands own source-tree mutation.
        {
            InputIntent intent;
            char utf8_buf[5];
            View* caret_view = NULL;
            int rich_caret_offset = 0;
            caret_get_position(state, &caret_view, &rich_caret_offset);
            View* intent_target = caret_view && rich_editable_from_target(caret_view)
                ? caret_view
                : (focused ? focused : caret_view);
            if (intent_target && input_intent_from_text_input(text_event->codepoint,
                    &intent, utf8_buf, sizeof(utf8_buf)) &&
                dispatch_contenteditable_event(&evcon, intent_target,
                                               &intent)) {
                evcon.need_repaint = true;
                break;
            }
        }

        // Re-fetch focused element (dispatch may have rebuilt the DOM)
        focused = focus_get(state);

        // For form text inputs and textareas, insert through the shared form
        // edit path. It dispatches beforeinput before mutation and input after
        // mutation, so inline oninput handlers observe the updated value.
        bool is_form_input = false;
        if (focused && focused->is_element()) {
            DomElement* elem = lam::dom_require_element(focused);
            if (elem->form_control() &&
                (elem->form->control_type == FORM_CONTROL_TEXT ||
                 elem->form->control_type == FORM_CONTROL_TEXTAREA)) {
                is_form_input = true;
                bool editable = !form_control_is_user_readonly(state, static_cast<View*>(elem));
                int caret_offset = 0;
                if (editable && caret_get_offset(state, &caret_offset)) {
                    uint32_t a, b;
                    if (had_input_selection) {
                        a = (uint32_t)input_sel_start;
                        b = (uint32_t)input_sel_end;
                    } else {
                        a = b = (uint32_t)caret_offset;
                    }
                    char utf8_buf[5];
                    size_t utf8_len = utf8_encode_z(text_event->codepoint, utf8_buf);
                    if (utf8_len > 0) {
                        // Pre-mutation `input`: the Reactive_UI contract where an
                        // app template owns the text and splices it itself, so
                        // claiming it skips the engine's own insert below. UA
                        // behavior must NOT see this one — it fires before the
                        // value exists, and claiming it would stop typing.
                        if (dispatch_lambda_handler(&evcon, focused, "input", nullptr,
                                                    nullptr, /*allow_behavior=*/false,
                                                    /*legacy_author=*/true)) {
                            evcon.need_repaint = true;
                            View* live_focus = focus_get(state);
                            if (live_focus && live_focus->is_element()) {
                                DomElement* live_elem = lam::dom_require_element(live_focus);
                                if (tc_is_text_control(live_elem)) {
                                    uint32_t next_offset = a + (uint32_t)utf8_len;
                                    dispatch_form_caret_collapse(&evcon, live_elem, state,
                                        live_focus, next_offset, "lambdaInsertText");
                                }
                            }
                        } else {
                            DomDocument* restore_doc = elem->doc;
                            const char* restore_id = elem->id;
                            bool replaced = dispatch_form_text_replace(&evcon, elem, state, focused,
                                a, b, utf8_buf, (uint32_t)utf8_len,
                                INPUT_INTENT_INSERT_TEXT);
                            if (replaced) {
                                restore_form_text_focus_after_input(state, restore_doc, restore_id);
                            }
                        }
                    }
                }
            }
        }

        if (!is_form_input && focused && caret_has_projection(state)) {
            // Delete any existing selection first
            if (selection_has(state)) {
                // TODO: delete selected text
                state_store_selection_clear(state);
            }

            // TODO: insert character at caret position
            // This requires access to the text content of the focused element

            // Move caret forward
            state_store_caret_move(state, 1);
        }
        evcon.need_repaint = true;
        break;
    }
    default:
        log_debug("Unhandled event type: %d", event->type);
        break;
    }

    // Refresh viewport scroll snapshot after the event mutates scroll panes.
    // Reflow consumes `pending_viewport_scroll_*`, so keep it synchronized.
    if (event->type == RDT_EVENT_SCROLL) {
        // Element scroll does not trigger layout, but geometry observers must
        // resample after its scroll state changes just like viewport scrolling.
        js_dom_observers_post_layout();
    }
    bool viewport_scrolled = sync_viewport_scroll_state(&evcon);
    if (viewport_scrolled) {
        // Wheel/default scrolling publishes the state snapshot before JS runs,
        // so window.scrollX/Y are live inside the listener.
        radiant_dispatch_window_event(uicon, event_context_target_document(&evcon), "scroll");
    }

    bool target_doc_reflowed = process_event_target_document_reflow(&evcon);
    if (target_doc_reflowed) {
        evcon.need_repaint = true;
    }

    // Process pending reflows if any state changes require relayout
    DocState* state = (DocState*)uicon->document->state;
    if (state && state->needs_reflow) {
        log_debug("Processing pending reflows before repaint");
        reflow_process_pending(state);

        // If reflow is still needed after processing, trigger actual relayout
        if (state->needs_reflow) {
            // Trigger relayout by marking the event context
            evcon.need_repaint = true;  // repaint includes relayout
            log_debug("Reflow required, will trigger relayout");
        }
    }

    // Selection repaint changes both the old and new highlight spans. A
    // caret-sized dirty region leaves stale highlight pixels during live drag.
    if (evcon.need_repaint && selection_has(state)) {
        state->dirty_tracker.full_repaint = true;
    }

    // Phase 19: detect caret-only repaint — no DOM changes, no reflow, only caret moved
    if (evcon.need_repaint) {
        if (caret_prepare_selective_repaint(state)) {
            log_info("[TIMING] caret-only repaint detected, marking dirty for caret regions");
        }
    }

    if (evcon.need_repaint) {
        if (state) doc_state_mark_dirty(state);
        if (uicon->document && uicon->document != evcon.target_document &&
            uicon->document->state) {
            doc_state_request_repaint(uicon->document->state);
        }
        to_repaint();
    }
    log_debug("end of event %d", event->type);

    state_end_event_cascade(cascade_state, cascade_log, cascade_id);
    event_context_cleanup(&evcon);
}

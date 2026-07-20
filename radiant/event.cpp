#include "event.hpp"
#include "render.hpp"
#include "view.hpp"
#include "radiant.hpp"
#include "rdt_video.h"
#include "../lib/tagged.hpp"
#include "../lib/mem_factory.h"
#include "../lib/font/font.h"

#include "../lib/log.h"
#include "../lib/side_stack.h"
#include "../lib/utf.h"
#include "../lib/str.h"
// str.h included via view.hpp
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/template_registry.h"
#include "../lambda/render_map.h"
#include "../lambda/lambda.h"         // Context (input_context)
#include "../lambda/lambda-data.hpp"  // EvalContext
#include "../lambda/transpiler.hpp"   // Runtime (heap and name_pool)
#include "../lambda/mark_builder.hpp" // MarkBuilder for event object construction
#include "../lambda/js/js_dom.h"      // js_dom_set_document for HTML event handlers
#include "../lambda/js/js_dom_events.h" // js_dom_dispatch_event + native event factories
#include "../lambda/js/js_runtime.h"   // js_new_object / js_property_set / js_array_new / js_array_push
#include "../lambda/js/js_dom_platform.h"
#include "../lambda/js/js_dom_observers.h"

// CE-3 follow-up: DataTransfer factory from js_clipboard.cpp (no public
// header — js_clipboard installs globals through js_dom_set_document). We
// only need the two-string builder for the paste/drop dispatch path.
extern "C" Item js_data_transfer_new_with_strings(const char* text_plain,
                                                  const char* text_html);
extern "C" Context* _lambda_rt;
extern "C" void js_dom_notify_mutation(DomJsMutationKind kind,
                                        void* target, void* parent);
extern "C" void js_dom_notify_mutation_detail(DomJsMutationKind kind,
                                               void* target, void* parent,
                                               const char* attribute_name,
                                               const char* old_value);
#include "../lib/hashmap.h"           // hashmap utilities used by DocState maps
#include "../lib/memtrack.h"          // mem_free
#include <chrono>       // timing for reactive event dispatch
#include <string.h>

// thread-local eval context used by heap allocation functions
extern __thread EvalContext* context;
extern __thread Context* input_context;
DomDocument* show_html_doc(Url *base, char* doc_filename, int viewport_width, int viewport_height);
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);
extern "C" int js_check_exception(void);
extern "C" Item js_clear_exception(void);
extern "C" const char* js_get_exception_message(void);

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

// Forward declarations for HTML event handler post-rebuild
struct CssEngine;
void apply_stylesheet_to_dom_tree_fast(DomElement* root, struct CssStylesheet* stylesheet,
                                        struct SelectorMatcher* matcher, Pool* pool, CssEngine* engine);
void apply_inline_styles_to_tree(DomElement* root, Element* html_root, Pool* pool, int depth = 0);
void collect_inline_styles_from_dom(DomElement* elem, CssEngine* engine, Pool* pool,
                                     struct CssStylesheet*** stylesheets, int* count, int depth = 0);
struct SelectorMatcher* selector_matcher_create(Pool* pool);
static void clear_cascaded_styles_recursive(DomNode* node);
static bool radiant_dispatch_simple_event(EventContext* evcon, View* target,
                                          const char* type,
                                          bool bubbles, bool cancelable);

// Forward declarations for event targeting
void target_html_doc(EventContext* evcon, ViewTree* view_tree);
void target_block_view(EventContext* evcon, ViewBlock* block);
void target_inline_view(EventContext* evcon, ViewSpan* view_span);
void target_text_view(EventContext* evcon, ViewText* text);
void update_scroller(ViewBlock* block, float content_width, float content_height);
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
            jw_kv_int(&w, "x", event->mouse_button.x);
            jw_kv_int(&w, "y", event->mouse_button.y);
            jw_kv_int(&w, "button", event->mouse_button.button);
            jw_kv_int(&w, "clicks", event->mouse_button.clicks);
            jw_kv_int(&w, "mods", event->mouse_button.mods);
            break;
        case RDT_EVENT_MOUSE_MOVE:
        case RDT_EVENT_MOUSE_DRAG:
            jw_kv_int(&w, "x", event->mouse_position.x);
            jw_kv_int(&w, "y", event->mouse_position.y);
            break;
        case RDT_EVENT_SCROLL:
            jw_kv_int(&w, "x", event->scroll.x);
            jw_kv_int(&w, "y", event->scroll.y);
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
        editing_log_write_surface_core_fields(w, surface, false);
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
    float saved_window_width = uicon->window_width;
    float saved_window_height = uicon->window_height;
    int saved_viewport_width = uicon->viewport_width;
    int saved_viewport_height = uicon->viewport_height;

    uicon->document = doc;

    if (iframe_container &&
        (iframe_container->view_type == RDT_VIEW_BLOCK ||
         iframe_container->view_type == RDT_VIEW_INLINE_BLOCK)) {
        ViewBlock* block = lam::view_require_block(iframe_container);
        if (block->width > 0) {
            uicon->window_width = block->width;
            uicon->viewport_width = (int)block->width; // INT_CAST_OK: UiContext viewport dimensions are integer CSS pixels.
        }
        if (block->height > 0) {
            uicon->window_height = block->height;
            uicon->viewport_height = (int)block->height; // INT_CAST_OK: UiContext viewport dimensions are integer CSS pixels.
        }
    }

    layout_html_doc(uicon, doc, true);
    if (iframe_container) {
        restore_embedded_document_scroll_model(doc);
    }

    uicon->document = saved_doc;
    uicon->window_width = saved_window_width;
    uicon->window_height = saved_window_height;
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
                                                        uint8_t depth);

static DomDocument* event_context_find_focused_document_in_view(View* view,
                                                                uint8_t depth) {
    if (!view || depth > 8) return NULL;
    if ((view->view_type == RDT_VIEW_BLOCK ||
         view->view_type == RDT_VIEW_INLINE_BLOCK ||
         view->view_type == RDT_VIEW_LIST_ITEM) &&
        view->is_element()) {
        ViewBlock* block = lam::view_require_block(view);
        if (block->embed && block->embedp()->doc) {
            DomDocument* found = event_context_find_focused_document(
                block->embedp()->doc, (uint8_t)(depth + 1));
            if (found) return found;
        }
    }
    if (!view->is_element()) return NULL;
    DomElement* elem = lam::dom_require_element(view);
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        DomDocument* found = event_context_find_focused_document_in_view(
            static_cast<View*>(child), depth);
        if (found) return found;
    }
    return NULL;
}

static DomDocument* event_context_find_focused_document(DomDocument* doc,
                                                        uint8_t depth) {
    if (!doc) return NULL;
    DocState* state = doc->state;
    if (state && focus_get(state)) return doc;
    if (!doc->view_tree || !doc->view_tree->root) return NULL;
    return event_context_find_focused_document_in_view(doc->view_tree->root, depth);
}

static Item call_template_event_handler(fn_ptr handler_func, Item model_item,
                                        Item event_item) {
    typedef Item (*TemplateEventHandlerFn)(Item, Item);
    union {
        fn_ptr raw;
        TemplateEventHandlerFn typed;
    } handler;
    // template_registry stores generated handlers as erased fn_ptr; event
    // handlers are emitted with the stable (Item model, Item event) ABI.
    handler.raw = handler_func;
    return handler.typed(model_item, event_item);
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
    do {
        if (view->is_block()) {
            ViewBlock* block = lam::view_require_block(view);
            if (radiant_stack_is_deferred_from_normal_flow(view)) {
                // skip deferred stacking entries; target_block_view walks them in reverse paint order.
            } else {
                target_block_view(evcon, block);
            }
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            if (radiant_stack_is_deferred_from_normal_flow(view)) {
                view = view->next();
                continue;
            }
            ViewSpan* span = lam::view_require_element(view);
            target_inline_view(evcon, span);
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require_text(view);
            target_text_view(evcon, text);
        }
        view = view->next();
    } while (view && !evcon->target);
}

void target_text_view(EventContext* evcon, ViewText* text) {
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

    log_debug("target text:'%t' start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d, blk_x:%d",
        str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, evcon->block.x);

    // First check if mouse is within the text rect bounds (use rect height, not char height)
    if (x <= event->x && event->x < rect_right && y <= event->y && event->y < rect_bottom) {
        // Mouse is in this text rect - set target and return
        log_debug("hit on text rect at (%d, %d)", event->x, event->y);
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
            if (elem->has_attribute("data-editable")) return true;
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
                case HTM_TAG_A:
                case HTM_TAG_BUTTON:
                case HTM_TAG_INPUT:
                case HTM_TAG_SELECT:
                case HTM_TAG_TEXTAREA:
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

void target_block_view(EventContext* evcon, ViewBlock* block) {
    log_enter();
    BlockBlot pa_block = evcon->block;  FontBox pa_font = evcon->font;
    evcon->block.x = pa_block.x + block->x;  evcon->block.y = pa_block.y + block->y;
    MousePositionEvent* event = &evcon->event.mouse_position;
    // target the scrollbars first
    View* view = NULL;
    bool hover = false;
    if (block->scroller && block->scroll_mut()->pane) {
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
        scroll_state_get_position_for_view(state, static_cast<View*>(block), block->scroll()->pane,
                                           &scroll_x, &scroll_y, NULL, NULL);
        evcon->block.x -= scroll_x;
        evcon->block.y -= scroll_y;
    }

    // Check if this block is a child-window webview — stop hit-testing here.
    // In child-window mode, the OS delivers events directly to the native web view.
    // Radiant should not process events that land inside the webview area.
    if (block->embed && block->embedp()->webview &&
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
    if (block->embed && block->embedp()->webview &&
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
    if (block->embed && block->embedp()->doc) {
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
    bool is_replaced_block = self_tag == HTM_TAG_IMG || self_tag == HTM_TAG_VIDEO ||
        self_tag == HTM_TAG_CANVAS || self_tag == HTM_TAG_IFRAME ||
        self_tag == HTM_TAG_EMBED || self_tag == HTM_TAG_OBJECT ||
        self_tag == HTM_TAG_HR;
    if (!evcon->target &&
        (is_replaced_block ||
         !(is_in_rich_editable_subtree(static_cast<View*>(block)) && !is_rich_editable_host(static_cast<View*>(block))))) { // check the block itself
        // use the block's own accumulated position (parent + block offset),
        // not the restored parent position
        float x = evcon->block.x + block->x, y = evcon->block.y + block->y;
        if (x <= event->x && event->x < x + block->width &&
            y <= event->y && event->y < y + block->height) {
            log_debug("hit on block: %s", block->node_name());
            evcon->target = static_cast<View*>(block);
            evcon->offset_x = event->x - x;
            evcon->offset_y = event->y - y;
        }
        else {
            log_debug("hit not on block: %s, x: %.1f, y: %.1f, ex: %d, ey: %d, right: %.1f, bottom: %.1f",
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
    if (name == HTM_TAG_A) {
        log_debug("fired at anchor tag");
        if (evcon->event.type == RDT_EVENT_MOUSE_DOWN) {
            log_debug("mouse down at anchor tag");
            // §7 unification (U-2): skip default link navigation if a JS
            // mousedown listener called event.preventDefault().
            if (evcon->default_prevented) {
                log_debug("anchor nav suppressed by preventDefault()");
            } else {
                const char* href = span->get_attribute("href");
                if (href) {
                log_debug("got anchor href: %s", href);
                evcon->new_url = (char*)href;
                const char* target = span->get_attribute("target");
                if (target) {
                    log_debug("got anchor target: %s", target);
                    evcon->new_target = (char*)target;
                }
                else {
                    log_debug("no anchor target found");
                }
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

static TemplateEntry* template_registry_find(const char* template_ref) {
    if (!g_template_registry || !template_ref) return NULL;
    for (TemplateEntry* entry = g_template_registry->first;
         entry; entry = entry->next) {
        if (entry->template_ref == template_ref) return entry;
    }
    return NULL;
}

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
        default:                return "";
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
        default:
            // GLFW/Radiant printable letter, digit, and space codes already
            // match the legacy DOM virtual-key values.
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

    Pool* temp_pool = mem_pool_create(NULL, MEM_ROLE_TEMP, "event.temp");
    Arena* temp_arena = mem_arena_create(NULL, temp_pool, MEM_ROLE_TEMP, "event.arena");
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
    mem_pool_destroy(temp_pool);
    return copied;
}

/**
 * Build a Lambda map Item representing an event object.
 * Contains: {type, target_class, target_tag, x, y}
 * For "input" events: adds "char" (typed character as UTF-8 string)
 * For "keydown" events: adds "key" (key name string, e.g. "Backspace", "Enter")
 * Uses doc->input (created during load_lambda_script_doc) for allocation.
 */
static Item build_lambda_event_map(DomDocument* doc, View* target,
                                   const char* event_name, EventContext* evcon,
                                   const InputIntent* intent = nullptr) {
    if (!doc || !doc->input) return ItemNull;

    MarkBuilder builder(doc->input);
    MapBuilder mb = builder.map();
    mb.put("type", event_name);

    if (intent && intent->type != INPUT_INTENT_NONE) {
        const char* input_type = input_intent_type_name(intent->type);
        mb.put("input_type", input_type);
        if (intent->data) mb.put("data", intent->data);
        else mb.putNull("data");
        if (intent->data_mime) mb.put("mime", intent->data_mime);
        else mb.putNull("mime");
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
        mb.put("x", (int64_t)evcon->event.mouse_button.x);
        mb.put("y", (int64_t)evcon->event.mouse_button.y);
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
        int event_x = 0, event_y = 0;
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
                hit = dom_hit_test_to_boundary(static_cast<View*>(doc->view_tree->root), (float)event_x, (float)event_y);
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

    return mb.final();
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

static DomElement* find_element_by_author_id(DomNode* node, const char* id);

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

    EditingSurface resolved;
    const EditingSurface* surface = nullptr;
    if (state->editing.rich_transaction_phase != EDITING_RICH_TX_IDLE &&
        state->editing.rich_transaction_target &&
        editing_surface_from_target(state->editing.rich_transaction_target, &resolved) &&
        editing_surface_is_rich(&resolved) && resolved.owner) {
        surface = &resolved;
    } else if (state->editing.has_active_surface &&
               editing_surface_is_rich(&state->editing.active_surface) &&
               state->editing.active_surface.owner) {
        surface = &state->editing.active_surface;
    }

    DomNode* doc_root = static_cast<DomNode*>(doc->root);
    if (!surface || !surface->owner) return doc_root;

    DomElement* owner = surface->owner;
    if (!dom_node_is_within_root(static_cast<DomNode*>(owner), doc_root) &&
        owner->id && owner->id[0]) {
        DomElement* live_owner = find_element_by_author_id(doc_root, owner->id);
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
                item.element = dom_element_to_element(dom_elem);

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
                        TemplateEntry* tmpl = template_registry_find(lookup.template_ref);

                        if (tmpl && tmpl->handlers) {
                            for (TemplateHandlerEntry* h = tmpl->handlers; h; h = h->next) {
                                if (strcmp(h->event_name, event_name) == 0) {
                                    log_debug("dispatch_emit: found '%s' handler on parent tmpl=%s",
                                             event_name, tmpl->name ? tmpl->name : tmpl->template_ref);

                                    // invoke parent handler with (parent_source_item, event_data)
                                    call_template_event_handler(h->handler_func,
                                        lookup.source_item, event_data);

                                    // For edit handlers, mark dirty after in-place mutation
                                    if (tmpl->is_edit) {
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
 * visual caret/highlight follows after a transaction. See
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

    UiContext* uicon = g_emit_handler_ctx->evcon ? g_emit_handler_ctx->evcon->ui_context : NULL;
    if (!apply_source_selection_to_doc(uicon, g_emit_handler_ctx->doc, selection)) {
        log_debug("dispatch_set_selection: deferred selection until after rebuild");
    }
    return ItemNull;
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
static bool dispatch_lambda_handler(EventContext* evcon, View* target, const char* event_name,
                                    const InputIntent* intent = nullptr) {
    if (!g_template_registry || g_template_registry->count == 0) {
        return false;
    }

    log_debug("dispatch_lambda_handler: searching for '%s' handler, registry has %d templates",
             event_name, g_template_registry->count);

    // walk up from target through DomNode ancestry
    DomNode* node = static_cast<DomNode*>(target);
#ifndef NDEBUG
    int depth = 0;
#endif
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            DomElement* dom_elem = lam::dom_require_element(node);
            if (!dom_elem->is_synthetic()) {
                // construct Item from native element pointer
                Item result_item;
                result_item.element = dom_element_to_element(dom_elem);

                // reverse lookup: which template produced this element?
                RenderMapLookup lookup;
                if (render_map_reverse_lookup(result_item, &lookup)) {
                    log_debug("dispatch_lambda_handler: reverse lookup hit at depth=%d, tmpl_ref=%s",
                             depth, lookup.template_ref ? lookup.template_ref : "(null)");
                    // find the TemplateEntry by template_ref
                    TemplateEntry* tmpl = template_registry_find(lookup.template_ref);

                    if (tmpl && tmpl->handlers) {
                        // find handler for this event name
                        for (TemplateHandlerEntry* h = tmpl->handlers; h; h = h->next) {
                            if (strcmp(h->event_name, event_name) == 0) {
                                log_debug("dispatch_lambda_handler: invoking '%s' handler on tmpl=%s",
                                         event_name, tmpl->name ? tmpl->name : tmpl->template_ref);

                                using namespace std::chrono;
                                auto t_start = high_resolution_clock::now();

                                // Set up eval context for heap allocation during handler/retransform.
                                // After run_script_mir returns, the thread-local context is stale
                                // (pointed to a stack-local Runner). Restore it from the retained runtime.
                                EvalContext handler_ctx;
                                memset(&handler_ctx, 0, sizeof(handler_ctx));
                                DomDocument* doc = event_context_target_document(evcon);
                                Runtime* rt = doc ? doc->lambda_runtime : nullptr;
                                EvalContext* saved_context = context;
                                Context* saved_input_context = input_context;
                                Context* saved_lambda_rt = _lambda_rt;
                                if (rt && rt->heap) {
                                    handler_ctx.heap = rt->heap;
                                    handler_ctx.name_pool = rt->name_pool;
                                    handler_ctx.pool = rt->reuse_pool ?
                                        rt->reuse_pool : rt->heap->pool;
                                    handler_ctx.type_info = type_info;
                                    // Retained handlers outlive Runner's stack context; bind a
                                    // live side stack before generated code reads `_lambda_rt`.
                                    if (!lambda_side_stack_bind((Context*)&handler_ctx)) {
                                        log_error("lambda event handler: failed to bind side stack");
                                        return true;
                                    }
                                    context = &handler_ctx;
                                    _lambda_rt = (Context*)&handler_ctx;
                                }
                                // Phase 5: Set ui_mode + arena so retransformed body functions
                                // allocate fat DomElements/DomTexts on the result arena.
                                if (rt && rt->ui_mode && rt->result_arena) {
                                    handler_ctx.ui_mode = true;
                                    handler_ctx.arena = rt->result_arena;
                                    input_context = (Context*)&handler_ctx;
                                } else {
                                    // Clear input_context to prevent stale arena access
                                    // during list expansion in retransformed body functions.
                                    input_context = nullptr;
                                }

                                // build event object map: {type, target_class, target_tag, x, y}
                                Item event_item = build_lambda_event_map(doc, target, event_name, evcon, intent);

                                // set up emit context so handlers can call emit()
                                EmitHandlerContext emit_ctx;
                                emit_ctx.doc = doc;
                                emit_ctx.target = target;
                                emit_ctx.model_item = lookup.source_item;
                                emit_ctx.template_ref = lookup.template_ref;
                                emit_ctx.evcon = evcon;
                                emit_ctx.has_pending_selection = false;
                                emit_ctx.pending_selection = ItemNull;
                                EmitHandlerContext* saved_emit_ctx = g_emit_handler_ctx;
                                g_emit_handler_ctx = &emit_ctx;

                                // invoke handler: Item handler(Item model, Item event)
                                call_template_event_handler(h->handler_func,
                                    lookup.source_item, event_item);

                                auto t_handler = high_resolution_clock::now();

                                // restore emit context
                                g_emit_handler_ctx = saved_emit_ctx;

                                // For edit handlers, the model was mutated in-place
                                // via edit_map_update (inline mode). Mark the entry
                                // dirty so retransform re-renders with updated data.
                                if (tmpl->is_edit) {
                                    render_map_mark_dirty(lookup.source_item, lookup.template_ref);
                                }

                                // after handler, check for dirty entries and retransform
                                if (render_map_has_dirty()) {
                                    RetransformResult results[16];
                                    int count = render_map_retransform_with_results(results, 16);
                                    auto t_retransform = high_resolution_clock::now();

                                    // Phase 14: No-op elision — skip rebuild if output unchanged
                                    bool any_changed = false;
                                    int reported = count <= 16 ? count : 16;
                                    for (int i = 0; i < reported; i++) {
                                        if (!item_deep_equal(results[i].old_result, results[i].new_result)) {
                                            any_changed = true;
                                            break;
                                        }
                                    }

                                    if (any_changed) {
                                        // incremental DOM rebuild (falls back to full if map not ready)
                                        rebuild_lambda_doc_incremental(evcon->ui_context, results, reported);
                                    }
                                    auto t_rebuild = high_resolution_clock::now();

                                    using std::chrono::duration;
                                    using std::chrono::duration_cast;
                                    log_info("[TIMING] event dispatch: handler=%.2fms retransform=%.2fms rebuild=%.2fms total=%.2fms%s",
                                        duration<double, std::milli>(t_handler - t_start).count(),
                                        duration<double, std::milli>(t_retransform - t_handler).count(),
                                        duration<double, std::milli>(t_rebuild - t_retransform).count(),
                                        duration<double, std::milli>(t_rebuild - t_start).count(),
                                        any_changed ? "" : " (no-op elided)");
                                } else {
                                    log_info("[TIMING] event dispatch: handler=%.2fms (no dirty entries)",
                                        duration<double, std::milli>(t_handler - t_start).count());
                                }

                                if (emit_ctx.has_pending_selection) {
                                    if (apply_source_selection_to_doc(evcon->ui_context, doc, emit_ctx.pending_selection)) {
                                        log_debug("dispatch_lambda_handler: applied pending source selection");
                                        evcon->need_repaint = true;
                                    } else {
                                        log_debug("dispatch_lambda_handler: pending source selection did not resolve");
                                    }
                                }

                                // restore previous context
                                context = saved_context;
                                input_context = saved_input_context;
                                _lambda_rt = saved_lambda_rt;

                                return true;
                            }
                        }
                        log_debug("dispatch_lambda_handler: tmpl found but no '%s' handler", event_name);
                    }
                }
            }
        }
        node = node->parent;
#ifndef NDEBUG
        depth++;
#endif
    }

    log_debug("dispatch_lambda_handler: no handler found after walking %d levels", depth);
    return false;
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

static bool dispatch_editing_lambda_event(EventContext* evcon, View* target,
                                          const char* type,
                                          const EditingIntent* intent,
                                          void* user) {
    (void)user;
    return dispatch_lambda_handler(evcon, target, type, intent);
}

static bool dispatch_editing_copy_selection(DocState* state,
                                            const char* prefix,
                                            void* user) {
    (void)user;
    return copy_current_selection_to_clipboard(state, prefix);
}

static EditingDispatchHooks dispatch_editing_hooks() {
    EditingDispatchHooks hooks = {};
    hooks.dispatch_input_event = dispatch_editing_input_event;
    hooks.dispatch_lambda_event = dispatch_editing_lambda_event;
    hooks.copy_selection = dispatch_editing_copy_selection;
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

static bool dispatch_rich_consumer_transaction_operation(EventContext* evcon,
                                                         View* target,
                                                         const InputIntent* intent,
                                                         const char* operation) {
    if (!evcon || !target || !intent || intent->type == INPUT_INTENT_NONE) return false;
    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface)) return false;
    if (!editing_surface_is_rich(&surface)) return false;

    EditingDispatchHooks hooks = dispatch_editing_hooks();
    DocState* state = event_context_target_state(evcon);
    event_log_editing_clipboard_intent(state, &surface, intent, nullptr);

    EditingTransaction tx;
    memset(&tx, 0, sizeof(tx));
    tx.surface = &surface;
    tx.intent = intent;
    tx.hooks = &hooks;
    tx.operation = operation ? operation : "consumer";
    tx.dispatch_input_without_mutation = true;
    return editing_run_transaction(evcon, &tx, nullptr, nullptr, nullptr);
}

static bool dispatch_rich_consumer_transaction(EventContext* evcon,
                                               View* target,
                                               const InputIntent* intent) {
    return dispatch_rich_consumer_transaction_operation(evcon, target, intent,
                                                        "consumer");
}

struct RichDefaultTransactionArgs {
    View* fallback_view;
    int fallback_offset;
};

static bool rich_transaction_default_mutate_scoped(
    EventContext* evcon, DocState* state, const EditingSurface* surface,
    const EditingIntent* intent, void* user);

static bool rich_transaction_default_mutate_unscoped(
        EventContext* evcon, DocState* state, const EditingSurface* surface,
        const EditingIntent* intent, void* user) {
    (void)evcon;
    (void)user;
    if (!state || !surface || !surface->owner || !intent ||
        !state->dom_selection || state->dom_selection->range_count == 0) {
        return false;
    }

    bool inserts_text = intent->type == INPUT_INTENT_INSERT_TEXT ||
        intent->type == INPUT_INTENT_INSERT_REPLACEMENT_TEXT ||
        intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT ||
        intent->type == INPUT_INTENT_INSERT_FROM_COMPOSITION ||
        intent->type == INPUT_INTENT_INSERT_FROM_PASTE ||
        intent->type == INPUT_INTENT_INSERT_FROM_PASTE_AS_QUOTATION ||
        intent->type == INPUT_INTENT_INSERT_FROM_YANK ||
        intent->type == INPUT_INTENT_INSERT_FROM_DROP;
    if (!inserts_text || !intent->data) return false;

    DomSelection* selection = state->dom_selection;
    DomRange* range = selection->ranges[0];
    DomNode* owner = static_cast<DomNode*>(surface->owner);
    if (!range || !range->start.node || !range->end.node ||
        !dom_node_is_descendant_of(range->start.node, owner) ||
        !dom_node_is_descendant_of(range->end.node, owner)) {
        return false;
    }

    const char* exception = nullptr;
    bool replaced_selection = !dom_range_collapsed(range);
    if (replaced_selection) {
        if (!dom_range_delete_contents(range, &exception) || exception) {
            log_debug("rich_transaction_default_mutate: delete selection rejected: %s",
                      exception ? exception : "unknown");
            return false;
        }
        // A Range deletion can span CharacterData and child removals. Publish
        // one subtree-level childList mutation so observers reconcile the
        // complete default action instead of seeing only the final insertion.
        js_dom_notify_mutation(DOM_JS_MUTATION_TREE_REPLACE, owner, owner);
    }

    size_t byte_len = strlen(intent->data);
    uint32_t u16_len = tc_utf8_to_utf16_length(
        intent->data, (uint32_t)byte_len);
    DomBoundary insertion = range->start;
    bool inserted = false;
    if (insertion.node->is_text()) {
        DomText* text = lam::dom_require_text(insertion.node);
        const char* old_text = text->text ? text->text : "";
        inserted = dom_text_replace_data_contents(
            state, text, insertion.offset, 0,
            intent->data, byte_len, u16_len);
        if (inserted) {
            js_dom_notify_mutation_detail(DOM_JS_MUTATION_TEXT, text,
                                          text->parent, nullptr, old_text);
            dom_selection_collapse(selection, insertion.node,
                                   insertion.offset + u16_len, &exception);
        }
    } else {
        DomDocument* doc = surface->owner->doc;
        DomText* text = DomText::create_detached_copy(
            doc, intent->data, byte_len);
        inserted = text && dom_range_insert_node(
            range, static_cast<DomNode*>(text), &exception);
        if (inserted) {
            js_dom_notify_mutation(DOM_JS_MUTATION_CHILD_INSERT,
                                   text, text->parent);
            dom_selection_collapse(selection, static_cast<DomNode*>(text),
                                   u16_len, &exception);
        }
    }
    if (!inserted || exception) {
        log_debug("rich_transaction_default_mutate: insertText rejected: %s",
                  exception ? exception : "allocation failed");
        return false;
    }

    // DOM3 retains only narrow plain-text insertion/replacement as a native
    // contenteditable default; structural, clipboard, history, and IME edits
    // are delivered to the script-owned editor without a DOM mutation.
    log_debug("rich_transaction_default_mutate: inserted %zu UTF-8 bytes",
              byte_len);
    return true;
}

static bool dispatch_rich_transaction_defaultable(EventContext* evcon,
                                                  View* target,
                                                  const InputIntent* intent,
                                                  View* fallback_view,
                                                  int fallback_offset) {
    if (!evcon || !target || !intent || intent->type == INPUT_INTENT_NONE) {
        return false;
    }

    EditingSurface surface;
    if (!editing_surface_from_target(target, &surface)) return false;
    if (!editing_surface_is_rich(&surface)) return false;

    EditingDispatchHooks hooks = dispatch_editing_hooks();

    DocState* state = event_context_target_state(evcon);
    event_log_editing_clipboard_intent(state, &surface, intent, nullptr);

    RichDefaultTransactionArgs args;
    args.fallback_view = fallback_view;
    args.fallback_offset = fallback_offset;

    EditingTransaction tx;
    memset(&tx, 0, sizeof(tx));
    tx.surface = &surface;
    tx.intent = intent;
    tx.hooks = &hooks;
    tx.mutate = rich_transaction_default_mutate_scoped;
    tx.mutate_user = &args;
    tx.operation = "default";
    tx.dispatch_input_without_mutation = false;
    tx.mutation_invalidates_layout = true;
    tx.mutation_invalidates_paint = true;

    bool mutated = false;
    return editing_run_transaction(evcon, &tx, nullptr, &mutated, nullptr);
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

static DomElement* find_element_by_author_id(DomNode* node, const char* id) {
    if (!node || !id || !id[0]) return nullptr;
    if (node->node_type == DOM_NODE_ELEMENT) {
        DomElement* elem = lam::dom_require_element(node);
        if (elem->id && strcmp(elem->id, id) == 0) return elem;
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            DomElement* found = find_element_by_author_id(child, id);
            if (found) return found;
        }
    }
    return nullptr;
}

static bool dispatch_rich_select_all_default(EventContext* evcon,
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
        DomElement* live_owner = find_element_by_author_id(
            static_cast<DomNode*>(owner->doc->root), owner->id);
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
        log_debug("dispatch_rich_select_all_default: rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    rich_select_all_sync_descendant_text_controls(state, owner_node);
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("dispatch_rich_select_all_default: restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    event_log_editing_selection(state, &surface, intent, "selectAll", 0, 0);
    return true;
}

static bool rich_select_all_transaction_mutate(EventContext* evcon,
                                               DocState* state,
                                               const EditingSurface* surface,
                                               const EditingIntent* intent,
                                               void* user) {
    (void)surface;
    View* target = (View*)user;
    return dispatch_rich_select_all_default(evcon, state, target, intent);
}

static bool dispatch_rich_select_all_transaction(EventContext* evcon,
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

    EditingDispatchHooks hooks = dispatch_editing_hooks();

    EditingTransaction tx;
    memset(&tx, 0, sizeof(tx));
    tx.surface = &surface;
    tx.intent = intent;
    tx.hooks = &hooks;
    tx.mutate = rich_select_all_transaction_mutate;
    tx.mutate_user = target;
    tx.operation = "selectAll";
    tx.dispatch_input_without_mutation = false;
    tx.mutation_invalidates_paint = true;
    return editing_run_transaction(evcon, &tx, nullptr, nullptr, nullptr);
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

    EditingDispatchHooks hooks = dispatch_editing_hooks();

    uint32_t saved_selection_start = 0;
    uint32_t saved_selection_end = 0;
    uint8_t saved_selection_direction = 0;
    form_control_get_selection(state, target,
                               &saved_selection_start,
                               &saved_selection_end,
                               &saved_selection_direction);

    bool prevented = false;
    editing_dispatch_form_beforeinput(evcon, &surface, &intent, &hooks,
                                      &prevented);
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
            DomElement* live_by_id = find_element_by_author_id(
                static_cast<DomNode*>(elem->doc->root), elem->id);
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
        te_history_input_type_set(input_intent_type_name(input_type));
    bool ok = te_replace_byte_range_no_events(live_elem, state, live_target, start, end,
                                              repl, repl_len);
    te_history_input_type_restore(previous_history_input_type);
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
    DomElement* live_elem = find_element_by_author_id(
        static_cast<DomNode*>(doc->root), id);
    if (!live_elem || !tc_is_text_control(live_elem)) return;
    focus_set(state, static_cast<View*>(live_elem), false);
}

static uint32_t dispatch_form_text_paste(EventContext* evcon, DomElement* elem,
                                         DocState* state, View* target,
                                         const char* text, uint32_t len) {
    if (!evcon || !elem || !state || !target || !text || len == 0) return 0;
    char* sanitized = nullptr;
    uint32_t sanitized_len = 0;
    uint32_t start = 0, end = 0;
    if (!te_prepare_paste_replacement(elem, state, text, len, &sanitized,
                                      &sanitized_len, &start, &end)) {
        return 0;
    }

    EditingSurface surface;
    EditingSurface* surface_ptr = nullptr;
    if (editing_surface_from_target(target, &surface) &&
        editing_surface_is_text_control(&surface)) {
        surface_ptr = &surface;
    }
    event_log_editing_clipboard(state, surface_ptr, "paste", len, 0);

    bool ok = dispatch_form_text_replace(evcon, elem, state, target,
                                         start, end,
                                         sanitized, sanitized_len,
                                         INPUT_INTENT_INSERT_FROM_PASTE);
    mem_free(sanitized);
    return ok ? sanitized_len : 0;
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
    bool editable = !form_control_is_readonly(state, static_cast<View*>(elem)) &&
        !form_control_is_disabled(state, static_cast<View*>(elem));
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

static int form_control_live_value_len_int(DomElement* elem) {
    uint32_t value_len = 0;
    form_control_live_value(elem, &value_len);
    return (int)value_len; // INT_CAST_OK: text-control byte offsets use StateStore int APIs.
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
    editing_dispatch_log_intent(evcon, &surface, &intent);

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
                                         View* focused, bool log_inserted) {
    const char* clip = clipboard_get_text();
    if (clip && *clip) {
        evcon->paste_text = clip;
        dispatch_lambda_handler(evcon, focused, "paste");
        evcon->paste_text = nullptr;
        focused = focus_get(state);
        if (focused && focused->is_element()) {
            DomElement* elem = lam::dom_require_element(focused);
            if (tc_is_text_control(elem)) {
                uint32_t inserted = dispatch_form_text_paste(
                    evcon, elem, state, focused, clip, (uint32_t)strlen(clip));
                if (log_inserted) {
                    log_debug("Textarea paste: %u bytes inserted", inserted);
                }
            }
        }
    }
    evcon->need_repaint = true;
}

static void dispatch_form_modified_delete(EventContext* evcon, DomElement* elem,
                                          DocState* state, View* target,
                                          const char* value, int value_len,
                                          int caret, bool backward,
                                          bool line_backward) {
    uint32_t start = (uint32_t)caret;
    uint32_t end = start;
    InputIntentType intent = INPUT_INTENT_DELETE_WORD_FORWARD;
    if (backward) {
        start = line_backward
            ? te_line_start(value, (uint32_t)value_len, (uint32_t)caret)
            : te_prev_word_byte(value, (uint32_t)value_len, (uint32_t)caret);
        intent = line_backward ? INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD
                               : INPUT_INTENT_DELETE_WORD_BACKWARD;
    } else {
        end = te_next_word_byte(value, (uint32_t)value_len, (uint32_t)caret);
    }
    if (end > start) {
        dispatch_form_text_replace(evcon, elem, state, target, start, end,
                                   nullptr, 0, intent);
    }
    evcon->need_repaint = true;
}

static bool dispatch_form_modified_delete_key(EventContext* evcon,
                                              DomElement* elem,
                                              DocState* state,
                                              View* target,
                                              const char* value,
                                              int value_len,
                                              int caret,
                                              int key,
                                              bool alt,
                                              bool cmd) {
    if ((alt || cmd) && key == RDT_KEY_BACKSPACE) {
        dispatch_form_modified_delete(
            evcon, elem, state, target, value, value_len, caret, true, cmd);
        return true;
    }
    if (alt && key == RDT_KEY_DELETE) {
        dispatch_form_modified_delete(
            evcon, elem, state, target, value, value_len, caret, false, false);
        return true;
    }
    return false;
}

static void dispatch_form_delete_key(EventContext* evcon, DomElement* elem,
                                     DocState* state, View* target,
                                     const char* value, int value_len, int caret,
                                     bool backward, bool had_lambda_keydown,
                                     bool had_keydown_selection,
                                     int keydown_sel_start,
                                     int keydown_sel_end,
                                     bool had_keydown_caret,
                                     int keydown_caret_offset,
                                     bool collapse_lambda_selection) {
    bool editable = !form_control_is_readonly(state, static_cast<View*>(elem)) &&
        !form_control_is_disabled(state, static_cast<View*>(elem));
    if (backward && had_lambda_keydown) {
        int base = had_keydown_caret ? keydown_caret_offset : caret;
        const char* operation = "lambdaDeleteBackward";
        if (collapse_lambda_selection && had_keydown_selection) {
            base = keydown_sel_start;
            operation = "lambdaDeleteSelection";
        }
        if (base < 0) base = 0;
        if (base > value_len) base = value_len;
        if ((collapse_lambda_selection && had_keydown_selection) ||
            (base > 0 && value)) {
            int new_len = form_control_live_value_len_int(elem);
            uint32_t collapse = (uint32_t)(base <= new_len ? base : new_len);
            dispatch_form_caret_collapse(evcon, elem, state, target,
                                         collapse, operation);
        }
    } else if (!had_lambda_keydown && editable) {
        uint32_t start = 0;
        uint32_t end = 0;
        if (had_keydown_selection) {
            start = (uint32_t)keydown_sel_start;
            end = (uint32_t)keydown_sel_end;
        } else if (backward && caret > 0 && value) {
            int previous = caret - 1;
            while (previous > 0 &&
                   ((unsigned char)value[previous] & 0xC0) == 0x80) {
                previous--;
            }
            start = (uint32_t)previous;
            end = (uint32_t)caret;
        } else if (!backward && value && caret < value_len) {
            int next = caret + 1;
            while (next < value_len &&
                   ((unsigned char)value[next] & 0xC0) == 0x80) {
                next++;
            }
            start = (uint32_t)caret;
            end = (uint32_t)next;
        } else {
            start = end = backward ? 0 : (uint32_t)caret;
        }
        if (end > start) {
            dispatch_form_text_replace(
                evcon, elem, state, target, start, end, nullptr, 0,
                backward ? INPUT_INTENT_DELETE_CONTENT_BACKWARD
                         : INPUT_INTENT_DELETE_CONTENT_FORWARD);
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

    int value_len = form_control_live_value_len_int(elem);
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
        return input_type == INPUT_INTENT_HISTORY_UNDO
            ? te_history_undo(elem)
            : te_history_redo(elem);
    }

    InputIntent intent;
    intent.type = input_type;
    intent.data = "";

    EditingDispatchHooks hooks = dispatch_editing_hooks();

    SmTransitionGuard sm_guard(state, SM_FAMILY_FORM_TEXT,
                               SM_EV_FORM_HISTORY, target);
    bool prevented = false;
    editing_dispatch_form_beforeinput(evcon, &surface, &intent, &hooks,
                                      &prevented);
    sm_observe_action(state, SM_ACT_DISPATCH_BEFOREINPUT);
    if (prevented) {
        log_debug("dispatch_form_history: beforeinput prevented inputType=%s",
                  input_intent_type_name(input_type));
        return true;
    }

    uint32_t old_len = event_log_text_len(elem->form ? elem->form->value : nullptr);
    bool did = input_type == INPUT_INTENT_HISTORY_UNDO
        ? te_history_undo(elem)
        : te_history_redo(elem);
    if (did) {
        FormControlProp* form = elem->form;
        uint32_t new_len = event_log_text_len(form ? form->value : nullptr);
        uint32_t selection_start = form ? form->selection_start : 0;
        uint32_t selection_end = form ? form->selection_end : 0;
        EditHistory* history = form ? (EditHistory*)form->history : nullptr;
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
    return dispatch_rich_consumer_transaction(evcon, range_view, &intent);
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
    // cannot remain focused while a rich transaction targets its outer host.
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
    return dispatch_rich_consumer_transaction(evcon, range_view, &intent);
}

static bool dispatch_rich_drop_transaction_at_range(EventContext* evcon,
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
        log_debug("dispatch_rich_drop_transaction_at_range: drop range rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    selection_transition(state, SELECTION_TRANSITION_END_POINTER_SELECTION,
                         nullptr);
    dispatch_rich_selection_snapshot(evcon, state, target, "dropTarget",
                                     &intent);

    return dispatch_rich_consumer_transaction(evcon, target, &intent);
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

    te_ime_begin(elem);
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

    EditingDispatchHooks hooks = dispatch_editing_hooks();

    radiant_dispatch_composition_event(&context.event, context.target,
                                       "compositionupdate",
                                       preedit ? preedit : "");
    bool prevented = false;
    editing_dispatch_form_beforeinput(&context.event, &context.surface, &intent, &hooks,
                                      &prevented);
    if (prevented) {
        log_debug("radiant_dispatch_form_text_ime_update: beforeinput prevented");
        event_log_editing_composition(context.state, context.surface_ptr, &intent,
                                      "update", len, 0, caret_cp);
        return true;
    }
    te_ime_update(elem, preedit, len, caret_cp);
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
        te_ime_commit_finish(elem, committed, len);
        dispatch_form_text_replace(&context.event, elem, context.state, context.target,
                                   start, end, committed, len,
                                   INPUT_INTENT_INSERT_FROM_COMPOSITION);
    } else {
        EditingDispatchHooks hooks = dispatch_editing_hooks();

        bool prevented = false;
        if (intent.type == INPUT_INTENT_DELETE_COMPOSITION_TEXT && context.surface_ptr) {
            editing_dispatch_form_beforeinput(&context.event, context.surface_ptr, &intent,
                                              &hooks, &prevented);
        }
        te_ime_commit_finish(elem, committed, len);
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

    View* target = surface->view ? surface->view : static_cast<View*>(surface->owner);
    if (!target) return false;

    const char* event_name = "compositionupdate";
    const char* phase = "update";
    uint32_t preedit_len = 0;
    uint32_t commit_len = 0;
    if (intent->type == INPUT_INTENT_COMPOSITION_START) {
        event_name = "compositionstart";
        phase = "start";
    } else if (intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT) {
        event_name = "compositionupdate";
        phase = "update";
        preedit_len = event_log_text_len(intent->data);
    } else if (intent->type == INPUT_INTENT_INSERT_FROM_COMPOSITION) {
        event_name = "compositionend";
        phase = "commit";
        commit_len = event_log_text_len(intent->data);
    } else if (intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT) {
        event_name = "compositionend";
        phase = "cancel";
    }

    radiant_dispatch_composition_event(evcon, target, event_name, intent->data);
    DocState* state = event_context_target_state(evcon);
    event_log_editing_composition(state, surface, intent, phase, preedit_len,
                                  commit_len, intent->composition_caret);
    bool composing = intent->type == INPUT_INTENT_COMPOSITION_START ||
        intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT;
    editing_interaction_set_composing(state, surface, composing);
    bool handled = false;
    // Complex rich-host IME reconciliation is outside DOM3. Keep the event
    // stream observable, but leave mutation to the editor model.
    handled = dispatch_rich_consumer_transaction(evcon, target, intent);
    if (handled) {
        evcon->need_repaint = true;
    }
    return true;
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

    if (kind == DOM_JS_MUTATION_STYLE ||
        kind == DOM_JS_MUTATION_STYLE_REPAINT ||
        kind == DOM_JS_MUTATION_TEXT) {
        root->set_styles_resolved(false);
        return;
    }

    clear_cascaded_styles_recursive(static_cast<DomNode*>(root));

    Pool* pool = doc->document_pool;
    CssEngine* css_engine = (CssEngine*)doc->services.cached_css_engine;
    bool epoch_scope = style_epoch_cascade_begin(
        doc, root, css_engine, false);
    if (pool && css_engine && matcher) {
        for (int i = 0; i < doc->stylesheet_count; i++) {
            if (doc->stylesheets[i]) {
                apply_stylesheet_to_dom_tree_fast(root, doc->stylesheets[i],
                                                  matcher, pool, css_engine);
            }
        }
    }

    if (!root->is_synthetic()) {
        apply_inline_styles_to_tree(root, dom_element_to_element(root), pool);
    }
    if (epoch_scope) style_epoch_cascade_end(doc);
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
    SelectorMatcher* matcher = selector_matcher_create(pool);
    state_configure_selector_matcher((DocState*)doc->state, matcher);

    // Clear previously cascaded declarations so removed classes/attributes cannot
    // keep stale winning CSS declarations in specified_style.
    clear_cascaded_styles_recursive(static_cast<DomNode*>(doc->root));

    bool epoch_scope = style_epoch_cascade_begin(
        doc, doc->root, css_engine, false);

    // Apply all cached stylesheets
    for (int i = 0; i < doc->stylesheet_count; i++) {
        if (doc->stylesheets[i]) {
            apply_stylesheet_to_dom_tree_fast(doc->root, doc->stylesheets[i], matcher, pool, css_engine);
        }
    }

    // Re-apply inline style="" attributes
    apply_inline_styles_to_tree(doc->root, doc->html_root, pool);
    if (epoch_scope) style_epoch_cascade_end(doc);

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
    EvalContext  handler_ctx;
    EvalContext* saved_ctx;
    Context*     saved_input_ctx;
    Context*     saved_lambda_rt;
    DomDocument* doc;
    ArrayList*   tmp_type_list;
    bool         active;
} JsCtxScope;

// JIT-compiled JS reads the runtime pool for StringBuf allocation (template
// literals, etc.) via the global `_lambda_rt` (see js_mir_expression_lowering
// jm_transpile_template_literal). It is set during the script/module batch and
// restored afterward, so it is stale when a retained JS event handler fires in
// a later turn — a template literal in that handler would dereference a
// dangling `_lambda_rt->pool`. The JS context scope below must point it at the
// same handler_ctx it installs as `context`.
static bool radiant_js_ctx_enter(JsCtxScope* s, EventContext* evcon) {
    s->active = false;
    s->tmp_type_list = nullptr;
    s->doc = event_context_target_document(evcon);
    if (!s->doc || !s->doc->js.mir_ctx || !s->doc->js.runtime_heap) return false;
    memset(&s->handler_ctx, 0, sizeof(s->handler_ctx));
    Heap* heap = (Heap*)s->doc->js.runtime_heap;
    s->handler_ctx.heap = heap;
    s->handler_ctx.name_pool = (NamePool*)s->doc->js.runtime_name_pool;
    s->handler_ctx.pool = s->doc->js.runtime_pool ?
        (Pool*)s->doc->js.runtime_pool : heap->pool;
    // Allocate a per-dispatch type_list. C-level `js_create_event` callers
    // need a non-NULL type_list so map_rebuild_for_type_change can append
    // freshly-created TypeMaps. Compiled JS handlers swap to their own
    // module type_list via the `_with_tl` wrappers and restore on return,
    // so the per-dispatch list is only used by our C-side construction.
    s->tmp_type_list = arraylist_new(16);
    s->handler_ctx.type_list = s->tmp_type_list;
    s->saved_ctx = context;
    s->saved_input_ctx = input_context;
    s->saved_lambda_rt = _lambda_rt;
    context = &s->handler_ctx;
    input_context = nullptr;
    // JIT'd handler bodies read _lambda_rt->pool for StringBuf allocation;
    // point it at the live handler context so template literals in a retained
    // event handler allocate from the valid runtime pool, not a stale one.
    _lambda_rt = (Context*)&s->handler_ctx;
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
    context = s->saved_ctx;
    input_context = s->saved_input_ctx;
    _lambda_rt = s->saved_lambda_rt;
    if (s->tmp_type_list) {
        arraylist_free(s->tmp_type_list);
        s->tmp_type_list = nullptr;
    }
    post_html_handler_rebuild(evcon, t_start, t_handler);
    s->active = false;
}

struct JsDispatchScope {
    EventContext* evcon;
    JsCtxScope scope;
    std::chrono::high_resolution_clock::time_point t_start;
    bool active;

    JsDispatchScope(EventContext* event_context) {
        evcon = event_context;
        active = false;
        if (radiant_js_ctx_enter(&scope, evcon)) {
            t_start = std::chrono::high_resolution_clock::now();
            active = true;
        }
    }

    ~JsDispatchScope() {
        if (active) radiant_js_ctx_exit(&scope, evcon, t_start);
    }
};

static bool rich_transaction_default_mutate_scoped(
        EventContext* evcon, DocState* state, const EditingSurface* surface,
        const EditingIntent* intent, void* user) {
    DomDocument* doc = surface && surface->owner ? surface->owner->doc : nullptr;
    bool active_page_context = context && js_dom_get_document() == doc;
    if (active_page_context) {
        return rich_transaction_default_mutate_unscoped(
            evcon, state, surface, intent, user);
    }

    // Native contenteditable defaults run after beforeinput dispatch has
    // restored the retained page context. MutationObserver records allocate JS
    // values, so the entire DOM mutation and publication must re-enter that
    // context as one scope rather than notifying from a context-free callback.
    JsDispatchScope mutation_scope(evcon);
    if (!mutation_scope.active) return false;
    return rich_transaction_default_mutate_unscoped(
        evcon, state, surface, intent, user);
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
        context = scope.saved_ctx;
        input_context = scope.saved_input_ctx;
        _lambda_rt = scope.saved_lambda_rt;
        if (scope.tmp_type_list) arraylist_free(scope.tmp_type_list);
        scope.active = false;
    }
}

typedef Item (*RadiantJsEventBuilder)(void* userdata);

static bool radiant_dispatch_built_event(EventContext* evcon, View* target,
                                         RadiantJsEventBuilder build_event,
                                         void* userdata,
                                         bool read_prevented,
                                         bool* dispatched = nullptr) {
    if (dispatched) *dispatched = false;
    DomElement* dom_target = radiant_view_to_dom_element(target);
    if (!dom_target || !build_event) return false;
    JsDispatchScope dispatch_scope(evcon);
    DomDocument* target_doc = event_context_target_document(evcon);
    bool active_batch_context = context && js_dom_get_document() == target_doc;
    // Testdriver and execCommand actions can synchronously re-enter native
    // dispatch while the page's batch context is already active. Such documents
    // do not yet retain js_mir_ctx, so requiring a fresh scope silently drops
    // beforeinput/input instead of reusing the live allocation context.
    if (!dispatch_scope.active && !active_batch_context) return false;
    if (dispatched) *dispatched = true;
    Item ev = build_event(userdata);
    Item target_item = js_dom_wrap_element(dom_target);
    js_dom_dispatch_event(target_item, ev);
    return read_prevented ? js_event_is_default_prevented(ev) : false;
}

/**
 * §7 unification (U-1): dispatch a "mouseover" / "mouseout" / generic mouse
 * event through the JS EventTarget pipeline at the given target view. Returns
 * true if default action should be prevented.
 */
typedef struct {
    const char* type;
    int client_x;
    int client_y;
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
                                         const char* type, int client_x, int client_y,
                                         int button, int buttons,
                                         bool ctrl, bool shift, bool alt, bool meta,
                                         int detail,
                                         bool* dispatched = nullptr)
{
    MouseEventBuildArgs args = {
        type, client_x, client_y, button, buttons,
        ctrl, shift, alt, meta, detail, -1.0
    };
    return radiant_dispatch_built_event(evcon, target, build_mouse_event_item,
        &args, true, dispatched);
}

extern "C" bool radiant_dispatch_event_sim_mouse(UiContext* uicon, View* target,
    const char* type, int client_x, int client_y, int button, int buttons,
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
    int client_x;
    int client_y;
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
                                           const char* type, int client_x,
                                           int client_y, int button, int buttons,
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
    const char* type, int client_x, int client_y, int button, int buttons,
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
    int legacy_key_code;
    bool ctrl;
    bool shift;
    bool alt;
    bool meta;
    bool repeat;
} KeyboardEventBuildArgs;

static Item build_keyboard_event_item(void* userdata) {
    KeyboardEventBuildArgs* args = (KeyboardEventBuildArgs*)userdata;
    return js_create_native_keyboard_event(args->type, args->key_name, args->key_name,
        args->legacy_key_code,
        args->ctrl, args->shift, args->alt, args->meta, args->repeat);
}

static bool radiant_dispatch_keyboard_event(EventContext* evcon, View* target,
                                            const char* type, int key_code,
                                            int mods, bool repeat)
{
    const char* key_name = key_code_to_name(key_code);
    KeyboardEventBuildArgs args = {
        type,
        key_name,
        key_code_to_legacy_code(key_code),
        (mods & RDT_MOD_CTRL) != 0,
        (mods & RDT_MOD_SHIFT) != 0,
        (mods & RDT_MOD_ALT) != 0,
        (mods & RDT_MOD_SUPER) != 0,
        repeat
    };
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
    js_property_set(obj, start_key, start);
    js_property_set(obj, end_key,   end);
    js_property_set(obj, so_key,    (Item){.item = i2it((long)r->start.offset)});
    js_property_set(obj, eo_key,    (Item){.item = i2it((long)r->end.offset)});
    bool collapsed = (r->start.node == r->end.node) &&
                     (r->start.offset == r->end.offset);
    js_property_set(obj, col_key, (Item){.item = b2it(collapsed)});
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
        // Non-transaction fallback: compute the StaticRange[] snapshot before
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
        &args, true);
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
        &args, false);
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
    bool prevented = false;
    {
        JsDispatchScope dispatch_scope(&evcon);
        if (!dispatch_scope.active) {
            event_context_cleanup(&evcon);
            return false;
        }
        // event_sim selected the native form control; mirror that into JS DOM
        // selectedness before firing change so handlers reading target.value see it.
        js_dom_select_set_selected_index_bridge((void*)dom_target,
                                                (Item){.item = i2it(selected_index)});
        Item target_item = js_dom_wrap_element(dom_target);
        Item input_ev = js_create_event("input", true, false);
        js_dom_dispatch_event(target_item, input_ev);
        Item change_ev = js_create_event("change", true, false);
        js_dom_dispatch_event(target_item, change_ev);
        prevented = js_event_is_default_prevented(change_ev);
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
        const char* type, int client_x, int client_y);

static bool radiant_dispatch_drag_event(EventContext* evcon, View* target,
                                        const char* type, int cx, int cy)
{
    DomElement* dom_target = radiant_view_to_dom_element(target);
    if (!dom_target) return false;
    JsDispatchScope dispatch_scope(evcon);
    if (!dispatch_scope.active) return false;
    Item target_item = js_dom_wrap_element(dom_target);
    bool prevented = js_dispatch_drag_event_to_element(target_item, type, cx, cy);
    log_debug("JSDND: dispatched '%s' at (%d,%d) prevented=%d", type, cx, cy, prevented);
    return prevented;
}

/**
 * §7 unification (U-5): dispatch a "wheel" event via the JS EventTarget
 * pipeline. Returns true if default action (native scroll) should be
 * suppressed (event.preventDefault()).
 */
typedef struct {
    int client_x;
    int client_y;
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
                                         int client_x, int client_y,
                                         double delta_x, double delta_y,
                                         int mods)
{
    WheelEventBuildArgs args = {client_x, client_y, delta_x, delta_y, mods};
    return radiant_dispatch_built_event(evcon, target, build_wheel_event_item,
        &args, true);
}

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event) {
    memset(evcon, 0, sizeof(EventContext));
    evcon->ui_context = uicon;
    evcon->event = *event;
    evcon->target_document = uicon
        ? event_context_find_focused_document(uicon->document, 0)
        : NULL;
    if (!evcon->target_document && uicon) evcon->target_document = uicon->document;
    // load default font Arial, size 16 px
    setup_font(uicon, &evcon->font, &uicon->default_font);
    evcon->new_cursor = CSS_VALUE_AUTO;
    radiant_document_ensure_state(uicon->document, "event_context_init");
}

void event_context_cleanup(EventContext* evcon) {
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
 * Recursively clear specified_style and styles_resolved flag on every element
 * in the subtree rooted at `node`. Used before re-cascading the document so
 * that declarations from previously matching pseudo-class rules (e.g. :hover)
 * are removed when those rules no longer match.
 */
static void clear_cascaded_styles_recursive(DomNode* node) {
    if (!node) return;
    if (node->is_element()) {
        DomElement* e = lam::dom_require_element(node);
        dom_element_clear(e);
        // Pseudo declarations share the base cascade epoch; otherwise a :hover
        // recascade reads declarations that no longer match.
        dom_element_clear_pseudo_styles(e);
        e->set_styles_resolved(false);
        for (DomNode* c = e->first_child; c; c = c->next_sibling) {
            clear_cascaded_styles_recursive(c);
        }
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
        if (!stylesheet) continue;
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

        bool epoch_scope = style_epoch_cascade_begin(
            doc, doc->root, css_engine, false);

        SelectorMatcher* matcher = selector_matcher_create(pool);
        state_configure_selector_matcher(state, matcher);
        for (int i = 0; i < doc->stylesheet_count; i++) {
            if (doc->stylesheets[i]) {
                apply_stylesheet_to_dom_tree_fast(doc->root, doc->stylesheets[i],
                                                  matcher, pool, css_engine);
            }
        }
        apply_inline_styles_to_tree(doc->root, doc->html_root, pool);
        if (epoch_scope) style_epoch_cascade_end(doc);
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

        reflow_schedule(state, view, REFLOW_SUBTREE, CHANGE_PSEUDO_STATE);

        // Always mark for repaint
        dirty_mark_element(state, view);
        doc_state_mark_dirty(state);
    }
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
    if (elem->tag() != HTM_TAG_INPUT) return false;
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

void radiant_uncheck_radio_group(View* root, const char* name, View* exclude,
                                 DocState* state, bool sync_pseudo) {
    if (!root || !name || !state) return;

    View* current = root;
    while (current) {
        if (current != exclude && is_radio(current)) {
            ViewElement* elem = lam::view_require_element(current);
            const char* elem_name = elem->get_attribute("name");
            if (elem_name && strcmp(elem_name, name) == 0 &&
                form_control_get_checked(state, current)) {
                form_control_uncheck_radio_group_peer(state, current);
                if (sync_pseudo) {
                    sync_pseudo_state(current, PSEUDO_STATE_CHECKED, false);
                }
                log_debug("uncheck_radio_group: unchecked radio name=%s", elem_name);
            }
        }

        if (current->is_element()) {
            ViewElement* ce = lam::view_require_element(current);
            if (ce->first_child) {
                current = static_cast<View*>(ce->first_child);
                continue;
            }
        }
        if (current->next()) {
            current = current->next();
            continue;
        }
        current = current->parent;
        while (current && !current->next()) {
            current = current->parent;
        }
        if (current) current = current->next();
    }
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
            if (elem->tag() == HTM_TAG_LABEL) {
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
            if (search->is_block()) {
                ViewBlock* block = lam::view_require_block(search);
                if (block->first_child) {
                    search = block->first_child;
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
        if (child->is_block()) {
            ViewBlock* block = lam::view_require_block(child);
            View* nested = block->first_child;
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

/**
 * Toggle checkbox or radio button state on click
 * @return true if the click was handled (element was checkbox/radio)
 */
static bool handle_checkbox_radio_click(EventContext* evcon, View* target) {
    // Find the actual checkbox/radio input (may be target or associated via label)
    View* input = find_checkbox_radio_input(target);
    if (!input) return false;

    DocState* state = event_context_target_state(evcon);
    if (!state) return false;

    ViewElement* elem = lam::view_require_element(input);

    // Check if disabled
    if (state_get_pseudo_state(state, input, PSEUDO_STATE_DISABLED)) {
        log_debug("handle_checkbox_radio_click: element is disabled");
        return false;
    }

    if (is_checkbox(input)) {
        // Toggle checkbox state
        bool is_checked = state_get_pseudo_state(state, input, PSEUDO_STATE_CHECKED);
        bool new_state = !is_checked;

        form_control_set_checked(state, input, new_state);
        sync_pseudo_state(input, PSEUDO_STATE_CHECKED, new_state);

        log_debug("handle_checkbox_radio_click: input->is_block()=%d view_type=%d", input->is_block(), input->view_type);

        log_debug("handle_checkbox_radio_click: toggled checkbox to %s", new_state ? "checked" : "unchecked");
        doc_state_request_repaint(state);
        evcon->need_repaint = true;
        return true;
    }

    if (is_radio(input)) {
        // Radio button: only allow checking, not unchecking by click
        // Also need to uncheck other radio buttons in the same name group
        bool is_checked = state_get_pseudo_state(state, input, PSEUDO_STATE_CHECKED);

        if (!is_checked) {
            // Uncheck other radio buttons in the same group
            const char* name = elem->get_attribute("name");
            if (name) {
                // Find the document root
                View* root = input;
                while (root->parent) {
                    root = root->parent;
                }
                radiant_uncheck_radio_group(root, name, input, state, true);
            }

            // Check this radio button through centralized writer API.
            form_control_set_checked(state, input, true);
            sync_pseudo_state(input, PSEUDO_STATE_CHECKED, true);

            log_debug("handle_checkbox_radio_click: checked radio name=%s", name ? name : "(none)");
            doc_state_request_repaint(state);
            evcon->need_repaint = true;
        }
        return true;
    }

    return false;
}

// ============================================================================
// Select Dropdown Handling
// ============================================================================

/**
 * Check if an element is a select dropdown
 */
static bool is_select(View* view) {
    if (!view || !view->is_element()) return false;
    ViewElement* elem = lam::view_require_element(view);
    return elem->tag() == HTM_TAG_SELECT;
}

/**
 * Find the select element from a click target (may be inside the select)
 */
static View* find_select_element(View* target) {
    if (!target) return nullptr;

    View* current = target;
    while (current) {
        if (is_select(current)) return current;
        current = current->parent;
    }
    return nullptr;
}

/**
 * Calculate dropdown popup dimensions
 */
static void calculate_dropdown_dimensions(ViewBlock* select, DocState* state, float scale) {
    if (!select || !state || !select->form) return;

    int option_count = select->form->option_count;
    if (option_count <= 0) option_count = 1;

    // Maximum visible options
    int max_visible = 10;
    int visible_count = (option_count < max_visible) ? option_count : max_visible;

    // Option height based on select height (each option same height as closed select)
    float option_height = select->height;

    // Calculate popup dimensions
    doc_state_set_dropdown_geometry(state, state->dropdown_x, state->dropdown_y,
        select->width * scale, visible_count * option_height * scale);
}

/**
 * Handle click on select to toggle dropdown
 */
static bool handle_select_click(EventContext* evcon, View* target) {
    log_debug("handle_select_click: target=%p, target_tag=%d", (void*)target,
        (target && target->is_element()) ? (lam::view_require_element(target))->tag() : -1);

    View* select_view = find_select_element(target);
    log_debug("handle_select_click: select_view=%p", (void*)select_view);
    if (!select_view) return false;

    DocState* state = event_context_target_state(evcon);
    if (!state) return false;

    ViewBlock* select = lam::view_require_block(select_view);
    bool disabled = !select->form || form_control_is_disabled(state, static_cast<View*>(select));
    log_debug("handle_select_click: select->form=%p, disabled=%d",
        (void*)select->form, disabled ? 1 : 0);
    if (disabled) return false;

    float scale = evcon->ui_context->pixel_ratio > 0 ? evcon->ui_context->pixel_ratio : 1.0f;

    if (state->open_dropdown == select_view) {
        // Close the dropdown
        log_debug("handle_select_click: closing dropdown");
        doc_state_close_dropdown(state, static_cast<View*>(select));
        return true;
    }

    // Close any other open dropdown first
    if (state->open_dropdown) {
        doc_state_close_dropdown(state, state->open_dropdown);
    }

    // Open this dropdown
    log_debug("handle_select_click: opening dropdown with %d options", select->form->option_count);
    doc_state_open_dropdown(state, static_cast<View*>(select));

    float abs_x = 0.0f, abs_y = 0.0f;
    view_to_absolute_position(select_view, select->x, select->y + select->height,
                              0.0f, 0.0f, &abs_x, &abs_y);

    doc_state_set_dropdown_geometry(state, abs_x * scale, abs_y * scale,
        state->dropdown_width, state->dropdown_height);
    calculate_dropdown_dimensions(select, state, scale);
    return true;
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
 * @param mouse_y Mouse Y position in physical pixels
 * @return true if an option was selected
 */
static bool handle_dropdown_option_click(EventContext* evcon, float mouse_x, float mouse_y) {
    DocState* state = nullptr;
    ViewBlock* select = event_open_dropdown_select(evcon, &state);
    if (!select) return false;

    float scale = evcon->ui_context->pixel_ratio > 0 ? evcon->ui_context->pixel_ratio : 1.0f;

    log_debug("handle_dropdown_option_click: mouse=(%.1f, %.1f), dropdown=(%.1f, %.1f, %.1f, %.1f)",
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
    float option_height = select->height * scale;
    int clicked_index = (int)((mouse_y - state->dropdown_y) / option_height);

    log_debug("handle_dropdown_option_click: option_height=%.1f, clicked_index=%d, option_count=%d",
             option_height, clicked_index, select->form->option_count);

    if (clicked_index >= 0 && clicked_index < select->form->option_count) {
        log_debug("handle_dropdown_option_click: selecting option %d", clicked_index);
        form_control_set_selected_index(state, static_cast<View*>(select), clicked_index);

        // Close dropdown
        doc_state_close_dropdown(state, static_cast<View*>(select));
        return true;
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

    float scale = evcon->ui_context->pixel_ratio > 0 ? evcon->ui_context->pixel_ratio : 1.0f;

    // Check if mouse is within dropdown popup
    if (mouse_x < state->dropdown_x || mouse_x > state->dropdown_x + state->dropdown_width ||
        mouse_y < state->dropdown_y || mouse_y > state->dropdown_y + state->dropdown_height) {
        if (form_control_get_hover_index(state, static_cast<View*>(select)) != -1) {
            form_control_set_hover_index(state, static_cast<View*>(select), -1);
        }
        return;
    }

    // Calculate which option is hovered
    float option_height = select->height * scale;
    int hover_index = (int)((mouse_y - state->dropdown_y) / option_height);

    if (hover_index >= 0 && hover_index < select->form->option_count) {
        if (form_control_get_hover_index(state, static_cast<View*>(select)) != hover_index) {
            form_control_set_hover_index(state, static_cast<View*>(select), hover_index);
        }
    }
}

/**
 * Handle keyboard navigation in dropdown
 */
static bool handle_dropdown_key(EventContext* evcon, int key) {
    DocState* state = nullptr;
    ViewBlock* select = event_open_dropdown_select(evcon, &state);
    if (!select) return false;

    int hover = form_control_get_hover_index(state, static_cast<View*>(select));
    int count = select->form->option_count;

    switch (key) {
    case RDT_KEY_UP:
        if (hover > 0) {
            form_control_set_hover_index(state, static_cast<View*>(select), hover - 1);
        }
        return true;

    case RDT_KEY_DOWN:
        if (hover < count - 1) {
            form_control_set_hover_index(state, static_cast<View*>(select), hover + 1);
        }
        return true;

    case RDT_KEY_ENTER:
        if (hover >= 0 && hover < count) {
            form_control_set_selected_index(state, static_cast<View*>(select), hover);
            doc_state_close_dropdown(state, static_cast<View*>(select));
        }
        return true;

    case RDT_KEY_ESCAPE:
        doc_state_close_dropdown(state, static_cast<View*>(select));
        return true;
    }

    return false;
}

/**
 * Close dropdown if clicking outside
 */
static void close_dropdown_if_outside(EventContext* evcon, float mouse_x, float mouse_y) {
    DocState* state = nullptr;
    ViewBlock* select = event_open_dropdown_select(evcon, &state);
    if (!select) return;

    float scale = evcon->ui_context->pixel_ratio > 0 ? evcon->ui_context->pixel_ratio : 1.0f;

    // Calculate select box absolute position
    float select_abs_x = select->x;
    float select_abs_y = select->y;
    View* parent = select->parent;
    while (parent) {
        if (parent->is_block()) {
            ViewBlock* pblock = lam::view_require_block(parent);
            select_abs_x += pblock->x;
            select_abs_y += pblock->y;
        }
        parent = parent->parent;
    }
    select_abs_x *= scale;
    select_abs_y *= scale;
    float select_w = select->width * scale;
    float select_h = select->height * scale;

    // Check if click is on the select itself (toggle handled elsewhere)
    if (mouse_x >= select_abs_x && mouse_x <= select_abs_x + select_w &&
        mouse_y >= select_abs_y && mouse_y <= select_abs_y + select_h) {
        return;  // Click on select box, let handle_select_click deal with it
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
        case HTM_TAG_A:
            // <a> is focusable if it has href
            return elem->get_attribute("href") != NULL;
        case HTM_TAG_BUTTON:
        case HTM_TAG_SELECT:
        case HTM_TAG_TEXTAREA:
            return true;
        case HTM_TAG_INPUT: {
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
    return te_blur_should_dispatch_change(prev_elem);
}

static void dispatch_focus_change_observed(EventContext* evcon, View* target) {
    if (!evcon || !target) return;
    dispatch_lambda_handler(evcon, target, "change");
    radiant_dispatch_simple_event(evcon, target, "change", true, false);
    sm_observe_action(event_context_target_state(evcon),
                      SM_ACT_DISPATCH_CHANGE);
}

static void dispatch_focus_blur_observed(EventContext* evcon,
                                         View* target,
                                         View* related_target) {
    if (!evcon || !target) return;
    dispatch_lambda_handler(evcon, target, "blur");
    radiant_dispatch_focus_event(evcon, target, "blur", related_target);
    sm_observe_action(event_context_target_state(evcon),
                      SM_ACT_DISPATCH_BLUR);
    radiant_dispatch_focus_event(evcon, target, "focusout", related_target);
}

/**
 * Update focus state when an element gains/loses focus
 * @param from_keyboard true if focus change was triggered by keyboard (Tab key, etc.)
 */
void update_focus_state(EventContext* evcon, View* new_focus, bool from_keyboard) {
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
        radiant_dispatch_focus_event(evcon, new_focus, "focusin", prev_focus);

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

// find iframe by name and set new src using selector
DomNode* set_iframe_src_by_name(DomElement *document, const char *target_name, const char *new_src) {
    if (!document || !target_name || !new_src) {
        log_error("Invalid parameters to set_iframe_src_by_name");
        return NULL;
    }
    // get memory pool from document
    Pool* pool = document->doc ? document->doc->document_pool : nullptr;
    if (!pool) {
        log_error("Document has no memory pool");
        return NULL;
    }

    // construct selector string: iframe[name="target_name"]
    char selector_str[256];
    int len = snprintf(selector_str, sizeof(selector_str), "iframe[name=\"%s\"]", target_name);
    if (len < 0 || len >= (int)sizeof(selector_str)) {
        log_error("Selector string too long");
        return NULL;
    }

    log_debug("parsing iframe selector: %s", selector_str);
    // tokenize the selector
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(selector_str, (size_t)len, pool, &token_count);
    if (!tokens || token_count == 0) {
        log_error("Failed to tokenize selector");
        return NULL;
    }
    // parse the selector
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
    if (!selector) {
        log_error("Failed to parse selector");
        return NULL;
    }
    // create selector matcher
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) {
        log_error("Failed to create selector matcher");
        return NULL;
    }
    state_configure_selector_matcher(document->doc ? (DocState*)document->doc->state : nullptr, matcher);

    // find the iframe element matching the selector
    DomElement* iframe_element = selector_matcher_find_first(matcher, selector, document);
    if (iframe_element) {
        log_debug("Found iframe with name='%s', setting src to: %s", target_name, new_src);
        // set the src attribute
        if (!iframe_element->set_attribute("src", new_src)) {
            log_error("Failed to set src attribute");
            selector_matcher_destroy(matcher);
            return NULL;
        }
        log_debug("iframe src attribute set successfully");
        selector_matcher_destroy(matcher);
        return iframe_element;  // Return DomElement* (which is a DomNodeBase*)
    }

    log_debug("No iframe found with name='%s'", target_name);
    selector_matcher_destroy(matcher);
    return NULL;
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

// find a DomElement by its id attribute (for fragment navigation)
static DomElement* find_element_by_id(DomElement* root, const char* id) {
    if (!root || !id) return nullptr;
    const char* elem_id = root->get_attribute("id");
    if (elem_id && strcmp(elem_id, id) == 0) return root;
    for (DomNode* child_node = root->first_child; child_node; child_node = child_node->next_sibling) {
        if (!child_node->is_element()) continue;
        DomElement* found = find_element_by_id(child_node->as_element(), id);
        if (found) return found;
    }
    return nullptr;
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
    GlyphInfo glyph = font_get_glyph(font->font_handle, codepoint);
    if (glyph.id == 0) return false;
    *out_advance = glyph.advance_x;
    return true;
}

/**
 * Calculate character offset from mouse click position within a text rect
 * Returns the byte offset closest to the click position, aligned to UTF-8 character boundaries
 */
int calculate_char_offset_from_position(EventContext* evcon, ViewText* text,
    TextRect* rect, int mouse_x, int mouse_y) {
    unsigned char* str = text->text_data();
    float x = evcon->block.x + rect->x;

    unsigned char* p = str + rect->start_index;
    EventTextRun run = event_text_run(text, rect);
    unsigned char* end = run.end;
    int byte_offset = rect->start_index;  // track byte offset for return value

    float pixel_ratio = (evcon->ui_context && evcon->ui_context->pixel_ratio > 0)
        ? evcon->ui_context->pixel_ratio : 1.0f;

    // Get letter-spacing and word-spacing from font style (same as used in layout)
    float letter_spacing = evcon->font.style ? evcon->font.style->letter_spacing : 0.0f;
    float word_spacing = evcon->font.style ? evcon->font.style->word_spacing : 0.0f;

    bool has_space = false;

    if (run.is_pdf && run.pdf_width > 0.0f) {
        float visible_width = pdf_text_run_visible_natural_width(evcon, rect, run.pdf_copy_space);
        if (visible_width > 0.0f) {
            float local_pdf_x = (float)mouse_x - x;
            if (local_pdf_x <= 0.0f) return rect->start_index;
            if (local_pdf_x >= run.pdf_width) return run.visible_end_offset;
            mouse_x = (int)(x + (local_pdf_x * visible_width / run.pdf_width)); // INT_CAST_OK: event mouse coordinate API is integer-based
        }
    }

    log_debug("calculate_char_offset: mouse_x=%d, start x=%.1f, rect.width=%.1f, rect.length=%d, block.x=%.1f, rect.x=%.1f",
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
            LoadedGlyph* glyph = font_load_glyph(evcon->font.font_handle, &_sd, codepoint, false);
            if (!glyph) {
                log_error("Could not load codepoint U+%04X", codepoint);
                p += bytes;
                byte_offset += bytes;
                continue;
            }
            wd = glyph->advance_x / pixel_ratio;
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
    float pixel_ratio = (evcon->ui_context && evcon->ui_context->pixel_ratio > 0)
        ? evcon->ui_context->pixel_ratio : 1.0f;
    bool has_space = false;

    // Debug: log initial state
    log_debug("[CALC-POS] target_offset=%d, rect->x=%.1f, rect->start_index=%d, pixel_ratio=%.1f, y_ppem=%d",
        target_offset, rect->x, rect->start_index, pixel_ratio,
        evcon->font.font_handle ? (int)font_handle_get_physical_size_px(evcon->font.font_handle) : -1);

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
    if (!fbox.font_handle || !fbox.style) return rect->x;

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
    if (!fbox.font_handle || !fbox.style) return rect->start_index;

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
    // use canonical StateStore selection boundaries or editing transactions.
    // ------------------------------------------------------------------

    // find target view based on mouse position
    int mouse_x, mouse_y;
    switch (event->type) {
    case RDT_EVENT_MOUSE_MOVE: {
        MousePositionEvent* motion = &event->mouse_position;
        log_debug("Mouse event at (%d, %d)", motion->x, motion->y);
        mouse_x = motion->x;  mouse_y = motion->y;
        target_html_doc(&evcon, doc->view_tree);
        event_log_hit_target(cascade_log, cascade_id, &evcon);

        // Update hover state based on new target
        update_hover_state(&evcon, evcon.target);

        // Update dropdown hover if open
        update_dropdown_hover(&evcon, (float)mouse_x, (float)mouse_y);

        if (evcon.target) {
            log_debug("Target view found at position (%d, %d)", mouse_x, mouse_y);
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
            log_debug("No target view found at position (%d, %d)", mouse_x, mouse_y);
        }

        // fire drag event if dragging in progress
        DocState* state = event_context_target_state(&evcon);

        // Handle element drag-and-drop
        if (state && state->drag_drop && (state->drag_drop->pending || state->drag_drop->active)) {
            DragDropState* dd = state->drag_drop;
            DragTransitionArgs motion_args = { .x = (float)motion->x, .y = (float)motion->y };
            drag_transition(state, DRAG_TRANSITION_UPDATE_DROP_MOTION, &motion_args);

            if (dd->pending && !dd->active) {
                // check movement threshold (5px in physical pixels)
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
                                                (int)dd->current_x, (int)dd->current_y);
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
                        (int)motion->x, (int)motion->y);
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
                    log_debug("[TA DRAG] motion=(%d,%d) char_offset=%d",
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
        log_debug("Mouse button event (%d, %d)", btn_event->x, btn_event->y);
        mouse_x = btn_event->x;  mouse_y = btn_event->y; // changed to use btn_event's y
        target_html_doc(&evcon, doc->view_tree);
        event_log_hit_target(cascade_log, cascade_id, &evcon);

        // Forward mouse button events to layer-mode webview
        if (evcon.target && evcon.target->is_element()) {
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
        // Right-click on a text control opens the native context menu.
        if (event->type == RDT_EVENT_MOUSE_DOWN &&
            btn_event->button == GLFW_MOUSE_BUTTON_RIGHT &&
            state && evcon.target && evcon.target->is_element()) {
            DomElement* hit = lam::dom_require_element(evcon.target);
            if (tc_is_text_control(hit)) {
                context_menu_open(state, evcon.target,
                    (float)btn_event->x, (float)btn_event->y);
                break;
            }
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

            dispatch_lambda_handler(&evcon, evcon.target, "mousedown");
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
                DomText* fallback_text = editing_rich_find_text_descendant(
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
                    log_info("[CARET FINAL] mouse=(%d,%d) local=(%.1f,%.1f) render=(%.1f,%.1f) offset=%d block=(%.1f,%.1f) rect=(%.1f,%.1f %.1fx%.1f)",
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
                if (target_elem->form_control() &&
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

                } else if (target_elem->form_control() &&
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
                        (int)btn_event->x, (int)btn_event->y);
                    if (ov) {
                        radiant_dispatch_drag_event(&evcon,
                            static_cast<View*>(evcon.target), "drop",
                            (int)btn_event->x, (int)btn_event->y);
                    }
                }
                if (drag_src) {
                    radiant_dispatch_drag_event(&evcon, drag_src,
                        "dragend", (int)btn_event->x, (int)btn_event->y);
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

                        // CE-5 / ED2-2: lower drop-on-editable to
                        // beforeinput {insertFromDrop}. When dragover stored
                        // a DOM target range, run the same defaultable rich
                        // transaction path used by simulated text drop.
                        if (editing_host_lookup(static_cast<DomNode*>(dd->drop_target),
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
                                dispatch_rich_consumer_transaction(&evcon, dd->source_view, &del);
                            }
                            bool inserted = dd->has_drop_range &&
                                dispatch_rich_drop_transaction_at_range(&evcon,
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
                                dispatch_rich_consumer_transaction(&evcon, dd->drop_target, &ins);
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
            if (state && state->open_dropdown) {
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

            // Only process other click handlers if dropdown wasn't involved and not a drag
            if (!dropdown_handled && !drag_handled && !text_selection_drag_handled) {
                // Dispatch click through JS EventTarget before built-in default
                // actions so listeners or IDL handlers can call preventDefault().
                bool js_click_dispatched = false;
                View* click_check_radio = evcon.target
                    ? find_checkbox_radio_input(evcon.target) : nullptr;
                DocState* click_check_radio_state = click_check_radio
                    ? event_context_target_state(&evcon) : nullptr;
                bool click_check_radio_had_state = click_check_radio &&
                    click_check_radio_state;
                bool click_check_radio_before = click_check_radio_had_state
                    ? state_get_pseudo_state(click_check_radio_state,
                                             click_check_radio,
                                             PSEUDO_STATE_CHECKED)
                    : false;
                if (evcon.target) {
                    bool prevented = radiant_dispatch_mouse_event(&evcon, evcon.target,
                        "click", mouse_x, mouse_y,
                        btn_event->button, 0,
                        event_mod_ctrl(btn_event->mods),
                        event_mod_shift(btn_event->mods),
                        event_mod_alt(btn_event->mods),
                        event_mod_super(btn_event->mods),
                        1,
                        &js_click_dispatched);
                    if (prevented) evcon.default_prevented = true;
                }

                // Handle checkbox/radio click toggle
                log_debug("MOUSE_UP: evcon.target=%p", evcon.target);
                bool click_check_radio_changed = false;
                if (click_check_radio_had_state) {
                    bool after = state_get_pseudo_state(click_check_radio_state,
                                                        click_check_radio,
                                                        PSEUDO_STATE_CHECKED);
                    click_check_radio_changed = after != click_check_radio_before;
                    if (evcon.default_prevented && click_check_radio_changed) {
                        form_control_set_checked(click_check_radio_state,
                                                 click_check_radio,
                                                 click_check_radio_before);
                        sync_pseudo_state(click_check_radio,
                                          PSEUDO_STATE_CHECKED,
                                          click_check_radio_before);
                        doc_state_request_repaint(click_check_radio_state);
                        click_check_radio_changed = false;
                    }
                }
                if (evcon.target && !evcon.default_prevented &&
                    (!js_click_dispatched || (click_check_radio && !click_check_radio_changed))) {
                    handle_checkbox_radio_click(&evcon, evcon.target);
                }

                // Handle click on select element to toggle dropdown
                if (evcon.target && !evcon.default_prevented) {
                    handle_select_click(&evcon, evcon.target);
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

                // Dispatch to Lambda template event handlers
                if (evcon.target) {
                    if (dispatch_lambda_handler(&evcon, evcon.target, "click")) {
                        evcon.need_repaint = true;
                    }
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
            log_debug("Target view found at position (%d, %d)", mouse_x, mouse_y);
            if (evcon.event.type == RDT_EVENT_MOUSE_UP) {
                dispatch_lambda_handler(&evcon, evcon.target, "mouseup");
            }
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            log_debug("No target view found at position (%d, %d)", mouse_x, mouse_y);
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
                DomElement* target_elem = find_element_by_id(doc->root, fragment_id);
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
                // find iframe with the target name
                DomNode* elmt = set_iframe_src_by_name(doc->root, evcon.new_target, evcon.new_url);
                View* iframe = find_view(doc->view_tree->root, elmt);
                if (iframe) {
                    log_debug("found iframe view");
                    if ((iframe->view_type == RDT_VIEW_BLOCK || iframe->view_type == RDT_VIEW_INLINE_BLOCK) && (lam::view_require_block(iframe))->embed) {
                        log_debug("updating doc of iframe view");
                        ViewBlock* block = lam::view_require_block(iframe);
                        // reset scroll position
                        if (block->scroller && block->scroll_mut()->pane) {
                            block->scroll()->pane->reset();
                            block->content_width = 0;  block->content_height = 0;
                        }
                        // load the new document
                        // Use iframe dimensions as viewport (already in CSS logical pixels)
                        int css_vw = (int)block->width;
                        int css_vh = (int)block->height;
                        DomDocument* old_doc = block->embedp()->doc;
                        if (evcon.ui_context->font_ctx) {
                            // Glyph-cache keys retain raw document FontHandle addresses; clear
                            // them before iframe navigation can free and reuse those addresses.
                            font_context_reset_glyph_caches(evcon.ui_context->font_ctx);
                        }
                        DomDocument* new_doc = block->embed->doc =
                            load_html_doc(evcon.ui_context->document->url, evcon.new_url, css_vw, css_vh,
                                          1.0f);  // Layout uses CSS pixels, pixel_ratio not needed
                        if (new_doc) {
                            radiant_document_ensure_state(new_doc, "iframe_target_navigation");
                            // Set scale for nested document
                            // Iframe content uses default scale (1.0), combined with display pixel_ratio
                            new_doc->viewport.given_scale = 1.0f;
                            new_doc->viewport.scale = new_doc->viewport.given_scale * evcon.ui_context->pixel_ratio;

                            if (new_doc->html_root) {
                                // HTML/Markdown/XML documents: need CSS layout
                                // Save parent document and window dimensions
                                DomDocument* parent_doc = evcon.ui_context->document;
                                float saved_window_width = evcon.ui_context->window_width;
                                float saved_window_height = evcon.ui_context->window_height;
                                // Set document context to iframe doc for proper URL resolution (e.g., images)
                                evcon.ui_context->document = new_doc;
                                // iframe dimensions are now in CSS pixels
                                int saved_viewport_width = evcon.ui_context->viewport_width;
                                int saved_viewport_height = evcon.ui_context->viewport_height;
                                evcon.ui_context->window_width = (float)css_vw;
                                evcon.ui_context->window_height = (float)css_vh;
                                evcon.ui_context->viewport_width = css_vw;
                                evcon.ui_context->viewport_height = css_vh;
                                // Process @font-face rules before layout (critical for custom fonts)
                                process_document_font_faces(evcon.ui_context, new_doc);
                                layout_html_doc(evcon.ui_context, new_doc, false);
                                // Restore parent document and window/viewport dimensions
                                evcon.ui_context->document = parent_doc;
                                evcon.ui_context->window_width = saved_window_width;
                                evcon.ui_context->window_height = saved_window_height;
                                evcon.ui_context->viewport_width = saved_viewport_width;
                                evcon.ui_context->viewport_height = saved_viewport_height;
                            }
                            // For pre-laid-out documents, view_tree is already set
                            if (new_doc->view_tree && new_doc->view_tree->root) {
                                ViewBlock* root = lam::view_require_block(new_doc->view_tree->root);
                                // Disable inner doc's viewport scroller — iframe container handles scrolling
                                if (root->scroller) {
                                    if (root->content_height > root->height) {
                                        root->height = root->content_height;
                                    }
                                    root->scroller = NULL;
                                }
                                // Use width/height for PDF (content_width/height may be 0)
                                block->content_width = root->content_width > 0 ? root->content_width : root->width;
                                block->content_height = root->content_height > 0 ? root->content_height : root->height;
                                update_scroller(block, block->content_width, block->content_height);
                            }
                        }
                        clear_document_interaction_state_before_detach(old_doc);
                        free_document(old_doc);
                        doc_state_mark_dirty(uicon->document->state);
                    }
                    else {
                        log_debug("iframe view has no embed");
                    }
                } else {
                    log_debug("failed to find iframe view");
                }
            }
            else {
                // -- Main page navigation: route through browsing session for history management --
                int css_vw = evcon.ui_context->viewport_width;
                int css_vh = evcon.ui_context->viewport_height;
                BrowsingSession* session = evcon.ui_context->browsing_session;
                DomDocument* new_doc = nullptr;
                if (session) {
                    // save current scroll position in history
                    ViewBlock* root_block = doc->view_tree ? lam::view_require_block(doc->view_tree->root) : nullptr;
                    if (root_block && root_block->scroller && root_block->scroll_mut()->pane) {
                        float scroll_y = 0.0f;
                        scroll_state_get_position_for_view(state, static_cast<View*>(root_block), root_block->scroll()->pane,
                                                           NULL, &scroll_y, NULL, NULL);
                        session_save_scroll_position(session, scroll_y);
                    }
                    log_info("browse_nav: navigating via session to %s", new_url);
                    new_doc = session_navigate(session, evcon.ui_context, new_url, css_vw, css_vh);
                } else {
                    // no session (local file, headless), fallback to direct navigation
                    log_info("browse_nav: no session, navigating directly to %s", new_url);
                    DomDocument* old_doc = evcon.ui_context->document;
                    new_doc = show_html_doc(evcon.ui_context->document->url, (char*)new_url, css_vw, css_vh);
                    free_document(old_doc);
                }
                if (new_doc) {
                    // update window title from <title> tag
                    const char* page_title = session ? session_current_title(session) : nullptr;
                    if (!page_title) page_title = session_extract_title(new_doc);
                    if (page_title) {
                        char title_buf[512];
                        snprintf(title_buf, sizeof(title_buf), "Lambda - %s", page_title);
                        update_window_title(title_buf);
                    }
                }
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
        if (evcon.target && evcon.target->is_element()) {
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
            log_debug("Target view found at position (%d, %d)", mouse_x, mouse_y);
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
            log_debug("No target view found at position (%d, %d)", mouse_x, mouse_y);
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
            if (handle_dropdown_key(&evcon, key_event->key)) {
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

        // Clipboard on a rich/contenteditable surface: fire the JS 'paste'/'copy'
        // ClipboardEvent (with a store-backed clipboardData) so a script-owned
        // editor's addEventListener('paste'|'copy') handler runs — browsers fire
        // these on Cmd/Ctrl+V and Cmd/Ctrl+C. If the handler preventDefault()s
        // (the editor performs the paste/copy itself), stop here; otherwise fall
        // through to the native default (rich paste transaction / selection copy).
        if ((key_event->mods & (RDT_MOD_SUPER | RDT_MOD_CTRL)) &&
            (key_event->key == RDT_KEY_V || key_event->key == RDT_KEY_C)) {
            EditingSurface clip_surface;
            if (intent_target &&
                editing_surface_from_target(intent_target, &clip_surface) &&
                editing_surface_is_rich(&clip_surface) && focused) {
                const char* clip_type = key_event->key == RDT_KEY_V ? "paste" : "copy";
                if (radiant_dispatch_clipboard_event(&evcon, focused, clip_type)) {
                    evcon.default_prevented = true;
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
            if (tag == HTM_TAG_INPUT && key_event->key == RDT_KEY_SPACE) {
                if (is_checkbox(focused) || is_radio(focused)) {
                    bool js_click_dispatched = false;
                    radiant_dispatch_mouse_event(&evcon, focused, "click",
                        0, 0, 0, 0, false, false, false, false, 1,
                        &js_click_dispatched);
                    if (!js_click_dispatched && !evcon.default_prevented) {
                        handle_checkbox_radio_click(&evcon, focused);
                    }
                    handled = true;
                }
            } else if (tag == HTM_TAG_BUTTON) {
                // Disabled buttons are inert.
                DomElement* delem = lam::dom_require_element(focused);
                bool disabled = delem->form_control() && form_control_is_disabled(state, static_cast<View*>(delem));
                if (!disabled) {
                    radiant_dispatch_mouse_event(&evcon, focused, "click",
                        0, 0, 0, 0, false, false, false, false, 1);
                    handled = true;
                }
            } else if (tag == HTM_TAG_SELECT) {
                // Space / Enter on a focused <select> opens (or toggles)
                // the dropdown popup, matching native browser behavior.
                DomElement* delem = lam::dom_require_element(focused);
                bool disabled = delem->form_control() && form_control_is_disabled(state, static_cast<View*>(delem));
                if (!disabled) {
                    handled = handle_select_click(&evcon, focused);
                }
            }
            if (handled) {
                evcon.need_repaint = true;
                break;
            }
        }

        // capture selection state before dispatch (needed for caret adjustment after)
        bool had_keydown_selection = false;
        int keydown_sel_start = 0;
        int keydown_sel_end_capture = 0;
        if (selection_has(state)) {
            had_keydown_selection = true;
            selection_get_range(state, &keydown_sel_start, &keydown_sel_end_capture);
        }
        int keydown_caret_offset = 0;
        bool had_keydown_caret = caret_get_offset(state, &keydown_caret_offset);

        // Rich-text editing path (Phase R4): translate platform key events
        // into browser-like beforeinput intents for data-editable/contenteditable
        // template output. Native form controls continue down the existing
        // text-control path.
        {
            InputIntent intent;
            if (intent_target && input_intent_from_key_event(key_event, &intent)) {
                if (intent.type == INPUT_INTENT_SELECT_ALL) {
                    EditingSurface surface;
                    View* rich_select_all_target =
                        rich_keyboard_target_from_selection(state, intent_target,
                                                            &surface);
                    if (rich_select_all_target) {
                        dispatch_rich_select_all_transaction(&evcon, state,
                            rich_select_all_target, &intent);
                        evcon.need_repaint = true;
                        break;
                    }
                }
                bool handled = false;
                handled = dispatch_rich_consumer_transaction(
                    &evcon, intent_target, &intent);
                if (handled) {
                    evcon.need_repaint = true;
                    break;
                }
            }
        }

        // dispatch "keydown" event to Lambda handler for actionable keys
        bool had_lambda_keydown = false;
        if (focused && (key_event->key == RDT_KEY_BACKSPACE ||
                        key_event->key == RDT_KEY_DELETE ||
                        key_event->key == RDT_KEY_ENTER ||
                        key_event->key == RDT_KEY_ESCAPE)) {
            if (dispatch_lambda_handler(&evcon, focused, "keydown")) {
                evcon.need_repaint = true;
                had_lambda_keydown = true;
            }
            // Re-fetch focused element (dispatch may have rebuilt the DOM)
            focused = focus_get(state);
        }

        // Dispatch keydown through JS EventTarget for inline, IDL, and
        // addEventListener handlers.
        if (focused) {
            bool prevented = radiant_dispatch_keyboard_event(&evcon, focused,
                "keydown", key_event->key, key_event->mods, false);
            if (prevented) evcon.default_prevented = true;
            focused = focus_get(state);
        }

        // Handle arrow keys and caret adjustment for text input form controls
        int form_caret_offset = 0;
        if (focused && focused->is_element() && caret_get_offset(state, &form_caret_offset)) {
            DomElement* focus_elem = lam::dom_require_element(focused);
            if (focus_elem->form_control() &&
                focus_elem->form->control_type == FORM_CONTROL_TEXT) {

                uint32_t live_value_len = 0;
                const char* value = form_control_live_value(focus_elem, &live_value_len);
                int value_len = (int)live_value_len; // INT_CAST_OK: text-control byte offsets use StateStore int APIs.
                int cur = form_caret_offset;
                if (cur < 0) cur = 0;
                if (cur > value_len) cur = value_len;
                bool alt = (key_event->mods & RDT_MOD_ALT)   != 0;
                bool ctrl = (key_event->mods & RDT_MOD_CTRL)  != 0;
                bool cmd = (key_event->mods & RDT_MOD_SUPER) != 0;
                bool shift = (key_event->mods & RDT_MOD_SHIFT) != 0;

                // F4: Cmd+Z = undo, Cmd+Shift+Z (or Ctrl+Y) = redo. Bypass
                // the rest of the input-branch dispatch on consume.
                if (cmd && key_event->key == RDT_KEY_Z) {
                    InputIntentType history_type = (key_event->mods & RDT_MOD_SHIFT)
                        ? INPUT_INTENT_HISTORY_REDO
                        : INPUT_INTENT_HISTORY_UNDO;
                    bool did = dispatch_form_history_via_controller(
                        &evcon, focus_elem, state, focused, history_type);
                    if (did) {
                        // Restore caret to the snapshot's selection end.
                        int vlen = form_control_live_value_len_int(focus_elem);
                        int caret_offset = 0;
                        if (caret_get_offset(state, &caret_offset) && caret_offset > vlen) {
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, (uint32_t)vlen, "historyClamp");
                        }
                        evcon.need_repaint = true;
                    }
                    break;
                }
                if ((cmd || (key_event->mods & RDT_MOD_CTRL)) &&
                    key_event->key == RDT_KEY_Y) {
                    if (dispatch_form_history_via_controller(
                            &evcon, focus_elem, state, focused,
                            INPUT_INTENT_HISTORY_REDO)) {
                        int vlen = form_control_live_value_len_int(focus_elem);
                        int caret_offset = 0;
                        if (caret_get_offset(state, &caret_offset) && caret_offset > vlen) {
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, (uint32_t)vlen, "historyClamp");
                        }
                        evcon.need_repaint = true;
                    }
                    break;
                }

                if ((cmd || ctrl) && key_event->key == RDT_KEY_A) {
                    if (dispatch_form_select_all(&evcon, focus_elem, state, focused)) {
                        evcon.need_repaint = true;
                    }
                    break;
                }
                if ((cmd || ctrl) && key_event->key == RDT_KEY_C) {
                    dispatch_form_copy_selection(&evcon, focus_elem, state,
                                                 focused, "form input copy");
                    break;
                }
                if ((cmd || ctrl) && key_event->key == RDT_KEY_X) {
                    if (dispatch_form_cut_selection(&evcon, focus_elem, state,
                                                    focused)) {
                        evcon.need_repaint = true;
                    }
                    break;
                }

                // F6: Cmd+V paste into single-line input. te_paste sanitizes
                // newlines (CR/LF -> space) and clamps to maxlength before
                // delegating to te_replace_byte_range, which fires
                // beforeinput/input and pushes an undo entry. Caret is
                // positioned by te_replace_byte_range.
                if ((cmd || ctrl) && key_event->key == RDT_KEY_V) {
                    // Ctrl+V and Cmd+V are the same primary paste action; limiting
                    // text controls to Cmd bypassed paste on non-macOS testdrivers.
                    dispatch_form_keyboard_paste(&evcon, state, focused, false);
                    break;
                }

                // F3: Alt+Left/Right → word jump using te_prev/next_word_byte
                // on the live UTF-8 buffer; Shift extends from the current
                // text-control anchor through the unified selection helper.
                if (alt && key_event->key == RDT_KEY_LEFT) {
                    uint32_t new_off = te_prev_word_byte(value, (uint32_t)value_len,
                                                         (uint32_t)cur);
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, new_off, shift, "extendWordBackward", "moveWordBackward");
                    evcon.need_repaint = true;
                    break;
                }
                if (alt && key_event->key == RDT_KEY_RIGHT) {
                    uint32_t new_off = te_next_word_byte(value, (uint32_t)value_len,
                                                         (uint32_t)cur);
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, new_off, shift, "extendWordForward", "moveWordForward");
                    evcon.need_repaint = true;
                    break;
                }
                // F3: Cmd+Left == Home, Cmd+Right == End on macOS for
                // single-line inputs.
                if (cmd && key_event->key == RDT_KEY_LEFT) {
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, 0, shift, "extendLineStart", "moveLineStart");
                    evcon.need_repaint = true;
                    break;
                }
                if (cmd && key_event->key == RDT_KEY_RIGHT) {
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, (uint32_t)value_len, shift, "extendLineEnd", "moveLineEnd");
                    evcon.need_repaint = true;
                    break;
                }
                // F3: Up/Down in single-line <input> mirrors Chrome —
                // move caret to start/end of value (no vertical motion).
                if (key_event->key == RDT_KEY_UP) {
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, 0, shift, "extendLineStart", "moveLineStart");
                    evcon.need_repaint = true;
                    break;
                }
                if (key_event->key == RDT_KEY_DOWN) {
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, (uint32_t)value_len, shift, "extendLineEnd", "moveLineEnd");
                    evcon.need_repaint = true;
                    break;
                }
                // F3: Alt+Backspace → delete previous word.
                //     Cmd+Backspace → delete to start of value.
                if (dispatch_form_modified_delete_key(
                        &evcon, focus_elem, state, focused, value, value_len,
                        cur, key_event->key, alt, cmd)) {
                    break;
                }

                if (key_event->key == RDT_KEY_LEFT) {
                    // move caret left by one UTF-8 character
                    if (cur > 0 && value) {
                        int new_off = cur - 1;
                        // walk back to UTF-8 character boundary
                        while (new_off > 0 && ((unsigned char)value[new_off] & 0xC0) == 0x80)
                            new_off--;
                        if (shift) {
                            dispatch_form_selection_extend(&evcon, focus_elem, state,
                                focused, cur, new_off, "extendCharacterBackward");
                        } else {
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, (uint32_t)new_off, "moveCharacterBackward");
                        }
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_RIGHT) {
                    // move caret right by one UTF-8 character
                    if (cur < value_len && value) {
                        uint32_t cp;
                        int bytes = str_utf8_decode(value + cur, (size_t)(value_len - cur), &cp);
                        if (bytes > 0) {
                            int new_off = cur + bytes;
                            if (shift) {
                                dispatch_form_selection_extend(&evcon, focus_elem, state,
                                    focused, cur, new_off, "extendCharacterForward");
                            } else {
                                dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                    focused, (uint32_t)new_off, "moveCharacterForward");
                            }
                        }
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_HOME) {
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, 0, shift, "extendLineStart", "moveLineStart");
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_END) {
                    dispatch_form_navigation(&evcon, focus_elem, state, focused,
                        cur, (uint32_t)value_len, shift, "extendLineEnd", "moveLineEnd");
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_BACKSPACE) {
                    dispatch_form_delete_key(&evcon, focus_elem, state, focused,
                        value, value_len, cur, true, had_lambda_keydown,
                        had_keydown_selection, keydown_sel_start,
                        keydown_sel_end_capture, had_keydown_caret,
                        keydown_caret_offset, false);
                    break;
                } else if (key_event->key == RDT_KEY_DELETE) {
                    dispatch_form_delete_key(&evcon, focus_elem, state, focused,
                        value, value_len, cur, false, had_lambda_keydown,
                        had_keydown_selection, keydown_sel_start,
                        keydown_sel_end_capture, had_keydown_caret,
                        keydown_caret_offset, false);
                    break;
                }
            }
        }

        // Handle arrow keys and caret adjustment for textarea form controls
        int textarea_caret_offset = 0;
        if (focused && focused->is_element() && caret_get_offset(state, &textarea_caret_offset)) {
            DomElement* focus_elem = lam::dom_require_element(focused);
            if (focus_elem->form_control() &&
                focus_elem->form->control_type == FORM_CONTROL_TEXTAREA) {

                uint32_t live_value_len = 0;
                const char* value = form_control_live_value(focus_elem, &live_value_len);
                int value_len = (int)live_value_len; // INT_CAST_OK: text-control byte offsets use StateStore int APIs.
                int cur = textarea_caret_offset;
                if (cur < 0) cur = 0;
                if (cur > value_len) cur = value_len;

                // helper: compute line start offset and line length for a given line
                auto line_start_off = [&](int line) -> int {
                    if (!value || line <= 0) return 0;
                    int ln = 0;
                    for (int i = 0; i < value_len; i++) {
                        if (value[i] == '\n') {
                            ln++;
                            if (ln == line) return i + 1;
                        }
                    }
                    return value_len;
                };

                auto line_len_from = [&](int off) -> int {
                    int i = 0;
                    while (off + i < value_len && value[off + i] != '\n') i++;
                    return i;
                };

                // compute current line and column (byte offset within line)
                int cur_line = 0, cur_col = 0;
                if (value) {
                    for (int i = 0; i < cur && i < value_len; i++) {
                        if (value[i] == '\n') { cur_line++; cur_col = 0; }
                        else cur_col++;
                    }
                }

                // count total lines
                int total_lines = 1;
                if (value) {
                    for (int i = 0; i < value_len; i++) {
                        if (value[i] == '\n') total_lines++;
                    }
                }

                bool shift = (key_event->mods & RDT_MOD_SHIFT) != 0;
                bool cmd = (key_event->mods & RDT_MOD_SUPER) != 0;

                // helper: begin or extend selection for shift-modified keys
                // through the unified form editing surface.
                auto sel_begin_or_extend = [&](int new_off, const char* operation) {
                    dispatch_form_selection_extend(&evcon, focus_elem, state,
                        focused, cur, new_off, operation);
                };

                // Cmd+C: copy selected textarea text to clipboard
                if ((cmd || (key_event->mods & RDT_MOD_CTRL)) &&
                    key_event->key == RDT_KEY_C) {
                    dispatch_form_copy_selection(&evcon, focus_elem, state,
                                                 focused, "textarea copy");
                    break;
                }

                // Cmd/Ctrl+X: cut selected textarea text through the same
                // deleteByCut transaction used by context-menu Cut.
                if ((cmd || (key_event->mods & RDT_MOD_CTRL)) &&
                    key_event->key == RDT_KEY_X) {
                    if (dispatch_form_cut_selection(&evcon, focus_elem, state,
                                                    focused)) {
                        evcon.need_repaint = true;
                    }
                    break;
                }

                // Cmd+V: paste clipboard text into textarea. F6 routes
                // through te_paste so newline normalization (\r\n → \n) and
                // maxlength clamping happen in one place; caret + undo are
                // handled by te_replace_byte_range.
                if ((cmd || (key_event->mods & RDT_MOD_CTRL)) &&
                    key_event->key == RDT_KEY_V) {
                    // Primary paste is platform-neutral even though the physical
                    // modifier differs; both routes share the form transaction.
                    dispatch_form_keyboard_paste(&evcon, state, focused, true);
                    break;
                }

                // Cmd/Ctrl+A: select all textarea text through the shared
                // editing surface so keyboard and context-menu selection share
                // one logging and projection path.
                if ((cmd || (key_event->mods & RDT_MOD_CTRL)) &&
                    key_event->key == RDT_KEY_A) {
                    if (dispatch_form_select_all(&evcon, focus_elem, state, focused)) {
                        evcon.need_repaint = true;
                    }
                    break;
                }

                // F4: Cmd+Z = undo, Cmd+Shift+Z (or Ctrl+Y) = redo.
                if (cmd && key_event->key == RDT_KEY_Z) {
                    InputIntentType history_type = (key_event->mods & RDT_MOD_SHIFT)
                        ? INPUT_INTENT_HISTORY_REDO
                        : INPUT_INTENT_HISTORY_UNDO;
                    bool did = dispatch_form_history_via_controller(
                        &evcon, focus_elem, state, focused, history_type);
                    if (did) {
                        evcon.need_repaint = true;
                    }
                    break;
                }
                if ((cmd || (key_event->mods & RDT_MOD_CTRL)) &&
                    key_event->key == RDT_KEY_Y) {
                    if (dispatch_form_history_via_controller(
                            &evcon, focus_elem, state, focused,
                            INPUT_INTENT_HISTORY_REDO)) {
                        evcon.need_repaint = true;
                    }
                    break;
                }

                bool alt = (key_event->mods & RDT_MOD_ALT) != 0;

                // F3: Alt+Left/Right → word jump (with Shift = extend).
                if (alt && key_event->key == RDT_KEY_LEFT) {
                    int new_off = (int)te_prev_word_byte(
                        value, (uint32_t)value_len, (uint32_t)cur);
                    if (shift) sel_begin_or_extend(new_off, "extendWordBackward");
                    else {
                        dispatch_form_caret_collapse(&evcon, focus_elem, state, focused,
                            (uint32_t)new_off, "moveWordBackward");
                    }
                    evcon.need_repaint = true;
                    break;
                }
                if (alt && key_event->key == RDT_KEY_RIGHT) {
                    int new_off = (int)te_next_word_byte(
                        value, (uint32_t)value_len, (uint32_t)cur);
                    if (shift) sel_begin_or_extend(new_off, "extendWordForward");
                    else {
                        dispatch_form_caret_collapse(&evcon, focus_elem, state, focused,
                            (uint32_t)new_off, "moveWordForward");
                    }
                    evcon.need_repaint = true;
                    break;
                }

                // F3: Alt+Backspace → delete previous word.
                //     Cmd+Backspace → delete to start of current line.
                if (dispatch_form_modified_delete_key(
                        &evcon, focus_elem, state, focused, value, value_len,
                        cur, key_event->key, alt, cmd)) {
                    break;
                }

                // F3: PgUp / PgDn → move caret by ~10 logical lines (no
                // viewport-aware metrics yet; matches the heuristic in the
                // proposal §3.5).
                if (key_event->key == RDT_KEY_PAGE_UP ||
                    key_event->key == RDT_KEY_PAGE_DOWN) {
                    int delta = (key_event->key == RDT_KEY_PAGE_UP) ? -10 : 10;
                    int target_line = cur_line + delta;
                    if (target_line < 0) target_line = 0;
                    if (target_line > total_lines - 1) target_line = total_lines - 1;
                    int loff = line_start_off(target_line);
                    int llen = line_len_from(loff);
                    int target_col = cur_col < llen ? cur_col : llen;
                    int new_off = loff + target_col;
                    if (shift) {
                        sel_begin_or_extend(new_off,
                            key_event->key == RDT_KEY_PAGE_UP
                                ? "extendPageBackward" : "extendPageForward");
                    }
                    else {
                        dispatch_form_caret_collapse(&evcon, focus_elem, state, focused,
                            (uint32_t)new_off,
                            key_event->key == RDT_KEY_PAGE_UP
                                ? "movePageBackward" : "movePageForward");
                    }
                    evcon.need_repaint = true;
                    break;
                }

                if (key_event->key == RDT_KEY_LEFT) {
                    int new_off = cur;
                    if (cur > 0 && value) {
                        new_off = cur - 1;
                        while (new_off > 0 && ((unsigned char)value[new_off] & 0xC0) == 0x80)
                            new_off--;
                    }
                    if (shift) {
                        sel_begin_or_extend(new_off, "extendCharacterBackward");
                    } else {
                        // collapse selection if active, else move caret
                        if (selection_has(state)) {
                            int start = 0, end = 0;
                            selection_get_range(state, &start, &end);
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, (uint32_t)start, "collapseSelectionStart");
                        } else {
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, (uint32_t)new_off, "moveCharacterBackward");
                        }
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_RIGHT) {
                    int new_off = cur;
                    if (cur < value_len && value) {
                        uint32_t cp;
                        int bytes = str_utf8_decode(value + cur, (size_t)(value_len - cur), &cp);
                        if (bytes > 0) new_off = cur + bytes;
                    }
                    if (shift) {
                        sel_begin_or_extend(new_off, "extendCharacterForward");
                    } else {
                        if (selection_has(state)) {
                            int start = 0, end = 0;
                            selection_get_range(state, &start, &end);
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, (uint32_t)end, "collapseSelectionEnd");
                        } else {
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, (uint32_t)new_off, "moveCharacterForward");
                        }
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_UP) {
                    int new_off = cur;
                    if (cur_line > 0) {
                        int prev_line_off = line_start_off(cur_line - 1);
                        int prev_line_len = line_len_from(prev_line_off);
                        int target_col = cur_col < prev_line_len ? cur_col : prev_line_len;
                        new_off = prev_line_off + target_col;
                    }
                    if (shift) {
                        sel_begin_or_extend(new_off, "extendLineBackward");
                    } else {
                        dispatch_form_caret_collapse(&evcon, focus_elem, state,
                            focused, (uint32_t)new_off, "moveLineBackward");
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_DOWN) {
                    int new_off = cur;
                    if (cur_line < total_lines - 1) {
                        int next_line_off = line_start_off(cur_line + 1);
                        int next_line_len = line_len_from(next_line_off);
                        int target_col = cur_col < next_line_len ? cur_col : next_line_len;
                        new_off = next_line_off + target_col;
                    }
                    if (shift) {
                        sel_begin_or_extend(new_off, "extendLineForward");
                    } else {
                        dispatch_form_caret_collapse(&evcon, focus_elem, state,
                            focused, (uint32_t)new_off, "moveLineForward");
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_HOME) {
                    int new_off = shift && cmd ? 0 : line_start_off(cur_line);
                    if (shift) {
                        sel_begin_or_extend(new_off,
                            cmd ? "extendDocumentStart" : "extendLineStart");
                    } else {
                        dispatch_form_caret_collapse(&evcon, focus_elem, state,
                            focused, (uint32_t)new_off,
                            cmd ? "moveDocumentStart" : "moveLineStart");
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_END) {
                    int loff = line_start_off(cur_line);
                    int new_off = shift && cmd ? value_len : loff + line_len_from(loff);
                    if (shift) {
                        sel_begin_or_extend(new_off,
                            cmd ? "extendDocumentEnd" : "extendLineEnd");
                    } else {
                        dispatch_form_caret_collapse(&evcon, focus_elem, state,
                            focused, (uint32_t)new_off,
                            cmd ? "moveDocumentEnd" : "moveLineEnd");
                    }
                    evcon.need_repaint = true;
                    break;
                } else if (key_event->key == RDT_KEY_BACKSPACE) {
                    dispatch_form_delete_key(&evcon, focus_elem, state, focused,
                        value, value_len, cur, true, had_lambda_keydown,
                        had_keydown_selection, keydown_sel_start,
                        keydown_sel_end_capture, had_keydown_caret,
                        keydown_caret_offset, true);
                } else if (key_event->key == RDT_KEY_DELETE) {
                    dispatch_form_delete_key(&evcon, focus_elem, state, focused,
                        value, value_len, cur, false, had_lambda_keydown,
                        had_keydown_selection, keydown_sel_start,
                        keydown_sel_end_capture, had_keydown_caret,
                        keydown_caret_offset, true);
                } else if (key_event->key == RDT_KEY_ENTER) {
                    bool editable = !form_control_is_readonly(state, static_cast<View*>(focus_elem)) &&
                        !form_control_is_disabled(state, static_cast<View*>(focus_elem));
                    if (had_lambda_keydown) {
                        // Lambda handler processed the enter; adjust caret
                        if (had_keydown_selection) {
                            // selection was replaced with '\n': caret goes to sel_start + 1
                            int new_len = form_control_live_value_len_int(focus_elem);
                            int new_off = keydown_sel_start + 1;
                            uint32_t collapse_off = (uint32_t)(new_off <= new_len ? new_off : new_len);
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, collapse_off, "lambdaInsertParagraph");
                        } else {
                            // normal enter: advance caret by 1 byte
                            int new_len = form_control_live_value_len_int(focus_elem);
                            int new_off = (had_keydown_caret ? keydown_caret_offset : cur) + 1;
                            uint32_t collapse_off = (uint32_t)(new_off <= new_len ? new_off : new_len);
                            dispatch_form_caret_collapse(&evcon, focus_elem, state,
                                focused, collapse_off, "lambdaInsertParagraph");
                        }
                    } else if (editable) {
                        // Plain HTML textarea: insert newline ourselves.
                        uint32_t a, b;
                        if (had_keydown_selection) {
                            a = (uint32_t)keydown_sel_start;
                            b = (uint32_t)keydown_sel_end_capture;
                        } else {
                            a = b = (uint32_t)cur;
                        }
                        dispatch_form_text_replace(&evcon, focus_elem, state, focused,
                                                   a, b, "\n", 1,
                                                   INPUT_INTENT_INSERT_PARAGRAPH);
                    }
                    evcon.need_repaint = true;
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

            if (!editing_controller_handle_rich_navigation(&evcon, state,
                    key_event, &controller_hooks) &&
                !caret_in_rich_surface) {
                // Non-rich compatibility branch only. Rich/editable mutation
                // and selection ownership belongs to the intent transaction
                // path above plus editing_controller_handle_rich_navigation().
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
                        // Cut selection (Ctrl+X / Cmd+X)
                        if (ctrl || cmd) {
                            if (selection_has(state)) {
                                copy_current_selection_to_clipboard(state, "legacy cut");

                                // TODO: delete selected text
                                state_store_selection_clear(state);
                                evcon.need_repaint = true;
                            }
                        }
                        break;

                    case RDT_KEY_BACKSPACE:
                        // TODO: delete selection or character before caret
                        evcon.need_repaint = true;
                        break;

                    case RDT_KEY_DELETE:
                        // TODO: delete selection or character after caret
                        evcon.need_repaint = true;
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
                dispatch_rich_transaction_defaultable(&evcon, intent_target,
                    &intent, intent_target, rich_caret_offset)) {
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
                bool editable = !form_control_is_readonly(state, static_cast<View*>(elem)) &&
                    !form_control_is_disabled(state, static_cast<View*>(elem));
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
                        if (dispatch_lambda_handler(&evcon, focused, "input")) {
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

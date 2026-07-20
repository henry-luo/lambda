#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../lambda/lambda-data.hpp"
#include "../lambda/template_registry.h"
#include "../radiant/event.hpp"
#include "../radiant/render.hpp"
#include "../radiant/view.hpp"

TemplateRegistry* g_template_registry = NULL;
UiContext ui_context{};

extern "C" bool js_dom_option_is_selected(void* dom_elem) {
    DomElement* option = (DomElement*)dom_elem;
    if (!option) return false;
    // The standalone state-store target omits LambdaJS; use the native mirror
    // that keeps reconstructed form views independent of a JS EvalContext.
    if (option->has_option_selectedness()) return option->option_selectedness();
    return option->has_attribute("selected");
}

void render_map_record_path(Item source_item, const char* template_ref,
                            const int* path_indices, int depth) {
    (void)source_item;
    (void)template_ref;
    (void)path_indices;
    (void)depth;
}

bool is_view_focusable(View* view) {
    (void)view;
    return false;
}

bool is_view_programmatically_focusable(View* view) {
    // StateStore now validates programmatic focus through the event-owned
    // predicate; this standalone target intentionally has no event subsystem.
    return is_view_focusable(view);
}

void view_pool_release_detached_subtree(DomNode* root) {
    (void)root;
}

void rdt_path_free(RdtPath* path) {
    (void)path;
}

RdtPath* rdt_path_clone(const RdtPath* path) {
    (void)path;
    return NULL;
}

RdtPicture* rdt_picture_dup(RdtPicture* picture) {
    return picture;
}

void rdt_picture_free(RdtPicture* picture) {
    (void)picture;
}

void editing_surface_clear(EditingSurface* out) {
    if (out) memset(out, 0, sizeof(*out));
}

bool editing_surface_from_target(View* target, EditingSurface* out) {
    (void)target;
    editing_surface_clear(out);
    return false;
}

bool editing_surface_from_focus(DocState* state, EditingSurface* out) {
    (void)state;
    editing_surface_clear(out);
    return false;
}

bool editing_surface_is_rich(const EditingSurface* surface) {
    (void)surface;
    return false;
}

bool editing_surface_is_text_control(const EditingSurface* surface) {
    (void)surface;
    return false;
}

const char* editing_surface_kind_name(EditingSurfaceKind kind) {
    (void)kind;
    return "none";
}

const char* editing_mode_name(EditingMode mode) {
    (void)mode;
    return "none";
}

bool tc_is_text_control(DomElement* elem) {
    (void)elem;
    return false;
}

FormControlProp* tc_get_or_create_form(DomElement* elem) {
    (void)elem;
    return NULL;
}

uint32_t tc_utf8_to_utf16_length(const char* s, uint32_t byte_len) {
    (void)s;
    return byte_len;
}

uint32_t tc_utf16_to_utf8_offset(const char* s, uint32_t byte_len, uint32_t u16) {
    (void)s;
    return u16 < byte_len ? u16 : byte_len;
}

uint32_t tc_utf8_to_utf16_offset(const char* s, uint32_t byte_len, uint32_t u8) {
    (void)s;
    return u8 < byte_len ? u8 : byte_len;
}

void tc_ensure_init(DomElement* elem) {
    (void)elem;
}

void tc_set_value(DomElement* elem, const char* new_val, size_t new_len) {
    (void)elem;
    (void)new_val;
    (void)new_len;
}

void tc_set_selection_range(DomElement* elem, uint32_t start, uint32_t end,
                            uint8_t dir) {
    (void)elem;
    (void)start;
    (void)end;
    (void)dir;
}

void tc_notify_selection_changed(DomElement* elem) {
    (void)elem;
}

void tc_sync_selection_to_form(DomElement* elem, DocState* state) {
    (void)elem;
    (void)state;
}

const char* tc_active_selected_text(DocState* state, uint32_t* out_byte_len) {
    (void)state;
    if (out_byte_len) *out_byte_len = 0;
    return NULL;
}

void tc_set_active_element(DocState* state, DomElement* elem) {
    if (state) state->active_text_control = elem;
}

DomElement* tc_get_active_element(DocState* state) {
    return state ? state->active_text_control : NULL;
}

void tc_set_last_focused_text_control(DocState* state, DomElement* elem) {
    if (state) state->last_focused_text_control = elem;
}

DomElement* tc_get_last_focused_text_control(DocState* state) {
    return state ? state->last_focused_text_control : NULL;
}

void tc_reset_focus_state(DocState* state) {
    if (!state) return;
    state->active_text_control = NULL;
    state->last_focused_text_control = NULL;
}

bool te_ime_is_composing(DomElement* elem) {
    (void)elem;
    return false;
}

bool source_pos_from_dom_boundary(const DomBoundary* boundary, SourcePosC* out) {
    (void)boundary;
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

void source_pos_free(SourcePosC* pos) {
    (void)pos;
}

bool dom_boundary_from_source_pos(DomNode* dom_root, const SourcePosC* pos,
                                  DomBoundary* out) {
    (void)dom_root;
    (void)pos;
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

extern "C" const char* glfwGetClipboardString(GLFWwindow* window) {
    (void)window;
    return NULL;
}

extern "C" void glfwSetClipboardString(GLFWwindow* window, const char* string) {
    (void)window;
    (void)string;
}

extern "C" void clipboard_store_write_text(const char* text) {
    (void)text;
}

extern "C" const char* clipboard_store_read_text(void) {
    return NULL;
}

extern "C" void clipboard_store_write_html(const char* html, const char* plain_text) {
    (void)html;
    (void)plain_text;
}

extern "C" bool focus_transition(DocState* state, FocusTransitionKind kind,
                                  FocusTransitionArgs* args) {
    (void)state;
    (void)kind;
    (void)args;
    return false;
}

extern "C" bool caret_transition(DocState* state, CaretTransitionKind kind,
                                  CaretTransitionArgs* args) {
    (void)state;
    (void)kind;
    (void)args;
    return false;
}

extern "C" bool selection_transition(DocState* state,
                                      SelectionTransitionKind kind,
                                      SelectionTransitionArgs* args) {
    (void)state;
    (void)kind;
    (void)args;
    return false;
}

extern "C" bool radiant_state_validate_interaction(DocState* state,
                                                    StateValidationReport* report) {
    (void)state;
    if (report) {
        memset(report, 0, sizeof(*report));
        report->ok = true;
    }
    return true;
}

extern "C" void radiant_state_assert_valid(DocState* state, const char* context) {
    (void)state;
    (void)context;
}

extern "C" void sm_transition_scope_begin(SmTransitionScope* scope,
                                           DocState* state,
                                           SmFamily family,
                                           SmEvent event,
                                           View* target) {
    (void)state;
    (void)family;
    (void)event;
    (void)target;
    if (scope) memset(scope, 0, sizeof(*scope));
}

extern "C" void sm_transition_scope_commit(SmTransitionScope* scope) {
    if (scope) scope->committed = true;
}

extern "C" void sm_transition_scope_end(SmTransitionScope* scope) {
    (void)scope;
}

extern "C" void sm_observe_action(DocState* state, uint32_t action) {
    (void)state;
    (void)action;
}

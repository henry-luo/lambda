#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "lambda/core/data.hpp"
#include "radiant/event.hpp"
#include "radiant/render.hpp"
#include "radiant/view.hpp"

UiContext ui_context{};

// This slice has no style resolver or layout view pool; unexpected use must fail.
DisplayValue resolve_display_value(void*) { abort(); }
void view_tree_release_retired_subtree(ViewTree*, DomNode*) { abort(); }

void radiant_event_host_sync_pseudo_state(View*, uint32_t, bool) {
    // this standalone target has no cascade; native UI fixtures check popup restyling.
}

void radiant_event_host_sync_focus_style(View*, uint32_t) {
    // this standalone target has no cascade; native UI fixtures check focus restyling.
}

void layout_content_visibility_invalidate(DocState* state) {
    // this target has no layout pipeline; relevance projection is tested in view/UI fixtures.
    (void)state;
}

extern "C" bool dom_option_selectedness(void* dom_elem) {
    DomElement* option = (DomElement*)dom_elem;
    if (!option) return false;
    // This standalone target omits LambdaJS and its expando map, so use the
    // native mirror to preserve DOM-owned selectedness without an EvalContext.
    if (option->has_option_selectedness()) return option->option_selectedness();
    return option->has_attribute("selected");
}

// focus eligibility is stubbed here; this target owns no CSSOM cascade.
CssomCascadeQueryScope::CssomCascadeQueryScope() : previous(nullptr), refreshed(nullptr) {}
CssomCascadeQueryScope::~CssomCascadeQueryScope() = default;

bool is_view_programmatically_focusable(View* view) {
    // StateStore tests need the DOM focus contract without linking the event
    // dispatcher; synthetic element fixtures model programmatic focus targets.
    return view && view->is_element();
}

bool radiant_event_host_focusable(View* view, bool programmatic) {
    return programmatic && is_view_programmatically_focusable(view);
}

void view_pool_release_detached_subtree(DomNode* root) {
    (void)root;
}

void view_pool_release_detached_subtree_in_tree(ViewTree*, DomNode* root) {
    view_pool_release_detached_subtree(root);
}

void rdt_path_free(RdtPath* path) {
    (void)path;
}

size_t rdt_path_storage_bytes(const RdtPath*) { return 0; } // stub paths own no geometry buffers

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

// test pictures are neither SVG documents nor ThorVG text
Element* rdt_picture_get_svg_root(RdtPicture* picture) {
    (void)picture;
    return nullptr;
}

bool rdt_picture_is_text(RdtPicture* picture) {
    (void)picture;
    return false;
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

// ESO43 hoisted the undo/redo ring into ViewState, so state_store.cpp frees it
// on detach/prune/destroy during form-state destruction. text_edit.cpp is not
// part of this target, and the ring is never populated here — nothing in these
// tests takes an edit — but the production-owned symbol must still resolve.
void te_history_free(EditHistory* h) {
    (void)h;
}

bool tc_is_text_control(DomElement* elem) {
    (void)elem;
    return false;
}

FormControlProp* tc_get_or_create_form(DomElement* elem) {
    (void)elem;
    return NULL;
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

// state_store.cpp mirrors the OS clipboard through the platform seam
const char* radiant_platform_get_clipboard(RadiantWindow* window) {
    (void)window;
    return NULL;
}

void radiant_platform_set_clipboard(RadiantWindow* window, const char* text) {
    (void)window;
    (void)text;
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

// This target has no input-value mirror; native form state is held by StateStore.
extern "C" const char* radiant_input_peek_live_value(DomElement*) {
    return nullptr;
}

// Containing-block geometry belongs to view_pool, absent from this pool fixture.
// Fail loudly if a future test starts depending on that production-only path.
void view_get_layout_position(View*, TextRect*, float*, float*, View*) {
    abort();
}

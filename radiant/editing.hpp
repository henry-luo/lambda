#ifndef RADIANT_EDITING_HPP
#define RADIANT_EDITING_HPP

// shared editing surface resolver for form text controls and contenteditable.
// canonical design: vibe/radiant/Radiant_Design_Editing2.md (§4.2).
// vibe/radiant/Radiant_Design_Editing.md is phased out (historical E1-E7 record).

#include "../lambda/input/css/dom_node.hpp"

class DomElement;
struct DocState;

enum EditingSurfaceKind {
    EDIT_SURFACE_NONE = 0,
    EDIT_SURFACE_TEXT_CONTROL,
    EDIT_SURFACE_CONTENTEDITABLE,
    EDIT_SURFACE_LAMBDA_TEMPLATE
};

enum EditingMode {
    EDIT_MODE_RICH = 0,
    EDIT_MODE_PLAINTEXT_ONLY,
    EDIT_MODE_SINGLE_LINE_TEXT,
    EDIT_MODE_MULTI_LINE_TEXT,
    EDIT_MODE_PASSWORD_TEXT
};

struct EditingSurface {
    EditingSurfaceKind kind;
    EditingMode mode;
    DomElement* owner;
    View* view;
    bool readonly;
    bool disabled;
    bool target_in_false_island;
};

void editing_surface_clear(EditingSurface* out);

bool editing_surface_from_target(View* target, EditingSurface* out);
bool editing_surface_from_focus(DocState* state, EditingSurface* out);

bool editing_surface_is_rich(const EditingSurface* surface);
bool editing_surface_is_text_control(const EditingSurface* surface);

// Stage 4B Phase 3: a rich editing host explicitly marked `data-script-edit`
// is script-managed — its input events are routed to script handlers (JS
// addEventListener / Lambda `on`) which own the document model and apply every
// edit. The native rich-edit behavior layer is bypassed for such surfaces
// (contenteditable acts purely as a routing flag). Unmarked contenteditable
// keeps the native engine, the Phase-4 parity safety net. Transitional: once
// the native engine is retired (Phase 5) all contenteditable routes to script.
bool editing_surface_is_script_managed(const EditingSurface* surface);

const char* editing_surface_kind_name(EditingSurfaceKind kind);
const char* editing_mode_name(EditingMode mode);

#endif // RADIANT_EDITING_HPP

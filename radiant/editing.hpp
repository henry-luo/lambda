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

const char* editing_surface_kind_name(EditingSurfaceKind kind);
const char* editing_mode_name(EditingMode mode);

// Layer-A helpers (formerly in the retired editing_rich_transaction.cpp):
// `find_text_descendant` backs click-to-place-caret in a rich host;
// `is_composition_intent` classifies IME composition input. Both are pure
// classification/navigation, not editing apply.
#include "editing_intent.hpp"
DomText* editing_rich_find_text_descendant(DomNode* node, bool last);
bool editing_rich_is_composition_intent(const EditingIntent* intent);

#endif // RADIANT_EDITING_HPP

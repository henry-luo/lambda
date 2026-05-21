#ifndef RADIANT_EDITING_HOST_HPP
#define RADIANT_EDITING_HOST_HPP

// EditingHost — central recognition + lookup of `contenteditable` editing
// hosts. See vibe/radiant/Radiant_Design_Content_Editable.md §4.
//
// One concept, one resolver: replaces the ad-hoc `contenteditable` reads
// that used to live in event.cpp (focus / hit-test) and dom_range.cpp
// (selection.modify confinement).

#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

struct EditingHost {
    // Nearest ancestor element with contenteditable="true"|""|"plaintext-only".
    // nullptr if `node` is not inside any editing host.
    DomElement* host;

    enum Mode {
        Rich,            // contenteditable="true" or "" (bool form)
        PlaintextOnly    // contenteditable="plaintext-only"
    } mode;

    // True iff the query node sits inside a contenteditable="false" subtree
    // that is itself nested within `host`. Per HTML spec, ="false" islands
    // are non-editable widgets embedded in an otherwise-editable host. The
    // selection may still cross the boundary; input must no-op inside it.
    bool target_in_false_island;
};

// Resolve the editing host (if any) that contains `node`.
// Returns true and fills `*out` if `node` is inside a host; false otherwise.
// `out` may be nullptr to query existence only.
bool editing_host_lookup(const DomNode* node, EditingHost* out);

// Convenience wrappers.
inline bool node_is_editable(const DomNode* node) {
    EditingHost h;
    if (!editing_host_lookup(node, &h)) return false;
    return !h.target_in_false_island;
}

inline DomElement* editing_host_of(const DomNode* node) {
    EditingHost h;
    return editing_host_lookup(node, &h) ? h.host : nullptr;
}

// HTMLElement.contentEditable / .isContentEditable IDL.
// Returns one of "true", "false", "plaintext-only", "inherit".
// The returned string is a static constant — do not free.
const char* html_element_get_contentEditable(DomElement* element);

// Computed: walks ancestors honouring inheritance and ="false" islands.
bool html_element_get_isContentEditable(DomElement* element);

// Setter: per HTML spec, "true" | "false" | "plaintext-only" | "inherit"
// (case-insensitive). An empty string maps to "inherit". Any other value
// is a SyntaxError — returns false and leaves the attribute unchanged.
// On success, returns true and the attribute is set (or removed for
// "inherit").
bool html_element_set_contentEditable(DomElement* element, const char* value);

#endif // RADIANT_EDITING_HOST_HPP

// editing_host.cpp — central `contenteditable` lookup + IDL.
// See vibe/radiant/Radiant_Design_Content_Editable.md §4.

#include "event.hpp"

#include <string.h>
#include <strings.h>  // strcasecmp

#include "../lib/log.h"

namespace {

// Classify the contenteditable attribute on a single element.
//   has_attr:   element has the attribute at all (after dom mutation it may
//               be present-with-no-value or with a value).
//   is_host:    contenteditable in {true, "", plaintext-only}.
//   is_false:   contenteditable="false" — a non-editable island.
//   mode:       valid only when is_host=true; selects Rich vs PlaintextOnly.
struct CeClass {
    bool has_attr;
    bool is_host;
    bool is_false;
    EditingHost::Mode mode;
};

static CeClass classify_ce_attr(DomElement* e) {
    CeClass c{};
    if (!e) return c;
    if (!dom_element_has_attribute(e, "contenteditable")) return c;
    c.has_attr = true;
    const char* v = dom_element_get_attribute(e, "contenteditable");
    // bool-style: <div contenteditable> -> v is "" or nullptr.
    if (!v || *v == '\0' || strcasecmp(v, "true") == 0) {
        c.is_host = true;
        c.mode = EditingHost::Rich;
        return c;
    }
    if (strcasecmp(v, "plaintext-only") == 0) {
        c.is_host = true;
        c.mode = EditingHost::PlaintextOnly;
        return c;
    }
    if (strcasecmp(v, "false") == 0) {
        c.is_false = true;
        return c;
    }
    // "inherit" or any unknown value: no opinion at this element.
    return c;
}

}  // namespace

bool editing_host_lookup(const DomNode* node, EditingHost* out) {
    if (!node) return false;

    // Walk ancestors. We care about the NEAREST attribute that says
    // something definitive: true|""|plaintext-only|false. "inherit" /
    // unknown / absent => keep walking.
    //
    // Tracks whether, on the path from `node` up to (but not including) the
    // host, we cross any contenteditable="false" boundary. If yes, the
    // query node lives inside a false-island within the host.
    const DomNode* p = node->is_text() ? node->parent : node;
    bool saw_false_below_host = false;

    while (p) {
        if (p->is_element()) {
            DomElement* e = const_cast<DomElement*>(p->as_element());
            CeClass c = classify_ce_attr(e);
            if (c.is_host) {
                if (out) {
                    out->host = e;
                    out->mode = c.mode;
                    out->target_in_false_island = saw_false_below_host;
                }
                return true;
            }
            if (c.is_false) {
                // We are inside a non-editable subtree at this level. If a
                // host exists above, we are inside a false-island within it.
                saw_false_below_host = true;
            }
        }
        p = p->parent;
    }
    return false;
}

const char* html_element_get_contentEditable(DomElement* element) {
    if (!element) return "inherit";
    if (!dom_element_has_attribute(element, "contenteditable")) return "inherit";
    const char* v = dom_element_get_attribute(element, "contenteditable");
    if (!v || *v == '\0' || strcasecmp(v, "true") == 0) return "true";
    if (strcasecmp(v, "false") == 0) return "false";
    if (strcasecmp(v, "plaintext-only") == 0) return "plaintext-only";
    return "inherit";
}

bool html_element_get_isContentEditable(DomElement* element) {
    if (!element) return false;
    EditingHost h;
    if (!editing_host_lookup(element, &h)) return false;
    return !h.target_in_false_island;
}

bool html_element_set_contentEditable(DomElement* element, const char* value) {
    if (!element || !value) return false;

    // Empty string is treated as "inherit" per WHATWG HTML.
    if (*value == '\0' || strcasecmp(value, "inherit") == 0) {
        dom_element_remove_attribute(element, "contenteditable");
        return true;
    }
    if (strcasecmp(value, "true") == 0) {
        return dom_element_set_attribute(element, "contenteditable", "true");
    }
    if (strcasecmp(value, "false") == 0) {
        return dom_element_set_attribute(element, "contenteditable", "false");
    }
    if (strcasecmp(value, "plaintext-only") == 0) {
        return dom_element_set_attribute(element, "contenteditable", "plaintext-only");
    }
    // SyntaxError — log and refuse. Caller (JS bridge) is responsible for
    // raising the DOMException; the C surface returns false.
    log_debug("html_element_set_contentEditable: invalid value '%s'", value);
    return false;
}

#pragma once

#include "../../../lib/str.h"
#include <stddef.h>
#include <stdbool.h>

// numeric order is the existing cascade/CSSOM ABI; keep names and legacy syntax together.
#define CSS_PSEUDO_ELEMENT_IDENTITIES(X) \
    X(BEFORE, "before", true) \
    X(AFTER, "after", true) \
    X(FIRST_LINE, "first-line", true) \
    X(FIRST_LETTER, "first-letter", true) \
    X(SELECTION, "selection", false) \
    X(MARKER, "marker", false) \
    X(PLACEHOLDER, "placeholder", false) \
    X(BACKDROP, "backdrop", false) \
    X(FILE_SELECTOR_BUTTON, "file-selector-button", false) \
    X(FOOTNOTE_CALL, "footnote-call", false) \
    X(FOOTNOTE_MARKER, "footnote-marker", false)

typedef enum PseudoElementType {
    PSEUDO_ELEMENT_NONE = 0,
#define CSS_PSEUDO_ENUM(kind, name, legacy) PSEUDO_ELEMENT_##kind,
    CSS_PSEUDO_ELEMENT_IDENTITIES(CSS_PSEUDO_ENUM)
#undef CSS_PSEUDO_ENUM
} PseudoElementType;

typedef struct CssPseudoElementName {
    const char* name;
    PseudoElementType type;
    bool legacy_colon;
} CssPseudoElementName;

static inline const CssPseudoElementName* css_pseudo_element_lookup(const char* name) {
    if (!name) return NULL;
    static const CssPseudoElementName names[] = {
#define CSS_PSEUDO_NAME(kind, name, legacy) {name, PSEUDO_ELEMENT_##kind, legacy},
        CSS_PSEUDO_ELEMENT_IDENTITIES(CSS_PSEUDO_NAME)
#undef CSS_PSEUDO_NAME
        {"-webkit-input-placeholder", PSEUDO_ELEMENT_PLACEHOLDER, false},
        {"-moz-placeholder", PSEUDO_ELEMENT_PLACEHOLDER, true},
        {"-ms-input-placeholder", PSEUDO_ELEMENT_PLACEHOLDER, true}
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(*names); index++) {
        if (str_ieq_cstr(name, names[index].name)) return &names[index];
    }
    return NULL;
}

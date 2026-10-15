#pragma once

#include <stdint.h>
#include <stdbool.h>

enum CssPointerCapability {
    CSS_POINTER_NONE = 0,
    CSS_POINTER_COARSE = 1,
    CSS_POINTER_FINE = 2,
};

// frontend-provided capabilities; aggregate pointer flags can describe mixed devices.
typedef struct CssInputCapabilities {
    uint8_t pointer;
    uint8_t any_pointer;
    bool hover;
    bool any_hover;
    bool known;
} CssInputCapabilities;

static inline bool css_input_capabilities_valid(const CssInputCapabilities* input) {
    return input && (input->pointer == CSS_POINTER_NONE || input->pointer == CSS_POINTER_COARSE ||
        input->pointer == CSS_POINTER_FINE) &&
        !(input->any_pointer & ~(CSS_POINTER_COARSE | CSS_POINTER_FINE)) &&
        (input->any_pointer & input->pointer) == input->pointer &&
        (!input->hover || (input->pointer != CSS_POINTER_NONE && input->any_hover)) &&
        (!input->any_hover || input->any_pointer != CSS_POINTER_NONE);
}

static inline bool css_input_capabilities_equal(const CssInputCapabilities* a, const CssInputCapabilities* b) {
    return a && b && a->pointer == b->pointer && a->any_pointer == b->any_pointer &&
        a->hover == b->hover && a->any_hover == b->any_hover && a->known == b->known;
}

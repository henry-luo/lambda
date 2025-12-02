/**
 * @file input-html-tokens.cpp
 * @brief HTML element classification and token data
 */

#include "input-html-tokens.h"
#include <cstring>
#include <strings.h>

// HTML5 void elements (self-closing tags)
// Also includes legacy HTML 1.0 void elements like NEXTID for backwards compatibility
const char* HTML5_VOID_ELEMENTS[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", "command",
    "keygen", "menuitem", "slot",
    // HTML 1.0 legacy void elements
    "nextid", "isindex",
    NULL
};

// HTML5 semantic elements that should be parsed as containers
const char* HTML5_SEMANTIC_ELEMENTS[] = {
    "article", "aside", "details", "figcaption", "figure", "footer",
    "header", "main", "mark", "nav", "section", "summary", "time",
    "audio", "video", "canvas", "svg", "math", "datalist", "dialog",
    "meter", "output", "progress", "template", "search", "hgroup",
    NULL
};

// HTML5 elements that contain raw text (like script, style)
const char* HTML5_RAW_TEXT_ELEMENTS[] = {
    "script", "style", "textarea", "title", "xmp", "iframe", "noembed",
    "noframes", "noscript", "plaintext", NULL
};

// HTML5 elements that should preserve whitespace
const char* HTML5_PREFORMATTED_ELEMENTS[] = {
    "pre", "code", "kbd", "samp", "var", "listing", "xmp", "plaintext", NULL
};

// HTML5 block-level elements
const char* HTML5_BLOCK_ELEMENTS[] = {
    "address", "article", "aside", "blockquote", "details", "dialog", "dd", "div",
    "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
    "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "li", "main", "nav", "ol",
    "p", "pre", "section", "table", "ul", "canvas", "audio", "video", NULL
};

// HTML5 inline elements
const char* HTML5_INLINE_ELEMENTS[] = {
    "a", "abbr", "acronym", "b", "bdi", "bdo", "big", "br", "button", "cite",
    "code", "dfn", "em", "i", "img", "input", "kbd", "label", "map", "mark",
    "meter", "noscript", "object", "output", "progress", "q", "ruby", "s",
    "samp", "script", "select", "small", "span", "strong", "sub", "sup",
    "textarea", "time", "tt", "u", "var", "wbr", NULL
};

bool html_is_semantic_element(const char* tag_name) {
    for (int i = 0; HTML5_SEMANTIC_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HTML5_SEMANTIC_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool html_is_void_element(const char* tag_name) {
    for (int i = 0; HTML5_VOID_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HTML5_VOID_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool html_is_raw_text_element(const char* tag_name) {
    for (int i = 0; HTML5_RAW_TEXT_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HTML5_RAW_TEXT_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool html_is_preformatted_element(const char* tag_name) {
    for (int i = 0; HTML5_PREFORMATTED_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HTML5_PREFORMATTED_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool html_is_block_element(const char* tag_name) {
    for (int i = 0; HTML5_BLOCK_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HTML5_BLOCK_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool html_is_inline_element(const char* tag_name) {
    for (int i = 0; HTML5_INLINE_ELEMENTS[i]; i++) {
        if (strcasecmp(tag_name, HTML5_INLINE_ELEMENTS[i]) == 0) {
            return true;
        }
    }
    return false;
}

// HTML5 custom element validation (simplified)
bool html_is_valid_custom_element_name(const char* name) {
    if (!name || strlen(name) == 0) return false;

    // custom elements must contain a hyphen and start with a lowercase letter
    bool has_hyphen = false;
    if (name[0] < 'a' || name[0] > 'z') return false;

    for (int i = 1; name[i]; i++) {
        if (name[i] == '-') {
            has_hyphen = true;
        } else if (!((name[i] >= 'a' && name[i] <= 'z') ||
                     (name[i] >= '0' && name[i] <= '9') ||
                     name[i] == '-' || name[i] == '.' || name[i] == '_')) {
            return false;
        }
    }

    return has_hyphen;
}

// Check if attribute is a data attribute (HTML5 feature)
bool html_is_data_attribute(const char* attr_name) {
    return strncmp(attr_name, "data-", 5) == 0;
}

// Check if attribute is an ARIA attribute (accessibility)
bool html_is_aria_attribute(const char* attr_name) {
    return strncmp(attr_name, "aria-", 5) == 0;
}

// Optional end tag / auto-close support per HTML spec
// Returns true if opening <new_tag> should implicitly close <current_tag>
// This implements the "Tag omission in text/html" rules from the HTML Living Standard
bool html_tag_closes_parent(const char* current_tag, const char* new_tag) {
    if (!current_tag || !new_tag) return false;

    // DT/DD auto-close rules (HTML spec 4.4.10, 4.4.11):
    // - A dt element's end tag can be omitted if followed by another dt or dd element
    // - A dd element's end tag can be omitted if followed by another dd or dt element,
    //   or if there is no more content in the parent element
    if (strcasecmp(current_tag, "dt") == 0 || strcasecmp(current_tag, "dd") == 0) {
        if (strcasecmp(new_tag, "dt") == 0 || strcasecmp(new_tag, "dd") == 0) {
            return true;
        }
    }

    // LI auto-close rules (HTML spec 4.4.8):
    // - A li element's end tag can be omitted if followed by another li element
    //   or if there is no more content in the parent element
    if (strcasecmp(current_tag, "li") == 0) {
        if (strcasecmp(new_tag, "li") == 0) {
            return true;
        }
    }

    // P auto-close rules (HTML spec 4.4.1):
    // - A p element's end tag can be omitted if followed by certain block elements
    if (strcasecmp(current_tag, "p") == 0) {
        // P closes when followed by: address, article, aside, blockquote, details,
        // dialog, div, dl, fieldset, figcaption, figure, footer, form, h1-h6,
        // header, hgroup, hr, main, menu, nav, ol, p, pre, search, section, table, ul
        const char* p_closers[] = {
            "address", "article", "aside", "blockquote", "details", "dialog",
            "div", "dl", "fieldset", "figcaption", "figure", "footer", "form",
            "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "hr",
            "main", "menu", "nav", "ol", "p", "pre", "search", "section",
            "table", "ul", NULL
        };
        for (int i = 0; p_closers[i]; i++) {
            if (strcasecmp(new_tag, p_closers[i]) == 0) {
                return true;
            }
        }
    }

    // TR auto-close rules:
    // - A tr element's end tag can be omitted if followed by another tr
    if (strcasecmp(current_tag, "tr") == 0) {
        if (strcasecmp(new_tag, "tr") == 0) {
            return true;
        }
    }

    // TD/TH auto-close rules:
    // - A td/th element's end tag can be omitted if followed by td, th, or tr
    if (strcasecmp(current_tag, "td") == 0 || strcasecmp(current_tag, "th") == 0) {
        if (strcasecmp(new_tag, "td") == 0 || strcasecmp(new_tag, "th") == 0 ||
            strcasecmp(new_tag, "tr") == 0) {
            return true;
        }
    }

    // THEAD/TBODY/TFOOT auto-close rules:
    // These close when followed by another table section
    if (strcasecmp(current_tag, "thead") == 0 || strcasecmp(current_tag, "tbody") == 0 ||
        strcasecmp(current_tag, "tfoot") == 0) {
        if (strcasecmp(new_tag, "thead") == 0 || strcasecmp(new_tag, "tbody") == 0 ||
            strcasecmp(new_tag, "tfoot") == 0) {
            return true;
        }
    }

    // OPTION auto-close rules:
    // - An option element's end tag can be omitted if followed by another option or optgroup
    if (strcasecmp(current_tag, "option") == 0) {
        if (strcasecmp(new_tag, "option") == 0 || strcasecmp(new_tag, "optgroup") == 0) {
            return true;
        }
    }

    // OPTGROUP auto-close rules:
    // - An optgroup element's end tag can be omitted if followed by another optgroup
    if (strcasecmp(current_tag, "optgroup") == 0) {
        if (strcasecmp(new_tag, "optgroup") == 0) {
            return true;
        }
    }

    // RP/RT auto-close rules (ruby annotations):
    // - rp/rt element's end tag can be omitted if followed by rp or rt
    if (strcasecmp(current_tag, "rp") == 0 || strcasecmp(current_tag, "rt") == 0) {
        if (strcasecmp(new_tag, "rp") == 0 || strcasecmp(new_tag, "rt") == 0) {
            return true;
        }
    }

    return false;
}

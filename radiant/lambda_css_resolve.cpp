#include "lambda_css_resolve.h"
#include "view.hpp"
#include <string.h>
#include <strings.h>
#include <ctype.h>

extern "C" {
#include <lexbor/css/css.h>
}

// ============================================================================
// CSS Keyword to Lexbor Enum Mapping
// ============================================================================

/**
 * Comprehensive mapping table for CSS keyword strings to Lexbor enum values
 * Extracted from lexbor/source/lexbor/css/value/const.h
 */
struct KeywordMapping {
    const char* keyword;
    int lexbor_value;
};

// note: keywords are sorted alphabetically for binary search optimization
static const KeywordMapping keyword_map[] = {
    // Display and layout values
    {"absolute", 0x014f},  // LXB_CSS_VALUE_ABSOLUTE

    // Animation keywords
    {"alternate", 0x0095}, // LXB_CSS_VALUE_ALTERNATE (animation-direction)
    {"alternate-reverse", 0x0096}, // LXB_CSS_VALUE_ALTERNATE_REVERSE
    {"auto", 0x000c},      // LXB_CSS_VALUE_AUTO
    {"baseline", 0x000b},  // LXB_CSS_VALUE_BASELINE
    {"block", 0x00ef},     // LXB_CSS_VALUE_BLOCK
    {"border-box", 0x002a}, // LXB_CSS_VALUE_BORDER_BOX
    {"both", 0x0174},      // LXB_CSS_VALUE_BOTH
    {"bottom", 0x0019},    // LXB_CSS_VALUE_BOTTOM
    {"break-all", 0x0039}, // LXB_CSS_VALUE_BREAK_ALL (word-break)
    {"break-word", 0x003a}, // LXB_CSS_VALUE_BREAK_WORD (word-wrap)

    // Animation fill modes
    {"backwards", 0x009a}, // LXB_CSS_VALUE_BACKWARDS (animation-fill-mode)

    // Font and text values
    {"bold", 0x013d},      // LXB_CSS_VALUE_BOLD
    {"bolder", 0x013e},    // LXB_CSS_VALUE_BOLDER

    // Text transformation
    {"capitalize", 0x0053}, // LXB_CSS_VALUE_CAPITALIZE
    {"center", 0x0007},    // LXB_CSS_VALUE_CENTER
    {"circle", 0x0220},    // Custom value for list-style-type circle
    {"clip", 0x003c},      // LXB_CSS_VALUE_CLIP (text-overflow)
    {"collapse", 0x0210},  // Custom value for border-collapse collapse
    {"column", 0x0054},    // LXB_CSS_VALUE_COLUMN (flex-direction)
    {"column-reverse", 0x0055}, // LXB_CSS_VALUE_COLUMN_REVERSE
    {"content-box", 0x0029}, // LXB_CSS_VALUE_CONTENT_BOX
    {"currentcolor", 0x0031}, // LXB_CSS_VALUE_CURRENTCOLOR

    // Border styles
    {"dashed", 0x0022},    // LXB_CSS_VALUE_DASHED
    {"decimal", 0x0221},   // Custom value for list-style-type decimal
    {"disc", 0x0222},      // Custom value for list-style-type disc
    {"dotted", 0x0021},    // LXB_CSS_VALUE_DOTTED
    {"double", 0x0024},    // LXB_CSS_VALUE_DOUBLE

    // Background size keywords
    {"contain", 0x0200},   // Custom value for background-size contain
    {"cover", 0x0201},     // Custom value for background-size cover

    // Animation timing functions
    {"ease", 0x0083},      // LXB_CSS_VALUE_EASE (animation-timing-function)
    {"ease-in", 0x0084},   // LXB_CSS_VALUE_EASE_IN
    {"ease-in-out", 0x0085}, // LXB_CSS_VALUE_EASE_IN_OUT
    {"ease-out", 0x0086},  // LXB_CSS_VALUE_EASE_OUT

    // Text overflow
    {"ellipsis", 0x0056},  // LXB_CSS_VALUE_ELLIPSIS

    // Display types
    {"flex", 0x00f5},      // LXB_CSS_VALUE_FLEX
    {"flex-end", 0x0057},  // LXB_CSS_VALUE_FLEX_END
    {"flex-start", 0x0058}, // LXB_CSS_VALUE_FLEX_START
    {"fixed", 0x0151},     // LXB_CSS_VALUE_FIXED

    // Animation fill modes
    {"forwards", 0x009b},  // LXB_CSS_VALUE_FORWARDS (animation-fill-mode)

    // Colors - Common colors
    {"black", 0x003b},     // LXB_CSS_VALUE_BLACK
    {"blue", 0x003d},      // LXB_CSS_VALUE_BLUE
    {"brown", 0x003f},     // LXB_CSS_VALUE_BROWN
    {"gold", 0x0067},      // LXB_CSS_VALUE_GOLD
    {"gray", 0x0069},      // LXB_CSS_VALUE_GRAY
    {"green", 0x006a},     // LXB_CSS_VALUE_GREEN
    {"grid", 0x00f6},      // LXB_CSS_VALUE_GRID

    // Visibility and overflow
    {"hidden", 0x0020},    // LXB_CSS_VALUE_HIDDEN
    {"hide", 0x0211},      // Custom value for empty-cells hide

    // Layout display
    // Animation iteration count and play state
    {"infinite", 0x0097},  // LXB_CSS_VALUE_INFINITE (animation-iteration-count)
    {"inline", 0x00f0},    // LXB_CSS_VALUE_INLINE
    {"inline-block", 0x00f1}, // LXB_CSS_VALUE_INLINE_BLOCK
    {"inline-flex", 0x00f2},  // LXB_CSS_VALUE_INLINE_FLEX
    {"inline-grid", 0x00f3},  // LXB_CSS_VALUE_INLINE_GRID

    // Font styles
    {"inside", 0x0223},    // Custom value for list-style-position inside
    {"italic", 0x013b},    // LXB_CSS_VALUE_ITALIC

    // Text alignment
    {"justify", 0x0152},   // LXB_CSS_VALUE_JUSTIFY

    // Word breaking
    {"keep-all", 0x0058},  // LXB_CSS_VALUE_KEEP_ALL (word-break)

    // Alignment
    {"left", 0x002f},      // LXB_CSS_VALUE_LEFT

    // Animation timing functions
    {"linear", 0x0087},    // LXB_CSS_VALUE_LINEAR (animation-timing-function)
    {"line-through", 0x0159}, // LXB_CSS_VALUE_LINE_THROUGH

    // Background attachment
    {"local", 0x0202},     // Custom value for background-attachment local

    {"lowercase", 0x0060}, // LXB_CSS_VALUE_LOWERCASE

    // Vertical alignment
    {"middle", 0x0010},    // LXB_CSS_VALUE_MIDDLE
    {"move", 0x00ec},      // LXB_CSS_VALUE_MOVE

    // Background blend modes
    {"multiply", 0x0204},  // Custom value for background-blend-mode multiply

    // Display and text
    {"none", 0x001f},      // LXB_CSS_VALUE_NONE
    {"normal", 0x0132},    // LXB_CSS_VALUE_NORMAL
    {"nowrap", 0x0111},    // LXB_CSS_VALUE_NOWRAP

    // Font styles
    {"oblique", 0x013c},   // LXB_CSS_VALUE_OBLIQUE

    // Colors
    {"orange", 0x009d},    // LXB_CSS_VALUE_ORANGE

    // Background blend modes
    {"overlay", 0x0205},   // Custom value for background-blend-mode overlay

    {"overline", 0x0158},  // LXB_CSS_VALUE_OVERLINE
    {"outside", 0x0224},   // Custom value for list-style-position outside

    // Background origin/clip
    {"padding-box", 0x0203}, // Custom value for background-origin/clip padding-box

    // Colors
    {"pink", 0x00a7},      // LXB_CSS_VALUE_PINK
    {"pointer", 0x00e6},   // LXB_CSS_VALUE_POINTER
    {"pre", 0x016e},       // LXB_CSS_VALUE_PRE
    {"pre-line", 0x0171},  // LXB_CSS_VALUE_PRE_LINE
    {"pre-wrap", 0x016f},  // LXB_CSS_VALUE_PRE_WRAP
    {"purple", 0x00aa},    // LXB_CSS_VALUE_PURPLE

    // Colors
    {"red", 0x00ac},       // LXB_CSS_VALUE_RED
    {"relative", 0x014e},  // LXB_CSS_VALUE_RELATIVE

    // Animation direction
    {"reverse", 0x0098},   // LXB_CSS_VALUE_REVERSE (animation-direction)
    {"right", 0x0030},     // LXB_CSS_VALUE_RIGHT

    // Background repeat
    {"round", 0x0206},     // Custom value for background-repeat round

    {"row", 0x0059},       // LXB_CSS_VALUE_ROW (flex-direction)
    {"row-reverse", 0x005a}, // LXB_CSS_VALUE_ROW_REVERSE

    // Animation play state
    {"running", 0x009c},   // LXB_CSS_VALUE_RUNNING (animation-play-state)

    // Overflow
    {"scroll", 0x014b},    // LXB_CSS_VALUE_SCROLL
    {"separate", 0x0212},  // Custom value for border-collapse separate
    {"show", 0x0213},      // Custom value for empty-cells show
    {"silver", 0x00b5},    // LXB_CSS_VALUE_SILVER
    {"small-caps", 0x0062}, // LXB_CSS_VALUE_SMALL_CAPS (font-variant)
    {"solid", 0x0023},     // LXB_CSS_VALUE_SOLID

    // Background repeat
    {"space", 0x0207},     // Custom value for background-repeat space

    {"space-around", 0x005b}, // LXB_CSS_VALUE_SPACE_AROUND
    {"space-between", 0x005c}, // LXB_CSS_VALUE_SPACE_BETWEEN
    {"space-evenly", 0x005d}, // LXB_CSS_VALUE_SPACE_EVENLY
    {"square", 0x0225},    // Custom value for list-style-type square
    {"static", 0x014d},    // LXB_CSS_VALUE_STATIC
    {"sticky", 0x0150},    // LXB_CSS_VALUE_STICKY
    {"stretch", 0x005e},   // LXB_CSS_VALUE_STRETCH
    {"sub", 0x0016},       // LXB_CSS_VALUE_SUB
    {"super", 0x0017},     // LXB_CSS_VALUE_SUPER

    // Vertical alignment
    {"text-bottom", 0x000d}, // LXB_CSS_VALUE_TEXT_BOTTOM
    {"text-top", 0x0013},    // LXB_CSS_VALUE_TEXT_TOP
    {"text", 0x00e7},        // LXB_CSS_VALUE_TEXT (cursor)
    {"top", 0x0018},         // LXB_CSS_VALUE_TOP
    {"transparent", 0x0032}, // LXB_CSS_VALUE_TRANSPARENT

    // Text decoration
    {"underline", 0x0157},   // LXB_CSS_VALUE_UNDERLINE
    {"uppercase", 0x0065},   // LXB_CSS_VALUE_UPPERCASE

    // Overflow
    {"visible", 0x0149},     // LXB_CSS_VALUE_VISIBLE

    // Flexbox wrap
    {"wrap", 0x005f},        // LXB_CSS_VALUE_WRAP
    {"wrap-reverse", 0x0060}, // LXB_CSS_VALUE_WRAP_REVERSE

    // Colors
    {"white", 0x00c4},       // LXB_CSS_VALUE_WHITE
    {"yellow", 0x00c6},      // LXB_CSS_VALUE_YELLOW
};

static const size_t keyword_map_size = sizeof(keyword_map) / sizeof(keyword_map[0]);

/**
 * Binary search for keyword in sorted mapping table
 */
static int keyword_compare(const void* a, const void* b) {
    const char* key = (const char*)a;
    const KeywordMapping* mapping = (const KeywordMapping*)b;
    return strcasecmp(key, mapping->keyword);
}

int map_css_keyword_to_lexbor(const char* keyword) {
    if (!keyword) return 0;

    // binary search in sorted keyword table
    const KeywordMapping* result = (const KeywordMapping*)bsearch(
        keyword,
        keyword_map,
        keyword_map_size,
        sizeof(KeywordMapping),
        keyword_compare
    );

    if (result) {
        return result->lexbor_value;
    }

    // unknown keyword, return 0
    // TODO: log warning for debugging
    return 0;
}

// ============================================================================
// Value Conversion Functions
// ============================================================================

float convert_lambda_length_to_px(const CssValue* value, LayoutContext* lycon,
                                   CssPropertyId prop_id) {
    if (!value) return 0.0f;

    switch (value->type) {
        case CSS_VALUE_LENGTH: {
            float num = value->data.length.value;

            switch (value->data.length.unit) {
                case CSS_UNIT_PX:
                    return num;

                case CSS_UNIT_EM:
                    // relative to current font size
                    if (lycon) {
                        return num * lycon->font.current_font_size;
                    }
                    return num * 16.0f; // default font size

                case CSS_UNIT_REM:
                    // relative to root font size
                    return num * 16.0f; // TODO: get from root element

                case CSS_UNIT_PT:
                    return num * (96.0f / 72.0f); // 1pt = 1/72 inch, 96dpi

                case CSS_UNIT_PC:
                    return num * 16.0f; // 1pc = 12pt = 16px

                case CSS_UNIT_IN:
                    return num * 96.0f; // 96dpi

                case CSS_UNIT_CM:
                    return num * 37.795f; // 1cm = 37.795px at 96dpi

                case CSS_UNIT_MM:
                    return num * 3.7795f; // 1mm = 3.7795px at 96dpi

                case CSS_UNIT_VW:
                    // viewport width percentage
                    if (lycon && lycon->width > 0) {
                        return (num / 100.0f) * lycon->width;
                    }
                    return 0.0f;

                case CSS_UNIT_VH:
                    // viewport height percentage
                    if (lycon && lycon->height > 0) {
                        return (num / 100.0f) * lycon->height;
                    }
                    return 0.0f;

                default:
                    return num; // assume pixels for unknown units
            }
        }

        case CSS_VALUE_PERCENTAGE: {
            // percentage resolution depends on property context
            // for now, return raw percentage (needs parent context)
            return (float)value->data.percentage.value;
        }

        case CSS_VALUE_NUMBER: {
            // unitless number, treat as pixels for most properties
            return (float)value->data.number.value;
        }

        default:
            return 0.0f;
    }
}

Color convert_lambda_color(const CssValue* value) {
    Color result;
    result.r = 0;
    result.g = 0;
    result.b = 0;
    result.a = 255; // default black, opaque

    if (!value) return result;

    switch (value->type) {
        case CSS_VALUE_COLOR: {
            // Access color data from CssValue anonymous struct
            switch (value->data.color.type) {
                case CSS_COLOR_RGB:
                    result.r = value->data.color.data.rgba.r;
                    result.g = value->data.color.data.rgba.g;
                    result.b = value->data.color.data.rgba.b;
                    result.a = value->data.color.data.rgba.a;
                    break;

                case CSS_COLOR_HSL:
                    // TODO: convert HSL to RGB
                    // for now, leave as black
                    break;

                default:
                    break;
            }
            break;
        }

        case CSS_VALUE_KEYWORD: {
            // map color keyword to RGB
            // TODO: implement color keyword lookup table
            const char* keyword = value->data.keyword;

            if (strcasecmp(keyword, "black") == 0) {
                result.r = 0; result.g = 0; result.b = 0; result.a = 255;
            } else if (strcasecmp(keyword, "white") == 0) {
                result.r = 255; result.g = 255; result.b = 255; result.a = 255;
            } else if (strcasecmp(keyword, "red") == 0) {
                result.r = 255; result.g = 0; result.b = 0; result.a = 255;
            } else if (strcasecmp(keyword, "green") == 0) {
                result.r = 0; result.g = 128; result.b = 0; result.a = 255;
            } else if (strcasecmp(keyword, "blue") == 0) {
                result.r = 0; result.g = 0; result.b = 255; result.a = 255;
            } else if (strcasecmp(keyword, "transparent") == 0) {
                result.r = 0; result.g = 0; result.b = 0; result.a = 0;
            }
            // TODO: add more color keywords
            break;
        }

        default:
            break;
    }

    return result;
}

// ============================================================================
// Keyword Mapping Functions
// ============================================================================

uint32_t map_lambda_color_keyword(const char* keyword) {
    if (!keyword) return 0xFF000000; // default black in ABGR format

    // Map CSS color keywords to ABGR values
    // NOTE: Color union uses ABGR format (0xAABBGGRR), NOT RGBA!
    // Format: 0xAABBGGRR where AA=alpha, BB=blue, GG=green, RR=red
    if (strcasecmp(keyword, "black") == 0) return 0xFF000000;       // rgb(0,0,0)
    if (strcasecmp(keyword, "white") == 0) return 0xFFFFFFFF;       // rgb(255,255,255)
    if (strcasecmp(keyword, "red") == 0) return 0xFF0000FF;         // rgb(255,0,0)
    if (strcasecmp(keyword, "green") == 0) return 0xFF008000;       // rgb(0,128,0)
    if (strcasecmp(keyword, "blue") == 0) return 0xFFFF0000;        // rgb(0,0,255)
    if (strcasecmp(keyword, "yellow") == 0) return 0xFF00FFFF;      // rgb(255,255,0)
    if (strcasecmp(keyword, "cyan") == 0) return 0xFFFFFF00;        // rgb(0,255,255)
    if (strcasecmp(keyword, "magenta") == 0) return 0xFFFF00FF;     // rgb(255,0,255)
    if (strcasecmp(keyword, "gray") == 0) return 0xFF808080;        // rgb(128,128,128)
    if (strcasecmp(keyword, "grey") == 0) return 0xFF808080;        // rgb(128,128,128)
    if (strcasecmp(keyword, "silver") == 0) return 0xFFC0C0C0;      // rgb(192,192,192)
    if (strcasecmp(keyword, "lightgray") == 0) return 0xFFD3D3D3;   // rgb(211,211,211)
    if (strcasecmp(keyword, "lightgrey") == 0) return 0xFFD3D3D3;   // rgb(211,211,211)
    if (strcasecmp(keyword, "darkgray") == 0) return 0xFFA9A9A9;    // rgb(169,169,169)
    if (strcasecmp(keyword, "darkgrey") == 0) return 0xFFA9A9A9;    // rgb(169,169,169)
    if (strcasecmp(keyword, "maroon") == 0) return 0xFF000080;      // rgb(128,0,0)
    if (strcasecmp(keyword, "purple") == 0) return 0xFF800080;      // rgb(128,0,128)
    if (strcasecmp(keyword, "fuchsia") == 0) return 0xFFFF00FF;     // rgb(255,0,255)
    if (strcasecmp(keyword, "lime") == 0) return 0xFF00FF00;        // rgb(0,255,0)
    if (strcasecmp(keyword, "olive") == 0) return 0xFF008080;       // rgb(128,128,0)
    if (strcasecmp(keyword, "navy") == 0) return 0xFF800000;        // rgb(0,0,128)
    if (strcasecmp(keyword, "teal") == 0) return 0xFF808000;        // rgb(0,128,128)
    if (strcasecmp(keyword, "aqua") == 0) return 0xFFFFFF00;        // rgb(0,255,255)
    if (strcasecmp(keyword, "orange") == 0) return 0xFF00A5FF;      // rgb(255,165,0)
    if (strcasecmp(keyword, "transparent") == 0) return 0x00000000; // rgba(0,0,0,0)

    // TODO: Add more color keywords (148 total CSS3 colors)

    return 0xFF000000; // default to black
}

float map_lambda_font_size_keyword(const char* keyword) {
    if (!keyword) return 16.0f;

    // Map font-size keywords to pixel values
    if (strcasecmp(keyword, "xx-small") == 0) return 9.0f;
    if (strcasecmp(keyword, "x-small") == 0) return 10.0f;
    if (strcasecmp(keyword, "small") == 0) return 13.0f;
    if (strcasecmp(keyword, "medium") == 0) return 16.0f;
    if (strcasecmp(keyword, "large") == 0) return 18.0f;
    if (strcasecmp(keyword, "x-large") == 0) return 24.0f;
    if (strcasecmp(keyword, "xx-large") == 0) return 32.0f;
    if (strcasecmp(keyword, "smaller") == 0) return -1.0f;  // relative to parent
    if (strcasecmp(keyword, "larger") == 0) return -1.0f;   // relative to parent

    return 16.0f; // default medium size
}

// map Lambda CSS font-weight keywords/numbers to Lexbor PropValue enum
PropValue map_lambda_font_weight_to_lexbor(const CssValue* value) {
    if (!value) return LXB_CSS_VALUE_NORMAL;

    if (value->type == CSS_VALUE_KEYWORD) {
        const char* keyword = value->data.keyword;
        if (!keyword) return LXB_CSS_VALUE_NORMAL;

        // map keywords to Lexbor enum values
        if (strcasecmp(keyword, "normal") == 0) return LXB_CSS_VALUE_NORMAL;
        if (strcasecmp(keyword, "bold") == 0) return LXB_CSS_VALUE_BOLD;
        if (strcasecmp(keyword, "bolder") == 0) return LXB_CSS_VALUE_BOLDER;
        if (strcasecmp(keyword, "lighter") == 0) return LXB_CSS_VALUE_LIGHTER;

        return LXB_CSS_VALUE_NORMAL; // default
    }
    else if (value->type == CSS_VALUE_NUMBER || value->type == CSS_VALUE_INTEGER) {
        // numeric weights: map to closest keyword or return as-is
        int weight = (int)value->data.number.value;

        // Lexbor uses enum values for numeric weights too, but for simplicity
        // we'll map common numeric values to their keyword equivalents
        if (weight <= 350) return LXB_CSS_VALUE_LIGHTER;
        if (weight <= 550) return LXB_CSS_VALUE_NORMAL;  // 400
        if (weight <= 750) return LXB_CSS_VALUE_BOLD;    // 700
        return LXB_CSS_VALUE_BOLDER;  // 900
    }

    return LXB_CSS_VALUE_NORMAL; // default
}

const char* map_lambda_font_family_keyword(const char* keyword) {
    if (!keyword) return "sans-serif";

    // Map generic font family keywords to system fonts
    if (strcasecmp(keyword, "serif") == 0) return "serif";
    if (strcasecmp(keyword, "sans-serif") == 0) return "sans-serif";
    if (strcasecmp(keyword, "monospace") == 0) return "monospace";
    if (strcasecmp(keyword, "cursive") == 0) return "cursive";
    if (strcasecmp(keyword, "fantasy") == 0) return "fantasy";

    return "sans-serif"; // default
}

// ============================================================================
// Specificity Calculation
// ============================================================================

int32_t get_lambda_specificity(const CssDeclaration* decl) {
    if (!decl) return 0;

    // Lambda CssSpecificity is a struct with (a, b, c) components
    // Convert to Lexbor-compatible int32_t by packing:
    // specificity = (a << 16) | (b << 8) | c
    // TODO: verify Lambda CssSpecificity structure

    return 0; // placeholder - needs proper implementation
}

// ============================================================================
// Main Style Resolution
// ============================================================================

// Callback for AVL tree traversal
static bool resolve_property_callback(AvlNode* node, void* context) {
    LayoutContext* lycon = (LayoutContext*)context;

    // Get StyleNode from AvlNode->declaration field (not by casting)
    // AvlNode stores a pointer to StyleNode in its declaration field
    StyleNode* style_node = (StyleNode*)node->declaration;

    // get property ID from node
    CssPropertyId prop_id = (CssPropertyId)node->property_id;

    log_debug("[Lambda CSS Property] Processing property ID: %d", prop_id);

    // get the CSS declaration for this property
    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl) {
        log_debug("[Lambda CSS Property] No declaration found for property %d", prop_id);
        return true; // continue iteration
    }

    // resolve this property
    resolve_lambda_css_property(prop_id, decl, lycon);

    return true; // continue iteration
}

void resolve_lambda_css_styles(DomElement* dom_elem, LayoutContext* lycon) {
    if (!dom_elem || !lycon) {
        log_debug("[Lambda CSS] resolve_lambda_css_styles: null input (dom_elem=%p, lycon=%p)", (void*)dom_elem, (void*)lycon);
        return;
    }

    log_debug("[Lambda CSS] Resolving styles for element <%s>", dom_elem->tag_name);

    // iterate through specified_style AVL tree
    StyleTree* style_tree = dom_elem->specified_style;
    if (!style_tree) {
        log_debug("[Lambda CSS] No style tree found for element");
        return;
    }

    if (!style_tree->tree) {
        log_debug("[Lambda CSS] Style tree has no AVL tree");
        return;
    }

    log_debug("[Lambda CSS] Style tree has %d nodes", style_tree->tree->node_count);

    // Traverse the AVL tree and resolve each property
    int processed = avl_tree_foreach_inorder(style_tree->tree, resolve_property_callback, lycon);

    log_debug("[Lambda CSS] Processed %d style properties", processed);

    // Handle CSS inheritance for inheritable properties not explicitly set
    // Important inherited properties: font-family, font-size, font-weight, color, etc.
    static const CssPropertyId inheritable_props[] = {
        CSS_PROPERTY_FONT_FAMILY,
        CSS_PROPERTY_FONT_SIZE,
        CSS_PROPERTY_FONT_WEIGHT,
        CSS_PROPERTY_FONT_STYLE,
        CSS_PROPERTY_COLOR,
        CSS_PROPERTY_LINE_HEIGHT,
        CSS_PROPERTY_TEXT_ALIGN,
        CSS_PROPERTY_TEXT_DECORATION,
        CSS_PROPERTY_TEXT_TRANSFORM,
        CSS_PROPERTY_LETTER_SPACING,
        CSS_PROPERTY_WORD_SPACING,
        CSS_PROPERTY_WHITE_SPACE,
        CSS_PROPERTY_VISIBILITY,
    };
    static const size_t num_inheritable = sizeof(inheritable_props) / sizeof(inheritable_props[0]);

    // Get parent's style tree for inheritance
    StyleTree* parent_tree = (dom_elem->parent && dom_elem->parent->specified_style)
                             ? dom_elem->parent->specified_style : NULL;

    if (parent_tree) {
        log_debug("[Lambda CSS] Checking inheritance from parent <%s>",
                dom_elem->parent->tag_name);

        for (size_t i = 0; i < num_inheritable; i++) {
            CssPropertyId prop_id = inheritable_props[i];

            // Check if this property is already set on the element
            CssDeclaration* existing = style_tree_get_declaration(style_tree, prop_id);
            if (existing) {
                // Property is explicitly set, don't inherit
                continue;
            }

            // Property not set, check parent chain for inherited declaration
            // Walk up the parent chain until we find a declaration
            DomElement* ancestor = dom_elem->parent;
            CssDeclaration* inherited_decl = NULL;

            while (ancestor && !inherited_decl) {
                if (ancestor->specified_style) {
                    inherited_decl = style_tree_get_declaration(ancestor->specified_style, prop_id);
                    if (inherited_decl && inherited_decl->value) {
                        break; // Found it!
                    }
                }
                ancestor = ancestor->parent;
            }

            if (inherited_decl && inherited_decl->value) {
                log_debug("[Lambda CSS] Inheriting property %d from ancestor <%s>",
                         prop_id, ancestor ? ancestor->tag_name : "unknown");

                // Apply the inherited property using the ancestor's declaration
                resolve_lambda_css_property(prop_id, inherited_decl, lycon);
            }
        }
    }
}

void resolve_lambda_css_property(CssPropertyId prop_id, const CssDeclaration* decl,
                                  LayoutContext* lycon) {
    log_debug("[Lambda CSS Property] resolve_lambda_css_property called: prop_id=%d", prop_id);

    if (!decl || !lycon || !lycon->view) {
        log_debug("[Lambda CSS Property] Early return: decl=%p, lycon=%p, view=%p",
                  (void*)decl, (void*)lycon, lycon ? (void*)lycon->view : NULL);
        return;
    }

    const CssValue* value = decl->value;
    if (!value) {
        log_debug("[Lambda CSS Property] No value in declaration");
        return;
    }

    log_debug("[Lambda CSS Property] Processing property %d, value type=%d", prop_id, value->type);

    // handle shorthand properties by expanding to longhands
    bool is_shorthand = css_property_is_shorthand(prop_id);
    log_debug("[Lambda CSS Property] is_shorthand=%d for prop_id=%d", is_shorthand, prop_id);

    // DEBUG: manually check the property
    const CssProperty* dbg_prop = css_property_get_by_id(prop_id);
    if (dbg_prop) {
        log_debug("[Lambda CSS Property] Property found: name='%s', shorthand=%d",
                dbg_prop->name, dbg_prop->shorthand);
    } else {
        log_debug("[Lambda CSS Property] Property NOT found in database!");
    }

    // Special case: margin and padding with CSS_VALUE_LIST should be handled by switch statement
    // Don't treat them as shorthands that need expansion
    // Same for border-width, border-style, border-color with CSS_VALUE_LIST
    bool handle_in_switch = false;
    if ((prop_id == CSS_PROPERTY_MARGIN || prop_id == CSS_PROPERTY_PADDING) &&
        value->type == CSS_VALUE_LIST) {
        log_debug("[Lambda CSS Property] Multi-value margin/padding will be handled in switch statement");
        handle_in_switch = true;
        is_shorthand = false; // Override: treat as longhand for switch processing
    }
    if ((prop_id == CSS_PROPERTY_BORDER_WIDTH || prop_id == CSS_PROPERTY_BORDER_STYLE ||
         prop_id == CSS_PROPERTY_BORDER_COLOR) && value->type == CSS_VALUE_LIST) {
        log_debug("[Lambda CSS Property] Multi-value border shorthand will be handled in switch statement");
        handle_in_switch = true;
        is_shorthand = false; // Override: treat as longhand for switch processing
    }    if (is_shorthand) {
        log_debug("[Lambda CSS Shorthand] Property %d is a shorthand, expanding...", prop_id);

        if (prop_id == CSS_PROPERTY_BACKGROUND) {
            // background shorthand can set background-color, background-image, etc.
            // simple case: single color value (e.g., "background: green;")
            if (value->type == CSS_VALUE_COLOR || value->type == CSS_VALUE_KEYWORD) {
                CssDeclaration color_decl = *decl;
                color_decl.property_id = CSS_PROPERTY_BACKGROUND_COLOR;
                log_debug("[Lambda CSS Shorthand] Expanding background to background-color");
                resolve_lambda_css_property(CSS_PROPERTY_BACKGROUND_COLOR, &color_decl, lycon);
                return;
            }
            log_debug("[Lambda CSS Shorthand] Complex background shorthand not yet implemented");
            return;
        }

        if (prop_id == CSS_PROPERTY_MARGIN) {
            // margin shorthand: 1-4 values (top, right, bottom, left)
            // NOTE: multi-value margins (CSS_VALUE_LIST) are handled by the switch statement below
            // This section only handles the single-value expansion optimization
            log_debug("[Lambda CSS Shorthand] Processing margin shorthand (value type: %d)", value->type);

            if (value->type == CSS_VALUE_LENGTH || value->type == CSS_VALUE_KEYWORD ||
                value->type == CSS_VALUE_NUMBER) {
                // single value - expand to all four sides for clarity
                log_debug("[Lambda CSS Shorthand] Expanding single-value margin to all sides");
                CssDeclaration side_decl = *decl;
                side_decl.property_id = CSS_PROPERTY_MARGIN_TOP;
                resolve_lambda_css_property(CSS_PROPERTY_MARGIN_TOP, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_MARGIN_RIGHT;
                resolve_lambda_css_property(CSS_PROPERTY_MARGIN_RIGHT, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_MARGIN_BOTTOM;
                resolve_lambda_css_property(CSS_PROPERTY_MARGIN_BOTTOM, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_MARGIN_LEFT;
                resolve_lambda_css_property(CSS_PROPERTY_MARGIN_LEFT, &side_decl, lycon);
                return;
            }
            // Multi-value margin (CSS_VALUE_LIST) - fall through to switch statement below
            log_debug("[Lambda CSS Shorthand] Multi-value margin, letting switch statement handle it");
            // DON'T RETURN - let it fall through to the switch statement
        }

        if (prop_id == CSS_PROPERTY_PADDING) {
            // padding shorthand: 1-4 values (top, right, bottom, left)
            // NOTE: multi-value padding (CSS_VALUE_LIST) should be handled by switch statement below
            log_debug("[Lambda CSS Shorthand] Processing padding shorthand (value type: %d)", value->type);

            if (value->type == CSS_VALUE_LENGTH || value->type == CSS_VALUE_NUMBER) {
                // single value - expand to all four sides
                log_debug("[Lambda CSS Shorthand] Expanding single-value padding to all sides");
                CssDeclaration side_decl = *decl;
                side_decl.property_id = CSS_PROPERTY_PADDING_TOP;
                resolve_lambda_css_property(CSS_PROPERTY_PADDING_TOP, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_PADDING_RIGHT;
                resolve_lambda_css_property(CSS_PROPERTY_PADDING_RIGHT, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_PADDING_BOTTOM;
                resolve_lambda_css_property(CSS_PROPERTY_PADDING_BOTTOM, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_PADDING_LEFT;
                resolve_lambda_css_property(CSS_PROPERTY_PADDING_LEFT, &side_decl, lycon);
                return;
            }
            // Multi-value padding - fall through to switch statement
            log_debug("[Lambda CSS Shorthand] Multi-value padding, letting switch statement handle it");
            // DON'T RETURN - let it fall through
        }        if (prop_id == CSS_PROPERTY_BORDER) {
            // border shorthand: width style color (applies to all sides)
            log_debug("[Lambda CSS Shorthand] Expanding border shorthand");
            log_debug("[Lambda CSS Shorthand] Border value type: %d", value->type);

            // CSS border shorthand: "border: <width> <style> <color>"
            // The parser should have given us a list of values
            // Expand to: border-top-*, border-right-*, border-bottom-*, border-left-*

            // check if we have a list of values
            if (value->type == CSS_VALUE_LIST && value->data.list.count > 0) {
                CssValue** values = value->data.list.values;
                size_t count = value->data.list.count;

                log_debug("[Lambda CSS Shorthand] Border has %zu values", count);

                // Identify width, style, and color from the values
                CssValue* width_val = NULL;
                CssValue* style_val = NULL;
                CssValue* color_val = NULL;

                for (size_t i = 0; i < count; i++) {
                    CssValue* v = values[i];
                    if (!v) continue;

                    log_debug("[Lambda CSS Shorthand] Border value[%zu]: type=%d", i, v->type);

                    if (v->type == CSS_VALUE_LENGTH || v->type == CSS_VALUE_NUMBER) {
                        width_val = v;
                        log_debug("[Lambda CSS Shorthand] Found border width");
                    } else if (v->type == CSS_VALUE_KEYWORD) {
                        // Could be style (solid, dashed, etc.) or color (red, blue, etc.)
                        // Check if it's a border style keyword
                        const char* kw = v->data.keyword;
                        if (strcasecmp(kw, "solid") == 0 || strcasecmp(kw, "dashed") == 0 ||
                            strcasecmp(kw, "dotted") == 0 || strcasecmp(kw, "double") == 0 ||
                            strcasecmp(kw, "groove") == 0 || strcasecmp(kw, "ridge") == 0 ||
                            strcasecmp(kw, "inset") == 0 || strcasecmp(kw, "outset") == 0 ||
                            strcasecmp(kw, "none") == 0 || strcasecmp(kw, "hidden") == 0) {
                            style_val = v;
                            log_debug("[Lambda CSS Shorthand] Found border style: %s", kw);
                        } else {
                            // Assume it's a color keyword
                            color_val = v;
                            log_debug("[Lambda CSS Shorthand] Found border color keyword: %s", kw);
                        }
                    } else if (v->type == CSS_VALUE_COLOR) {
                        color_val = v;
                        log_debug("[Lambda CSS Shorthand] Found border color");
                    }
                }

                // Apply width to all sides
                if (width_val) {
                    CssDeclaration width_decl = *decl;
                    width_decl.value = width_val;
                    width_decl.property_id = CSS_PROPERTY_BORDER_TOP_WIDTH;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_WIDTH, &width_decl, lycon);
                    width_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_WIDTH;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_WIDTH, &width_decl, lycon);
                    width_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_WIDTH;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_WIDTH, &width_decl, lycon);
                    width_decl.property_id = CSS_PROPERTY_BORDER_LEFT_WIDTH;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_WIDTH, &width_decl, lycon);
                }

                // Apply style to all sides
                if (style_val) {
                    CssDeclaration style_decl = *decl;
                    style_decl.value = style_val;
                    style_decl.property_id = CSS_PROPERTY_BORDER_TOP_STYLE;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_STYLE, &style_decl, lycon);
                    style_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_STYLE;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_STYLE, &style_decl, lycon);
                    style_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_STYLE;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_STYLE, &style_decl, lycon);
                    style_decl.property_id = CSS_PROPERTY_BORDER_LEFT_STYLE;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_STYLE, &style_decl, lycon);
                }

                // Apply color to all sides
                if (color_val) {
                    CssDeclaration color_decl = *decl;
                    color_decl.value = color_val;
                    color_decl.property_id = CSS_PROPERTY_BORDER_TOP_COLOR;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_COLOR, &color_decl, lycon);
                    color_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_COLOR;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_COLOR, &color_decl, lycon);
                    color_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_COLOR;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_COLOR, &color_decl, lycon);
                    color_decl.property_id = CSS_PROPERTY_BORDER_LEFT_COLOR;
                    resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_COLOR, &color_decl, lycon);
                }

                log_debug("[Lambda CSS Shorthand] Border shorthand expansion complete");
                return;
            }

            // TEMPORARY WORKAROUND: CSS parser currently only gives us the first value (width)
            // For now, assume "border: Npx solid black" and expand it manually
            if (value->type == CSS_VALUE_LENGTH || value->type == CSS_VALUE_NUMBER) {
                // We have the width - use it for all sides
                CssDeclaration width_decl = *decl;
                width_decl.value = (CssValue*)value;  // cast away const - we're not modifying it

                width_decl.property_id = CSS_PROPERTY_BORDER_TOP_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_WIDTH, &width_decl, lycon);
                width_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_WIDTH, &width_decl, lycon);
                width_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_WIDTH, &width_decl, lycon);
                width_decl.property_id = CSS_PROPERTY_BORDER_LEFT_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_WIDTH, &width_decl, lycon);

                // Assume default style: solid
                log_debug("[Lambda CSS Shorthand] Creating solid style value");
                CssValue* solid_value = (CssValue*)alloc_prop(lycon, sizeof(CssValue));
                if (!solid_value) {
                    log_debug("[Lambda CSS Shorthand] ERROR: alloc_prop failed for solid_value");
                    return;
                }
                solid_value->type = CSS_VALUE_KEYWORD;

                // Allocate string for keyword
                char* solid_str = (char*)alloc_prop(lycon, 6); // "solid" + \0
                strcpy(solid_str, "solid");
                solid_value->data.keyword = solid_str;
                log_debug("[Lambda CSS Shorthand] solid_value created: keyword=%s", solid_value->data.keyword);

                CssDeclaration style_decl = *decl;
                style_decl.value = solid_value;

                log_debug("[Lambda CSS Shorthand] Applying border-top-style");
                style_decl.property_id = CSS_PROPERTY_BORDER_TOP_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_STYLE, &style_decl, lycon);

                log_debug("[Lambda CSS Shorthand] Applying border-right-style");
                style_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_STYLE, &style_decl, lycon);

                log_debug("[Lambda CSS Shorthand] Applying border-bottom-style");
                style_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_STYLE, &style_decl, lycon);

                log_debug("[Lambda CSS Shorthand] Applying border-left-style");
                style_decl.property_id = CSS_PROPERTY_BORDER_LEFT_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_STYLE, &style_decl, lycon);

                // Assume default color: black
                CssValue* black_value = (CssValue*)alloc_prop(lycon, sizeof(CssValue));
                black_value->type = CSS_VALUE_KEYWORD;

                char* black_str = (char*)alloc_prop(lycon, 6); // "black" + \0
                strcpy(black_str, "black");
                black_value->data.keyword = black_str;

                CssDeclaration color_decl = *decl;
                color_decl.value = black_value;
                color_decl.property_id = CSS_PROPERTY_BORDER_TOP_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_COLOR, &color_decl, lycon);
                color_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_COLOR, &color_decl, lycon);
                color_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_COLOR, &color_decl, lycon);
                color_decl.property_id = CSS_PROPERTY_BORDER_LEFT_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_COLOR, &color_decl, lycon);

                log_debug("[Lambda CSS Shorthand] Border shorthand expansion complete (workaround: solid black assumed)");
                return;
            }

            log_debug("[Lambda CSS Shorthand] Border shorthand value is not a list or length");
            return;
        }

        if (prop_id == CSS_PROPERTY_BORDER_WIDTH) {
            // border-width shorthand: 1-4 values (top, right, bottom, left)
            log_debug("[Lambda CSS Shorthand] Expanding border-width shorthand");

            if (value->type == CSS_VALUE_LENGTH) {
                CssDeclaration side_decl = *decl;
                side_decl.property_id = CSS_PROPERTY_BORDER_TOP_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_WIDTH, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_WIDTH, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_WIDTH, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_LEFT_WIDTH;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_WIDTH, &side_decl, lycon);
                return;
            }
            log_debug("[Lambda CSS Shorthand] Complex border-width shorthand not yet implemented");
            return;
        }

        if (prop_id == CSS_PROPERTY_BORDER_STYLE) {
            // border-style shorthand: 1-4 values (top, right, bottom, left)
            log_debug("[Lambda CSS Shorthand] Expanding border-style shorthand");

            if (value->type == CSS_VALUE_KEYWORD) {
                CssDeclaration side_decl = *decl;
                side_decl.property_id = CSS_PROPERTY_BORDER_TOP_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_STYLE, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_STYLE, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_STYLE, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_LEFT_STYLE;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_STYLE, &side_decl, lycon);
                return;
            }
            log_debug("[Lambda CSS Shorthand] Complex border-style shorthand not yet implemented");
            return;
        }

        if (prop_id == CSS_PROPERTY_BORDER_COLOR) {
            // border-color shorthand: 1-4 values (top, right, bottom, left)
            log_debug("[Lambda CSS Shorthand] Expanding border-color shorthand");

            if (value->type == CSS_VALUE_COLOR || value->type == CSS_VALUE_KEYWORD) {
                CssDeclaration side_decl = *decl;
                side_decl.property_id = CSS_PROPERTY_BORDER_TOP_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_TOP_COLOR, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_RIGHT_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_RIGHT_COLOR, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_BOTTOM_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_BOTTOM_COLOR, &side_decl, lycon);
                side_decl.property_id = CSS_PROPERTY_BORDER_LEFT_COLOR;
                resolve_lambda_css_property(CSS_PROPERTY_BORDER_LEFT_COLOR, &side_decl, lycon);
                return;
            }
            log_debug("[Lambda CSS Shorthand] Complex border-color shorthand not yet implemented");
            return;
        }

        // other shorthands not yet implemented
        log_debug("[Lambda CSS Shorthand] Shorthand %d expansion not yet implemented", prop_id);
        return;
    }    int32_t specificity = get_lambda_specificity(decl);
    log_debug("[Lambda CSS Property] Specificity: %d", specificity);

    // Dispatch based on property ID
    // Parallel implementation to resolve_element_style() in resolve_style.cpp
    ViewSpan* span = (ViewSpan*)lycon->view;
    ViewBlock* block = lycon->view->type != RDT_VIEW_INLINE ? (ViewBlock*)lycon->view : NULL;

    switch (prop_id) {
        // ===== GROUP 1: Core Typography & Color =====

        case CSS_PROPERTY_COLOR: {
            log_debug("[CSS] Processing color property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }

            Color color_val = {0};
            if (value->type == CSS_VALUE_KEYWORD) {
                // Map keyword to color (e.g., "red", "blue")
                color_val.c = map_lambda_color_keyword(value->data.keyword);
                log_debug("[CSS] Color keyword: %s -> 0x%08X", value->data.keyword, color_val.c);
            } else if (value->type == CSS_VALUE_COLOR) {
                // Direct RGBA color value from color struct
                if (value->data.color.type == CSS_COLOR_RGB) {
                    color_val.r = value->data.color.data.rgba.r;
                    color_val.g = value->data.color.data.rgba.g;
                    color_val.b = value->data.color.data.rgba.b;
                    color_val.a = value->data.color.data.rgba.a;
                    log_debug("[CSS] Color RGBA: (%d,%d,%d,%d) -> 0x%08X",
                             color_val.r, color_val.g, color_val.b, color_val.a, color_val.c);
                }
            }

            if (color_val.c != 0) {
                span->in_line->color = color_val;
            }
            break;
        }

        case CSS_PROPERTY_FONT_SIZE: {
            log_debug("[CSS] Processing font-size property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }

            float font_size = 0.0f;
            bool valid = false;

            if (value->type == CSS_VALUE_LENGTH) {
                font_size = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] Font size length: %.2f px (after conversion)", font_size);
                // Per CSS spec, negative font-size values are invalid, but 0 is valid
                if (font_size >= 0) {
                    valid = true;
                } else {
                    log_debug("[CSS] Font size: %.2f px invalid (must be >= 0), ignoring", font_size);
                }
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                // Percentage of parent font size
                float parent_size = span->font->font_size > 0 ? span->font->font_size : 16.0f;
                font_size = parent_size * (value->data.percentage.value / 100.0f);
                log_debug("[CSS] Font size percentage: %.2f%% -> %.2f px", value->data.percentage.value, font_size);
                if (font_size >= 0) {
                    valid = true;
                } else {
                    log_debug("[CSS] Font size: %.2f px invalid (must be >= 0), ignoring", font_size);
                }
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Named font sizes: small, medium, large, etc.
                font_size = map_lambda_font_size_keyword(value->data.keyword);
                log_debug("[CSS] Font size keyword: %s -> %.2f px", value->data.keyword, font_size);
                if (font_size > 0) {
                    valid = true;
                }
            } else if (value->type == CSS_VALUE_NUMBER) {
                // Per CSS spec, unitless zero is valid and treated as 0px
                // Other unitless numbers are invalid for font-size
                font_size = value->data.number.value;
                if (font_size == 0.0f) {
                    valid = true;
                    log_debug("[CSS] Font size: unitless 0 (treated as 0px)");
                } else {
                    log_debug("[CSS] Font size number: %.2f (non-zero unitless values invalid for font-size)", font_size);
                }
            }

            if (valid) {
                span->font->font_size = font_size;
                log_debug("[CSS] Font size set to: %.2f px", font_size);
            } else {
                log_debug("[CSS] Font size not set (invalid value)");
            }
            break;
        }

        case CSS_PROPERTY_FONT_WEIGHT: {
            log_debug("[CSS] Processing font-weight property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
                log_debug("[CSS]   Created new FontProp with defaults");
            }

            // Map Lambda CSS value to Lexbor PropValue enum
            PropValue lexbor_weight = map_lambda_font_weight_to_lexbor(value);
            span->font->font_weight = lexbor_weight;

            if (value->type == CSS_VALUE_KEYWORD) {
                log_debug("[CSS] Font weight keyword: '%s' -> Lexbor enum: %d",
                         value->data.keyword, lexbor_weight);
            } else if (value->type == CSS_VALUE_NUMBER || value->type == CSS_VALUE_INTEGER) {
                log_debug("[CSS] Font weight number: %d -> Lexbor enum: %d",
                         (int)value->data.number.value, lexbor_weight);
            }
            break;
        }

        case CSS_PROPERTY_FONT_FAMILY: {
            log_debug("[CSS] Processing font-family property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }

            if (value->type == CSS_VALUE_STRING) {
                // Font family name as string (quotes already stripped during parsing)
                const char* family = value->data.string;
                if (family && strlen(family) > 0) {
                    span->font->family = strdup(family);
                    log_debug("[CSS] Font family: %s", family);
                }
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Keyword font family - check if generic or specific
                const char* keyword = value->data.keyword;
                const char* family = NULL;

                if (strcasecmp(keyword, "serif") == 0 ||
                    strcasecmp(keyword, "sans-serif") == 0 ||
                    strcasecmp(keyword, "monospace") == 0 ||
                    strcasecmp(keyword, "cursive") == 0 ||
                    strcasecmp(keyword, "fantasy") == 0) {
                    // Generic keyword - map it
                    family = map_lambda_font_family_keyword(keyword);
                    log_debug("[CSS] Font family generic keyword: %s -> %s", keyword, family);
                } else {
                    // Specific font name (e.g., Arial, Times) - quotes already stripped during parsing
                    family = keyword;
                    log_debug("[CSS] Font family specific name: %s", family);
                }

                if (family) {
                    span->font->family = strdup(family);
                    log_debug("[CSS] Set span->font->family = '%s' (ptr=%p)", span->font->family, span->font->family);
                }
            } else if (value->type == CSS_VALUE_LIST && value->data.list.count > 0) {
                // List of font families (e.g., "Arial, sans-serif")
                // Use the first available font family
                for (size_t i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;

                    const char* family = NULL;
                    log_debug("[CSS] Font family list item type: %d", item->type);
                    if (item->type == CSS_VALUE_STRING && item->data.string) {
                        family = item->data.string;
                        log_debug("[CSS] Font family STRING value: '%s'", family);
                    } else if (item->type == CSS_VALUE_KEYWORD && item->data.keyword) {
                        // Check if it's a generic font family keyword
                        const char* keyword = item->data.keyword;
                        if (strcasecmp(keyword, "serif") == 0 ||
                            strcasecmp(keyword, "sans-serif") == 0 ||
                            strcasecmp(keyword, "monospace") == 0 ||
                            strcasecmp(keyword, "cursive") == 0 ||
                            strcasecmp(keyword, "fantasy") == 0) {
                            // Generic keyword - map it
                            family = map_lambda_font_family_keyword(keyword);
                        } else {
                            // Specific font name (quotes already stripped during parsing)
                            family = keyword;
                        }
                    }

                    if (family && strlen(family) > 0) {
                        // Quotes already stripped during parsing
                        span->font->family = strdup(family);
                        log_debug("[CSS] Font family from list[%zu]: %s", i, family);
                        break; // Use first font in the list
                    }
                }
            }
            break;
        }

        case CSS_PROPERTY_LINE_HEIGHT: {
            log_debug("[CSS] Processing line-height property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    break; // inline elements don't have line-height in our model
                }
            }

            // Allocate lxb_css_property_line_height_t structure (compatible with Lexbor)
            lxb_css_property_line_height_t* line_height =
                (lxb_css_property_line_height_t*)alloc_prop(lycon, sizeof(lxb_css_property_line_height_t));

            if (!line_height) {
                log_debug("[CSS] Failed to allocate line_height structure");
                break;
            }

            // Line height can be number (multiplier), length, percentage, or 'normal'
            if (value->type == CSS_VALUE_NUMBER) {
                // Unitless number - multiply by font size
                line_height->type = LXB_CSS_VALUE__NUMBER;
                line_height->u.number.num = value->data.number.value;
                log_debug("[CSS] Line height number: %.2f", value->data.number.value);
                block->blk->line_height = line_height;
            } else if (value->type == CSS_VALUE_LENGTH) {
                line_height->type = LXB_CSS_VALUE__LENGTH;
                line_height->u.length.num = value->data.length.value;
                line_height->u.length.is_float = true;
                // Set unit - convert from CSS_UNIT to lxb_css_unit_t
                line_height->u.length.unit = (lxb_css_unit_t)value->data.length.unit;
                log_debug("[CSS] Line height length: %.2f px (unit: %d)", value->data.length.value, value->data.length.unit);
                block->blk->line_height = line_height;
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                line_height->type = LXB_CSS_VALUE__PERCENTAGE;
                line_height->u.percentage.num = value->data.percentage.value;
                log_debug("[CSS] Line height percentage: %.2f%%", value->data.percentage.value);
                block->blk->line_height = line_height;
            } else if (value->type == CSS_VALUE_KEYWORD) {
                const char* keyword = value->data.keyword;
                if (keyword && strcasecmp(keyword, "normal") == 0) {
                    line_height->type = LXB_CSS_VALUE_NORMAL;
                    log_debug("[CSS] Line height keyword: normal");
                    block->blk->line_height = line_height;
                } else if (keyword && strcasecmp(keyword, "inherit") == 0) {
                    line_height->type = LXB_CSS_VALUE_INHERIT;
                    log_debug("[CSS] Line height keyword: inherit");
                    block->blk->line_height = line_height;
                }
            }
            break;
        }

        // ===== GROUP 5: Text Properties =====

        case CSS_PROPERTY_TEXT_ALIGN: {
            log_debug("[CSS] Processing text-align property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    break; // inline elements don't have text-align
                }
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int align_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (align_value > 0) {
                    block->blk->text_align = align_value;
                    log_debug("[CSS] Text-align: %s -> 0x%04X", value->data.keyword, align_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_DECORATION: {
            log_debug("[CSS] Processing text-decoration property");
            if (!span->font) {
                span->font = alloc_font_prop(lycon);
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int deco_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (deco_value > 0) {
                    span->font->text_deco = deco_value;
                    log_debug("[CSS] Text-decoration: %s -> 0x%04X", value->data.keyword, deco_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_VERTICAL_ALIGN: {
            log_debug("[CSS] Processing vertical-align property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int valign_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (valign_value > 0) {
                    span->in_line->vertical_align = valign_value;
                    log_debug("[CSS] Vertical-align: %s -> 0x%04X", value->data.keyword, valign_value);
                }
            } else if (value->type == CSS_VALUE_LENGTH) {
                // Length values for vertical-align (e.g., 10px, -5px)
                // Store as offset - will need to extend PropValue to support length offsets
                log_debug("[CSS] Vertical-align length: %.2f px (not yet fully supported)", value->data.length.value);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                // Percentage values relative to line-height
                log_debug("[CSS] Vertical-align percentage: %.2f%% (not yet fully supported)", value->data.percentage.value);
            }
            break;
        }

        case CSS_PROPERTY_CURSOR: {
            log_debug("[CSS] Processing cursor property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int cursor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (cursor_value > 0) {
                    span->in_line->cursor = cursor_value;
                    log_debug("[CSS] Cursor: %s -> 0x%04X", value->data.keyword, cursor_value);
                }
            }
            break;
        }

        // ===== TODO: More property groups to be added =====

        // ===== GROUP 2: Box Model Basics =====

        case CSS_PROPERTY_WIDTH: {
            log_debug("[CSS] Processing width property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                // per CSS spec, negative values for width are invalid and should be ignored
                if (width < 0.0f) {
                    log_debug("[CSS] Width: %.2f px (negative, ignored per CSS spec)", width);
                    break;
                }
                block->blk->given_width = width;
                lycon->block.given_width = width;  // CRITICAL: Also set in LayoutContext for layout calculation
                block->blk->given_width_type = LXB_CSS_VALUE_INITIAL; // Mark as explicitly set
                log_debug("[CSS] Width: %.2f px", width);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                block->blk->given_width = 0.0f;
                lycon->block.given_width = 0.0f;
                block->blk->given_width_type = LXB_CSS_VALUE_INITIAL;
                log_debug("[CSS] Width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                // Calculate percentage width based on parent width
                float percentage = value->data.percentage.value;
                // per CSS spec, negative percentages for width are invalid
                if (percentage < 0.0f) {
                    log_debug("[CSS] Width: %.2f%% (negative, ignored per CSS spec)", percentage);
                    break;
                }
                // Calculate pixel value from percentage
                float parent_width = lycon->block.pa_block ? lycon->block.pa_block->width : 0;
                float width = percentage * parent_width / 100.0f;
                if (width < 0.0f) {
                    log_debug("[CSS] Width: %.2f%% (calculated %.2f px, negative, ignored)", percentage, width);
                    break;
                }
                block->blk->given_width = width;
                lycon->block.given_width = width;
                block->blk->given_width_type = LXB_CSS_VALUE__PERCENTAGE;
                log_debug("[CSS] Width: %.2f%% of parent %.2f px = %.2f px", percentage, parent_width, width);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Width: auto");
                block->blk->given_width_type = LXB_CSS_VALUE_AUTO;
                lycon->block.given_width = -1.0f;  // -1 means auto in LayoutContext
            }
            break;
        }

        case CSS_PROPERTY_HEIGHT: {
            log_debug("[CSS] Processing height property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_LENGTH) {
                log_debug("[CSS] Height before conversion: %.2f, unit: %d", value->data.length.value, value->data.length.unit);
                float height = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] Height after conversion: %.2f px", height);
                // per CSS spec, negative values for height are invalid and should be ignored
                if (height < 0.0f) {
                    log_debug("[CSS] Height: %.2f px (negative, ignored per CSS spec)", height);
                    break;
                }
                block->blk->given_height = height;
                lycon->block.given_height = height;  // CRITICAL: Also set in LayoutContext for layout calculation
                log_debug("[CSS] Height: %.2f px", height);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for height per CSS spec
                float height = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (height != 0.0f) {
                    log_debug("[CSS] Height: unitless %.2f (invalid, only 0 allowed)", height);
                    break;
                }
                block->blk->given_height = 0.0f;
                lycon->block.given_height = 0.0f;
                log_debug("[CSS] Height: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                // per CSS spec, negative percentages for height are invalid
                if (percentage < 0.0f) {
                    log_debug("[CSS] Height: %.2f%% (negative, ignored per CSS spec)", percentage);
                    break;
                }
                log_debug("[CSS] Height: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Height: auto");
                block->blk->given_height = -1.0f; // -1 means auto
                lycon->block.given_height = -1.0f;  // -1 means auto in LayoutContext
            }
            break;
        }

        case CSS_PROPERTY_MIN_WIDTH: {
            log_debug("[CSS] Processing min-width property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float min_width = convert_lambda_length_to_px(value, lycon, prop_id);
                // per CSS spec, negative values for min-width are invalid and should be ignored
                if (min_width < 0.0f) {
                    log_debug("[CSS] Min-width: %.2f px (negative, ignored per CSS spec)", min_width);
                    break;
                }
                block->blk->given_min_width = min_width;
                log_debug("[CSS] Min-width: %.2f px", min_width);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for min-width per CSS spec
                float min_width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (min_width != 0.0f) {
                    log_debug("[CSS] Min-width: unitless %.2f (invalid, only 0 allowed)", min_width);
                    break;
                }
                block->blk->given_min_width = 0.0f;
                log_debug("[CSS] Min-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                if (percentage < 0.0f) {
                    log_debug("[CSS] Min-width: %.2f%% (negative, ignored per CSS spec)", percentage);
                    break;
                }
                log_debug("[CSS] Min-width: %.2f%% (percentage not yet fully supported)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_MAX_WIDTH: {
            log_debug("[CSS] Processing max-width property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float max_width = convert_lambda_length_to_px(value, lycon, prop_id);
                // per CSS spec, negative values for max-width are invalid and should be ignored
                if (max_width < 0.0f) {
                    log_debug("[CSS] Max-width: %.2f px (negative, ignored per CSS spec)", max_width);
                    break;
                }
                block->blk->given_max_width = max_width;
                log_debug("[CSS] Max-width: %.2f px", max_width);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for max-width per CSS spec
                float max_width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (max_width != 0.0f) {
                    log_debug("[CSS] Max-width: unitless %.2f (invalid, only 0 allowed)", max_width);
                    break;
                }
                block->blk->given_max_width = 0.0f;
                log_debug("[CSS] Max-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                if (percentage < 0.0f) {
                    log_debug("[CSS] Max-width: %.2f%% (negative, ignored per CSS spec)", percentage);
                    break;
                }
                log_debug("[CSS] Max-width: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD && strcasecmp(value->data.keyword, "none") == 0) {
                block->blk->given_max_width = -1.0f; // -1 means none/unlimited
                log_debug("[CSS] Max-width: none");
            }
            break;
        }

        case CSS_PROPERTY_MIN_HEIGHT: {
            log_debug("[CSS] Processing min-height property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float min_height = convert_lambda_length_to_px(value, lycon, prop_id);
                // per CSS spec, negative values for min-height are invalid and should be ignored
                if (min_height < 0.0f) {
                    log_debug("[CSS] Min-height: %.2f px (negative, ignored per CSS spec)", min_height);
                    break;
                }
                block->blk->given_min_height = min_height;
                log_debug("[CSS] Min-height: %.2f px", min_height);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for min-height per CSS spec
                float min_height = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (min_height != 0.0f) {
                    log_debug("[CSS] Min-height: unitless %.2f (invalid, only 0 allowed)", min_height);
                    break;
                }
                block->blk->given_min_height = 0.0f;
                log_debug("[CSS] Min-height: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                if (percentage < 0.0f) {
                    log_debug("[CSS] Min-height: %.2f%% (negative, ignored per CSS spec)", percentage);
                    break;
                }
                log_debug("[CSS] Min-height: %.2f%% (percentage not yet fully supported)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_MAX_HEIGHT: {
            log_debug("[CSS] Processing max-height property");
            if (!block) break;
            if (!block->blk) {
                block->blk = alloc_block_prop(lycon);
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float max_height = convert_lambda_length_to_px(value, lycon, prop_id);
                // per CSS spec, negative values for max-height are invalid and should be ignored
                if (max_height < 0.0f) {
                    log_debug("[CSS] Max-height: %.2f px (negative, ignored per CSS spec)", max_height);
                    break;
                }
                block->blk->given_max_height = max_height;
                log_debug("[CSS] Max-height: %.2f px", max_height);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for max-height per CSS spec
                float max_height = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (max_height != 0.0f) {
                    log_debug("[CSS] Max-height: unitless %.2f (invalid, only 0 allowed)", max_height);
                    break;
                }
                block->blk->given_max_height = 0.0f;
                log_debug("[CSS] Max-height: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                if (percentage < 0.0f) {
                    log_debug("[CSS] Max-height: %.2f%% (negative, ignored per CSS spec)", percentage);
                    break;
                }
                log_debug("[CSS] Max-height: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD && strcasecmp(value->data.keyword, "none") == 0) {
                block->blk->given_max_height = -1.0f; // -1 means none/unlimited
                log_debug("[CSS] Max-height: none");
            }
            break;
        }

        case CSS_PROPERTY_MARGIN: {
            log_debug("[CSS Switch] Entered CSS_PROPERTY_MARGIN case! value type: %d, span: %p, bound: %p",
                    value->type, (void*)span, (void*)(span->bound));
            log_debug("[CSS] Processing margin shorthand property (value type: %d)", value->type);
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
                log_debug("[CSS Switch] Allocated new bound: %p", (void*)(span->bound));
            }            // CSS margin shorthand: 1-4 values (same as padding)
            // 1 value: all sides
            // 2 values: top/bottom, left/right
            // 3 values: top, left/right, bottom
            // 4 values: top, right, bottom, left
            // Note: margins can be length or 'auto' keyword

            if (value->type == CSS_VALUE_LENGTH) {
                // Single value - all sides get same value
                float margin = value->data.length.value;
                span->bound->margin.top = margin;
                span->bound->margin.right = margin;
                span->bound->margin.bottom = margin;
                span->bound->margin.left = margin;
                span->bound->margin.top_specificity = specificity;
                span->bound->margin.right_specificity = specificity;
                span->bound->margin.bottom_specificity = specificity;
                span->bound->margin.left_specificity = specificity;
                log_debug("[CSS] Margin (all): %.2f px", margin);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Single keyword (auto) - all sides get auto
                span->bound->margin.top_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.right_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.bottom_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.left_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.top_specificity = specificity;
                span->bound->margin.right_specificity = specificity;
                span->bound->margin.bottom_specificity = specificity;
                span->bound->margin.left_specificity = specificity;
                log_debug("[CSS] Margin (all): auto");
            } else if (value->type == CSS_VALUE_LIST) {
                // Multi-value margin
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;
                log_debug("[CSS Switch] CSS_VALUE_LIST: count=%zu", count);

                if (count == 2) {
                    // top/bottom, left/right (can be length or auto)
                    log_debug("[CSS Switch] Processing count==2: values[0]->type=%d, values[1]->type=%d",
                            values[0]->type, values[1]->type);
                    // Handle first value (top/bottom)
                    if (values[0]->type == CSS_VALUE_LENGTH) {
                        float vertical = values[0]->data.length.value;
                        span->bound->margin.top = vertical;
                        span->bound->margin.bottom = vertical;
                        log_debug("[CSS Switch] Set margin top/bottom = %.2f", vertical);
                    } else if (values[0]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.top_type = LXB_CSS_VALUE_AUTO;
                        span->bound->margin.bottom_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.top_specificity = specificity;
                    span->bound->margin.bottom_specificity = specificity;

                    // Handle second value (left/right)
                    if (values[1]->type == CSS_VALUE_LENGTH) {
                        float horizontal = values[1]->data.length.value;
                        span->bound->margin.left = horizontal;
                        span->bound->margin.right = horizontal;
                        log_debug("[CSS Switch] Set margin left/right = %.2f", horizontal);
                        log_debug("[CSS] Margin (2 values): %.2f %.2f px",
                                values[0]->data.length.value, horizontal);
                    } else if (values[1]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.left_type = LXB_CSS_VALUE_AUTO;
                        span->bound->margin.right_type = LXB_CSS_VALUE_AUTO;
                        log_debug("[CSS] Margin (2 values): %.2f auto",
                                values[0]->type == CSS_VALUE_LENGTH ? values[0]->data.length.value : 0.0f);
                    }
                    span->bound->margin.left_specificity = specificity;
                    span->bound->margin.right_specificity = specificity;
                } else if (count == 3) {
                    // top, left/right, bottom
                    // Handle top
                    if (values[0]->type == CSS_VALUE_LENGTH) {
                        span->bound->margin.top = values[0]->data.length.value;
                    } else if (values[0]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.top_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.top_specificity = specificity;

                    // Handle left/right
                    if (values[1]->type == CSS_VALUE_LENGTH) {
                        float horizontal = values[1]->data.length.value;
                        span->bound->margin.left = horizontal;
                        span->bound->margin.right = horizontal;
                    } else if (values[1]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.left_type = LXB_CSS_VALUE_AUTO;
                        span->bound->margin.right_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.left_specificity = specificity;
                    span->bound->margin.right_specificity = specificity;

                    // Handle bottom
                    if (values[2]->type == CSS_VALUE_LENGTH) {
                        span->bound->margin.bottom = values[2]->data.length.value;
                    } else if (values[2]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.bottom_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.bottom_specificity = specificity;
                    log_debug("[CSS] Margin (3 values)");
                } else if (count == 4) {
                    // top, right, bottom, left
                    // Handle top
                    if (values[0]->type == CSS_VALUE_LENGTH) {
                        span->bound->margin.top = values[0]->data.length.value;
                    } else if (values[0]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.top_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.top_specificity = specificity;

                    // Handle right
                    if (values[1]->type == CSS_VALUE_LENGTH) {
                        span->bound->margin.right = values[1]->data.length.value;
                    } else if (values[1]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.right_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.right_specificity = specificity;

                    // Handle bottom
                    if (values[2]->type == CSS_VALUE_LENGTH) {
                        span->bound->margin.bottom = values[2]->data.length.value;
                    } else if (values[2]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.bottom_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.bottom_specificity = specificity;

                    // Handle left
                    if (values[3]->type == CSS_VALUE_LENGTH) {
                        span->bound->margin.left = values[3]->data.length.value;
                    } else if (values[3]->type == CSS_VALUE_KEYWORD) {
                        span->bound->margin.left_type = LXB_CSS_VALUE_AUTO;
                    }
                    span->bound->margin.left_specificity = specificity;
                    log_debug("[CSS] Margin (4 values)");
                }
            }
            break;
        }

        case CSS_PROPERTY_PADDING: {
            log_debug("[CSS] Processing padding shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // CSS padding shorthand: 1-4 values
            // 1 value: all sides
            // 2 values: top/bottom, left/right
            // 3 values: top, left/right, bottom
            // 4 values: top, right, bottom, left

            if (value->type == CSS_VALUE_LENGTH || value->type == CSS_VALUE_PERCENTAGE) {
                // Single value - all sides get same value
                float padding;
                if (value->type == CSS_VALUE_LENGTH) {
                    padding = convert_lambda_length_to_px(value, lycon, prop_id);
                } else {
                    // Padding percentages are relative to parent width (per CSS spec)
                    float parent_width = lycon->block.pa_block ? lycon->block.pa_block->width : 0;
                    padding = value->data.percentage.value * parent_width / 100.0f;
                }
                span->bound->padding.top = padding;
                span->bound->padding.right = padding;
                span->bound->padding.bottom = padding;
                span->bound->padding.left = padding;
                span->bound->padding.top_specificity = specificity;
                span->bound->padding.right_specificity = specificity;
                span->bound->padding.bottom_specificity = specificity;
                span->bound->padding.left_specificity = specificity;
                log_debug("[CSS] Padding (all): %.2f px", padding);
            } else if (value->type == CSS_VALUE_LIST) {
                // Multi-value padding
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                // Helper to convert value to pixels
                auto convert_val = [&](CssValue* v) -> float {
                    if (v->type == CSS_VALUE_LENGTH) {
                        return convert_lambda_length_to_px(v, lycon, prop_id);
                    } else if (v->type == CSS_VALUE_PERCENTAGE) {
                        float parent_width = lycon->block.pa_block ? lycon->block.pa_block->width : 0;
                        return v->data.percentage.value * parent_width / 100.0f;
                    }
                    return 0.0f;
                };

                if (count == 2) {
                    // top/bottom, left/right
                    float vertical = convert_val(values[0]);
                    float horizontal = convert_val(values[1]);
                    span->bound->padding.top = vertical;
                    span->bound->padding.bottom = vertical;
                    span->bound->padding.left = horizontal;
                    span->bound->padding.right = horizontal;
                    span->bound->padding.top_specificity = specificity;
                    span->bound->padding.right_specificity = specificity;
                    span->bound->padding.bottom_specificity = specificity;
                    span->bound->padding.left_specificity = specificity;
                    log_debug("[CSS] Padding (vertical/horizontal): %.2f %.2f px", vertical, horizontal);
                } else if (count == 3) {
                    // top, left/right, bottom
                    float top = convert_val(values[0]);
                    float horizontal = convert_val(values[1]);
                    float bottom = convert_val(values[2]);
                    span->bound->padding.top = top;
                    span->bound->padding.left = horizontal;
                    span->bound->padding.right = horizontal;
                    span->bound->padding.bottom = bottom;
                    span->bound->padding.top_specificity = specificity;
                    span->bound->padding.right_specificity = specificity;
                    span->bound->padding.bottom_specificity = specificity;
                    span->bound->padding.left_specificity = specificity;
                    log_debug("[CSS] Padding (3 values): %.2f %.2f %.2f px", top, horizontal, bottom);
                } else if (count == 4) {
                    // top, right, bottom, left
                    span->bound->padding.top = convert_val(values[0]);
                    span->bound->padding.right = convert_val(values[1]);
                    span->bound->padding.bottom = convert_val(values[2]);
                    span->bound->padding.left = convert_val(values[3]);
                    span->bound->padding.top_specificity = specificity;
                    span->bound->padding.right_specificity = specificity;
                    span->bound->padding.bottom_specificity = specificity;
                    span->bound->padding.left_specificity = specificity;
                    log_debug("[CSS] Padding (4 values): %.2f %.2f %.2f %.2f px",
                            span->bound->padding.top, span->bound->padding.right,
                            span->bound->padding.bottom, span->bound->padding.left);
                }
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_TOP: {
            log_debug("[CSS] Processing margin-top property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->margin.top_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.top = margin;
                span->bound->margin.top_specificity = specificity;
                log_debug("[CSS] Margin-top: %.2f px", margin);
            } else if (value->type == CSS_VALUE_NUMBER) {
                float margin = (float)value->data.number.value;
                span->bound->margin.top = margin;
                span->bound->margin.top_specificity = specificity;
                log_debug("[CSS] Margin-top: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->margin.top_specificity = specificity;
                log_debug("[CSS] Margin-top: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword for margins
                span->bound->margin.top_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.top_specificity = specificity;
                log_debug("[CSS] Margin-top: auto");
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_RIGHT: {
            log_debug("[CSS] Processing margin-right property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->margin.right_specificity) {
                break; // lower or equal specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.right = margin;
                span->bound->margin.right_specificity = specificity;
                log_debug("[CSS] Margin-right: %.2f px", margin);
            } else if (value->type == CSS_VALUE_NUMBER) {
                float margin = (float)value->data.number.value;
                span->bound->margin.right = margin;
                span->bound->margin.right_specificity = specificity;
                log_debug("[CSS] Margin-right: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->margin.right_specificity = specificity;
                log_debug("[CSS] Margin-right: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                span->bound->margin.right_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.right_specificity = specificity;
                log_debug("[CSS] Margin-right: auto");
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_BOTTOM: {
            log_debug("[CSS] Processing margin-bottom property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->margin.bottom_specificity) {
                break; // lower or equal specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.bottom = margin;
                span->bound->margin.bottom_specificity = specificity;
                log_debug("[CSS] Margin-bottom: %.2f px", margin);
            } else if (value->type == CSS_VALUE_NUMBER) {
                float margin = (float)value->data.number.value;
                span->bound->margin.bottom = margin;
                span->bound->margin.bottom_specificity = specificity;
                log_debug("[CSS] Margin-bottom: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->margin.bottom_specificity = specificity;
                log_debug("[CSS] Margin-bottom: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                span->bound->margin.bottom_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.bottom_specificity = specificity;
                log_debug("[CSS] Margin-bottom: auto");
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_LEFT: {
            log_debug("[CSS] Processing margin-left property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->margin.left_specificity) {
                break; // lower or equal specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.left = margin;
                span->bound->margin.left_specificity = specificity;
                log_debug("[CSS] Margin-left: %.2f px", margin);
            } else if (value->type == CSS_VALUE_NUMBER) {
                float margin = (float)value->data.number.value;
                span->bound->margin.left = margin;
                span->bound->margin.left_specificity = specificity;
                log_debug("[CSS] Margin-left: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->margin.left_specificity = specificity;
                log_debug("[CSS] Margin-left: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                span->bound->margin.left_type = LXB_CSS_VALUE_AUTO;
                span->bound->margin.left_specificity = specificity;
                log_debug("[CSS] Margin-left: auto");
            }
            break;
        }

        case CSS_PROPERTY_PADDING_TOP: {
            log_debug("[CSS] Processing padding-top property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->padding.top_specificity) {
                break; // lower or equal specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.top = padding;
                span->bound->padding.top_specificity = specificity;
                log_debug("[CSS] Padding-top: %.2f px", padding);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for padding per CSS spec
                float padding = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (padding != 0.0f) {
                    log_debug("[CSS] Padding-top: unitless %.2f (invalid, only 0 allowed)", padding);
                    break;
                }
                span->bound->padding.top = 0.0f;
                span->bound->padding.top_specificity = specificity;
                log_debug("[CSS] Padding-top: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->padding.top_specificity = specificity;
                log_debug("[CSS] Padding-top: %.2f%% (percentage)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_PADDING_RIGHT: {
            log_debug("[CSS] Processing padding-right property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->padding.right_specificity) {
                break; // lower or equal specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.right = padding;
                span->bound->padding.right_specificity = specificity;
                log_debug("[CSS] Padding-right: %.2f px", padding);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for padding per CSS spec
                float padding = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (padding != 0.0f) {
                    log_debug("[CSS] Padding-right: unitless %.2f (invalid, only 0 allowed)", padding);
                    break;
                }
                span->bound->padding.right = 0.0f;
                span->bound->padding.right_specificity = specificity;
                log_debug("[CSS] Padding-right: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->padding.right_specificity = specificity;
                log_debug("[CSS] Padding-right: %.2f%% (percentage)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_PADDING_BOTTOM: {
            log_debug("[CSS] Processing padding-bottom property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->padding.bottom_specificity) {
                break; // lower or equal specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.bottom = padding;
                span->bound->padding.bottom_specificity = specificity;
                log_debug("[CSS] Padding-bottom: %.2f px", padding);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for padding per CSS spec
                float padding = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (padding != 0.0f) {
                    log_debug("[CSS] Padding-bottom: unitless %.2f (invalid, only 0 allowed)", padding);
                    break;
                }
                span->bound->padding.bottom = 0.0f;
                span->bound->padding.bottom_specificity = specificity;
                log_debug("[CSS] Padding-bottom: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->padding.bottom_specificity = specificity;
                log_debug("[CSS] Padding-bottom: %.2f%% (percentage)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_PADDING_LEFT: {
            log_debug("[CSS] Processing padding-left property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->padding.left_specificity) {
                break; // lower or equal specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.left = padding;
                span->bound->padding.left_specificity = specificity;
                log_debug("[CSS] Padding-left: %.2f px", padding);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for padding per CSS spec
                float padding = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (padding != 0.0f) {
                    log_debug("[CSS] Padding-left: unitless %.2f (invalid, only 0 allowed)", padding);
                    break;
                }
                span->bound->padding.left = 0.0f;
                span->bound->padding.left_specificity = specificity;
                log_debug("[CSS] Padding-left: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                span->bound->padding.left_specificity = specificity;
                log_debug("[CSS] Padding-left: %.2f%% (percentage)", percentage);
            }
            break;
        }

        // ===== GROUP 3: Background & Borders =====

        case CSS_PROPERTY_BACKGROUND_COLOR: {
            log_debug("[CSS] Processing background-color property (value type=%d)", value->type);
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            Color bg_color = {0};
            if (value->type == CSS_VALUE_KEYWORD) {
                // Map keyword to color (e.g., "red", "lightgray")
                const char* kw = value->data.keyword ? value->data.keyword : "(null)";
                bg_color.c = map_lambda_color_keyword(value->data.keyword);
                log_debug("[CSS] Background color keyword: '%s' -> 0x%08X", kw, bg_color.c);
            } else if (value->type == CSS_VALUE_COLOR) {
                // Direct RGBA color value
                if (value->data.color.type == CSS_COLOR_RGB) {
                    bg_color.r = value->data.color.data.rgba.r;
                    bg_color.g = value->data.color.data.rgba.g;
                    bg_color.b = value->data.color.data.rgba.b;
                    bg_color.a = value->data.color.data.rgba.a;
                    log_debug("[CSS] Background color RGBA: (%d,%d,%d,%d) -> 0x%08X",
                             bg_color.r, bg_color.g, bg_color.b, bg_color.a, bg_color.c);
                }
            }

            if (bg_color.c != 0) {
                span->bound->background->color = bg_color;
                log_debug("[CSS] Set background color to 0x%08X", bg_color.c);
            } else {
                log_debug("[CSS] Skipping background color (color is 0)");
            }
            break;
        }

        // ===== GROUP 16: Background Advanced Properties =====

        case CSS_PROPERTY_BACKGROUND_ATTACHMENT: {
            log_debug("[CSS] Processing background-attachment property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                // Values: scroll, fixed, local
                log_debug("[CSS] background-attachment: %s", value->data.keyword);
                // TODO: Store attachment value when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_ORIGIN: {
            log_debug("[CSS] Processing background-origin property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                // Values: border-box, padding-box, content-box
                log_debug("[CSS] background-origin: %s", value->data.keyword);
                // TODO: Store origin value when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_CLIP: {
            log_debug("[CSS] Processing background-clip property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                // Values: border-box, padding-box, content-box
                log_debug("[CSS] background-clip: %s", value->data.keyword);
                // TODO: Store clip value when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_POSITION_X: {
            log_debug("[CSS] Processing background-position-x property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float pos_x = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] background-position-x: %.2fpx", pos_x);
                // TODO: Store position-x when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float pos_x_percent = value->data.percentage.value;
                log_debug("[CSS] background-position-x: %.2f%%", pos_x_percent);
                // TODO: Store position-x percentage when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Values: left, center, right
                log_debug("[CSS] background-position-x: %s", value->data.keyword);
                // TODO: Store position-x keyword when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_POSITION_Y: {
            log_debug("[CSS] Processing background-position-y property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float pos_y = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] background-position-y: %.2fpx", pos_y);
                // TODO: Store position-y when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float pos_y_percent = value->data.percentage.value;
                log_debug("[CSS] background-position-y: %.2f%%", pos_y_percent);
                // TODO: Store position-y percentage when BackgroundProp is extended
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Values: top, center, bottom
                log_debug("[CSS] background-position-y: %s", value->data.keyword);
                // TODO: Store position-y keyword when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_BLEND_MODE: {
            log_debug("[CSS] Processing background-blend-mode property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                // Values: normal, multiply, screen, overlay, darken, lighten, etc.
                log_debug("[CSS] background-blend-mode: %s", value->data.keyword);
                // TODO: Store blend mode when BackgroundProp is extended
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_WIDTH: {
            log_debug("[CSS] Processing border-top-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.top_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.top = width;
                span->bound->border->width.top_specificity = specificity;
                log_debug("[CSS] Border-top-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-top-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.top = 0.0f;
                span->bound->border->width.top_specificity = specificity;
                log_debug("[CSS] Border-top-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Keywords: thin (1px), medium (3px), thick (5px)
                const char* keyword = value->data.keyword;
                float width = 3.0f; // default to medium
                if (strcasecmp(keyword, "thin") == 0) width = 1.0f;
                else if (strcasecmp(keyword, "thick") == 0) width = 5.0f;
                span->bound->border->width.top = width;
                span->bound->border->width.top_specificity = specificity;
                log_debug("[CSS] Border-top-width keyword: %s -> %.2f px", keyword, width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RIGHT_WIDTH: {
            log_debug("[CSS] Processing border-right-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.right_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.right = width;
                span->bound->border->width.right_specificity = specificity;
                log_debug("[CSS] Border-right-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-right-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.right = 0.0f;
                span->bound->border->width.right_specificity = specificity;
                log_debug("[CSS] Border-right-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_KEYWORD) {
                const char* keyword = value->data.keyword;
                float width = 3.0f;
                if (strcasecmp(keyword, "thin") == 0) width = 1.0f;
                else if (strcasecmp(keyword, "thick") == 0) width = 5.0f;
                span->bound->border->width.right = width;
                span->bound->border->width.right_specificity = specificity;
                log_debug("[CSS] Border-right-width keyword: %s -> %.2f px", keyword, width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH: {
            log_debug("[CSS] Processing border-bottom-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.bottom_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.bottom = width;
                span->bound->border->width.bottom_specificity = specificity;
                log_debug("[CSS] Border-bottom-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-bottom-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.bottom = 0.0f;
                span->bound->border->width.bottom_specificity = specificity;
                log_debug("[CSS] Border-bottom-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_KEYWORD) {
                const char* keyword = value->data.keyword;
                float width = 3.0f;
                if (strcasecmp(keyword, "thin") == 0) width = 1.0f;
                else if (strcasecmp(keyword, "thick") == 0) width = 5.0f;
                span->bound->border->width.bottom = width;
                span->bound->border->width.bottom_specificity = specificity;
                log_debug("[CSS] Border-bottom-width keyword: %s -> %.2f px", keyword, width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_LEFT_WIDTH: {
            log_debug("[CSS] Processing border-left-width property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->width.left_specificity) {
                break; // lower specificity, skip
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float width = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->width.left = width;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border-left-width: %.2f px", width);
            } else if (value->type == CSS_VALUE_NUMBER) {
                // unitless zero is valid for border-width per CSS spec
                float width = value->data.number.value;
                // per CSS spec, only unitless zero is valid (treated as 0px)
                if (width != 0.0f) {
                    log_debug("[CSS] Border-left-width: unitless %.2f (invalid, only 0 allowed)", width);
                    break;
                }
                span->bound->border->width.left = 0.0f;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border-left-width: 0 (unitless zero)");
            } else if (value->type == CSS_VALUE_KEYWORD) {
                const char* keyword = value->data.keyword;
                float width = 3.0f;
                if (strcasecmp(keyword, "thin") == 0) width = 1.0f;
                else if (strcasecmp(keyword, "thick") == 0) width = 5.0f;
                span->bound->border->width.left = width;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border-left-width keyword: %s -> %.2f px", keyword, width);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_STYLE: {
            log_debug("[CSS] Processing border-top-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_val = map_css_keyword_to_lexbor(value->data.keyword);
                span->bound->border->top_style = lexbor_val;
                log_debug("[CSS] Border-top-style: %s -> %d", value->data.keyword, lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RIGHT_STYLE: {
            log_debug("[CSS] Processing border-right-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_val = map_css_keyword_to_lexbor(value->data.keyword);
                span->bound->border->right_style = lexbor_val;
                log_debug("[CSS] Border-right-style: %s -> %d", value->data.keyword, lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM_STYLE: {
            log_debug("[CSS] Processing border-bottom-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_val = map_css_keyword_to_lexbor(value->data.keyword);
                span->bound->border->bottom_style = lexbor_val;
                log_debug("[CSS] Border-bottom-style: %s -> %d", value->data.keyword, lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_LEFT_STYLE: {
            log_debug("[CSS] Processing border-left-style property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_val = map_css_keyword_to_lexbor(value->data.keyword);
                span->bound->border->left_style = lexbor_val;
                log_debug("[CSS] Border-left-style: %s -> %d", value->data.keyword, lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_COLOR: {
            log_debug("[CSS] Processing border-top-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->top_color_specificity) {
                break; // lower specificity, skip
            }

            Color color = {0};
            if (value->type == CSS_VALUE_KEYWORD) {
                color.c = map_lambda_color_keyword(value->data.keyword);
                log_debug("[CSS] Border-top-color keyword: %s -> 0x%08X", value->data.keyword, color.c);
            } else if (value->type == CSS_VALUE_COLOR) {
                if (value->data.color.type == CSS_COLOR_RGB) {
                    color.r = value->data.color.data.rgba.r;
                    color.g = value->data.color.data.rgba.g;
                    color.b = value->data.color.data.rgba.b;
                    color.a = value->data.color.data.rgba.a;
                    log_debug("[CSS] Border-top-color RGBA: (%d,%d,%d,%d)", color.r, color.g, color.b, color.a);
                }
            }

            if (color.c != 0) {
                span->bound->border->top_color = color;
                span->bound->border->top_color_specificity = specificity;
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RIGHT_COLOR: {
            log_debug("[CSS] Processing border-right-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->right_color_specificity) {
                break; // lower specificity, skip
            }

            Color color = {0};
            if (value->type == CSS_VALUE_KEYWORD) {
                color.c = map_lambda_color_keyword(value->data.keyword);
                log_debug("[CSS] Border-right-color keyword: %s -> 0x%08X", value->data.keyword, color.c);
            } else if (value->type == CSS_VALUE_COLOR) {
                if (value->data.color.type == CSS_COLOR_RGB) {
                    color.r = value->data.color.data.rgba.r;
                    color.g = value->data.color.data.rgba.g;
                    color.b = value->data.color.data.rgba.b;
                    color.a = value->data.color.data.rgba.a;
                    log_debug("[CSS] Border-right-color RGBA: (%d,%d,%d,%d)", color.r, color.g, color.b, color.a);
                }
            }

            if (color.c != 0) {
                span->bound->border->right_color = color;
                span->bound->border->right_color_specificity = specificity;
            }
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM_COLOR: {
            log_debug("[CSS] Processing border-bottom-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->bottom_color_specificity) {
                break; // lower specificity, skip
            }

            Color color = {0};
            if (value->type == CSS_VALUE_KEYWORD) {
                color.c = map_lambda_color_keyword(value->data.keyword);
                log_debug("[CSS] Border-bottom-color keyword: %s -> 0x%08X", value->data.keyword, color.c);
            } else if (value->type == CSS_VALUE_COLOR) {
                if (value->data.color.type == CSS_COLOR_RGB) {
                    color.r = value->data.color.data.rgba.r;
                    color.g = value->data.color.data.rgba.g;
                    color.b = value->data.color.data.rgba.b;
                    color.a = value->data.color.data.rgba.a;
                    log_debug("[CSS] Border-bottom-color RGBA: (%d,%d,%d,%d)", color.r, color.g, color.b, color.a);
                }
            }

            if (color.c != 0) {
                span->bound->border->bottom_color = color;
                span->bound->border->bottom_color_specificity = specificity;
            }
            break;
        }

        case CSS_PROPERTY_BORDER_LEFT_COLOR: {
            log_debug("[CSS] Processing border-left-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Check specificity before overwriting
            if (specificity < span->bound->border->left_color_specificity) {
                break; // lower specificity, skip
            }

            Color color = {0};
            if (value->type == CSS_VALUE_KEYWORD) {
                color.c = map_lambda_color_keyword(value->data.keyword);
                log_debug("[CSS] Border-left-color keyword: %s -> 0x%08X", value->data.keyword, color.c);
            } else if (value->type == CSS_VALUE_COLOR) {
                if (value->data.color.type == CSS_COLOR_RGB) {
                    color.r = value->data.color.data.rgba.r;
                    color.g = value->data.color.data.rgba.g;
                    color.b = value->data.color.data.rgba.b;
                    color.a = value->data.color.data.rgba.a;
                    log_debug("[CSS] Border-left-color RGBA: (%d,%d,%d,%d)", color.r, color.g, color.b, color.a);
                }
            }

            if (color.c != 0) {
                span->bound->border->left_color = color;
                span->bound->border->left_color_specificity = specificity;
            }
            break;
        }

        case CSS_PROPERTY_BORDER: {
            log_debug("[CSS] Processing border shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // Border shorthand: <width> <style> <color> (any order)
            // Parse values from the list or single value
            float border_width = -1.0f;
            int border_style = -1;
            Color border_color = {0};

            if (value->type == CSS_VALUE_LIST) {
                // Multiple values
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                for (size_t i = 0; i < count; i++) {
                    CssValue* val = values[i];
                    if (val->type == CSS_VALUE_LENGTH) {
                        // Width - convert to pixels
                        border_width = convert_lambda_length_to_px(val, lycon, prop_id);
                    } else if (val->type == CSS_VALUE_KEYWORD) {
                        // Could be width keyword, style, or color
                        const char* keyword = val->data.keyword;
                        if (strcasecmp(keyword, "thin") == 0) {
                            border_width = 1.0f;
                        } else if (strcasecmp(keyword, "medium") == 0) {
                            border_width = 3.0f;
                        } else if (strcasecmp(keyword, "thick") == 0) {
                            border_width = 5.0f;
                        } else if (strcasecmp(keyword, "solid") == 0 || strcasecmp(keyword, "dashed") == 0 ||
                                   strcasecmp(keyword, "dotted") == 0 || strcasecmp(keyword, "double") == 0 ||
                                   strcasecmp(keyword, "groove") == 0 || strcasecmp(keyword, "ridge") == 0 ||
                                   strcasecmp(keyword, "inset") == 0 || strcasecmp(keyword, "outset") == 0 ||
                                   strcasecmp(keyword, "none") == 0 || strcasecmp(keyword, "hidden") == 0) {
                            // Style keyword
                            border_style = map_css_keyword_to_lexbor(keyword);
                        } else {
                            // Color keyword
                            border_color.c = map_lambda_color_keyword(keyword);
                        }
                    } else if (val->type == CSS_VALUE_COLOR) {
                        // Color
                        if (val->data.color.type == CSS_COLOR_RGB) {
                            border_color.r = val->data.color.data.rgba.r;
                            border_color.g = val->data.color.data.rgba.g;
                            border_color.b = val->data.color.data.rgba.b;
                            border_color.a = val->data.color.data.rgba.a;
                        }
                    }
                }
            } else {
                // Single value
                if (value->type == CSS_VALUE_LENGTH) {
                    border_width = convert_lambda_length_to_px(value, lycon, prop_id);
                } else if (value->type == CSS_VALUE_KEYWORD) {
                    const char* keyword = value->data.keyword;
                    if (strcasecmp(keyword, "thin") == 0) {
                        border_width = 1.0f;
                    } else if (strcasecmp(keyword, "medium") == 0) {
                        border_width = 3.0f;
                    } else if (strcasecmp(keyword, "thick") == 0) {
                        border_width = 5.0f;
                    } else if (strcasecmp(keyword, "solid") == 0 || strcasecmp(keyword, "dashed") == 0 ||
                               strcasecmp(keyword, "dotted") == 0 || strcasecmp(keyword, "double") == 0 ||
                               strcasecmp(keyword, "groove") == 0 || strcasecmp(keyword, "ridge") == 0 ||
                               strcasecmp(keyword, "inset") == 0 || strcasecmp(keyword, "outset") == 0 ||
                               strcasecmp(keyword, "none") == 0 || strcasecmp(keyword, "hidden") == 0) {
                        border_style = map_css_keyword_to_lexbor(keyword);
                    } else {
                        border_color.c = map_lambda_color_keyword(keyword);
                    }
                } else if (value->type == CSS_VALUE_COLOR) {
                    if (value->data.color.type == CSS_COLOR_RGB) {
                        border_color.r = value->data.color.data.rgba.r;
                        border_color.g = value->data.color.data.rgba.g;
                        border_color.b = value->data.color.data.rgba.b;
                        border_color.a = value->data.color.data.rgba.a;
                    }
                }
            }

            // Apply to all 4 sides
            if (border_width >= 0) {
                span->bound->border->width.top = border_width;
                span->bound->border->width.right = border_width;
                span->bound->border->width.bottom = border_width;
                span->bound->border->width.left = border_width;
                span->bound->border->width.top_specificity = specificity;
                span->bound->border->width.right_specificity = specificity;
                span->bound->border->width.bottom_specificity = specificity;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border width (all sides): %.2f px", border_width);
            }
            if (border_style >= 0) {
                span->bound->border->top_style = border_style;
                span->bound->border->right_style = border_style;
                span->bound->border->bottom_style = border_style;
                span->bound->border->left_style = border_style;
                log_debug("[CSS] Border style (all sides): %d", border_style);
            }
            if (border_color.c != 0) {
                span->bound->border->top_color = border_color;
                span->bound->border->right_color = border_color;
                span->bound->border->bottom_color = border_color;
                span->bound->border->left_color = border_color;
                span->bound->border->top_color_specificity = specificity;
                span->bound->border->right_color_specificity = specificity;
                span->bound->border->bottom_color_specificity = specificity;
                span->bound->border->left_color_specificity = specificity;
                log_debug("[CSS] Border color (all sides): 0x%08X", border_color.c);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP: {
            log_debug("[CSS] Processing border-top shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            // Note: Border-top shorthand sets width, style, and color for top
            log_debug("[CSS] border-top: shorthand parsing not yet fully implemented");
            break;
        }

        case CSS_PROPERTY_BORDER_RIGHT: {
            log_debug("[CSS] Processing border-right shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            log_debug("[CSS] border-right: shorthand parsing not yet fully implemented");
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM: {
            log_debug("[CSS] Processing border-bottom shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            log_debug("[CSS] border-bottom: shorthand parsing not yet fully implemented");
            break;
        }

        case CSS_PROPERTY_BORDER_LEFT: {
            log_debug("[CSS] Processing border-left shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }
            log_debug("[CSS] border-left: shorthand parsing not yet fully implemented");
            break;
        }

        case CSS_PROPERTY_BORDER_STYLE: {
            log_debug("[CSS] Processing border-style shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // CSS border-style shorthand: 1-4 keyword values
            // 1 value: all sides
            // 2 values: top/bottom, left/right
            // 3 values: top, left/right, bottom
            // 4 values: top, right, bottom, left

            if (value->type == CSS_VALUE_KEYWORD) {
                // Single value - all sides get same style
                int border_style = map_css_keyword_to_lexbor(value->data.keyword);
                if (border_style > 0) {
                    span->bound->border->top_style = border_style;
                    span->bound->border->right_style = border_style;
                    span->bound->border->bottom_style = border_style;
                    span->bound->border->left_style = border_style;
                    log_debug("[CSS] Border-style (all): %s -> 0x%04X", value->data.keyword, border_style);
                }
            } else if (value->type == CSS_VALUE_LIST) {
                // Multi-value border-style
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                if (count == 2 && values[0]->type == CSS_VALUE_KEYWORD && values[1]->type == CSS_VALUE_KEYWORD) {
                    // top/bottom, left/right
                    int vertical = map_css_keyword_to_lexbor(values[0]->data.keyword);
                    int horizontal = map_css_keyword_to_lexbor(values[1]->data.keyword);
                    span->bound->border->top_style = vertical;
                    span->bound->border->bottom_style = vertical;
                    span->bound->border->left_style = horizontal;
                    span->bound->border->right_style = horizontal;
                    log_debug("[CSS] Border-style (2 values): %s %s", values[0]->data.keyword, values[1]->data.keyword);
                } else if (count == 3 && values[0]->type == CSS_VALUE_KEYWORD &&
                           values[1]->type == CSS_VALUE_KEYWORD && values[2]->type == CSS_VALUE_KEYWORD) {
                    // top, left/right, bottom
                    int top = map_css_keyword_to_lexbor(values[0]->data.keyword);
                    int horizontal = map_css_keyword_to_lexbor(values[1]->data.keyword);
                    int bottom = map_css_keyword_to_lexbor(values[2]->data.keyword);
                    span->bound->border->top_style = top;
                    span->bound->border->left_style = horizontal;
                    span->bound->border->right_style = horizontal;
                    span->bound->border->bottom_style = bottom;
                    log_debug("[CSS] Border-style (3 values): %s %s %s", values[0]->data.keyword, values[1]->data.keyword, values[2]->data.keyword);
                } else if (count == 4 && values[0]->type == CSS_VALUE_KEYWORD &&
                           values[1]->type == CSS_VALUE_KEYWORD && values[2]->type == CSS_VALUE_KEYWORD &&
                           values[3]->type == CSS_VALUE_KEYWORD) {
                    // top, right, bottom, left
                    int top = map_css_keyword_to_lexbor(values[0]->data.keyword);
                    int right = map_css_keyword_to_lexbor(values[1]->data.keyword);
                    int bottom = map_css_keyword_to_lexbor(values[2]->data.keyword);
                    int left = map_css_keyword_to_lexbor(values[3]->data.keyword);
                    span->bound->border->top_style = top;
                    span->bound->border->right_style = right;
                    span->bound->border->bottom_style = bottom;
                    span->bound->border->left_style = left;
                    log_debug("[CSS] Border-style (4 values): %s %s %s %s", values[0]->data.keyword, values[1]->data.keyword, values[2]->data.keyword, values[3]->data.keyword);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_WIDTH: {
            log_debug("[CSS] Processing border-width shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // CSS border-width shorthand: 1-4 length values
            // 1 value: all sides
            // 2 values: top/bottom, left/right
            // 3 values: top, left/right, bottom
            // 4 values: top, right, bottom, left

            if (value->type == CSS_VALUE_LENGTH) {
                // Single value - all sides get same width
                float width = value->data.length.value;

                // Check specificity for each side before setting
                if (specificity >= span->bound->border->width.top_specificity) {
                    span->bound->border->width.top = width;
                    span->bound->border->width.top_specificity = specificity;
                }
                if (specificity >= span->bound->border->width.right_specificity) {
                    span->bound->border->width.right = width;
                    span->bound->border->width.right_specificity = specificity;
                }
                if (specificity >= span->bound->border->width.bottom_specificity) {
                    span->bound->border->width.bottom = width;
                    span->bound->border->width.bottom_specificity = specificity;
                }
                if (specificity >= span->bound->border->width.left_specificity) {
                    span->bound->border->width.left = width;
                    span->bound->border->width.left_specificity = specificity;
                }
                log_debug("[CSS] Border-width (all): %.2f px", width);
            } else if (value->type == CSS_VALUE_LIST) {
                // Multi-value border-width
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                if (count == 2 && values[0]->type == CSS_VALUE_LENGTH && values[1]->type == CSS_VALUE_LENGTH) {
                    // top/bottom, left/right
                    float vertical = values[0]->data.length.value;
                    float horizontal = values[1]->data.length.value;

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->width.top_specificity) {
                        span->bound->border->width.top = vertical;
                        span->bound->border->width.top_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.bottom_specificity) {
                        span->bound->border->width.bottom = vertical;
                        span->bound->border->width.bottom_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.left_specificity) {
                        span->bound->border->width.left = horizontal;
                        span->bound->border->width.left_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.right_specificity) {
                        span->bound->border->width.right = horizontal;
                        span->bound->border->width.right_specificity = specificity;
                    }
                    log_debug("[CSS] Border-width (2 values): %.2f %.2f px", vertical, horizontal);
                } else if (count == 3 && values[0]->type == CSS_VALUE_LENGTH &&
                           values[1]->type == CSS_VALUE_LENGTH && values[2]->type == CSS_VALUE_LENGTH) {
                    // top, left/right, bottom
                    float top = values[0]->data.length.value;
                    float horizontal = values[1]->data.length.value;
                    float bottom = values[2]->data.length.value;

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->width.top_specificity) {
                        span->bound->border->width.top = top;
                        span->bound->border->width.top_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.left_specificity) {
                        span->bound->border->width.left = horizontal;
                        span->bound->border->width.left_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.right_specificity) {
                        span->bound->border->width.right = horizontal;
                        span->bound->border->width.right_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.bottom_specificity) {
                        span->bound->border->width.bottom = bottom;
                        span->bound->border->width.bottom_specificity = specificity;
                    }
                    log_debug("[CSS] Border-width (3 values): %.2f %.2f %.2f px", top, horizontal, bottom);
                } else if (count == 4 && values[0]->type == CSS_VALUE_LENGTH &&
                           values[1]->type == CSS_VALUE_LENGTH && values[2]->type == CSS_VALUE_LENGTH &&
                           values[3]->type == CSS_VALUE_LENGTH) {
                    // top, right, bottom, left
                    float top = values[0]->data.length.value;
                    float right = values[1]->data.length.value;
                    float bottom = values[2]->data.length.value;
                    float left = values[3]->data.length.value;

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->width.top_specificity) {
                        span->bound->border->width.top = top;
                        span->bound->border->width.top_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.right_specificity) {
                        span->bound->border->width.right = right;
                        span->bound->border->width.right_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.bottom_specificity) {
                        span->bound->border->width.bottom = bottom;
                        span->bound->border->width.bottom_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->width.left_specificity) {
                        span->bound->border->width.left = left;
                        span->bound->border->width.left_specificity = specificity;
                    }
                    log_debug("[CSS] Border-width (4 values): %.2f %.2f %.2f %.2f px", top, right, bottom, left);
                    span->bound->border->width.bottom_specificity = specificity;
                    span->bound->border->width.left_specificity = specificity;
                    log_debug("[CSS] Border-width (4 values): %.2f %.2f %.2f %.2f px", top, right, bottom, left);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_COLOR: {
            log_debug("[CSS] Processing border-color shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // CSS border-color shorthand: 1-4 color values
            // 1 value: all sides
            // 2 values: top/bottom, left/right
            // 3 values: top, left/right, bottom
            // 4 values: top, right, bottom, left

            if (value->type == CSS_VALUE_COLOR || value->type == CSS_VALUE_KEYWORD) {
                // Single value - all sides get same color
                Color color = convert_lambda_color(value);

                // Check specificity for each side before setting
                if (specificity >= span->bound->border->top_color_specificity) {
                    span->bound->border->top_color = color;
                    span->bound->border->top_color_specificity = specificity;
                }
                if (specificity >= span->bound->border->right_color_specificity) {
                    span->bound->border->right_color = color;
                    span->bound->border->right_color_specificity = specificity;
                }
                if (specificity >= span->bound->border->bottom_color_specificity) {
                    span->bound->border->bottom_color = color;
                    span->bound->border->bottom_color_specificity = specificity;
                }
                if (specificity >= span->bound->border->left_color_specificity) {
                    span->bound->border->left_color = color;
                    span->bound->border->left_color_specificity = specificity;
                }
                log_debug("[CSS] Border-color (all): 0x%08X", color.c);
            } else if (value->type == CSS_VALUE_LIST) {
                // Multi-value border-color
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                if (count == 2) {
                    // top/bottom, left/right
                    Color vertical = convert_lambda_color(values[0]);
                    Color horizontal = convert_lambda_color(values[1]);

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->top_color_specificity) {
                        span->bound->border->top_color = vertical;
                        span->bound->border->top_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->bottom_color_specificity) {
                        span->bound->border->bottom_color = vertical;
                        span->bound->border->bottom_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->left_color_specificity) {
                        span->bound->border->left_color = horizontal;
                        span->bound->border->left_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->right_color_specificity) {
                        span->bound->border->right_color = horizontal;
                        span->bound->border->right_color_specificity = specificity;
                    }
                    log_debug("[CSS] Border-color (2 values): 0x%08X 0x%08X", vertical.c, horizontal.c);
                } else if (count == 3) {
                    // top, left/right, bottom
                    Color top = convert_lambda_color(values[0]);
                    Color horizontal = convert_lambda_color(values[1]);
                    Color bottom = convert_lambda_color(values[2]);

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->top_color_specificity) {
                        span->bound->border->top_color = top;
                        span->bound->border->top_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->left_color_specificity) {
                        span->bound->border->left_color = horizontal;
                        span->bound->border->left_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->right_color_specificity) {
                        span->bound->border->right_color = horizontal;
                        span->bound->border->right_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->bottom_color_specificity) {
                        span->bound->border->bottom_color = bottom;
                        span->bound->border->bottom_color_specificity = specificity;
                    }
                    log_debug("[CSS] Border-color (3 values): 0x%08X 0x%08X 0x%08X", top.c, horizontal.c, bottom.c);
                } else if (count == 4) {
                    // top, right, bottom, left
                    Color top = convert_lambda_color(values[0]);
                    Color right = convert_lambda_color(values[1]);
                    Color bottom = convert_lambda_color(values[2]);
                    Color left = convert_lambda_color(values[3]);

                    // Check specificity for each side before setting
                    if (specificity >= span->bound->border->top_color_specificity) {
                        span->bound->border->top_color = top;
                        span->bound->border->top_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->right_color_specificity) {
                        span->bound->border->right_color = right;
                        span->bound->border->right_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->bottom_color_specificity) {
                        span->bound->border->bottom_color = bottom;
                        span->bound->border->bottom_color_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->left_color_specificity) {
                        span->bound->border->left_color = left;
                        span->bound->border->left_color_specificity = specificity;
                    }
                    log_debug("[CSS] Border-color (4 values): 0x%08X 0x%08X 0x%08X 0x%08X", top.c, right.c, bottom.c, left.c);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RADIUS: {
            log_debug("[CSS] Processing border-radius shorthand property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            // CSS border-radius shorthand: 1-4 length values
            // 1 value: all corners
            // 2 values: top-left/bottom-right, top-right/bottom-left
            // 3 values: top-left, top-right/bottom-left, bottom-right
            // 4 values: top-left, top-right, bottom-right, bottom-left

            if (value->type == CSS_VALUE_LENGTH) {
                // Single value - all corners get same radius
                float radius = value->data.length.value;

                // Check specificity before setting each corner
                if (specificity >= span->bound->border->radius.tl_specificity) {
                    span->bound->border->radius.top_left = radius;
                    span->bound->border->radius.tl_specificity = specificity;
                }
                if (specificity >= span->bound->border->radius.tr_specificity) {
                    span->bound->border->radius.top_right = radius;
                    span->bound->border->radius.tr_specificity = specificity;
                }
                if (specificity >= span->bound->border->radius.br_specificity) {
                    span->bound->border->radius.bottom_right = radius;
                    span->bound->border->radius.br_specificity = specificity;
                }
                if (specificity >= span->bound->border->radius.bl_specificity) {
                    span->bound->border->radius.bottom_left = radius;
                    span->bound->border->radius.bl_specificity = specificity;
                }
                log_debug("[CSS] Border-radius (all): %.2f px", radius);
            } else if (value->type == CSS_VALUE_LIST) {
                // Multi-value border-radius
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                if (count == 2 && values[0]->type == CSS_VALUE_LENGTH && values[1]->type == CSS_VALUE_LENGTH) {
                    // top-left/bottom-right, top-right/bottom-left
                    float diagonal1 = values[0]->data.length.value;
                    float diagonal2 = values[1]->data.length.value;

                    if (specificity >= span->bound->border->radius.tl_specificity) {
                        span->bound->border->radius.top_left = diagonal1;
                        span->bound->border->radius.tl_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.tr_specificity) {
                        span->bound->border->radius.top_right = diagonal2;
                        span->bound->border->radius.tr_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.br_specificity) {
                        span->bound->border->radius.bottom_right = diagonal1;
                        span->bound->border->radius.br_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.bl_specificity) {
                        span->bound->border->radius.bottom_left = diagonal2;
                        span->bound->border->radius.bl_specificity = specificity;
                    }
                    log_debug("[CSS] Border-radius (2 values): %.2f %.2f px", diagonal1, diagonal2);
                } else if (count == 3 && values[0]->type == CSS_VALUE_LENGTH &&
                           values[1]->type == CSS_VALUE_LENGTH && values[2]->type == CSS_VALUE_LENGTH) {
                    // top-left, top-right/bottom-left, bottom-right
                    float top_left = values[0]->data.length.value;
                    float diagonal = values[1]->data.length.value;
                    float bottom_right = values[2]->data.length.value;

                    if (specificity >= span->bound->border->radius.tl_specificity) {
                        span->bound->border->radius.top_left = top_left;
                        span->bound->border->radius.tl_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.tr_specificity) {
                        span->bound->border->radius.top_right = diagonal;
                        span->bound->border->radius.tr_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.br_specificity) {
                        span->bound->border->radius.bottom_right = bottom_right;
                        span->bound->border->radius.br_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.bl_specificity) {
                        span->bound->border->radius.bottom_left = diagonal;
                        span->bound->border->radius.bl_specificity = specificity;
                    }
                    log_debug("[CSS] Border-radius (3 values): %.2f %.2f %.2f px", top_left, diagonal, bottom_right);
                } else if (count == 4 && values[0]->type == CSS_VALUE_LENGTH &&
                           values[1]->type == CSS_VALUE_LENGTH && values[2]->type == CSS_VALUE_LENGTH &&
                           values[3]->type == CSS_VALUE_LENGTH) {
                    // top-left, top-right, bottom-right, bottom-left
                    float top_left = values[0]->data.length.value;
                    float top_right = values[1]->data.length.value;
                    float bottom_right = values[2]->data.length.value;
                    float bottom_left = values[3]->data.length.value;

                    if (specificity >= span->bound->border->radius.tl_specificity) {
                        span->bound->border->radius.top_left = top_left;
                        span->bound->border->radius.tl_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.tr_specificity) {
                        span->bound->border->radius.top_right = top_right;
                        span->bound->border->radius.tr_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.br_specificity) {
                        span->bound->border->radius.bottom_right = bottom_right;
                        span->bound->border->radius.br_specificity = specificity;
                    }
                    if (specificity >= span->bound->border->radius.bl_specificity) {
                        span->bound->border->radius.bottom_left = bottom_left;
                        span->bound->border->radius.bl_specificity = specificity;
                    }
                    log_debug("[CSS] Border-radius (4 values): %.2f %.2f %.2f %.2f px", top_left, top_right, bottom_right, bottom_left);
                }
            }
            break;
        }

        // ===== GROUP 15: Additional Border Properties =====

        case CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS: {
            log_debug("[CSS] Processing border-top-left-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float radius = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->radius.top_left = radius;
                span->bound->border->radius.tl_specificity = specificity;
                log_debug("[CSS] border-top-left-radius: %.2fpx", radius);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS: {
            log_debug("[CSS] Processing border-top-right-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float radius = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->radius.top_right = radius;
                span->bound->border->radius.tr_specificity = specificity;
                log_debug("[CSS] border-top-right-radius: %.2fpx", radius);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS: {
            log_debug("[CSS] Processing border-bottom-right-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float radius = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->radius.bottom_right = radius;
                span->bound->border->radius.br_specificity = specificity;
                log_debug("[CSS] border-bottom-right-radius: %.2fpx", radius);
            }
            break;
        }

        case CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS: {
            log_debug("[CSS] Processing border-bottom-left-radius property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->border) {
                span->bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float radius = convert_lambda_length_to_px(value, lycon, prop_id);
                span->bound->border->radius.bottom_left = radius;
                span->bound->border->radius.bl_specificity = specificity;
                log_debug("[CSS] border-bottom-left-radius: %.2fpx", radius);
            }
            break;
        }

        // ===== GROUP 4: Layout Properties =====

        case CSS_PROPERTY_DISPLAY: {
            log_debug("[CSS] Processing display property");
            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_val = map_css_keyword_to_lexbor(value->data.keyword);
                log_debug("[CSS] Display: %s -> %d", value->data.keyword, lexbor_val);                // Set display on the view (ViewGroup has DisplayValue with outer and inner)
                if (block) {
                    // Map single-value display to outer/inner pair following CSS Display Level 3 spec
                    // See: https://www.w3.org/TR/css-display-3/#the-display-properties
                    switch (lexbor_val) {
                        case LXB_CSS_VALUE_BLOCK:
                            block->display.outer = LXB_CSS_VALUE_BLOCK;
                            block->display.inner = LXB_CSS_VALUE_FLOW;
                            break;
                        case LXB_CSS_VALUE_INLINE:
                            block->display.outer = LXB_CSS_VALUE_INLINE;
                            block->display.inner = LXB_CSS_VALUE_FLOW;
                            break;
                        case LXB_CSS_VALUE_INLINE_BLOCK:
                            block->display.outer = LXB_CSS_VALUE_INLINE_BLOCK;
                            block->display.inner = LXB_CSS_VALUE_FLOW;
                            break;
                        case LXB_CSS_VALUE_FLEX:
                            block->display.outer = LXB_CSS_VALUE_BLOCK;
                            block->display.inner = LXB_CSS_VALUE_FLEX;
                            log_debug("[CSS] Display flex: outer=BLOCK, inner=FLEX");
                            break;
                        case LXB_CSS_VALUE_INLINE_FLEX:
                            block->display.outer = LXB_CSS_VALUE_INLINE_BLOCK;
                            block->display.inner = LXB_CSS_VALUE_FLEX;
                            break;
                        case LXB_CSS_VALUE_GRID:
                            block->display.outer = LXB_CSS_VALUE_BLOCK;
                            block->display.inner = LXB_CSS_VALUE_GRID;
                            break;
                        case LXB_CSS_VALUE_INLINE_GRID:
                            block->display.outer = LXB_CSS_VALUE_INLINE;
                            block->display.inner = LXB_CSS_VALUE_GRID;
                            break;
                        case LXB_CSS_VALUE_TABLE:
                            block->display.outer = LXB_CSS_VALUE_BLOCK;
                            block->display.inner = LXB_CSS_VALUE_TABLE;
                            break;
                        case LXB_CSS_VALUE_INLINE_TABLE:
                            block->display.outer = LXB_CSS_VALUE_INLINE;
                            block->display.inner = LXB_CSS_VALUE_TABLE;
                            break;
                        case LXB_CSS_VALUE_LIST_ITEM:
                            block->display.outer = LXB_CSS_VALUE_LIST_ITEM;
                            block->display.inner = LXB_CSS_VALUE_FLOW;
                            break;
                        case LXB_CSS_VALUE_NONE:
                            block->display.outer = LXB_CSS_VALUE_NONE;
                            block->display.inner = LXB_CSS_VALUE_NONE;
                            break;
                        default:
                            // Unknown or unsupported - default to block flow
                            log_debug("[CSS] Unknown display value %d, defaulting to block flow", lexbor_val);
                            block->display.outer = LXB_CSS_VALUE_BLOCK;
                            block->display.inner = LXB_CSS_VALUE_FLOW;
                            break;
                    }
                }
            }
            break;
        }

        case CSS_PROPERTY_POSITION: {
            log_debug("[CSS] Processing position property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_val = map_css_keyword_to_lexbor(value->data.keyword);
                block->position->position = lexbor_val;
                log_debug("[CSS] Position: %s -> %d", value->data.keyword, lexbor_val);
            }
            break;
        }

        case CSS_PROPERTY_TOP: {
            log_debug("[CSS] Processing top property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float top = value->data.length.value;
                block->position->top = top;
                block->position->has_top = true;
                log_debug("[CSS] Top: %.2f px", top);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Top: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Top: auto");
                block->position->has_top = false;
            }
            break;
        }

        case CSS_PROPERTY_LEFT: {
            log_debug("[CSS] Processing left property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float left = value->data.length.value;
                block->position->left = left;
                block->position->has_left = true;
                log_debug("[CSS] Left: %.2f px", left);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Left: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Left: auto");
                block->position->has_left = false;
            }
            break;
        }

        // ===== GROUP 6: Remaining Position Properties =====

        case CSS_PROPERTY_RIGHT: {
            log_debug("[CSS] Processing right property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float right = value->data.length.value;
                block->position->right = right;
                block->position->has_right = true;
                log_debug("[CSS] Right: %.2f px", right);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Right: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Right: auto");
                block->position->has_right = false;
            }
            break;
        }

        case CSS_PROPERTY_BOTTOM: {
            log_debug("[CSS] Processing bottom property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float bottom = value->data.length.value;
                block->position->bottom = bottom;
                block->position->has_bottom = true;
                log_debug("[CSS] Bottom: %.2f px", bottom);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Bottom: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Bottom: auto");
                block->position->has_bottom = false;
            }
            break;
        }

        case CSS_PROPERTY_Z_INDEX: {
            log_debug("[CSS] Processing z-index property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_NUMBER || value->type == CSS_VALUE_INTEGER) {
                int z = (int)value->data.number.value;
                block->position->z_index = z;
                log_debug("[CSS] Z-index: %d", z);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword - typically means z-index = 0
                log_debug("[CSS] Z-index: auto");
                block->position->z_index = 0;
            }
            break;
        }

        // ===== GROUP 7: Float and Clear =====

        case CSS_PROPERTY_FLOAT: {
            log_debug("[CSS] Processing float property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int float_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (float_value > 0) {
                    block->position->float_prop = float_value;
                    log_debug("[CSS] Float: %s -> 0x%04X", value->data.keyword, float_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_CLEAR: {
            log_debug("[CSS] Processing clear property");
            if (!block) break;
            if (!block->position) {
                block->position = (PositionProp*)alloc_prop(lycon, sizeof(PositionProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int clear_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (clear_value > 0) {
                    block->position->clear = clear_value;
                    log_debug("[CSS] Clear: %s -> 0x%04X", value->data.keyword, clear_value);
                }
            }
            break;
        }

        // ===== GROUP 8: Overflow Properties =====

        case CSS_PROPERTY_OVERFLOW: {
            log_debug("[CSS] Processing overflow property (sets both x and y)");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int overflow_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (overflow_value > 0) {
                    block->scroller->overflow_x = overflow_value;
                    block->scroller->overflow_y = overflow_value;
                    log_debug("[CSS] Overflow: %s -> 0x%04X (both x and y)", value->data.keyword, overflow_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_OVERFLOW_X: {
            log_debug("[CSS] Processing overflow-x property");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int overflow_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (overflow_value > 0) {
                    block->scroller->overflow_x = overflow_value;
                    log_debug("[CSS] Overflow-x: %s -> 0x%04X", value->data.keyword, overflow_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_OVERFLOW_Y: {
            log_debug("[CSS] Processing overflow-y property");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int overflow_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (overflow_value > 0) {
                    block->scroller->overflow_y = overflow_value;
                    log_debug("[CSS] Overflow-y: %s -> 0x%04X", value->data.keyword, overflow_value);
                }
            }
            break;
        }

        // ===== GROUP 9: White-space Property =====

        case CSS_PROPERTY_WHITE_SPACE: {
            log_debug("[CSS] Processing white-space property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    break; // inline elements don't have white-space
                }
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int whitespace_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (whitespace_value > 0) {
                    block->blk->white_space = whitespace_value;
                    log_debug("[CSS] White-space: %s -> 0x%04X", value->data.keyword, whitespace_value);
                }
            }
            break;
        }

        // ===== GROUP 10: Visibility and Opacity =====

        case CSS_PROPERTY_VISIBILITY: {
            log_debug("[CSS] Processing visibility property");
            // Visibility applies to all elements, stored in ViewSpan
            if (value->type == CSS_VALUE_KEYWORD) {
                int visibility_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (visibility_value > 0) {
                    span->visibility = visibility_value;
                    log_debug("[CSS] Visibility: %s -> 0x%04X", value->data.keyword, visibility_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_OPACITY: {
            log_debug("[CSS] Processing opacity property");
            if (!span->in_line) {
                span->in_line = (InlineProp*)alloc_prop(lycon, sizeof(InlineProp));
            }

            if (value->type == CSS_VALUE_NUMBER) {
                float opacity = value->data.number.value;
                // Clamp opacity to 0.0 - 1.0 range
                if (opacity < 0.0f) opacity = 0.0f;
                if (opacity > 1.0f) opacity = 1.0f;
                span->in_line->opacity = opacity;
                log_debug("[CSS] Opacity: %.2f", opacity);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float opacity = value->data.percentage.value / 100.0f;
                // Clamp opacity to 0.0 - 1.0 range
                if (opacity < 0.0f) opacity = 0.0f;
                if (opacity > 1.0f) opacity = 1.0f;
                span->in_line->opacity = opacity;
                log_debug("[CSS] Opacity: %.2f%% -> %.2f", value->data.percentage.value, opacity);
            }
            break;
        }

        case CSS_PROPERTY_CLIP: {
            log_debug("[CSS] Processing clip property");
            if (!block) break;
            if (!block->scroller) {
                block->scroller = (ScrollProp*)alloc_prop(lycon, sizeof(ScrollProp));
            }

            // CSS clip property uses rect(top, right, bottom, left) syntax
            // For now, just log that it's present. Full rect parsing would be complex.
            // The clip field exists in ScrollProp as a Bound structure
            log_debug("[CSS] Clip property detected (rect parsing not yet implemented)");
            block->scroller->has_clip = true;
            // TODO: Parse rect() values and set block->scroller->clip bounds
            break;
        }

        // ===== GROUP 11: Box Sizing =====

        case CSS_PROPERTY_BOX_SIZING: {
            log_debug("[CSS] Processing box-sizing property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    break; // inline elements don't have box-sizing
                }
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int boxsizing_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (boxsizing_value > 0) {
                    block->blk->box_sizing = boxsizing_value;
                    log_debug("[CSS] Box-sizing: %s -> 0x%04X", value->data.keyword, boxsizing_value);
                }
            }
            break;
        }

        // ===== GROUP 12: Advanced Typography Properties =====

        case CSS_PROPERTY_FONT_STYLE: {
            log_debug("[CSS] Processing font-style property");
            if (!span->font) {
                log_debug("[CSS] font-style: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    span->font->font_style = lexbor_value;
                    log_debug("[CSS] font-style: %s -> 0x%04X", value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_TRANSFORM: {
            log_debug("[CSS] Processing text-transform property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] text-transform: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    // Note: Adding text_transform field to BlockProp would be needed
                    // For now, log the value that would be set
                    log_debug("[CSS] text-transform: %s -> 0x%04X (field not yet added to BlockProp)",
                             value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_OVERFLOW: {
            log_debug("[CSS] Processing text-overflow property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] text-overflow: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    // Note: Adding text_overflow field to BlockProp would be needed
                    log_debug("[CSS] text-overflow: %s -> 0x%04X (field not yet added to BlockProp)",
                             value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_WORD_BREAK: {
            log_debug("[CSS] Processing word-break property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] word-break: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    // Note: Adding word_break field to BlockProp would be needed
                    log_debug("[CSS] word-break: %s -> 0x%04X (field not yet added to BlockProp)",
                             value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_WORD_WRAP: {
            log_debug("[CSS] Processing word-wrap property");
            if (!block || !block->blk) {
                if (block) {
                    block->blk = alloc_block_prop(lycon);
                } else {
                    log_debug("[CSS] word-wrap: Cannot apply to inline element without block context");
                    break;
                }
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    // Note: Adding word_wrap field to BlockProp would be needed
                    log_debug("[CSS] word-wrap: %s -> 0x%04X (field not yet added to BlockProp)",
                             value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_FONT_VARIANT: {
            log_debug("[CSS] Processing font-variant property");
            if (!span->font) {
                log_debug("[CSS] font-variant: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    // Note: Adding font_variant field to FontProp would be needed
                    log_debug("[CSS] font-variant: %s -> 0x%04X (field not yet added to FontProp)",
                             value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_LETTER_SPACING: {
            log_debug("[CSS] Processing letter-spacing property");
            if (!span->font) {
                log_debug("[CSS] letter-spacing: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float spacing = convert_lambda_length_to_px(value, lycon, prop_id);
                // Note: Adding letter_spacing field to FontProp would be needed
                log_debug("[CSS] letter-spacing: %.2fpx (field not yet added to FontProp)", spacing);
            } else if (value->type == CSS_VALUE_KEYWORD && strcasecmp(value->data.keyword, "normal") == 0) {
                log_debug("[CSS] letter-spacing: normal -> 0px (field not yet added to FontProp)");
            }
            break;
        }

        case CSS_PROPERTY_WORD_SPACING: {
            log_debug("[CSS] Processing word-spacing property");
            if (!span->font) {
                log_debug("[CSS] word-spacing: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float spacing = convert_lambda_length_to_px(value, lycon, prop_id);
                // Note: Adding word_spacing field to FontProp would be needed
                log_debug("[CSS] word-spacing: %.2fpx (field not yet added to FontProp)", spacing);
            } else if (value->type == CSS_VALUE_KEYWORD && strcasecmp(value->data.keyword, "normal") == 0) {
                log_debug("[CSS] word-spacing: normal -> 0px (field not yet added to FontProp)");
            }
            break;
        }

        case CSS_PROPERTY_TEXT_SHADOW: {
            log_debug("[CSS] Processing text-shadow property");
            if (!span->font) {
                log_debug("[CSS] text-shadow: FontProp is NULL");
                break;
            }

            if (value->type == CSS_VALUE_KEYWORD && strcasecmp(value->data.keyword, "none") == 0) {
                log_debug("[CSS] text-shadow: none (field not yet added to FontProp)");
            } else {
                // TODO: Parse shadow offset, blur, and color
                // For now, just log that shadow is set
                log_debug("[CSS] text-shadow: complex value (needs full shadow parsing and field not yet added)");
            }
            break;
        }

        // ===== GROUP 13: Flexbox Properties =====

        case CSS_PROPERTY_FLEX_DIRECTION: {
            log_debug("[CSS] Processing flex-direction property");
            if (!block) {
                log_debug("[CSS] flex-direction: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed (same as resolve_style.cpp)
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    block->embed->flex->direction = lexbor_value;
                    log_debug("[CSS] flex-direction: %s -> 0x%04X", value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_FLEX_WRAP: {
            log_debug("[CSS] Processing flex-wrap property");
            if (!block) {
                log_debug("[CSS] flex-wrap: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    block->embed->flex->wrap = lexbor_value;
                    log_debug("[CSS] flex-wrap: %s -> 0x%04X", value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_JUSTIFY_CONTENT: {
            log_debug("[CSS] Processing justify-content property");
            if (!block) {
                log_debug("[CSS] justify-content: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    block->embed->flex->justify = lexbor_value;
                    log_debug("[CSS] justify-content: %s -> 0x%04X", value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_ITEMS: {
            log_debug("[CSS] Processing align-items property");
            if (!block) {
                log_debug("[CSS] align-items: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    block->embed->flex->align_items = lexbor_value;
                    log_debug("[CSS] align-items: %s -> 0x%04X", value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_CONTENT: {
            log_debug("[CSS] Processing align-content property");
            if (!block) {
                log_debug("[CSS] align-content: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    block->embed->flex->align_content = lexbor_value;
                    log_debug("[CSS] align-content: %s -> 0x%04X", value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_FLEX_GROW: {
            log_debug("[CSS] Processing flex-grow property");
            if (value->type == CSS_VALUE_NUMBER) {
                float grow_value = (float)value->data.number.value;
                span->flex_grow = grow_value;
                log_debug("[CSS] flex-grow: %.2f", grow_value);
            }
            break;
        }

        case CSS_PROPERTY_FLEX_SHRINK: {
            log_debug("[CSS] Processing flex-shrink property");
            if (value->type == CSS_VALUE_NUMBER) {
                float shrink_value = (float)value->data.number.value;
                span->flex_shrink = shrink_value;
                log_debug("[CSS] flex-shrink: %.2f", shrink_value);
            }
            break;
        }

        case CSS_PROPERTY_FLEX_BASIS: {
            log_debug("[CSS] Processing flex-basis property");
            if (value->type == CSS_VALUE_KEYWORD && strcasecmp(value->data.keyword, "auto") == 0) {
                span->flex_basis = -1; // -1 indicates auto
                span->flex_basis_is_percent = false;
                log_debug("[CSS] flex-basis: auto");
            } else if (value->type == CSS_VALUE_LENGTH) {
                float basis_value = convert_lambda_length_to_px(value, lycon, prop_id);
                span->flex_basis = (int)basis_value;
                span->flex_basis_is_percent = false;
                log_debug("[CSS] flex-basis: %.2fpx", basis_value);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                span->flex_basis = (int)value->data.percentage.value;
                span->flex_basis_is_percent = true;
                log_debug("[CSS] flex-basis: %d%%", span->flex_basis);
            }
            break;
        }

        case CSS_PROPERTY_ORDER: {
            log_debug("[CSS] Processing order property");
            if (value->type == CSS_VALUE_NUMBER || value->type == CSS_VALUE_INTEGER) {
                int order_value = (int)value->data.number.value;
                span->order = order_value;
                log_debug("[CSS] order: %d", order_value);
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_SELF: {
            log_debug("[CSS] Processing align-self property");
            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_value = map_css_keyword_to_lexbor(value->data.keyword);
                if (lexbor_value > 0) {
                    span->align_self = lexbor_value;
                    log_debug("[CSS] align-self: %s -> 0x%04X", value->data.keyword, lexbor_value);
                }
            }
            break;
        }

        case CSS_PROPERTY_FLEX_FLOW: {
            log_debug("[CSS] Processing flex-flow shorthand property");
            if (!block) {
                log_debug("[CSS] flex-flow: Cannot apply to non-block element");
                break;
            }

            // Allocate FlexProp if needed
            alloc_flex_prop(lycon, block);

            // Note: flex-flow is a shorthand for flex-direction and flex-wrap
            // Would need to parse both values from the declaration
            // For now, just log
            log_debug("[CSS] flex-flow: shorthand parsing not yet fully implemented");
            break;
        }

        case CSS_PROPERTY_FLEX: {
            log_debug("[CSS] Processing flex shorthand property");
            // flex is a shorthand for flex-grow, flex-shrink, and flex-basis
            // Common values: auto (1 1 auto), none (0 0 auto), <grow> (grow 1 0)
            // For now, just log
            log_debug("[CSS] flex: shorthand parsing not yet fully implemented");
            break;
        }

        // Animation Properties (Group 14)
        case CSS_PROPERTY_ANIMATION: {
            log_debug("[CSS] Processing animation shorthand property");
            // Note: Animation shorthand would be parsed into individual properties
            // For now, just log the value
            if (value->type == CSS_VALUE_KEYWORD) {
                log_debug("[CSS] animation: %s", value->data.keyword);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_NAME: {
            log_debug("[CSS] Processing animation-name property");
            if (value->type == CSS_VALUE_KEYWORD) {
                if (strcasecmp(value->data.keyword, "none") == 0) {
                    log_debug("[CSS] animation-name: none");
                } else {
                    log_debug("[CSS] animation-name: %s", value->data.keyword);
                }
            } else if (value->type == CSS_VALUE_STRING) {
                log_debug("[CSS] animation-name: \"%s\"", value->data.string);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_DURATION: {
            log_debug("[CSS] Processing animation-duration property");
            if (value->type == CSS_VALUE_TIME) {
                float duration = (float)value->data.length.value;
                log_debug("[CSS] animation-duration: %.3fs", duration);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_TIMING_FUNCTION: {
            log_debug("[CSS] Processing animation-timing-function property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* timing = value->data.keyword;
                int lexbor_value = map_css_keyword_to_lexbor(timing);
                if (lexbor_value > 0) {
                    log_debug("[CSS] animation-timing-function: %s -> 0x%04X", timing, lexbor_value);
                } else {
                    log_debug("[CSS] animation-timing-function: %s", timing);
                }
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_DELAY: {
            log_debug("[CSS] Processing animation-delay property");
            if (value->type == CSS_VALUE_TIME) {
                float delay = (float)value->data.length.value;
                log_debug("[CSS] animation-delay: %.3fs", delay);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_ITERATION_COUNT: {
            log_debug("[CSS] Processing animation-iteration-count property");
            if (value->type == CSS_VALUE_KEYWORD && strcasecmp(value->data.keyword, "infinite") == 0) {
                log_debug("[CSS] animation-iteration-count: infinite");
            } else if (value->type == CSS_VALUE_NUMBER) {
                float count = (float)value->data.number.value;
                log_debug("[CSS] animation-iteration-count: %.2f", count);
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_DIRECTION: {
            log_debug("[CSS] Processing animation-direction property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* direction = value->data.keyword;
                int lexbor_value = map_css_keyword_to_lexbor(direction);
                if (lexbor_value > 0) {
                    log_debug("[CSS] animation-direction: %s -> 0x%04X", direction, lexbor_value);
                } else {
                    log_debug("[CSS] animation-direction: %s", direction);
                }
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_FILL_MODE: {
            log_debug("[CSS] Processing animation-fill-mode property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* fill_mode = value->data.keyword;
                int lexbor_value = map_css_keyword_to_lexbor(fill_mode);
                if (lexbor_value > 0) {
                    log_debug("[CSS] animation-fill-mode: %s -> 0x%04X", fill_mode, lexbor_value);
                } else {
                    log_debug("[CSS] animation-fill-mode: %s", fill_mode);
                }
            }
            break;
        }

        case CSS_PROPERTY_ANIMATION_PLAY_STATE: {
            log_debug("[CSS] Processing animation-play-state property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* play_state = value->data.keyword;
                if (strcasecmp(play_state, "running") == 0) {
                    log_debug("[CSS] animation-play-state: running");
                } else if (strcasecmp(play_state, "paused") == 0) {
                    log_debug("[CSS] animation-play-state: paused");
                } else {
                    log_debug("[CSS] animation-play-state: %s", play_state);
                }
            }
            break;
        }

        // Table Properties (Group 17)
        case CSS_PROPERTY_TABLE_LAYOUT: {
            log_debug("[CSS] Processing table-layout property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* layout = value->data.keyword;
                if (strcasecmp(layout, "auto") == 0) {
                    log_debug("[CSS] table-layout: auto");
                } else if (strcasecmp(layout, "fixed") == 0) {
                    log_debug("[CSS] table-layout: fixed");
                } else {
                    log_debug("[CSS] table-layout: %s", layout);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_COLLAPSE: {
            log_debug("[CSS] Processing border-collapse property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* collapse = value->data.keyword;
                int lexbor_value = map_css_keyword_to_lexbor(collapse);
                if (lexbor_value > 0) {
                    log_debug("[CSS] border-collapse: %s -> 0x%04X", collapse, lexbor_value);
                } else {
                    log_debug("[CSS] border-collapse: %s", collapse);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_SPACING: {
            log_debug("[CSS] Processing border-spacing property");
            if (value->type == CSS_VALUE_LENGTH) {
                double spacing = convert_lambda_length_to_px(value, lycon, prop_id);
                log_debug("[CSS] border-spacing: %.2fpx", spacing);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                log_debug("[CSS] border-spacing: %s", value->data.keyword);
            }
            break;
        }

        case CSS_PROPERTY_CAPTION_SIDE: {
            log_debug("[CSS] Processing caption-side property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* side = value->data.keyword;
                if (strcasecmp(side, "top") == 0) {
                    log_debug("[CSS] caption-side: top");
                } else if (strcasecmp(side, "bottom") == 0) {
                    log_debug("[CSS] caption-side: bottom");
                } else {
                    log_debug("[CSS] caption-side: %s", side);
                }
            }
            break;
        }

        case CSS_PROPERTY_EMPTY_CELLS: {
            log_debug("[CSS] Processing empty-cells property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* cells = value->data.keyword;
                int lexbor_value = map_css_keyword_to_lexbor(cells);
                if (lexbor_value > 0) {
                    log_debug("[CSS] empty-cells: %s -> 0x%04X", cells, lexbor_value);
                } else {
                    log_debug("[CSS] empty-cells: %s", cells);
                }
            }
            break;
        }

        // List Properties (Group 18)
        case CSS_PROPERTY_LIST_STYLE_TYPE: {
            log_debug("[CSS] Processing list-style-type property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* type = value->data.keyword;
                int lexbor_value = map_css_keyword_to_lexbor(type);
                if (lexbor_value > 0) {
                    log_debug("[CSS] list-style-type: %s -> 0x%04X", type, lexbor_value);
                } else {
                    log_debug("[CSS] list-style-type: %s", type);
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_POSITION: {
            log_debug("[CSS] Processing list-style-position property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* position = value->data.keyword;
                int lexbor_value = map_css_keyword_to_lexbor(position);
                if (lexbor_value > 0) {
                    log_debug("[CSS] list-style-position: %s -> 0x%04X", position, lexbor_value);
                } else {
                    log_debug("[CSS] list-style-position: %s", position);
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_IMAGE: {
            log_debug("[CSS] Processing list-style-image property");
            if (value->type == CSS_VALUE_URL) {
                log_debug("[CSS] list-style-image: %s", value->data.url);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                if (strcasecmp(value->data.keyword, "none") == 0) {
                    log_debug("[CSS] list-style-image: none");
                } else {
                    log_debug("[CSS] list-style-image: %s", value->data.keyword);
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE: {
            log_debug("[CSS] Processing list-style shorthand property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* style = value->data.keyword;
                log_debug("[CSS] list-style: %s", style);
                // Note: Shorthand parsing would need more complex implementation
            }
            break;
        }

        case CSS_PROPERTY_COUNTER_RESET: {
            log_debug("[CSS] Processing counter-reset property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* reset = value->data.keyword;
                if (strcasecmp(reset, "none") == 0) {
                    log_debug("[CSS] counter-reset: none");
                } else {
                    log_debug("[CSS] counter-reset: %s", reset);
                }
            }
            break;
        }

        case CSS_PROPERTY_COUNTER_INCREMENT: {
            log_debug("[CSS] Processing counter-increment property");
            if (value->type == CSS_VALUE_KEYWORD) {
                const char* increment = value->data.keyword;
                if (strcasecmp(increment, "none") == 0) {
                    log_debug("[CSS] counter-increment: none");
                } else {
                    log_debug("[CSS] counter-increment: %s", increment);
                }
            }
            break;
        }

        default:
            // Unknown or unimplemented property
            log_debug("[CSS] Unimplemented property: %d", prop_id);
            break;
    }
}

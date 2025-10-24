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
    {"auto", 0x000c},      // LXB_CSS_VALUE_AUTO
    {"baseline", 0x000b},  // LXB_CSS_VALUE_BASELINE
    {"block", 0x00ef},     // LXB_CSS_VALUE_BLOCK
    {"border-box", 0x002a}, // LXB_CSS_VALUE_BORDER_BOX
    {"both", 0x0174},      // LXB_CSS_VALUE_BOTH
    {"bottom", 0x0019},    // LXB_CSS_VALUE_BOTTOM

    // Font and text values
    {"bold", 0x013d},      // LXB_CSS_VALUE_BOLD
    {"bolder", 0x013e},    // LXB_CSS_VALUE_BOLDER

    // Positioning and alignment
    {"center", 0x0007},    // LXB_CSS_VALUE_CENTER
    {"content-box", 0x0029}, // LXB_CSS_VALUE_CONTENT_BOX
    {"currentcolor", 0x0031}, // LXB_CSS_VALUE_CURRENTCOLOR

    // Border styles
    {"dashed", 0x0022},    // LXB_CSS_VALUE_DASHED
    {"dotted", 0x0021},    // LXB_CSS_VALUE_DOTTED
    {"double", 0x0024},    // LXB_CSS_VALUE_DOUBLE

    // Display types
    {"flex", 0x00f5},      // LXB_CSS_VALUE_FLEX
    {"fixed", 0x0151},     // LXB_CSS_VALUE_FIXED

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

    // Layout display
    {"inline", 0x00f0},    // LXB_CSS_VALUE_INLINE
    {"inline-block", 0x00f1}, // LXB_CSS_VALUE_INLINE_BLOCK
    {"inline-flex", 0x00f2},  // LXB_CSS_VALUE_INLINE_FLEX
    {"inline-grid", 0x00f3},  // LXB_CSS_VALUE_INLINE_GRID

    // Font styles
    {"italic", 0x013b},    // LXB_CSS_VALUE_ITALIC

    // Text alignment
    {"justify", 0x0152},   // LXB_CSS_VALUE_JUSTIFY

    // Alignment
    {"left", 0x002f},      // LXB_CSS_VALUE_LEFT
    {"line-through", 0x0159}, // LXB_CSS_VALUE_LINE_THROUGH

    // Vertical alignment
    {"middle", 0x0010},    // LXB_CSS_VALUE_MIDDLE
    {"move", 0x00ec},      // LXB_CSS_VALUE_MOVE

    // Display and text
    {"none", 0x001f},      // LXB_CSS_VALUE_NONE
    {"normal", 0x0132},    // LXB_CSS_VALUE_NORMAL
    {"nowrap", 0x0111},    // LXB_CSS_VALUE_NOWRAP

    // Font styles
    {"oblique", 0x013c},   // LXB_CSS_VALUE_OBLIQUE

    // Colors
    {"orange", 0x009d},    // LXB_CSS_VALUE_ORANGE
    {"overline", 0x0158},  // LXB_CSS_VALUE_OVERLINE

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
    {"right", 0x0030},     // LXB_CSS_VALUE_RIGHT

    // Overflow
    {"scroll", 0x014b},    // LXB_CSS_VALUE_SCROLL
    {"silver", 0x00b5},    // LXB_CSS_VALUE_SILVER
    {"solid", 0x0023},     // LXB_CSS_VALUE_SOLID
    {"static", 0x014d},    // LXB_CSS_VALUE_STATIC
    {"sticky", 0x0150},    // LXB_CSS_VALUE_STICKY
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

    // Overflow
    {"visible", 0x0149},     // LXB_CSS_VALUE_VISIBLE

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
    if (!keyword) return 0x000000FF; // default black

    // Map CSS color keywords to RGBA values
    // Format: 0xRRGGBBAA
    if (strcasecmp(keyword, "black") == 0) return 0x000000FF;
    if (strcasecmp(keyword, "white") == 0) return 0xFFFFFFFF;
    if (strcasecmp(keyword, "red") == 0) return 0xFF0000FF;
    if (strcasecmp(keyword, "green") == 0) return 0x008000FF;
    if (strcasecmp(keyword, "blue") == 0) return 0x0000FFFF;
    if (strcasecmp(keyword, "yellow") == 0) return 0xFFFF00FF;
    if (strcasecmp(keyword, "cyan") == 0) return 0x00FFFFFF;
    if (strcasecmp(keyword, "magenta") == 0) return 0xFF00FFFF;
    if (strcasecmp(keyword, "gray") == 0) return 0x808080FF;
    if (strcasecmp(keyword, "grey") == 0) return 0x808080FF;
    if (strcasecmp(keyword, "silver") == 0) return 0xC0C0C0FF;
    if (strcasecmp(keyword, "lightgray") == 0) return 0xD3D3D3FF;
    if (strcasecmp(keyword, "lightgrey") == 0) return 0xD3D3D3FF;
    if (strcasecmp(keyword, "darkgray") == 0) return 0xA9A9A9FF;
    if (strcasecmp(keyword, "darkgrey") == 0) return 0xA9A9A9FF;
    if (strcasecmp(keyword, "maroon") == 0) return 0x800000FF;
    if (strcasecmp(keyword, "purple") == 0) return 0x800080FF;
    if (strcasecmp(keyword, "fuchsia") == 0) return 0xFF00FFFF;
    if (strcasecmp(keyword, "lime") == 0) return 0x00FF00FF;
    if (strcasecmp(keyword, "olive") == 0) return 0x808000FF;
    if (strcasecmp(keyword, "navy") == 0) return 0x000080FF;
    if (strcasecmp(keyword, "teal") == 0) return 0x008080FF;
    if (strcasecmp(keyword, "aqua") == 0) return 0x00FFFFFF;
    if (strcasecmp(keyword, "orange") == 0) return 0xFFA500FF;
    if (strcasecmp(keyword, "transparent") == 0) return 0x00000000;

    // TODO: Add more color keywords (148 total CSS3 colors)

    return 0x000000FF; // default to black
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

int map_lambda_font_weight_keyword(const char* keyword) {
    if (!keyword) return 400;

    // Map font-weight keywords to numeric values
    if (strcasecmp(keyword, "normal") == 0) return 400;
    if (strcasecmp(keyword, "bold") == 0) return 700;
    if (strcasecmp(keyword, "bolder") == 0) return 900;  // simplified
    if (strcasecmp(keyword, "lighter") == 0) return 300; // simplified

    return 400; // default normal
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

    log_debug("  Processing property ID: %d", prop_id);

    // get the CSS declaration for this property
    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl) {
        log_debug("  No declaration found for property %d", prop_id);
        return true; // continue iteration
    }

    // resolve this property
    resolve_lambda_css_property(prop_id, decl, lycon);

    return true; // continue iteration
}

void resolve_lambda_css_styles(DomElement* dom_elem, LayoutContext* lycon) {
    if (!dom_elem || !lycon) {
        log_debug("[Lambda CSS] resolve_lambda_css_styles: null input (dom_elem=%p, lycon=%p)", dom_elem, lycon);
        return;
    }

    log_debug("[Lambda CSS] Resolving styles for element");

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
}

void resolve_lambda_css_property(CssPropertyId prop_id, const CssDeclaration* decl,
                                  LayoutContext* lycon) {
    log_debug("[Lambda CSS Property] resolve_lambda_css_property called: prop_id=%d", prop_id);

    if (!decl || !lycon || !lycon->view) {
        log_debug("[Lambda CSS Property] Early return: decl=%p, lycon=%p, view=%p",
                  decl, lycon, lycon ? lycon->view : NULL);
        return;
    }

    const CssValue* value = decl->value;
    if (!value) {
        log_debug("[Lambda CSS Property] No value in declaration");
        return;
    }

    log_debug("[Lambda CSS Property] Processing property %d, value type=%d", prop_id, value->type);

    int32_t specificity = get_lambda_specificity(decl);
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
                span->font = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
            }

            float font_size = 0.0f;
            if (value->type == CSS_VALUE_LENGTH) {
                font_size = value->data.length.value; // Use struct accessor
                log_debug("[CSS] Font size length: %.2f px", font_size);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                // Percentage of parent font size
                float parent_size = span->font->font_size > 0 ? span->font->font_size : 16.0f;
                font_size = parent_size * (value->data.percentage.value / 100.0f);
                log_debug("[CSS] Font size percentage: %.2f%% -> %.2f px", value->data.percentage.value, font_size);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Named font sizes: small, medium, large, etc.
                font_size = map_lambda_font_size_keyword(value->data.keyword);
                log_debug("[CSS] Font size keyword: %s -> %.2f px", value->data.keyword, font_size);
            }

            if (font_size > 0) {
                span->font->font_size = font_size;
            }
            break;
        }

        case CSS_PROPERTY_FONT_WEIGHT: {
            log_debug("[CSS] Processing font-weight property");
            if (!span->font) {
                span->font = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
            }

            int weight = 0;
            if (value->type == CSS_VALUE_NUMBER || value->type == CSS_VALUE_INTEGER) {
                // Numeric weight: 100, 200, ..., 900
                weight = (int)value->data.number.value;
                log_debug("[CSS] Font weight number: %d", weight);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Keyword: normal (400), bold (700), lighter, bolder
                weight = map_lambda_font_weight_keyword(value->data.keyword);
                log_debug("[CSS] Font weight keyword: %s -> %d", value->data.keyword, weight);
            }

            if (weight >= 100 && weight <= 900) {
                span->font->font_weight = weight;
            }
            break;
        }

        case CSS_PROPERTY_FONT_FAMILY: {
            log_debug("[CSS] Processing font-family property");
            if (!span->font) {
                span->font = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
            }

            if (value->type == CSS_VALUE_STRING) {
                // Font family name as string
                const char* family = value->data.string;
                if (family && strlen(family) > 0) {
                    span->font->family = strdup(family);
                    log_debug("[CSS] Font family: %s", family);
                }
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // Generic family: serif, sans-serif, monospace, etc.
                const char* family = map_lambda_font_family_keyword(value->data.keyword);
                if (family) {
                    span->font->family = strdup(family);
                    log_debug("[CSS] Font family keyword: %s -> %s", value->data.keyword, family);
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

            // Line height can be number (multiplier), length, percentage, or 'normal'
            // Store as-is for later computation
            if (value->type == CSS_VALUE_NUMBER) {
                // Unitless number - multiply by font size
                float multiplier = value->data.number.value;
                log_debug("[CSS] Line height number: %.2f", multiplier);
                // Store for later: line_height = font_size * multiplier
                // For now, store the multiplier (need to update BlockProp structure)
            } else if (value->type == CSS_VALUE_LENGTH) {
                float line_height = value->data.length.value;
                log_debug("[CSS] Line height length: %.2f px", line_height);
                // TODO: Store line_height in block->blk
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Line height percentage: %.2f%%", percentage);
                // line_height = font_size * (percentage / 100)
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'normal' keyword - typically 1.2 × font-size
                log_debug("[CSS] Line height keyword: normal");
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
                span->font = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
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
                block->blk = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float width = value->data.length.value;
                block->blk->given_width = width;
                block->blk->given_width_type = LXB_CSS_VALUE_INITIAL; // Mark as explicitly set
                log_debug("[CSS] Width: %.2f px", width);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                // For now, store percentage as-is (need parent width for proper calculation)
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Width: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Width: auto");
                block->blk->given_width_type = LXB_CSS_VALUE_AUTO;
            }
            break;
        }

        case CSS_PROPERTY_HEIGHT: {
            log_debug("[CSS] Processing height property");
            if (!block) break;
            if (!block->blk) {
                block->blk = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float height = value->data.length.value;
                block->blk->given_height = height;
                log_debug("[CSS] Height: %.2f px", height);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Height: %.2f%% (percentage not yet fully supported)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword
                log_debug("[CSS] Height: auto");
                block->blk->given_height = -1.0f; // -1 means auto
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_TOP: {
            log_debug("[CSS] Processing margin-top property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.top = margin;
                span->bound->margin.top_specificity = specificity;
                log_debug("[CSS] Margin-top: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Margin-top: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                // 'auto' keyword for margins
                span->bound->margin.top_type = LXB_CSS_VALUE_AUTO;
                log_debug("[CSS] Margin-top: auto");
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_RIGHT: {
            log_debug("[CSS] Processing margin-right property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.right = margin;
                span->bound->margin.right_specificity = specificity;
                log_debug("[CSS] Margin-right: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Margin-right: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                span->bound->margin.right_type = LXB_CSS_VALUE_AUTO;
                log_debug("[CSS] Margin-right: auto");
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_BOTTOM: {
            log_debug("[CSS] Processing margin-bottom property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.bottom = margin;
                span->bound->margin.bottom_specificity = specificity;
                log_debug("[CSS] Margin-bottom: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Margin-bottom: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                span->bound->margin.bottom_type = LXB_CSS_VALUE_AUTO;
                log_debug("[CSS] Margin-bottom: auto");
            }
            break;
        }

        case CSS_PROPERTY_MARGIN_LEFT: {
            log_debug("[CSS] Processing margin-left property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float margin = value->data.length.value;
                span->bound->margin.left = margin;
                span->bound->margin.left_specificity = specificity;
                log_debug("[CSS] Margin-left: %.2f px", margin);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Margin-left: %.2f%% (percentage)", percentage);
            } else if (value->type == CSS_VALUE_KEYWORD) {
                span->bound->margin.left_type = LXB_CSS_VALUE_AUTO;
                log_debug("[CSS] Margin-left: auto");
            }
            break;
        }

        case CSS_PROPERTY_PADDING_TOP: {
            log_debug("[CSS] Processing padding-top property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.top = padding;
                span->bound->padding.top_specificity = specificity;
                log_debug("[CSS] Padding-top: %.2f px", padding);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Padding-top: %.2f%% (percentage)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_PADDING_RIGHT: {
            log_debug("[CSS] Processing padding-right property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.right = padding;
                span->bound->padding.right_specificity = specificity;
                log_debug("[CSS] Padding-right: %.2f px", padding);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Padding-right: %.2f%% (percentage)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_PADDING_BOTTOM: {
            log_debug("[CSS] Processing padding-bottom property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.bottom = padding;
                span->bound->padding.bottom_specificity = specificity;
                log_debug("[CSS] Padding-bottom: %.2f px", padding);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Padding-bottom: %.2f%% (percentage)", percentage);
            }
            break;
        }

        case CSS_PROPERTY_PADDING_LEFT: {
            log_debug("[CSS] Processing padding-left property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }

            if (value->type == CSS_VALUE_LENGTH) {
                float padding = value->data.length.value;
                span->bound->padding.left = padding;
                span->bound->padding.left_specificity = specificity;
                log_debug("[CSS] Padding-left: %.2f px", padding);
            } else if (value->type == CSS_VALUE_PERCENTAGE) {
                float percentage = value->data.percentage.value;
                log_debug("[CSS] Padding-left: %.2f%% (percentage)", percentage);
            }
            break;
        }

        // ===== GROUP 3: Background & Borders =====

        case CSS_PROPERTY_BACKGROUND_COLOR: {
            log_debug("[CSS] Processing background-color property");
            if (!span->bound) {
                span->bound = (BoundaryProp*)alloc_prop(lycon, sizeof(BoundaryProp));
            }
            if (!span->bound->background) {
                span->bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
            }

            Color bg_color = {0};
            if (value->type == CSS_VALUE_KEYWORD) {
                // Map keyword to color (e.g., "red", "lightgray")
                bg_color.c = map_lambda_color_keyword(value->data.keyword);
                log_debug("[CSS] Background color keyword: %s -> 0x%08X", value->data.keyword, bg_color.c);
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

            if (value->type == CSS_VALUE_LENGTH) {
                float width = value->data.length.value;
                span->bound->border->width.top = width;
                span->bound->border->width.top_specificity = specificity;
                log_debug("[CSS] Border-top-width: %.2f px", width);
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

            if (value->type == CSS_VALUE_LENGTH) {
                float width = value->data.length.value;
                span->bound->border->width.right = width;
                span->bound->border->width.right_specificity = specificity;
                log_debug("[CSS] Border-right-width: %.2f px", width);
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

            if (value->type == CSS_VALUE_LENGTH) {
                float width = value->data.length.value;
                span->bound->border->width.bottom = width;
                span->bound->border->width.bottom_specificity = specificity;
                log_debug("[CSS] Border-bottom-width: %.2f px", width);
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

            if (value->type == CSS_VALUE_LENGTH) {
                float width = value->data.length.value;
                span->bound->border->width.left = width;
                span->bound->border->width.left_specificity = specificity;
                log_debug("[CSS] Border-left-width: %.2f px", width);
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

        // ===== GROUP 4: Layout Properties =====

        case CSS_PROPERTY_DISPLAY: {
            log_debug("[CSS] Processing display property");
            if (value->type == CSS_VALUE_KEYWORD) {
                int lexbor_val = map_css_keyword_to_lexbor(value->data.keyword);
                log_debug("[CSS] Display: %s -> %d", value->data.keyword, lexbor_val);

                // Set display on the view (ViewGroup has DisplayValue with outer and inner)
                if (block) {
                    // For block elements, set both outer and inner display
                    // Common values: block, inline, inline-block, flex, grid, none
                    block->display.outer = lexbor_val;
                    block->display.inner = lexbor_val;
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

        default:
            // Unknown or unimplemented property
            log_debug("[CSS] Unimplemented property: %d", prop_id);
            break;
    }
}

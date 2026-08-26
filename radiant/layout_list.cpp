/**
 * layout_list.cpp — List item counter handling and marker generation
 *
 * Extracted from layout_block.cpp to separate list-specific logic:
 * - OL/UL/MENU/DIR implicit counter-reset (CSS 2.1 §12.5)
 * - Reversed counter initial value computation (CSS Lists 3 §4.4.2)
 * - List-item auto-increment and <li value="N"> handling
 * - ::marker pseudo-element generation (inside and outside)
 * - Counter spec extraction from pseudo-element styles
 */

#include "layout.hpp"
#include "render.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style_node.hpp"
// Static helpers: extract counter property values from element CSS

static const char* counter_name_from_css_value(CssValue* item) {
    if (!item) return nullptr;
    if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
        return item->data.custom_property.name;
    }
    if (item->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(item->data.keyword);
        return info ? info->name : nullptr;
    }
    return nullptr;
}

static const char* counter_name_from_reversed_value(CssValue* item) {
    if (!item) return nullptr;
    if (item->type == CSS_VALUE_TYPE_FUNCTION && item->data.function) {
        CssFunction* function = item->data.function;
        if (!function->name || strcmp(function->name, "reversed") != 0 ||
            function->arg_count < 1) return nullptr;
        return function->args ? counter_name_from_css_value(function->args[0]) : nullptr;
    }
    return counter_name_from_css_value(item);
}

static bool get_element_counter_property_value(DomElement* elem, CssPropertyCode property,
                                               const char* counter_name,
                                               int implicit_value, int* out_value) {
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return false;
    AvlNode* node = avl_tree_search(elem->specified_style->tree, property);
    if (!node) return false;
    StyleNode* sn = (StyleNode*)node->declaration;
    if (!sn || !sn->winning_decl || !sn->winning_decl->value) return false;
    CssValue* val = sn->winning_decl->value;
    if (!val) return false;

    if (val->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < val->data.list.count; i++) {
            const char* name = counter_name_from_css_value(val->data.list.values[i]);
            if (name && strcmp(name, counter_name) == 0) {
                if (i + 1 < val->data.list.count &&
                    val->data.list.values[i + 1]->type == CSS_VALUE_TYPE_NUMBER) {
                    *out_value = (int)val->data.list.values[i + 1]->data.number.value; // INT_CAST_OK: counter value
                } else {
                    *out_value = implicit_value;
                }
                return true;
            }
        }
    } else {
        const char* name = counter_name_from_css_value(val);
        if (name && strcmp(name, counter_name) == 0) {
            *out_value = implicit_value;
            return true;
        }
    }
    return false;
}
// Extract counter-increment value for a specific counter from element's CSS
static bool get_element_counter_inc(DomElement* elem, const char* counter_name, int* out_value) {
    return get_element_counter_property_value(elem, CSS_PROPERTY_COUNTER_INCREMENT,
                                              counter_name, 1, out_value);
}
// Check if element's CSS counter-reset contains a specific counter name
static bool element_resets_counter(DomElement* elem, const char* counter_name) {
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return false;
    AvlNode* node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_COUNTER_RESET);
    if (!node) return false;
    StyleNode* sn = (StyleNode*)node->declaration;
    if (!sn || !sn->winning_decl || !sn->winning_decl->value) return false;
    CssValue* val = sn->winning_decl->value;

    auto check_name = [&](CssValue* item) -> bool {
        const char* name = counter_name_from_reversed_value(item);
        return name && strcmp(name, counter_name) == 0;
    };

    if (val->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < val->data.list.count; i++) {
            if (check_name(val->data.list.values[i])) return true;
        }
    } else {
        return check_name(val);
    }
    return false;
}
// Get the counter-set integer value for a specific counter name on an element
static bool get_element_counter_set_value(DomElement* elem, const char* counter_name, int* out_value) {
    return get_element_counter_property_value(elem, CSS_PROPERTY_COUNTER_SET,
                                              counter_name, 0, out_value);
}

static bool is_html_list_container_tag(NameId tag);

// DFS walk: calculate the dynamic initial value for a reversed counter.
// Skips subtrees that create a new scope (counter-reset) for the same counter.
// Per CSS Lists 3 §4.4.2: the last non-zero increment is added after the walk.
// Returns true if a counter-set was encountered (caller should stop walking).
static bool sum_reversed_counter_incs(DomElement* parent, const char* counter_name,
                                      int* total, int* last_nonzero, int* set_value,
                                      bool include_implicit_reversed_list_item) {
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(child);
        // HTML list containers establish an implicit list-item scope even when
        // no authored counter-reset declaration is present.
        if (include_implicit_reversed_list_item &&
            is_html_list_container_tag(elem->tag_id)) {
            continue;
        }
        // skip subtree if this element resets the same counter (new scope)
        if (element_resets_counter(elem, counter_name)) continue;
        // CSS Lists 3 §4.4.2: negate increments for a reversed counter and
        // retain the last non-zero value for the post-walk correction.
        int inc = 0;
        bool has_increment = get_element_counter_inc(elem, counter_name, &inc);
        if (!has_increment && include_implicit_reversed_list_item &&
            elem->tag_id == MARKUP_NAME_LI) {
            inc = -1;
            has_increment = true;
        }
        int increment_negated = -inc;
        if (has_increment && increment_negated != 0) {
            *last_nonzero = increment_negated;
        }
        // CSS Lists 3 §4.4.2: a counter-set stops the walk before this element's
        // increment is added; the final correction still uses its last value.
        int sv = 0;
        if (get_element_counter_set_value(elem, counter_name, &sv)) {
            *set_value = sv;
            return true;
        }
        *total += increment_negated;
        // recurse into children; stop if counter-set found in subtree
        if (sum_reversed_counter_incs(elem, counter_name, total, last_nonzero,
                                      set_value, include_implicit_reversed_list_item)) {
            return true;
        }
    }
    return false;
}
// Extract counter name from a reversed() CSS function value
static const char* get_reversed_counter_name(CssFunction* func) {
    if (!func || !func->name || strcmp(func->name, "reversed") != 0 ||
        func->arg_count < 1 || !func->args) return nullptr;
    return counter_name_from_css_value(func->args[0]);
}
// Counter Spec Extraction from Style Trees

const char* extract_counter_spec_from_style(StyleTree* style, CssPropertyCode css_property,
                                            LayoutContext* lycon) {
    if (!style || !style->tree) return nullptr;

    CssDeclaration* decl = style_tree_get_declaration(style, css_property);
    if (!decl || !decl->value) return nullptr;

    CssValue* value = decl->value;

    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
        return nullptr;
    }

    if (value->type == CSS_VALUE_TYPE_STRING && value->data.string) {
        return value->data.string;
    }

    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        return value->data.custom_property.name;
    }

    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        StringBuf* sb = stringbuf_new(lycon->doc->view_tree->prop_pool);
        if (!sb) return nullptr;

        int count = value->data.list.count;
        CssValue** values = value->data.list.values;
        for (int i = 0; i < count; i++) {
            CssValue* item = values[i];
            if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(item->data.keyword);
                if (info) {
                    if (sb->length > 0) stringbuf_append_char(sb, ' ');
                    stringbuf_append_str(sb, info->name);
                }
            } else if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
                if (sb->length > 0) stringbuf_append_char(sb, ' ');
                stringbuf_append_str(sb, item->data.custom_property.name);
            } else if (item->type == CSS_VALUE_TYPE_STRING && item->data.string) {
                if (sb->length > 0) stringbuf_append_char(sb, ' ');
                stringbuf_append_str(sb, item->data.string);
            } else if (item->type == CSS_VALUE_TYPE_NUMBER) {
                if (sb->length > 0) stringbuf_append_char(sb, ' ');
                stringbuf_append_int(sb, (int)item->data.number.value); // INT_CAST_OK: CSS numeric value to int
            }
        }

        if (sb->length > 0) {
            size_t len = sb->length;
            char* result = (char*)alloc_prop(lycon, len + 1);
            str_copy(result, len + 1, sb->str->chars, len);
            stringbuf_free(sb);
            return result;
        }
        stringbuf_free(sb);
    }

    return nullptr;
}

void apply_pseudo_counter_ops(LayoutContext* lycon, StyleTree* style) {
    if (!lycon->counter_context || !style) return;

    const char* cr = extract_counter_spec_from_style(style, CSS_PROPERTY_COUNTER_RESET, lycon);
    if (cr) {
        counter_reset(lycon->counter_context, cr);
    }

    const char* ci = extract_counter_spec_from_style(style, CSS_PROPERTY_COUNTER_INCREMENT, lycon);
    if (ci) {
        counter_increment(lycon->counter_context, ci);
    }

    const char* cs = extract_counter_spec_from_style(style, CSS_PROPERTY_COUNTER_SET, lycon);
    if (cs) {
        counter_set(lycon->counter_context, cs);
    }
}
// List Container Counter Setup

static bool is_html_list_container_tag(NameId tag) {
    return tag == MARKUP_NAME_OL || tag == MARKUP_NAME_UL ||
        tag == MARKUP_NAME_MENU || tag == MARKUP_NAME_DIR;
}

void setup_list_container_counters(LayoutContext* lycon, ViewBlock* block, DomElement* dom_elem) {
    if (!lycon->counter_context || !dom_elem) return;

    NameId tag = dom_elem->tag_id;
    if (!is_html_list_container_tag(tag)) return;
    // CSS 2.1 §12.5: OL, UL, MENU, DIR have implicit counter-reset: list-item
    // This creates a new list-item counter instance for each list container,
    // enabling counters(list-item, ".") to show nested numbering (e.g., "1.2.3").
    // Only skip if explicit counter-reset already includes "list-item".
    bool explicit_has_list_item = false;
    if (block->blk && block->block_mut()->counter_reset) {
        explicit_has_list_item = strstr(block->block()->counter_reset, "list-item") != nullptr;
    }
    if (explicit_has_list_item) return;
    // Check <ol start="N"> attribute: resets to N-1 so first li increments to N
    int start_value = 0;  // default: counter-reset: list-item 0
    bool is_reversed_ol = false;
    if (tag == MARKUP_NAME_OL) {
        // Check <ol reversed> attribute
        is_reversed_ol = dom_elem->has_attribute("reversed");
        const char* start_attr = dom_elem->get_attribute("start");
        if (is_reversed_ol) {
            if (start_attr) {
                start_value = atoi(start_attr) + 1;
            }
            // else: start_value stays 0; reversed initial computed below via DFS
        } else if (start_attr) {
            start_value = atoi(start_attr) - 1;
        }
    }
    char reset_spec[64];
    snprintf(reset_spec, sizeof(reset_spec), "list-item %d", start_value);
    counter_reset(lycon->counter_context, reset_spec);
    // For <ol reversed> without start attr, compute reversed initial value
    // using the same DFS algorithm as CSS counter-reset: reversed(list-item)
    if (is_reversed_ol && start_value == 0) {
        int total = 0, last_nz = 0, set_val = 0;
        bool has_set = sum_reversed_counter_incs(
            dom_elem, "list-item", &total, &last_nz, &set_val, true);
        if (last_nz != 0 || has_set) {
            // CSS Lists 3 §4.4.2: the shared walk already negates implicit and
            // explicit increments and omits the increment on a counter-set node.
            int initial = total + last_nz + set_val;
            char set_spec2[64];
            snprintf(set_spec2, sizeof(set_spec2), "list-item %d", initial);
            counter_set(lycon->counter_context, set_spec2);
        }
    }
}
// Reversed Counter Initial Value Computation

void compute_reversed_counter_initial(LayoutContext* lycon, DomElement* dom_elem) {
    if (!lycon->counter_context || !dom_elem) return;
    if (!dom_elem->specified_style || !dom_elem->specified_style->tree) return;

    AvlNode* cr_node = avl_tree_search(dom_elem->specified_style->tree,
                                        CSS_PROPERTY_COUNTER_RESET);
    if (!cr_node) return;

    StyleNode* style_node = (StyleNode*)cr_node->declaration;
    CssValue* cr_value = (style_node && style_node->winning_decl) ?
                         style_node->winning_decl->value : nullptr;
    // CSS Lists 3: for reversed() counters without explicit values, the
    // dynamic initial value is the negated increment sum plus the last
    // non-zero negated increment and any first counter-set value.
    auto compute_reversed = [&](CssFunction* func) {
        const char* rev_name = get_reversed_counter_name(func);
        if (!rev_name) return;
        int total = 0, last_nz = 0, set_val = 0;
        bool has_set = sum_reversed_counter_incs(dom_elem, rev_name, &total, &last_nz,
                                                &set_val, false);
        if (last_nz == 0 && !has_set) return; // no non-zero increments and no counter-set found
        int initial = total + last_nz + set_val;
        char set_spec[128];
        snprintf(set_spec, sizeof(set_spec), "%s %d", rev_name, initial);
        counter_set(lycon->counter_context, set_spec);
    };

    if (cr_value && cr_value->type == CSS_VALUE_TYPE_LIST) {
        int count = cr_value->data.list.count;
        CssValue** items = cr_value->data.list.values;
        for (int iv = 0; iv < count; iv++) {
            CssValue* item = items[iv];
            if (item->type != CSS_VALUE_TYPE_FUNCTION || !item->data.function)
                continue;
            // Skip if followed by explicit integer value
            if (iv + 1 < count && items[iv + 1]->type == CSS_VALUE_TYPE_NUMBER)
                continue;
            compute_reversed(item->data.function);
        }
    } else if (cr_value && cr_value->type == CSS_VALUE_TYPE_FUNCTION &&
               cr_value->data.function) {
        compute_reversed(cr_value->data.function);
    }
}
// List Item Counter + Marker Generation
bool layout_marker_is_outside(View* view) {
    if (!view || !view->is_element()) {
        return false;
    }
    DomElement* marker = lam::dom_require<DOM_NODE_ELEMENT>(view);
    if (view->view_type != RDT_VIEW_MARKER &&
        (!marker || !marker->tag_name || strcmp(marker->tag_name, "::marker") != 0)) {
        return false;
    }
    MarkerProp* marker_prop = marker
        ? reinterpret_cast<MarkerProp*>(marker->blk) : nullptr;
    // CSS Lists 3 §3: outside markers paint beside the principal box and do not
    // contribute to its in-flow size; inside markers remain ordinary inline content.
    return marker_prop && marker_prop->is_outside;
}

bool layout_list_item_has_in_flow_content(DomElement* element) {
    if (!element) return false;
    // CSS Lists 3: marker separation is content followed by a collapsible
    // gap; generated content therefore keeps that gap from being trailing.
    if (dom_element_has_before_content(element) ||
        dom_element_has_after_content(element)) {
        return true;
    }
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            if (layout_text_node_has_content(child)) return true;
            continue;
        }
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        DisplayValue child_display = resolve_display_value(child);
        if (layout_display_is_none(child_display) ||
            layout_element_is_abs_or_fixed(child_elem)) {
            continue;
        }
        return true;
    }
    return false;
}

static float list_marker_bullet_inline_size(float font_size, bool is_outside,
                                            bool reserves_first_line) {
    // Inside bullets participate in inline flow using the full marker-plus-gap
    // box; outside markers only need their paint box and following half-em gap.
    return (!is_outside || reserves_first_line)
        ? font_size * 1.375f
        : font_size * 0.35f + font_size * 0.5f;
}

static FontHandle* resolve_marker_font_for_layout(LayoutContext* lycon,
                                                  DomElement* list_elem,
                                                  float* marker_font_size,
                                                  FontProp* temporary_font) {
    if (!lycon || !list_elem || !marker_font_size || !temporary_font) return nullptr;
    FontProp* base_font = list_elem->font ? list_elem->font : lycon->font.style;
    if (!base_font) return font_box_handle(&lycon->font);

    StyleTree* marker_style = list_elem->pseudo_style(PSEUDO_STYLE_MARKER);
    CssDeclaration* font_size_decl = marker_style
        ? style_tree_get_declaration(marker_style, CSS_PROPERTY_FONT_SIZE) : nullptr;
    if (!font_size_decl || !font_size_decl->value) {
        *marker_font_size = base_font->font_size;
        if (base_font->used_zoom > 0.0f && base_font->used_zoom != 1.0f) {
            *temporary_font = *base_font;
            temporary_font->font_handle = nullptr;
            FontBox marker_box = {};
            setup_font(lycon->ui_context, &marker_box, temporary_font);
            return font_box_handle(&marker_box);
        }
        return base_font->font_handle ? base_font->font_handle : font_box_handle(&lycon->font);
    }

    *temporary_font = *base_font;
    temporary_font->font_handle = nullptr;
    LayoutFontSizeResult resolved = layout_resolve_font_size_value(
        lycon, font_size_decl->value, base_font, true);
    if (resolved.value < 0.0f || isnan(resolved.value)) {
        *marker_font_size = base_font->font_size;
        return base_font->font_handle ? base_font->font_handle : font_box_handle(&lycon->font);
    }
    temporary_font->font_size = resolved.value;
    temporary_font->font_size_from_medium = resolved.from_medium;
    FontBox marker_box = {};
    setup_font(lycon->ui_context, &marker_box, temporary_font);
    *marker_font_size = temporary_font->font_size;
    return font_box_handle(&marker_box);
}

static void sync_marker_line_height(LayoutContext* lycon, ViewBlock* block,
                                    MarkerProp* marker_prop, float marker_font_size) {
    if (!lycon || !block || !marker_prop) return;
    float used_line_height = marker_prop->line_height;
    if (!lycon->block.line_height_is_normal && block->blk && block->block()->line_height) {
        float resolved_line_height = layout_resolve_line_height_value(
            lycon, block->block()->line_height,
            lam::dom_require<DOM_NODE_ELEMENT>(block), marker_font_size);
        if (resolved_line_height > 0.0f) used_line_height = resolved_line_height;
    }
    if (used_line_height > 0.0f) {
        // CSS 2.1 §10.8.1: normal remains relative to the marker font; only an
        // inherited non-normal value resolves at the marker's own font size.
        marker_prop->line_height = used_line_height;
    }
}

static bool list_style_image_is_none(const ListStyleImage* image) {
    return !image || (image->gradient_type == GRADIENT_NONE &&
                      (!image->url || strcmp(image->url, "none") == 0));
}

static bool list_marker_intrinsic_size(DomElement* parent_elem, ImageSurface* image,
                                       float effective_zoom, float* width, float* height) {
    if (!parent_elem || !image || !image->has_intrinsic_size || !width || !height) {
        return false;
    }
    bool from_image_orientation = layout_image_orientation_uses_from_image(parent_elem);
    float image_width = (from_image_orientation || image->encoded_width <= 0) ?
        (float)image->width : (float)image->encoded_width;
    float image_height = (from_image_orientation || image->encoded_height <= 0) ?
        (float)image->height : (float)image->encoded_height;
    if (image_width <= 0.0f || image_height <= 0.0f) return false;

    // CSS zoom applies to the used dimensions of an intrinsic marker image;
    // retaining decoded dimensions leaves a dynamically zoomed marker in its old box.
    float used_zoom = effective_zoom > 0.0f ? effective_zoom : 1.0f;
    *width = image_width * used_zoom;
    *height = image_height * used_zoom;
    return true;
}

// Create a ::marker element with the given properties
static DomElement* create_marker_element(LayoutContext* lycon, DomElement* parent_elem,
                                         CssEnum marker_style, float font_size,
                                         float image_default_size, float image_gap,
                                         float effective_zoom,
                                         bool is_bullet_marker, bool is_outside,
                                         bool is_string_marker, const char* string_marker,
                                         const char* marker_css_content,
                                         FontHandle* font_handle,
                                         const ListStyleImage* image) {
    float bullet_size = font_size * 0.35f;  // ~5-6px at 16px font

    DomElement* marker_elem = DomElement::create(parent_elem->doc, "::marker", nullptr);
    if (!marker_elem) return nullptr;

    marker_elem->parent = parent_elem;

    MarkerProp* marker_prop = (MarkerProp*)alloc_prop(lycon, sizeof(MarkerProp));
    memset(marker_prop, 0, sizeof(MarkerProp));
    marker_prop->marker_type = marker_style;
    marker_prop->bullet_size = bullet_size;
    marker_prop->is_outside = is_outside;
    marker_prop->has_explicit_content = marker_css_content != nullptr;
    marker_prop->line_height = font_handle ? calc_normal_line_height(font_handle)
                                           : font_size * 1.2f;
    if (font_handle) {
        font_get_normal_lh_split(font_handle, &marker_prop->ascender,
                                 &marker_prop->descender);
    }
    DomElement* list_parent = parent_elem->parent_element();
    bool is_quirks = lycon->doc && lycon->doc->view_tree &&
        is_quirks_mode(lycon->doc->view_tree->html_version);
    marker_prop->reserves_first_line = is_quirks && is_outside &&
        (!list_parent || !is_html_list_container_tag(list_parent->tag_id));
    // CSS 2.1 §12.5: list-style-image overrides list-style-type when image loads successfully.
    if (image && image->gradient_type != GRADIENT_NONE) {
        marker_prop->image = *image;
        marker_prop->is_image_marker = true;
    } else if (image && image->url && strcmp(image->url, "none") != 0) {
        marker_prop->image.url = lam::promote_to_pool(lycon->pool, image->url).get();
        marker_prop->loaded_image = load_image(lycon->ui_context, marker_prop->image.url);
    }

    if (marker_css_content) {
        // ::marker { content: ... } overrides list-style-type
        radiant_retain_marker_text_content(marker_prop, lam::promote_to_pool(lycon->pool, marker_css_content));
        marker_prop->marker_type = CSS_VALUE_DECIMAL;  // force text rendering
    } else if (is_string_marker) {
        // CSS Lists 3 §4.1: use the string directly as marker content
        radiant_retain_marker_text_content(marker_prop, lam::promote_to_pool(lycon->pool, string_marker));
    } else if (!is_bullet_marker) {
        char marker_text[64];
        int marker_len = counter_format(lycon->counter_context, "list-item", marker_style, marker_text, sizeof(marker_text));
        if (marker_len > 0 && marker_len + 2 < (int)sizeof(marker_text)) {
            marker_text[marker_len] = '.';
            marker_text[marker_len + 1] = ' ';
            marker_text[marker_len + 2] = '\0';
            marker_len += 2;

            radiant_retain_marker_text_content(marker_prop, lam::promote_to_pool(lycon->pool, marker_text));
        }
    }
    if (!marker_prop->has_explicit_content && !is_string_marker &&
        marker_prop->text_content && font_handle) {
        size_t marker_length = strlen(marker_prop->text_content);
        if (marker_length > 0 && marker_prop->text_content[marker_length - 1] == ' ') {
            TextExtents space = font_measure_text(font_handle, " ", 1);
            marker_prop->trailing_space_width = space.width;
        }
    }
    // CSS Lists 3 §4.2: compute marker width from content
    // For text markers, measure actual text width; for bullets, use fixed bullet size + padding
    if (marker_prop->is_image_marker) {
        // CSS Lists 3 §3.3: an image marker without intrinsic dimensions uses
        // the default object size of 1em in both axes.
        marker_prop->content_width = image_default_size;
        marker_prop->width = image_default_size + image_gap;
        marker_prop->height = image_default_size;
    } else if (marker_prop->loaded_image) {
        ImageSurface* img = marker_prop->loaded_image;
        if (!img->has_intrinsic_size) {
            // CSS Lists 3 uses the list marker's 1em default object size when
            // an image has no natural width/height; do not expose SVG's generic
            // 300x150 fallback as a marker's intrinsic dimensions.
            marker_prop->content_width = image_default_size;
            marker_prop->width = image_default_size + image_gap;
            marker_prop->height = image_default_size;
        } else {
            float image_width = 0.0f;
            float image_height = 0.0f;
            if (list_marker_intrinsic_size(parent_elem, img, effective_zoom,
                                           &image_width, &image_height)) {
                marker_prop->content_width = image_width;
                marker_prop->width = image_width + image_gap;
                marker_prop->height = image_height;
            }
        }
    } else if (marker_prop->text_content && font_handle) {
        TextExtents extents = font_measure_text(font_handle, marker_prop->text_content,
                                                 (int)strlen(marker_prop->text_content)); // INT_CAST_OK: string length
        marker_prop->width = extents.width;
    } else if (is_bullet_marker) {
        marker_prop->width = list_marker_bullet_inline_size(
            font_size, is_outside, marker_prop->reserves_first_line);
    } else {
        // fallback: use em-based estimate
        marker_prop->width = font_size * 1.375f;
    }
    marker_elem->view_type = RDT_VIEW_MARKER;
    marker_elem->blk = (BlockProp*)marker_prop;

    return marker_elem;
}

void process_list_item(LayoutContext* lycon, ViewBlock* block, DomNode* elmt,
                       DomElement* dom_elem, DisplayValue display) {
    if (!lycon->counter_context) return;
    // Detect if parent is <ol reversed>
    bool parent_reversed = false;
    if (dom_elem && dom_elem->tag_id == MARKUP_NAME_LI) {
        DomElement* parent_elem = dom_elem->parent_element();
        if (parent_elem && parent_elem->tag_id == MARKUP_NAME_OL &&
            parent_elem->has_attribute("reversed")) {
            parent_reversed = true;
        }
    }
    // Handle <li value="N"> attribute: sets counter to N before increment
    if (dom_elem && dom_elem->tag_id == MARKUP_NAME_LI) {
        const char* value_attr = dom_elem->get_attribute("value");
        if (value_attr) {
            int li_value = atoi(value_attr);
            // CSS Lists 3: li[value] acts as counter-set: list-item <value>
            // Set to value±1 so auto-increment brings it to value
            int set_value = parent_reversed ? li_value + 1 : li_value - 1;
            char set_spec[64];
            snprintf(set_spec, sizeof(set_spec), "list-item %d", set_value);
            counter_set(lycon->counter_context, set_spec);
        }
    }
    // CSS Lists 3 §4.5: auto-increment list-item only if the element
    // does NOT already have an explicit counter-increment for list-item
    bool explicit_list_item_inc = (block->blk && block->block_mut()->counter_increment &&
                                   strstr(block->block()->counter_increment, "list-item") != nullptr);
    if (!explicit_list_item_inc) {
        counter_increment(lycon->counter_context, parent_reversed ? "list-item -1" : "list-item 1");
    }
    // CSS Display 3: `inline flow-root list-item` is laid out atomically but
    // retains the outside-marker behavior of its inline-level principal box.
    bool is_inline_list_item = display.list_item &&
        (display.outer == CSS_VALUE_INLINE ||
         (display.outer == CSS_VALUE_INLINE_BLOCK && display.inner != CSS_VALUE_FLOW_ROOT));
    // Set default list-style-position to outside if not specified
    // CSS 2.1 Section 12.5.1: Initial value is 'outside'
    bool is_outside_position = !is_inline_list_item;  // Default is outside, but inside for inline
    CssEnum list_style_position = block->blk
        ? block->block()->list_style_position : (CssEnum)0;
    for (DomElement* ancestor = dom_elem ? dom_elem->parent_element() : nullptr;
         list_style_position == 0 && ancestor;
         ancestor = ancestor->parent_element()) {
        if (ancestor->blk) list_style_position = ancestor->block()->list_style_position;
    }
    if (list_style_position != 0) {
        if (list_style_position == 1) {
            is_outside_position = false;
        } else if (!is_inline_list_item) {
            is_outside_position = true;
        }
    }
    // Generate list marker if list-style-type is not 'none'
    // CSS 2.1 §12.5: list-style-type is inherited. Check element first, then parent.
    CssEnum effective_list_style = block->blk ? block->block()->list_style_type : (CssEnum)0;
    if (effective_list_style == 0) {
        // CSS Lists 3 §4.1: list-style-type inherits through every inline
        // ancestor, so an intervening span must not hide its ordered-list value.
        for (DomElement* ancestor = dom_elem->parent_element(); ancestor;
             ancestor = ancestor->parent_element()) {
            if (ancestor->blk && ancestor->block()->list_style_type != 0) {
                effective_list_style = ancestor->block()->list_style_type;
                break;
            }
        }
        if (effective_list_style == 0) {
            effective_list_style = CSS_VALUE_DISC;
        }
    }
    // CSS Lists 3 §4.1: check for string marker value
    const char* string_marker = (block->blk) ? block->block()->list_style_type_string : nullptr;
    bool has_marker = (effective_list_style != CSS_VALUE_NONE) || (string_marker != nullptr);
    // Check for ::marker { content: ... } CSS override
    const char* marker_css_content = nullptr;
    DomElement* list_elem = lam::dom_require<DOM_NODE_ELEMENT>(elmt);
    if (list_elem->pseudo_style(PSEUDO_STYLE_MARKER)) {
        CssDeclaration* content_decl = style_tree_get_declaration(
            list_elem->pseudo_style(PSEUDO_STYLE_MARKER), CSS_PROPERTY_CONTENT);
        if (content_decl && content_decl->value) {
            CssValue* cv = content_decl->value;
            if (cv->type == CSS_VALUE_TYPE_KEYWORD && cv->data.keyword == CSS_VALUE_NONE) {
                has_marker = false;  // content: none suppresses marker
            } else if (!(cv->type == CSS_VALUE_TYPE_KEYWORD && cv->data.keyword == CSS_VALUE_NORMAL)) {
                // explicit content (not 'normal') - resolve using counter context
                marker_css_content = dom_element_get_pseudo_element_content_with_counters(
                    list_elem, 6, lycon->counter_context, lycon->scratch.arena);
                if (marker_css_content) {
                    has_marker = true;
                }
            }
        }
    }

    if (!has_marker) return;

    CssEnum marker_style = effective_list_style;
    bool is_string_marker = (string_marker != nullptr);

    bool is_bullet_marker = !is_string_marker && !marker_css_content &&
                            (marker_style == CSS_VALUE_DISC ||
                            marker_style == CSS_VALUE_CIRCLE ||
                            marker_style == CSS_VALUE_SQUARE ||
                            marker_style == CSS_VALUE_DISCLOSURE_CLOSED ||
                            marker_style == CSS_VALUE_DISCLOSURE_OPEN);

    if (!block->pseudo) {
        block->pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
        memset(block->pseudo, 0, sizeof(PseudoContentProp));
    }

    float marker_font_size = 16.0f;
    if (block->font && block->fontp()->font_size > 0.0f) {
        marker_font_size = block->fontp()->font_size;
    }
    float effective_zoom = layout_effective_zoom((View*)block);
    if (block->font) block->font->used_zoom = effective_zoom;
    DomElement* parent_elem = lam::dom_require<DOM_NODE_ELEMENT>(elmt);
    FontProp temporary_marker_font = {};
    FontHandle* marker_font_handle = resolve_marker_font_for_layout(
        lycon, parent_elem, &marker_font_size, &temporary_marker_font);
    // CSS Viewport 1 applies the effective zoom to the marker's used font and
    // image object size after the marker's computed font size is resolved.
    marker_font_size *= effective_zoom;
    float image_default_size = effective_zoom > 0.0f
        ? marker_font_size / effective_zoom : marker_font_size;
    float image_gap = 0.0f;
    if (marker_font_handle && image_default_size > 0.0f) {
        // CSS Lists 3 appends one space after an image marker; keep that
        // separator in the marker advance while retaining the image's box size.
        TextExtents space = font_measure_text(marker_font_handle, " ", 1);
        image_gap = effective_zoom > 0.0f
            ? space.width / effective_zoom : space.width;
    }
    if (block->pseudo->marker_generated && block->pseudo->marker &&
        block->pseudo->marker->blk) {
        // Retained marker boxes outlive style-only reflows, so refresh inherited
        // placement instead of leaving the marker in its pre-mutation mode.
        MarkerProp* marker_prop = reinterpret_cast<MarkerProp*>(
            block->pseudo->marker->blk);
        marker_prop->is_outside = is_outside_position;
        if (marker_prop->is_image_marker) {
            // CSS pseudo-element mutations update ::marker separately; resolve
            // its font before refreshing dimensions on the retained marker.
            marker_prop->content_width = image_default_size;
            marker_prop->width = image_default_size + image_gap;
            marker_prop->height = image_default_size;
        } else if (marker_prop->loaded_image &&
                   !marker_prop->loaded_image->has_intrinsic_size) {
            marker_prop->content_width = image_default_size;
            marker_prop->width = image_default_size + image_gap;
            marker_prop->height = image_default_size;
        } else if (marker_prop->loaded_image) {
            float image_width = 0.0f;
            float image_height = 0.0f;
            if (list_marker_intrinsic_size(parent_elem, marker_prop->loaded_image,
                                           effective_zoom, &image_width, &image_height)) {
                marker_prop->content_width = image_width;
                marker_prop->width = image_width + image_gap;
                marker_prop->height = image_height;
            }
        } else if (is_bullet_marker && !marker_prop->loaded_image) {
            // Marker placement and its inline reservation form one retained
            // invariant; switching inside/outside must refresh both values.
            marker_prop->bullet_size = marker_font_size * 0.35f;
            marker_prop->width = list_marker_bullet_inline_size(
                marker_font_size, is_outside_position,
                marker_prop->reserves_first_line);
        }
        sync_marker_line_height(lycon, block, marker_prop, marker_font_size);
    }

    if (!block->pseudo->marker_generated) {
        // CSS 2.1 §12.5: list-style-image is inherited; check self then parent
        const ListStyleImage* image = (block->blk) ? &block->block()->list_style_image : nullptr;
        if (list_style_image_is_none(image)) {
            DomElement* pe = dom_elem->parent_element();
            if (pe && pe->blk) {
                image = &pe->block()->list_style_image;
            }
        }

        DomElement* marker_elem = create_marker_element(
            lycon, parent_elem, marker_style, marker_font_size,
            image_default_size, image_gap, effective_zoom,
            is_bullet_marker, is_outside_position,
            is_string_marker, string_marker, marker_css_content,
            marker_font_handle, image);

        if (marker_elem) {
            block->pseudo->marker = marker_elem;
            block->pseudo->marker_generated = true;
            MarkerProp* marker_prop = reinterpret_cast<MarkerProp*>(marker_elem->blk);
            if (marker_prop && !is_outside_position &&
                marker_prop->trailing_space_width > 0.0f &&
                !layout_list_item_has_in_flow_content(dom_elem)) {
                // CSS Text 3 §4.1.3: an empty inside list item's default
                // marker separator is collapsible at the line end.
                marker_prop->width -= marker_prop->trailing_space_width;
                marker_prop->trailing_space_trimmed = true;
            }
            sync_marker_line_height(lycon, block,
                                    reinterpret_cast<MarkerProp*>(marker_elem->blk),
                                    marker_font_size);
        }
    }
    font_prop_release_handle(&temporary_marker_font);
}

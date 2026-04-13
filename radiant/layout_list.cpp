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

#include "layout_list.hpp"
#include "layout_counters.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style_node.hpp"

// ============================================================================
// Static helpers: extract counter property values from element CSS
// ============================================================================

// Extract counter-increment value for a specific counter from element's CSS
static bool get_element_counter_inc(DomElement* elem, const char* counter_name, int* out_value) {
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return false;
    AvlNode* node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_COUNTER_INCREMENT);
    if (!node) return false;
    StyleNode* sn = (StyleNode*)node->declaration;
    if (!sn || !sn->winning_decl || !sn->winning_decl->value) return false;
    CssValue* val = sn->winning_decl->value;
    if (!val) return false;

    auto match_name = [&](CssValue* item) -> const char* {
        if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name)
            return item->data.custom_property.name;
        if (item->type == CSS_VALUE_TYPE_KEYWORD) {
            const CssEnumInfo* info = css_enum_info(item->data.keyword);
            return info ? info->name : nullptr;
        }
        return nullptr;
    };

    if (val->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < val->data.list.count; i++) {
            const char* name = match_name(val->data.list.values[i]);
            if (name && strcmp(name, counter_name) == 0) {
                if (i + 1 < val->data.list.count &&
                    val->data.list.values[i + 1]->type == CSS_VALUE_TYPE_NUMBER) {
                    *out_value = (int)val->data.list.values[i + 1]->data.number.value; // INT_CAST_OK: counter value
                } else {
                    *out_value = 1;
                }
                return true;
            }
        }
    } else {
        const char* name = match_name(val);
        if (name && strcmp(name, counter_name) == 0) {
            *out_value = 1;
            return true;
        }
    }
    return false;
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
        const char* name = nullptr;
        if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
            name = item->data.custom_property.name;
        } else if (item->type == CSS_VALUE_TYPE_KEYWORD) {
            const CssEnumInfo* info = css_enum_info(item->data.keyword);
            if (info) name = info->name;
        } else if (item->type == CSS_VALUE_TYPE_FUNCTION && item->data.function) {
            CssFunction* func = item->data.function;
            if (func->name && strcmp(func->name, "reversed") == 0 &&
                func->arg_count >= 1 && func->args[0]) {
                if (func->args[0]->type == CSS_VALUE_TYPE_CUSTOM &&
                    func->args[0]->data.custom_property.name)
                    name = func->args[0]->data.custom_property.name;
                else if (func->args[0]->type == CSS_VALUE_TYPE_KEYWORD) {
                    const CssEnumInfo* info = css_enum_info(func->args[0]->data.keyword);
                    if (info) name = info->name;
                }
            }
        }
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
    if (!elem || !elem->specified_style || !elem->specified_style->tree) return false;
    AvlNode* node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_COUNTER_SET);
    if (!node) return false;
    StyleNode* sn = (StyleNode*)node->declaration;
    if (!sn || !sn->winning_decl || !sn->winning_decl->value) return false;
    CssValue* val = sn->winning_decl->value;
    if (!val) return false;

    auto match_name = [&](CssValue* item) -> const char* {
        if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name)
            return item->data.custom_property.name;
        if (item->type == CSS_VALUE_TYPE_KEYWORD) {
            const CssEnumInfo* info = css_enum_info(item->data.keyword);
            return info ? info->name : nullptr;
        }
        return nullptr;
    };

    if (val->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < val->data.list.count; i++) {
            const char* name = match_name(val->data.list.values[i]);
            if (name && strcmp(name, counter_name) == 0) {
                if (i + 1 < val->data.list.count &&
                    val->data.list.values[i + 1]->type == CSS_VALUE_TYPE_NUMBER) {
                    *out_value = (int)val->data.list.values[i + 1]->data.number.value; // INT_CAST_OK: counter value
                } else {
                    *out_value = 0;
                }
                return true;
            }
        }
    } else {
        const char* name = match_name(val);
        if (name && strcmp(name, counter_name) == 0) {
            *out_value = 0;
            return true;
        }
    }
    return false;
}

// DFS walk: sum non-zero counter-increments for a reversed counter in scope.
// Skips subtrees that create a new scope (counter-reset) for the same counter.
// Per CSS Lists 3 §4.4.2: at the first counter-set, adds its value to total and breaks.
// Returns true if a counter-set was encountered (caller should stop walking).
static bool sum_reversed_counter_incs(DomElement* parent, const char* counter_name,
                                      int* total, int* last_nonzero, int* set_value) {
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* elem = (DomElement*)child;

        // skip subtree if this element resets the same counter (new scope)
        if (element_resets_counter(elem, counter_name)) continue;

        // check counter-increment (processed before counter-set per spec §12.4)
        int inc = 0;
        if (get_element_counter_inc(elem, counter_name, &inc) && inc != 0) {
            *total += inc;
            *last_nonzero = inc;
        }

        // stop if this element has counter-set for the same counter
        // (counter-increment on this element is already counted above)
        // Per spec §4.4.2: add the counter-set value to total and break
        int sv = 0;
        if (get_element_counter_set_value(elem, counter_name, &sv)) {
            *set_value = sv;
            return true;
        }

        // recurse into children; stop if counter-set found in subtree
        if (sum_reversed_counter_incs(elem, counter_name, total, last_nonzero, set_value)) return true;
    }
    return false;
}

// Extract counter name from a reversed() CSS function value
static const char* get_reversed_counter_name(CssFunction* func) {
    if (!func || !func->name || strcmp(func->name, "reversed") != 0) return nullptr;
    if (func->arg_count < 1 || !func->args[0]) return nullptr;
    if (func->args[0]->type == CSS_VALUE_TYPE_CUSTOM &&
        func->args[0]->data.custom_property.name)
        return func->args[0]->data.custom_property.name;
    if (func->args[0]->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(func->args[0]->data.keyword);
        return info ? info->name : nullptr;
    }
    return nullptr;
}

// ============================================================================
// Counter Spec Extraction from Style Trees
// ============================================================================

const char* extract_counter_spec_from_style(StyleTree* style, CssPropertyId css_property,
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
        StringBuf* sb = stringbuf_new(lycon->doc->view_tree->pool);
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
        log_debug("    [Pseudo] Applying counter-reset: %s", cr);
        counter_reset(lycon->counter_context, cr);
    }

    const char* ci = extract_counter_spec_from_style(style, CSS_PROPERTY_COUNTER_INCREMENT, lycon);
    if (ci) {
        log_debug("    [Pseudo] Applying counter-increment: %s", ci);
        counter_increment(lycon->counter_context, ci);
    }

    const char* cs = extract_counter_spec_from_style(style, CSS_PROPERTY_COUNTER_SET, lycon);
    if (cs) {
        log_debug("    [Pseudo] Applying counter-set: %s", cs);
        counter_set(lycon->counter_context, cs);
    }
}

// ============================================================================
// List Container Counter Setup
// ============================================================================

void setup_list_container_counters(LayoutContext* lycon, ViewBlock* block, DomElement* dom_elem) {
    if (!lycon->counter_context || !dom_elem) return;

    uintptr_t tag = dom_elem->tag_id;
    if (tag != HTM_TAG_OL && tag != HTM_TAG_UL &&
        tag != HTM_TAG_MENU && tag != HTM_TAG_DIR) {
        return;
    }

    // CSS 2.1 §12.5: OL, UL, MENU, DIR have implicit counter-reset: list-item
    // This creates a new list-item counter instance for each list container,
    // enabling counters(list-item, ".") to show nested numbering (e.g., "1.2.3").
    // Only skip if explicit counter-reset already includes "list-item".
    bool explicit_has_list_item = false;
    if (block->blk && block->blk->counter_reset) {
        explicit_has_list_item = strstr(block->blk->counter_reset, "list-item") != nullptr;
    }
    if (explicit_has_list_item) return;

    // Check <ol start="N"> attribute: resets to N-1 so first li increments to N
    int start_value = 0;  // default: counter-reset: list-item 0
    bool is_reversed_ol = false;
    if (tag == HTM_TAG_OL) {
        // Check <ol reversed> attribute
        is_reversed_ol = dom_element_has_attribute(dom_elem, "reversed");
        const char* start_attr = dom_element_get_attribute(dom_elem, "start");
        if (is_reversed_ol) {
            if (start_attr) {
                start_value = atoi(start_attr) + 1;
            }
            // else: start_value stays 0; reversed initial computed below via DFS
            log_debug("    [List] OL reversed → counter-reset: list-item %d%s",
                      start_value, start_attr ? "" : " (will compute reversed initial)");
        } else if (start_attr) {
            start_value = atoi(start_attr) - 1;
            log_debug("    [List] OL start=%s → counter-reset: list-item %d", start_attr, start_value);
        }
    }
    char reset_spec[64];
    snprintf(reset_spec, sizeof(reset_spec), "list-item %d", start_value);
    counter_reset(lycon->counter_context, reset_spec);
    log_debug("    [List] Implicit counter-reset: %s for <%s>", reset_spec, dom_elem->tag_name);

    // For <ol reversed> without start attr, compute reversed initial value
    // using the same DFS algorithm as CSS counter-reset: reversed(list-item)
    if (is_reversed_ol && start_value == 0) {
        int total = 0, last_nz = 0, set_val = 0;
        // Account for implicit list-item auto-increment on LIs:
        // LIs without explicit counter-increment get implicit -1 (reversed)
        for (DomNode* child = dom_elem->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;
            DomElement* child_elem = (DomElement*)child;
            // skip subtree if it resets list-item (new scope)
            if (element_resets_counter(child_elem, "list-item")) continue;
            // check explicit counter-increment
            int inc = 0;
            bool has_explicit = get_element_counter_inc(child_elem, "list-item", &inc);
            if (has_explicit) {
                if (inc != 0) {
                    total += inc;
                    last_nz = inc;
                }
            } else if (child_elem->tag_id == HTM_TAG_LI) {
                // implicit list-item -1 for reversed OL
                total += -1;
                last_nz = -1;
            }
            // check counter-set
            int sv = 0;
            if (get_element_counter_set_value(child_elem, "list-item", &sv)) {
                set_val = sv;
                break;
            }
        }
        if (last_nz != 0) {
            int initial = -(total + last_nz) + set_val;
            char set_spec2[64];
            snprintf(set_spec2, sizeof(set_spec2), "list-item %d", initial);
            counter_set(lycon->counter_context, set_spec2);
            log_debug("    [List] OL reversed computed: total=%d, last_nz=%d, set_val=%d, initial=%d",
                      total, last_nz, set_val, initial);
        }
    }
}

// ============================================================================
// Reversed Counter Initial Value Computation
// ============================================================================

void compute_reversed_counter_initial(LayoutContext* lycon, DomElement* dom_elem) {
    if (!lycon->counter_context || !dom_elem) return;
    if (!dom_elem->specified_style || !dom_elem->specified_style->tree) return;

    AvlNode* cr_node = avl_tree_search(dom_elem->specified_style->tree,
                                        CSS_PROPERTY_COUNTER_RESET);
    if (!cr_node) return;

    StyleNode* style_node = (StyleNode*)cr_node->declaration;
    CssValue* cr_value = (style_node && style_node->winning_decl) ?
                         style_node->winning_decl->value : nullptr;

    // CSS Lists 3: For reversed() counters without explicit values,
    // compute initial value = -(total_non_zero_increments + last_non_zero_increment)
    // by DFS-walking the subtree, skipping nested scopes for the same counter.
    auto compute_reversed = [&](CssFunction* func) {
        const char* rev_name = get_reversed_counter_name(func);
        if (!rev_name) return;
        int total = 0, last_nz = 0, set_val = 0;
        bool has_set = sum_reversed_counter_incs(dom_elem, rev_name, &total, &last_nz, &set_val);
        if (last_nz == 0 && !has_set) return; // no non-zero increments and no counter-set found
        // CSS Lists 3 §4.4.2: initial = -(total + last_nz) + set_val
        int initial = -(total + last_nz) + set_val;
        char set_spec[128];
        snprintf(set_spec, sizeof(set_spec), "%s %d", rev_name, initial);
        counter_set(lycon->counter_context, set_spec);
        log_debug("    [Block] Reversed counter '%s': total=%d, last_nz=%d, set_val=%d, initial=%d",
                  rev_name, total, last_nz, set_val, initial);
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

// ============================================================================
// List Item Counter + Marker Generation
// ============================================================================

// Create a ::marker element with the given properties
static DomElement* create_marker_element(LayoutContext* lycon, DomElement* parent_elem,
                                         CssEnum marker_style, float font_size,
                                         bool is_bullet_marker, bool is_outside,
                                         bool is_string_marker, const char* string_marker,
                                         const char* marker_css_content,
                                         FontHandle* font_handle) {
    float bullet_size = font_size * 0.35f;  // ~5-6px at 16px font

    DomElement* marker_elem = dom_element_create(parent_elem->doc, "::marker", nullptr);
    if (!marker_elem) return nullptr;

    marker_elem->parent = parent_elem;

    MarkerProp* marker_prop = (MarkerProp*)alloc_prop(lycon, sizeof(MarkerProp));
    memset(marker_prop, 0, sizeof(MarkerProp));
    marker_prop->marker_type = marker_style;
    marker_prop->bullet_size = bullet_size;
    marker_prop->is_outside = is_outside;

    if (marker_css_content) {
        // ::marker { content: ... } overrides list-style-type
        size_t slen = strlen(marker_css_content);
        char* text_copy = (char*)arena_alloc(parent_elem->doc->arena, slen + 1);
        if (text_copy) {
            memcpy(text_copy, marker_css_content, slen + 1);
            marker_prop->text_content = text_copy;
        }
        marker_prop->marker_type = CSS_VALUE_DECIMAL;  // force text rendering
    } else if (is_string_marker) {
        // CSS Lists 3 §4.1: use the string directly as marker content
        size_t slen = strlen(string_marker);
        char* text_copy = (char*)arena_alloc(parent_elem->doc->arena, slen + 1);
        if (text_copy) {
            memcpy(text_copy, string_marker, slen + 1);
            marker_prop->text_content = text_copy;
        }
    } else if (!is_bullet_marker) {
        char marker_text[64];
        int marker_len = counter_format(lycon->counter_context, "list-item", marker_style, marker_text, sizeof(marker_text));
        if (marker_len > 0 && marker_len + 2 < (int)sizeof(marker_text)) { // INT_CAST_OK: size comparison
            marker_text[marker_len] = '.';
            marker_text[marker_len + 1] = ' ';
            marker_text[marker_len + 2] = '\0';
            marker_len += 2;

            char* text_copy = (char*)arena_alloc(parent_elem->doc->arena, marker_len + 1);
            if (text_copy) {
                memcpy(text_copy, marker_text, marker_len + 1);
                marker_prop->text_content = text_copy;
            }
        }
    }

    // CSS Lists 3 §4.2: compute marker width from content
    // For text markers, measure actual text width; for bullets, use fixed bullet size + padding
    if (marker_prop->text_content && font_handle) {
        TextExtents extents = font_measure_text(font_handle, marker_prop->text_content,
                                                 (int)strlen(marker_prop->text_content)); // INT_CAST_OK: string length
        marker_prop->width = extents.width;
    } else if (is_bullet_marker) {
        // bullet: disc/circle/square - use bullet_size + some padding
        marker_prop->width = bullet_size + font_size * 0.5f;
    } else {
        // fallback: use em-based estimate
        marker_prop->width = font_size * 1.375f;
    }

    marker_elem->view_type = RDT_VIEW_MARKER;
    marker_elem->blk = (BlockProp*)marker_prop;

    log_debug("    [List] Created %s::marker width=%.1f, bullet_size=%.1f, type=%s, text='%s'",
             is_outside ? "outside " : "", marker_prop->width, bullet_size,
             is_bullet_marker ? "bullet" : "text",
             marker_prop->text_content ? marker_prop->text_content : "");

    return marker_elem;
}

void process_list_item(LayoutContext* lycon, ViewBlock* block, DomNode* elmt,
                       DomElement* dom_elem, DisplayValue display) {
    if (!lycon->counter_context) return;

    // Detect if parent is <ol reversed>
    bool parent_reversed = false;
    if (dom_elem && dom_elem->tag_id == HTM_TAG_LI) {
        DomElement* parent_elem = dom_element_get_parent(dom_elem);
        if (parent_elem && parent_elem->tag_id == HTM_TAG_OL &&
            dom_element_has_attribute(parent_elem, "reversed")) {
            parent_reversed = true;
        }
    }

    // Handle <li value="N"> attribute: sets counter to N before increment
    if (dom_elem && dom_elem->tag_id == HTM_TAG_LI) {
        const char* value_attr = dom_element_get_attribute(dom_elem, "value");
        if (value_attr) {
            int li_value = atoi(value_attr);
            // CSS Lists 3: li[value] acts as counter-set: list-item <value>
            // Set to value±1 so auto-increment brings it to value
            int set_value = parent_reversed ? li_value + 1 : li_value - 1;
            char set_spec[64];
            snprintf(set_spec, sizeof(set_spec), "list-item %d", set_value);
            counter_set(lycon->counter_context, set_spec);
            log_debug("    [List] LI value=%s → counter-set: list-item %d", value_attr, set_value);
        }
    }

    // CSS Lists 3 §4.5: auto-increment list-item only if the element
    // does NOT already have an explicit counter-increment for list-item
    bool explicit_list_item_inc = (block->blk && block->blk->counter_increment &&
                                   strstr(block->blk->counter_increment, "list-item") != nullptr);
    if (!explicit_list_item_inc) {
        log_debug("    [List] Auto-%s list-item counter", parent_reversed ? "decrementing" : "incrementing");
        counter_increment(lycon->counter_context, parent_reversed ? "list-item -1" : "list-item 1");
    } else {
        log_debug("    [List] Skipping auto-increment (explicit counter-increment includes list-item)");
    }

    // For inline list-item (outer != LIST_ITEM), force inside position
    // since there's no block margin area for outside markers
    bool is_inline_list_item = (display.outer != CSS_VALUE_LIST_ITEM && display.list_item);

    // Set default list-style-position to outside if not specified
    // CSS 2.1 Section 12.5.1: Initial value is 'outside'
    bool is_outside_position = !is_inline_list_item;  // Default is outside, but inside for inline
    if (block->blk && block->blk->list_style_position != 0) {
        if (block->blk->list_style_position == 1) {
            is_outside_position = false;
            log_debug("    [List] list-style-position=inside (is_outside=0)");
        } else if (!is_inline_list_item) {
            is_outside_position = true;
            log_debug("    [List] list-style-position=outside (is_outside=1)");
        }
    } else {
        log_debug("    [List] Using default list-style-position=outside");
    }

    // Generate list marker if list-style-type is not 'none'
    // CSS 2.1 §12.5: list-style-type is inherited. Check element first, then parent.
    CssEnum effective_list_style = block->blk ? block->blk->list_style_type : (CssEnum)0;
    if (effective_list_style == 0) {
        // Inherit from parent <ol>/<ul> element
        DomElement* parent_elem = dom_element_get_parent(dom_elem);
        if (parent_elem && parent_elem->blk) {
            effective_list_style = parent_elem->blk->list_style_type;
        }
        if (effective_list_style == 0) {
            effective_list_style = CSS_VALUE_DISC;
        }
    }

    // CSS Lists 3 §4.1: check for string marker value
    const char* string_marker = (block->blk) ? block->blk->list_style_type_string : nullptr;
    bool has_marker = (effective_list_style != CSS_VALUE_NONE) || (string_marker != nullptr);

    // Check for ::marker { content: ... } CSS override
    const char* marker_css_content = nullptr;
    DomElement* list_elem = (DomElement*)elmt;
    if (list_elem->marker_styles) {
        CssDeclaration* content_decl = style_tree_get_declaration(
            list_elem->marker_styles, CSS_PROPERTY_CONTENT);
        if (content_decl && content_decl->value) {
            CssValue* cv = content_decl->value;
            if (cv->type == CSS_VALUE_TYPE_KEYWORD && cv->data.keyword == CSS_VALUE_NONE) {
                has_marker = false;  // content: none suppresses marker
                log_debug("    [List] ::marker { content: none } - suppressing marker");
            } else if (!(cv->type == CSS_VALUE_TYPE_KEYWORD && cv->data.keyword == CSS_VALUE_NORMAL)) {
                // explicit content (not 'normal') - resolve using counter context
                marker_css_content = dom_element_get_pseudo_element_content_with_counters(
                    list_elem, 6, lycon->counter_context, list_elem->doc->arena);
                if (marker_css_content) {
                    has_marker = true;
                    log_debug("    [List] ::marker { content: '%s' } - using CSS content", marker_css_content);
                }
            }
        }
    }

    if (!has_marker) return;

    CssEnum marker_style = effective_list_style;
    bool is_string_marker = (string_marker != nullptr);

    if (!is_string_marker) {
        const CssEnumInfo* info = css_enum_info(marker_style);
        log_debug("    [List] Generating marker with style: %s (0x%04X)", info ? info->name : "unknown", marker_style);
    } else {
        log_debug("    [List] Generating string marker: \"%s\"", string_marker);
    }

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

    if (!block->pseudo->marker_generated) {
        float font_size = 16.0f;
        if (block->font && block->font->font_size > 0) {
            font_size = block->font->font_size;
        }

        DomElement* parent_elem = (DomElement*)elmt;
        DomElement* marker_elem = create_marker_element(
            lycon, parent_elem, marker_style, font_size,
            is_bullet_marker, is_outside_position,
            is_string_marker, string_marker, marker_css_content,
            lycon->font.font_handle);

        if (marker_elem) {
            block->pseudo->marker = marker_elem;
            block->pseudo->marker_generated = true;
        }
    }
}

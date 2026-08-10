#include "layout.hpp"
#include "view.hpp"
#include "event.hpp"
#include "../lib/log.h"
#include <string.h>
#include <math.h>

/**
 * Layout support for HTML form controls.
 * Form controls are replaced elements with intrinsic dimensions.
 */

/**
 * Calculate intrinsic size for a text input based on size attribute and font.
 * Returns CONTENT-AREA dimensions (without border/padding).
 * The layout code adds actual CSS-resolved border/padding on top.
 */
struct FixedInputIntrinsicSize {
    const char* type;
    float width;
    float height;
};

static bool apply_fixed_input_intrinsic_size(FormControlProp* form, float pixel_ratio) {
    static const FixedInputIntrinsicSize sizes[] = {
        {"date", 119.0f, 17.0f},
        {"time", 96.0f, 17.0f},
        {"month", 149.0f, 17.0f},
        {"week", 141.0f, 17.0f},
        {"color", 44.0f, 23.0f},
    };
    for (const FixedInputIntrinsicSize& size : sizes) {
        if (strcmp(form->input_type, size.type) == 0) {
            form->intrinsic_width = size.width * pixel_ratio;
            form->intrinsic_height = size.height * pixel_ratio;
            return true;
        }
    }
    return false;
}

static void set_form_child_box(DomElement* elem, float x, float y,
                               float width, float height) {
    if (!elem) return;
    elem->x = x;
    elem->y = y;
    elem->width = elem->content_width = width;
    elem->height = elem->content_height = height;
}

static void zero_form_child_box(DomElement* elem) {
    set_form_child_box(elem, 0.0f, 0.0f, 0.0f, 0.0f);
}

static void layout_form_option_child(DomElement* option, bool listbox,
                                     float x, float* current_y, float width,
                                     float row_height) {
    if (!option) return;
    option->view_type = RDT_VIEW_BLOCK;
    if (!listbox) {
        zero_form_child_box(option);
        return;
    }
    set_form_child_box(option, x, *current_y, width, row_height);
    *current_y += row_height;
}

static float form_resolve_axis_size(ViewBlock* block, float intrinsic, bool horizontal,
                                    float* content_size) {
    if (!content_size) return 0.0f;
    const BlockProp* prop = block && block->blk ? block->block() : nullptr;
    float given = prop ? (horizontal ? prop->given_width : prop->given_height) : -1.0f;
    if (given >= 0.0f) {
        *content_size = layout_css_size_to_content_box(
            block->bound, layout_box_sizing(block), given, horizontal);
        return layout_css_size_to_border_box(
            block->bound, layout_box_sizing(block), given, horizontal);
    }
    *content_size = intrinsic;
    return layout_border_size_from_content_box(block, intrinsic, horizontal);
}

static void form_apply_axis_min_max(ViewBlock* block, bool horizontal,
                                    bool is_border_box, float* border_size,
                                    float* content_size) {
    if (!block || !block->blk || !border_size || !content_size) return;
    if (is_border_box) {
        *border_size = horizontal ? adjust_min_max_width(block, *border_size)
                                  : adjust_min_max_height(block, *border_size);
        *content_size = layout_content_size_from_border_box(
            block, *border_size, horizontal);
    } else {
        *content_size = horizontal ? adjust_min_max_width(block, *content_size)
                                   : adjust_min_max_height(block, *content_size);
        *border_size = layout_border_size_from_content_box(
            block, *content_size, horizontal);
    }
}

static bool form_control_has_specified_font(const ViewBlock* block) {
    StyleTree* style = block ? block->specified_style : nullptr;
    return style && (
        style_tree_get_declaration(style, CSS_PROPERTY_FONT) ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_FAMILY) ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_SIZE) ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_WEIGHT) ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_STYLE) ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_VARIANT));
}

static bool form_control_has_specified_line_height(const ViewBlock* block) {
    StyleTree* style = block ? block->specified_style : nullptr;
    return style && (
        style_tree_get_declaration(style, CSS_PROPERTY_LINE_HEIGHT) ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT));
}

static float textarea_used_line_height(LayoutContext* lycon, ViewBlock* block,
                                       FontProp* font, bool has_css_font) {
    if (!font || font->font_size <= 0.0f) return 0.0f;

    float line_height = 0.0f;
    if (block && block->blk && block->block_mut()->line_height) {
        const CssValue* value = block->block()->line_height;
        if (value->type == CSS_VALUE_TYPE_NUMBER) {
            line_height = value->data.number.value * font->font_size;
        } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
            line_height = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, value);
        } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            line_height = (value->data.number.value / 100.0f) * font->font_size;
        }
    }
    if (line_height > 0.0f) return line_height;

    // Keep the same UA fallback used for intrinsic textarea sizing.
    return has_css_font ? font->font_size * 1.2f : 15.0f;
}

static int textarea_visual_line_count(LayoutContext* lycon, FontProp* font,
                                      const char* value, float content_width) {
    if (!lycon || !font || !value || !*value || content_width <= 0.0f) {
        return 1;
    }

    FontBox font_box = {};
    setup_font(lycon->ui_context, &font_box, font);
    if (!font_box.font_handle) return 1;

    int line_count = 1;
    const char* line_start = value;
    const char* cursor = value;
    while (*cursor) {
        if (*cursor == '\n') {
            line_count++;
            cursor++;
            line_start = cursor;
            continue;
        }

        const char* next = cursor + 1;
        while ((*next & 0xC0) == 0x80) next++;
        int candidate_len = (int)(next - line_start);  // INT_CAST_OK: font API takes byte length.
        float candidate_width = font_measure_text(
            font_box.font_handle, line_start, candidate_len).width;
        if (cursor > line_start && candidate_width > content_width) {
            // Keep this greedy visual-line break in lockstep with textarea paint:
            // the overflowing glyph begins the next editable line.
            line_count++;
            line_start = cursor;
        }
        cursor = next;
    }
    return line_count;
}

static void calc_text_input_size(LayoutContext* lycon, ViewBlock* block,
                                 FormControlProp* form, FontProp* font) {
    float pr = lycon->ui_context->pixel_ratio;

    // Special fixed widths for date/time control types (Chrome UA intrinsic widths)
    // These are content-area widths (border-box minus 6px border+padding).
    // Chrome renders these at specific widths based on their picker format.
    if (form->input_type) {
        if (strcmp(form->input_type, "datetime-local") == 0) {
            // Chrome: ~211px border-box for HH:MM format, ~271px with seconds/ms
            // Width depends on the HTML value content attribute (not JS-set value).
            // If seconds (.ss or .sss) present in value attr → wider to show seconds field.
            float w = 205.0f;
            if (form->value && *form->value) {
                // Find the time part after 'T' and count colons there
                const char* t = strchr(form->value, 'T');
                if (t) {
                    int colons = 0;
                    for (const char* p = t + 1; *p; p++) if (*p == ':') colons++;
                    if (colons >= 2) w = 265.0f;  // has seconds → wider
                }
            }
            form->intrinsic_width = w * pr;
            form->intrinsic_height = 17.0f * pr;
            return;
        }
        if (apply_fixed_input_intrinsic_size(form, pr)) return;
    }

    int size = form->size > 0 ? form->size : FormDefaults::TEXT_SIZE_CHARS;

    // HTML spec §4.10.5.3.7: The size attribute specifies the width in "average character widths".
    // Chrome uses the advance width of '0' (U+0030) in the input's resolved font (the CSS 'ch'
    // unit). The calibrated default (145px for 20 chars at 13.3333px) matches Chrome's system
    // font. When CSS sets a different font-family (e.g. monospace), the '0' advance differs
    // significantly, so we measure the actual glyph and use it instead.
    float def_bp_h = 2 * (FormDefaults::TEXT_PADDING_H + FormDefaults::TEXT_BORDER);
    float default_content_w = FormDefaults::TEXT_WIDTH - def_bp_h;  // 145
    float ua_font_size = 13.3333f;
    float calibrated_char_w = default_content_w / FormDefaults::TEXT_SIZE_CHARS;  // 7.25

    float content_w = 0;
    bool uses_ua_default_width = size == FormDefaults::TEXT_SIZE_CHARS &&
        !form_control_has_specified_font(block);
    if (uses_ua_default_width) {
        // Keep the UA calibration only when the control retains the UA font.
        // With an author font, HTML's `size` is measured in that font's average
        // character width, including the default value of 20.
        content_w = default_content_w;
    } else if (font && font->font_size > 0 && lycon->ui_context) {
        FontBox temp_font;
        setup_font(lycon->ui_context, &temp_font, font);
        if (temp_font.font_handle) {
            GlyphInfo zero_glyph = font_get_glyph(temp_font.font_handle, '0');
            if (zero_glyph.advance_x > 0) {
                // HTML spec §4.10.5.3.7 + CSS Values §6.1.2 (ch unit):
                // Use the actual advance width of '0' from the resolved font.
                // However, font-family for unstyled inputs varies between
                // platforms and font backends — Chrome's UA-default Arial
                // gives '0' advance ≈ 7.25 at 13.3333px, but our font
                // backend may resolve a slightly different metric (e.g.
                // 7.4) producing border-box widths a few px wider than
                // Chrome. When the measured advance is within ~5% of the
                // calibrated UA value at the UA font size, snap to the
                // calibrated value so unstyled inputs match Chrome's UA
                // baseline. CSS-overridden fonts (monospace, bold, large
                // sizes, etc.) deviate well outside this tolerance and
                // continue to use the measured advance.
                float measured = zero_glyph.advance_x;
                if (font->font_size == ua_font_size) {
                    float ratio = measured / calibrated_char_w;
                    if (ratio >= 0.95f && ratio <= 1.05f) {
                        measured = calibrated_char_w;
                    }
                }
                content_w = measured * size;
            }
        }
    }
    if (content_w <= 0) {
        // Fallback: use calibrated formula (Chrome UA default at 13.3333px)
        content_w = default_content_w * size / FormDefaults::TEXT_SIZE_CHARS;
        if (font && font->font_size > 0 && font->font_size != ua_font_size) {
            content_w = content_w * font->font_size / ua_font_size;
        }
    }
    if (!uses_ua_default_width && !form->appearance_none) {
        // Native text controls reserve an inline editing gutter inside the CSS
        // content box; `appearance:none` removes that UA-only geometry.
        content_w += FormDefaults::TEXT_SIZE_CONTENT_GUTTER_H;
    }
    form->intrinsic_width = content_w;

    // Height: Chrome uses max(default_content_height, normal_line_height).
    // Default content height = TEXT_HEIGHT - border - padding = 17px.
    // When font-size is larger than default, line-height dominates.
    {
        float def_bp_v = 2 * (FormDefaults::TEXT_PADDING_V + FormDefaults::TEXT_BORDER);
        float default_content_h = (FormDefaults::TEXT_HEIGHT - def_bp_v) * pr;
        float line_h = default_content_h;
        if (font && font->font_size > 0 && lycon->ui_context) {
            FontBox temp_font;
            setup_font(lycon->ui_context, &temp_font, font);
            if (temp_font.font_handle) {
                line_h = calc_normal_line_height(temp_font.font_handle);
            }
        }
        if (font && font->font_size > 0 && font->font_size != ua_font_size) {
            float css_normal_line_h = font->font_size * 1.15f;
            line_h = css_normal_line_h;
        }
        if (form_control_has_specified_line_height(block) &&
            block->blk && block->block_mut()->line_height && font) {
            float resolved_line_h = layout_resolve_line_height_value(
                lycon, block->block()->line_height, block, font->font_size);
            // Native inputs reset inherited line-height, while an author line-height
            // sets the auto-height content box; glyph bounds alone are too short.
            if (resolved_line_h > 0.0f) line_h = resolved_line_h;
        }
        form->intrinsic_height = (line_h > default_content_h) ? line_h : default_content_h;
    }
}

/**
 * Calculate intrinsic size for a textarea based on cols/rows and font.
 * Returns border-box dimensions matching Chrome's UA defaults.
 * Chrome default (20 cols, 2 rows): 182x36 border-box.
 */
/**
 * Calculate intrinsic size for a textarea based on cols/rows and font.
 * Returns CONTENT-AREA dimensions (without border/padding).
 * The layout code adds actual CSS-resolved border/padding on top.
 *
 * HTML spec: textarea intrinsic height = rows × line-height.
 * When CSS overrides font-size/line-height, use the resolved values.
 * Fall back to Chrome UA defaults (monospace 13.333px) when no CSS overrides.
 */
static void calc_textarea_size(LayoutContext* lycon, ViewBlock* block, FormControlProp* form, FontProp* font) {
    float pr = lycon->ui_context->pixel_ratio;

    int cols = form->cols > 0 ? form->cols : FormDefaults::TEXTAREA_COLS;
    int rows = form->rows > 0 ? form->rows : FormDefaults::TEXTAREA_ROWS;

    if (font && font->font_size > 0) {
        // Determine if CSS overrides the UA default font-size.
        // font_size_from_medium is true when font-size is the initial value (CSS 'medium').
        // font-size ancestry does not reveal when author shorthand replaced UA control metrics.
        bool has_css_font = form_control_has_specified_font(block) ||
            !font->font_size_from_medium;
        float font_size = font->font_size;

        // Width: cols × char_width + scrollbar_reserve
        float char_w;
        float scrollbar_reserve;
        if (has_css_font) {
            // CSS specifies font — use space_width if available, else approximate
            char_w = (font->space_width > 0) ? font->space_width : font_size * 0.60f;
            scrollbar_reserve = 16.0f;
        } else {
            // UA default: Chrome monospace ~13.333px, char width ≈ 8px
            float ta_font = 13.333f;
            char_w = ta_font * 0.60f;
            scrollbar_reserve = 16.0f;
        }
        float content_w = cols * char_w + scrollbar_reserve;
        form->intrinsic_width = content_w * pr;

        // Height: rows × the same used line-height that establishes editable baselines.
        float line_ht = textarea_used_line_height(lycon, block, font, has_css_font);
        float content_h = rows * line_ht;
        form->intrinsic_height = content_h * pr;
    } else {
        // Fallback: content-area only (182x36 are border-box, subtract defaults)
        float def_bp_h = 2 * (FormDefaults::TEXTAREA_PADDING + FormDefaults::TEXTAREA_BORDER);
        float def_bp_v = 2 * (FormDefaults::TEXTAREA_PADDING + FormDefaults::TEXTAREA_BORDER);
        form->intrinsic_width = (182.0f - def_bp_h) * pr;
        form->intrinsic_height = (36.0f - def_bp_v) * pr;
    }
}

const char* form_button_label_text(ViewBlock* block, FormControlProp* form) {
    const char* text = form ? form->value : nullptr;
    if ((!text || !*text) && block) {
        text = block->get_attribute("value");
    }
    if ((!text || !*text) && form && form->input_type) {
        if (strcmp(form->input_type, "submit") == 0) return "Submit";
        if (strcmp(form->input_type, "reset") == 0) return "Reset";
    }
    return text;
}

/**
 * Calculate intrinsic size for a button based on content/value.
 * Returns border-box dimensions matching Chrome's UA defaults.
 * Chrome: padding 1px 6px, border 2px outset, height ~21px.
 */
static void calc_button_size(LayoutContext* lycon, ViewBlock* block, FormControlProp* form, FontProp* font) {
    float pr = lycon->ui_context->pixel_ratio;
    float zoom = layout_effective_zoom((View*)block);

    // Get button text from live value, value attribute, or input-type default.
    const char* text = form_button_label_text(block, form);

    if (text && *text && font && font->font_size > 0) {
        // author font metrics already define the button label's intrinsic content width.
        TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, text, (int)strlen(text)); // INT_CAST_OK: string length
        form->intrinsic_width = tw.max_content;
    } else {
        // Empty button: content width is 0 (border/padding added by layout)
        form->intrinsic_width = 0;
    }

    // Content height: border-box height should match TEXT_HEIGHT (21px) minus border and padding.
    // Using font_size directly would give incorrect height (font_size + border + padding != 21).
    // CSS UA stylesheets size buttons to match text input height for visual consistency.
    {
        float def_bp_v = 2 * (FormDefaults::BUTTON_PADDING_V + FormDefaults::BUTTON_BORDER) * zoom;
        float content_height = (FormDefaults::TEXT_HEIGHT * zoom - def_bp_v) * pr;
        if (block && block->display.inner == CSS_VALUE_FLEX &&
            font && font->font_size > 0 && lycon->ui_context) {
            FontBox temp_font;
            setup_font(lycon->ui_context, &temp_font, font);
            if (temp_font.font_handle) {
                // author flex buttons lay out real text children; the native
                // 15px control height would override their CSS normal line-height.
                float line_h = calc_normal_line_height(temp_font.font_handle);
                if (line_h > 0) content_height = line_h;
            }
        }
        form->intrinsic_height = content_height;
    }
}

float layout_select_combo_intrinsic_width(float max_text_width, bool has_ua_arrow) {
    float calculated = max_text_width;
    if (has_ua_arrow) {
        // Native combo boxes pixel-snap option text, then reserve the themed
        // arrow region and the two UA borders as part of the border box.
        calculated = ceilf(max_text_width) + FormDefaults::SELECT_NATIVE_ARROW_AREA +
            2.0f * FormDefaults::SELECT_BORDER;
    }
    float min_select_width = FormDefaults::SELECT_HEIGHT + 3.0f;
    return calculated > min_select_width ? calculated : min_select_width;
}

static float layout_select_listbox_row_height(const FormControlProp* form) {
    // Empty native listboxes use the compact anonymous-option metric; real
    // option rows use the 17px metric measured by their option layout.
    return form && form->option_count == 0
        ? FormDefaults::SELECT_EMPTY_LISTBOX_ROW_HEIGHT
        : FormDefaults::SELECT_OPTION_ROW_HEIGHT;
}

/**
 * Calculate intrinsic size for a select element based on option text.
 * Measures the longest option text using font metrics to determine width.
 * Chrome sizes select width to fit the longest option + dropdown arrow.
 */
static void calc_select_size(LayoutContext* lycon, ViewBlock* block, FormControlProp* form, FontProp* font) {
    float max_text_width = 0;
    float selected_text_width = 0;
    // CSS `appearance: none` removes the UA chrome; treat option text as min-content
    // (longest unbreakable word) so the SELECT collapses toward author intent — matches
    // Chrome where appearance-less <select> with `width: 100%` shrinks rather than
    // expanding to the longest option's max-content width.
    bool use_min_content = form && form->appearance_none;
    DocState* state = lycon && lycon->doc ? (DocState*)lycon->doc->state : nullptr;
    // Selection lives in ViewState; the form property is not an authoritative state cache.
    int selected_index = form_control_get_selected_index(state, (View*)block);

    int option_index = 0;
    for (DomElement* option = dom_select_next_option(block, nullptr); option;
         option = dom_select_next_option(block, option), option_index++) {
        float width = measure_direct_text_children_intrinsic_width(
            lycon, option, use_min_content, CSS_VALUE_NONE);
        if (width > max_text_width) max_text_width = width;
        if (option_index == selected_index) selected_text_width = width;
    }

    // Iterate through children to find longest option text
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        NameId ctag = child_elem->tag();

        if (ctag == MARKUP_NAME_OPTION) {
            // direct options were measured above together with nested optgroup options
        } else if (ctag == MARKUP_NAME_OPTGROUP) {
            // Measure optgroup label — shown as a header row in the dropdown (no indent)
            const char* label_attr = child_elem->get_attribute("label");
            if (label_attr) {
                size_t label_len = strlen(label_attr);
                if (label_len > 0) {
                    TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, label_attr, label_len);
                    float w = use_min_content ? tw.min_content : tw.max_content;
                    if (w > max_text_width) max_text_width = w;
                }
            }
            // Check options inside optgroup — they are indented in the dropdown on macOS Chrome
            for (DomNode* gc = child_elem->first_child; gc; gc = gc->next_sibling) {
                if (gc->is_element() && gc->as_element()->tag() == MARKUP_NAME_OPTION) {
                    float opt_text_width = measure_direct_text_children_intrinsic_width(
                        lycon, gc->as_element(), use_min_content, CSS_VALUE_NONE);
                    // Apply indent; blank options in an optgroup still occupy at least OPTGROUP_OPTION_MIN_WIDTH
                    float effective = opt_text_width + FormDefaults::OPTGROUP_OPTION_INDENT;
                    if (effective < FormDefaults::OPTGROUP_OPTION_MIN_WIDTH)
                        effective = FormDefaults::OPTGROUP_OPTION_MIN_WIDTH;
                    if (effective > max_text_width)
                        max_text_width = effective;
                }
            }
        }
    }

    // Chrome select border-box width includes text + arrow area + internal padding.
    // Chrome uses the system font for select text, which differs from the page font.
    // backend metrics measure with the page font — sometimes wider, sometimes narrower than Chrome.
    // A moderate overhead balances both cases across the test suite.

    // HTML §4.10.7: listbox mode when multiple attr is set OR size > 1
    // Listbox: no arrow, width = text content; height = visible_rows * row_height + 2px border
    bool is_listbox = form->multiple || form->select_size > 1;
    if (is_listbox) {
        // HTML §4.10.7: visible rows = size if given, else 4 for multiple, else max(1, option_count)
        int visible_rows;
        if (form->select_size > 0) {
            visible_rows = form->select_size;
        } else if (form->multiple) {
            visible_rows = 4;  // HTML spec default for multiple without explicit size
        } else {
            visible_rows = 1;
        }

        float row_height = layout_select_listbox_row_height(form);
        BoxMetrics box = layout_box_metrics(block);
        if (form->option_count == 0) {
            // With no option content, the native listbox contributes only its
            // actual padding and border; it has no themed minimum width.
            form->intrinsic_width = box.pad_border_h;
            form->intrinsic_height = visible_rows * row_height + box.pad_border_v;
        } else {
            // Native listboxes include each option's inline padding and the select border.
            float content_width = max_text_width + 2.0f * FormDefaults::OPTION_PADDING_H +
                2.0f * FormDefaults::SELECT_BORDER;
            float min_listbox_width = FormDefaults::SELECT_HEIGHT; // at least square
            form->intrinsic_width = content_width > min_listbox_width ? content_width : min_listbox_width;
            form->intrinsic_height = visible_rows * row_height + 2.0f;
        }
    } else {
        // Combo box mode
        // CSS `appearance: none` removes the native dropdown arrow; the page
        // typically supplies its own decoration via padding-right + ::after.
        // In that case we must NOT add UA arrow overhead, otherwise the
        // border-box ends up wider than what the author intended.
        bool has_ua_arrow = !form->appearance_none;
        if (form->appearance_base_select) {
            // The base select button is an inline flex row: selected label,
            // UA gap, picker icon, padding, and border all contribute intrinsically.
            form->intrinsic_width = selected_text_width + FormDefaults::BASE_SELECT_GAP +
                FormDefaults::BASE_SELECT_ICON_WIDTH +
                2.0f * (FormDefaults::BASE_SELECT_PADDING_H + FormDefaults::SELECT_BORDER);
        } else {
            form->intrinsic_width = layout_select_combo_intrinsic_width(max_text_width, has_ua_arrow);
        }
        // Add author-CSS horizontal padding + border so border-box width includes
        // text + arrow without the renderer overrunning the arrow area. (UA defaults
        // for padding=0 and border=1px are already accounted for in the overhead.)
        float pad_h = 0, border_h = 0;
        if (block->bound) {
            BoxMetrics block_box = layout_box_metrics(block);
            pad_h = block_box.padding_h;
            if (block->boundary()->border) {
                border_h = block_box.border_h;
                // subtract the UA default 1px borders already implicit in the layout
                border_h = border_h > 2.0f ? border_h - 2.0f : 0.0f;
            }
        }
        if (!form->appearance_base_select) form->intrinsic_width += pad_h + border_h;

        // Combo-box border-box height = content (font normal line-height)
        // + actual CSS padding + border. The UA default of 19px (font 13.3333,
        // padding 0, border 1) was a special case; once CSS overrides padding
        // or border we must recompute or the box squashes its content.
        float content_h = (FormDefaults::SELECT_HEIGHT - 2.0f); // UA default content-area
        if (font && font->font_size > 0 && lycon->ui_context) {
            FontBox temp_font;
            setup_font(lycon->ui_context, &temp_font, font);
            if (temp_font.font_handle) {
                float lh = calc_normal_line_height(temp_font.font_handle);
                if (lh > content_h) content_h = lh;
            }
        }
        float pad_v = 0, border_v = 0;
        if (block->bound) {
            BoxMetrics block_box = layout_box_metrics(block);
            pad_v = block_box.padding_v;
            border_v = block_box.border_v;
        }
        form->intrinsic_height = content_h + pad_v + border_v;
    }

    // Update given_width only if CSS didn't specify an explicit width
    if (block->blk && block->block_mut()->given_width < 0) {
        block->blk->given_width = form->intrinsic_width;
    }
    // A containment fallback is an already-resolved used height; native listbox
    // measurement must not overwrite it with the control's automatic height.
    if (block->blk && block->block_mut()->given_height < 0) {
        block->blk->given_height = form->intrinsic_height;
    }
}

/**
 * Layout a form control element.
 * Called from layout_block when the element owns the form-control role.
 */
void layout_form_control(LayoutContext* lycon, ViewBlock* block) {
    log_info("[FORM] layout_form_control ENTRY: block=%p, prop_type=%d, form=%p, tag=%s",
             block, block ? block->item_prop_debug_kind() : -1, block ? block->form : nullptr,
             (block && block->tag_name) ? block->tag_name : "?");
    if (!block || block->role_kind() != DomElement::ROLE_FORM || !block->form) {
        log_info("[FORM] layout_form_control SKIP: block=%p, prop_type=%d, form=%p",
                 block, block ? block->item_prop_debug_kind() : -1, block ? block->form : nullptr);
        return;
    }

    FormControlProp* form = block->form;
    FontProp* font = block->font ? block->font : lycon->font.style;
    float pr = lycon->ui_context->pixel_ratio;
    bool textarea_needs_baseline_set =
        form->control_type == FORM_CONTROL_TEXTAREA &&
        radiant::layout_uses_explicit_baseline_source(block);
    if (textarea_needs_baseline_set) {
        // Textarea children are its default value, not ordinary layout children.
        // Materialize that value before deriving the editable line-baseline set.
        tc_ensure_init(static_cast<DomElement*>(block));
    }

    log_debug("[FORM] layout_form_control: type=%d, tag=%s",
              form->control_type, block->tag_name ? block->tag_name : "?");

    // Calculate intrinsic size based on control type
    switch (form->control_type) {
    case FORM_CONTROL_TEXT:
        calc_text_input_size(lycon, block, form, font);
        break;

    case FORM_CONTROL_TEXTAREA:
        calc_textarea_size(lycon, block, form, font);
        break;

    case FORM_CONTROL_BUTTON:
        calc_button_size(lycon, block, form, font);
        break;

    case FORM_CONTROL_SELECT:
        calc_select_size(lycon, block, form, font);
        if (block->blk &&
            layout_block_has_automatic_size(block, true) &&
            !layout_block_has_automatic_size(block, false) &&
            layout_used_preferred_aspect_ratio(block) > 0.0f) {
            // Selects are replaced controls, but their intrinsic width is installed
            // before control layout; transfer the definite height before that metric
            // can hide the preferred ratio, then retain the auto minimum when allowed.
            layout_apply_preferred_ratio_to_replaced_auto_axes(lycon, block);
            bool min_width_is_auto = block->block()->given_min_width < 0.0f ||
                block->block()->given_min_width_type == CSS_VALUE_AUTO;
            if (min_width_is_auto && form->intrinsic_width > block->block()->given_width) {
                block->block_mut()->given_width = form->intrinsic_width;
                lycon->block.given_width = form->intrinsic_width;
            }
        }
        break;

    case FORM_CONTROL_CHECKBOX:
    case FORM_CONTROL_RADIO:
        // Fixed size set in resolve_htm_style
        form->intrinsic_width = FormDefaults::CHECK_SIZE * pr;
        form->intrinsic_height = FormDefaults::CHECK_SIZE * pr;
        break;

    case FORM_CONTROL_RANGE:
        // Fixed size set in resolve_htm_style
        break;

    case FORM_CONTROL_IMAGE:
        // Image button: replaced element, fixed size set in resolve_htm_style
        break;

    case FORM_CONTROL_HIDDEN:
        form->intrinsic_width = 0;
        form->intrinsic_height = 0;
        break;

    default:
        break;
    }

    BoxMetrics box = layout_box_metrics(block);

    // Check box-sizing model (default is content-box per CSS spec)
    bool is_border_box = layout_uses_border_box(block);

    // Apply CSS width/height if specified, otherwise use intrinsic
    // Note: intrinsic sizes are CONTENT-AREA (no border/padding included).
    // CSS width/height follows box-sizing model.
    float content_width = form->intrinsic_width;
    float content_height = form->intrinsic_height;
    float width, height;

    width = form_resolve_axis_size(block, form->intrinsic_width, true, &content_width);
    height = form_resolve_axis_size(block, form->intrinsic_height, false, &content_height);

    log_debug("[FORM] layout: intrinsic=%.1fx%.1f, given=%.1fx%.1f, border=%.1f/%.1f, padding=%.1f/%.1f, box_sizing=%s",
              form->intrinsic_width, form->intrinsic_height,
              block->blk ? block->block()->given_width : -1, block->blk ? block->block()->given_height : -1,
              box.border_h, box.border_v, box.padding_h, box.padding_v, is_border_box ? "border-box" : "content-box");

    // Set final dimensions
    // Apply CSS min-width/max-width constraints (e.g., max-width: 100px on textarea).
    // Per CSS spec: for content-box, min/max-width constrains content area;
    // for border-box, min/max-width constrains the border-box (already handled correctly).
    form_apply_axis_min_max(block, true, is_border_box, &width, &content_width);
    form_apply_axis_min_max(block, false, is_border_box, &height, &content_height);

    if (width > MAX_LAYOUT_DIMENSION) {
        // css Values 4 permits approximating an unsupported used extent; after
        // large padding is added, keep the replaced border box representable
        // and derive a non-negative content box from that final border box.
        width = MAX_LAYOUT_DIMENSION;
        content_width = layout_content_width_from_border_box(block, width);
    }
    if (height > MAX_LAYOUT_DIMENSION) {
        // Preserve the border-box/content-box invariant for the block axis too.
        height = MAX_LAYOUT_DIMENSION;
        content_height = layout_content_height_from_border_box(block, height);
    }
    block->width = width;
    block->height = height;
    block->content_width = content_width;
    block->content_height = content_height;

    if (block->content_width < 0) block->content_width = 0;
    if (block->content_height < 0) block->content_height = 0;

    // Set internal text baseline for inline-block baseline alignment.
    // Form controls with text have a virtual internal
    // baseline where their text content would sit. Without this, the parent's
    // inline layout treats them as replaced elements (baseline at bottom margin edge),
    // which causes the line box to be taller than expected.
    // Single-line selects expose the baseline of their internal selected-value button;
    // listbox selects have no such single internal line and keep the replaced baseline.
    bool is_single_line_select = form->control_type == FORM_CONTROL_SELECT &&
        !form->multiple && form->select_size <= 1;
    if (form->control_type == FORM_CONTROL_TEXT ||
        form->control_type == FORM_CONTROL_TEXTAREA ||
        form->control_type == FORM_CONTROL_BUTTON ||
        is_single_line_select) {
        float border_top = (block->bound && block->boundary_mut()->border) ? block->boundary_mut()->border->width.top : 0;
        float pad_top = block->bound ? block->boundary()->padding.top : 0;
        // Use the font ascender from font metrics (hhea_ascender).
        // This ensures the form control's internal text baseline aligns correctly
        // with surrounding inline text, keeping line box height minimal.
        float font_ascender = (font && font->ascender > 0) ? font->ascender : (font ? font->font_size * 0.8f : 13.0f);
        float select_baseline_offset = 0.0f;
        if (is_single_line_select) {
            float natural_line_height = FormDefaults::SELECT_HEIGHT - 2.0f;
            // A definite select height centers its native line box; collapsed
            // heights retain the baseline of the control's natural line box.
            select_baseline_offset = max(0.0f,
                (block->content_height - natural_line_height) * 0.5f);
        }
        lycon->block.last_line_ascender = border_top + pad_top + font_ascender +
            select_baseline_offset;
        lycon->block.last_line_max_ascender = lycon->block.last_line_ascender;

        if (textarea_needs_baseline_set) {
            bool has_css_font = form_control_has_specified_font(block) ||
                (font && !font->font_size_from_medium);
            float line_height = textarea_used_line_height(
                lycon, block, font, has_css_font);
            float font_descender = font && font->descender > 0.0f
                ? font->descender : 0.0f;
            float content_height = font_ascender + font_descender;
            // A vertical control's baseline axis is its logical block axis; the
            // horizontal surrogate's ascent can exceed that extent when the
            // specified font is taller than the control.
            float baseline_ascender = layout_block_inline_axis_is_vertical(block)
                ? min(font_ascender, block->content_width) : font_ascender;
            float first_baseline = border_top + pad_top + baseline_ascender +
                (line_height - content_height) * 0.5f;
            int visual_lines = textarea_visual_line_count(
                lycon, font, form->current_value ? form->current_value : form->value,
                block->content_width);
            float last_baseline = first_baseline +
                (float)(visual_lines - 1) * line_height;

            // CSS Box Alignment clamps a scroll container's line baseline to
            // its block-end border edge when editable content overflows.
            form->last_text_baseline_overflow = max(
                last_baseline - block->height, 0.0f);
            form->first_text_baseline = min(first_baseline, block->height);
            form->last_text_baseline = min(last_baseline, block->height);
        }
    }

    log_debug("[FORM] layout complete: w=%.1f h=%.1f cw=%.1f ch=%.1f",
              block->width, block->height, block->content_width, block->content_height);

    // For select (and other form controls with option/optgroup children):
    // - Listbox mode (multiple or size>1): position each option as a row inside the select.
    //   Each option gets the full content-area width and row_height, stacked from top.
    //   Chrome listbox: border=1px all sides, no padding, row_height=17px.
    // - Combo box mode: options are not rendered; report 0×0 (Chrome behaviour).
    if (block->is_element() && form->control_type == FORM_CONTROL_SELECT) {
        bool is_listbox = form->multiple || form->select_size > 1;

        float border_left = (block->bound && block->boundary_mut()->border) ? block->boundary_mut()->border->width.left : 0;
        float border_top  = (block->bound && block->boundary_mut()->border) ? block->boundary_mut()->border->width.top  : 0;
        float border_right= (block->bound && block->boundary_mut()->border) ? block->boundary_mut()->border->width.right : 0;
        float option_width = block->width - border_left - border_right - block->boundary()->padding.left - block->boundary()->padding.right;
        if (option_width < 0) option_width = 0;

        float row_height = layout_select_listbox_row_height(form);
        // hr margin-top per UA stylesheet: 0.5em (HTML spec §10 / Chrome UA)
        float fs = (font && font->font_size > 0) ? font->font_size : 13.333f;
        const float hr_margin_top = fs * 0.5f;
        float current_y = border_top;  // tracks y offset for next item in listbox

        for (DomNode* child = block->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;
            DomElement* celem = child->as_element();
            NameId ctag = celem->tag();

            if (ctag == MARKUP_NAME_OPTION) {
                layout_form_option_child(celem, is_listbox, border_left,
                                         &current_y, option_width, row_height);
            } else if (ctag == MARKUP_NAME_HR) {
                celem->view_type = RDT_VIEW_BLOCK;
                if (is_listbox) {
                    // hr inside listbox: zero height, margin-top = 0.5em (UA stylesheet)
                    current_y += hr_margin_top;
                    set_form_child_box(celem, border_left, current_y,
                                       option_width, 0.0f);
                } else {
                    zero_form_child_box(celem);
                }
            } else if (ctag == MARKUP_NAME_OPTGROUP) {
                celem->view_type = RDT_VIEW_BLOCK;
                zero_form_child_box(celem);
                // Recurse into optgroup children
                for (DomNode* gc = celem->first_child; gc; gc = gc->next_sibling) {
                    if (!gc->is_element()) continue;
                    DomElement* gcelem = gc->as_element();
                    uintptr_t gctag = gcelem->tag();
                    if (gctag == MARKUP_NAME_OPTION) {
                        layout_form_option_child(gcelem, is_listbox, border_left,
                                                 &current_y, option_width, row_height);
                    } else if (gctag == MARKUP_NAME_OPTGROUP) {
                        gcelem->view_type = RDT_VIEW_BLOCK;
                        zero_form_child_box(gcelem);
                    }
                }
            }
        }
    } else if (block->is_element()) {
        // Non-select form controls: mark any stray children as 0×0
        for (DomNode* child = block->first_child; child; child = child->next_sibling) {
            if (child->is_element()) {
                NameId ctag = child->as_element()->tag();
                if (ctag == MARKUP_NAME_OPTION || ctag == MARKUP_NAME_OPTGROUP) {
                    DomElement* celem = child->as_element();
                    celem->view_type = RDT_VIEW_BLOCK;
                    zero_form_child_box(celem);
                }
            }
        }
    }
}

/**
 * Check if an element is a form control that needs special layout.
 */
bool is_form_control(DomElement* elem) {
    if (!elem) return false;

    switch (elem->tag_id) {
    case MARKUP_NAME_INPUT:
    case MARKUP_NAME_BUTTON:
    case MARKUP_NAME_SELECT:
    case MARKUP_NAME_TEXTAREA:
        return true;
    default:
        return false;
    }
}

#include "layout.hpp"
#include "form_control.hpp"
#include "intrinsic_sizing.hpp"
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
static void calc_text_input_size(LayoutContext* lycon, FormControlProp* form, FontProp* font) {
    float pr = lycon->ui_context->pixel_ratio;

    // Special fixed widths for date/time control types (Chrome UA intrinsic widths)
    // These are content-area widths (border-box minus 6px border+padding).
    // Chrome renders these at specific widths based on their picker format.
    if (form->input_type) {
        if (strcmp(form->input_type, "date") == 0) {
            // Chrome: ~125px border-box (shows MM/DD/YYYY)
            form->intrinsic_width = 119.0f * pr;
            form->intrinsic_height = 17.0f * pr;
            return;
        }
        if (strcmp(form->input_type, "time") == 0) {
            // Chrome: ~102px border-box (shows HH:MM AM/PM)
            form->intrinsic_width = 96.0f * pr;
            form->intrinsic_height = 17.0f * pr;
            return;
        }
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
        if (strcmp(form->input_type, "month") == 0) {
            // Chrome: ~155px border-box (shows MMMM YYYY)
            form->intrinsic_width = 149.0f * pr;
            form->intrinsic_height = 17.0f * pr;
            return;
        }
        if (strcmp(form->input_type, "week") == 0) {
            // Chrome: ~147px border-box (shows Week ##, YYYY)
            form->intrinsic_width = 141.0f * pr;
            form->intrinsic_height = 17.0f * pr;
            return;
        }
        if (strcmp(form->input_type, "color") == 0) {
            // Chrome: ~53px border-box (small color swatch)
            form->intrinsic_width = 44.0f * pr;
            form->intrinsic_height = 23.0f * pr;
            return;
        }
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
    if (font && font->font_size > 0 && lycon->ui_context) {
        FontBox temp_font;
        setup_font(lycon->ui_context, &temp_font, font);
        if (temp_font.font_handle) {
            GlyphInfo zero_glyph = font_get_glyph(temp_font.font_handle, '0');
            if (zero_glyph.advance_x > 0) {
                // HTML spec §4.10.5.3.7 + CSS Values §6.1.2 (ch unit):
                // Use the actual advance width of '0' from the resolved font
                // whenever it can be measured. The calibrated constant below
                // is only a fallback for when no font handle is available.
                content_w = zero_glyph.advance_x * size;
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
        bool has_css_font = !font->font_size_from_medium;
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

        // Height: rows × line-height
        // Resolve line-height from the block's computed style (CSS 2.1 §10.8.1)
        float line_ht = 0;
        if (block && block->blk && block->blk->line_height) {
            const CssValue* lh = block->blk->line_height;
            if (lh->type == CSS_VALUE_TYPE_NUMBER) {
                // e.g. line-height: 1.5 → 1.5 × font-size
                line_ht = lh->data.number.value * font_size;
            } else if (lh->type == CSS_VALUE_TYPE_LENGTH) {
                // e.g. line-height: 20px, or line-height: 1em (resolved to px by style)
                line_ht = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, lh);
            } else if (lh->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // e.g. line-height: 120% → 1.2 × font-size
                line_ht = (lh->data.number.value / 100.0f) * font_size;
            }
            // CSS_VALUE_NORMAL or unrecognized → fall through to default
        }
        if (line_ht <= 0) {
            // 'normal' or no explicit line-height: approximate as font_size × 1.2
            // For UA default monospace (13.333px), this gives ~16px; Chrome uses ~15px.
            if (has_css_font) {
                line_ht = font_size * 1.2f;
            } else {
                line_ht = 15.0f;  // Chrome UA default for 13.333px monospace
            }
        }
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

/**
 * Calculate intrinsic size for a button based on content/value.
 * Returns border-box dimensions matching Chrome's UA defaults.
 * Chrome: padding 1px 6px, border 2px outset, height ~21px.
 */
static void calc_button_size(LayoutContext* lycon, ViewBlock* block, FormControlProp* form, FontProp* font) {
    float pr = lycon->ui_context->pixel_ratio;

    // Get button text from value attribute or child content
    const char* text = form->value;
    if (!text || !*text) {
        // Check input type for default text
        if (form->input_type) {
            if (strcmp(form->input_type, "submit") == 0) text = "Submit";
            else if (strcmp(form->input_type, "reset") == 0) text = "Reset";
        }
    }

    if (text && *text && font && font->font_size > 0) {
        // Use FreeType measurement for accurate button text width.
        // FreeType overestimates proportional font width by ~12.5% vs Chrome UA
        // (likely due to hinting/kerning differences), so apply a correction factor.
        TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, text, (int)strlen(text)); // INT_CAST_OK: string length
        form->intrinsic_width = tw.max_content * 0.875f;
    } else {
        // Empty button: content width is 0 (border/padding added by layout)
        form->intrinsic_width = 0;
    }

    // Content height: border-box height should match TEXT_HEIGHT (21px) minus border and padding.
    // Using font_size directly would give incorrect height (font_size + border + padding != 21).
    // CSS UA stylesheets size buttons to match text input height for visual consistency.
    {
        float def_bp_v = 2 * (FormDefaults::BUTTON_PADDING_V + FormDefaults::BUTTON_BORDER);
        form->intrinsic_height = (FormDefaults::TEXT_HEIGHT - def_bp_v) * pr;
    }
}

/**
 * Calculate intrinsic size for a select element based on option text.
 * Measures the longest option text using font metrics to determine width.
 * Chrome sizes select width to fit the longest option + dropdown arrow.
 */
static void calc_select_size(LayoutContext* lycon, ViewBlock* block, FormControlProp* form, FontProp* font) {
    float max_text_width = 0;

    // Iterate through children to find longest option text
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        uintptr_t ctag = child_elem->tag();

        if (ctag == HTM_TAG_OPTION) {
            // Get text from DomText children of option
            for (DomNode* tc = child_elem->first_child; tc; tc = tc->next_sibling) {
                DomText* dt = tc->as_text();
                if (dt && dt->text && dt->length > 0) {
                    TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, dt->text, dt->length);
                    if (tw.max_content > max_text_width)
                        max_text_width = tw.max_content;
                }
            }
        } else if (ctag == HTM_TAG_OPTGROUP) {
            // Measure optgroup label — shown as a header row in the dropdown (no indent)
            const char* label_attr = child_elem->get_attribute("label");
            if (label_attr) {
                size_t label_len = strlen(label_attr);
                if (label_len > 0) {
                    TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, label_attr, label_len);
                    if (tw.max_content > max_text_width)
                        max_text_width = tw.max_content;
                }
            }
            // Check options inside optgroup — they are indented in the dropdown on macOS Chrome
            for (DomNode* gc = child_elem->first_child; gc; gc = gc->next_sibling) {
                if (gc->is_element() && gc->as_element()->tag() == HTM_TAG_OPTION) {
                    float opt_text_width = 0;
                    for (DomNode* tc = gc->as_element()->first_child; tc; tc = tc->next_sibling) {
                        DomText* dt = tc->as_text();
                        if (dt && dt->text && dt->length > 0) {
                            TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, dt->text, dt->length);
                            if (tw.max_content > opt_text_width)
                                opt_text_width = tw.max_content;
                        }
                    }
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
    // FreeType measures with the page font — sometimes wider, sometimes narrower than Chrome.
    // A moderate overhead balances both cases across the test suite.

    // HTML §4.10.7: listbox mode when multiple attr is set OR size > 1
    // Listbox: no arrow, width = text content; height = visible_rows * row_height + 2px border
    bool is_listbox = form->multiple || form->select_size > 1;
    if (is_listbox) {
        // Listbox mode: width = max option text + small padding; no dropdown arrow
        // Chrome listbox select content-box width = max_text_width with some internal padding
        float content_width = max_text_width;
        float min_listbox_width = FormDefaults::SELECT_HEIGHT; // at least square
        form->intrinsic_width = content_width > min_listbox_width ? content_width : min_listbox_width;

        // HTML §4.10.7: visible rows = size if given, else 4 for multiple, else max(1, option_count)
        int visible_rows;
        if (form->select_size > 0) {
            visible_rows = form->select_size;
        } else if (form->multiple) {
            visible_rows = 4;  // HTML spec default for multiple without explicit size
        } else {
            visible_rows = 1;
        }

        // Chrome listbox select: row_height = 17px, border = 1px all sides
        // border-box height = visible_rows * row_height + top_border + bottom_border
        const float row_height = 17.0f;
        form->intrinsic_height = visible_rows * row_height + 2.0f;
    } else {
        // Combo box mode
        float overhead = FormDefaults::SELECT_ARROW_WIDTH + 1.0f; // arrow + small margin
        float min_select_width = FormDefaults::SELECT_HEIGHT + 3.0f; // ~22px minimum (matches Chrome empty)
        float calculated = max_text_width + overhead;
        form->intrinsic_width = calculated > min_select_width ? calculated : min_select_width;
        // Add author-CSS horizontal padding + border so border-box width includes
        // text + arrow without the renderer overrunning the arrow area. (UA defaults
        // for padding=0 and border=1px are already accounted for in the overhead.)
        float pad_h = 0, border_h = 0;
        if (block->bound) {
            pad_h = block->bound->padding.left + block->bound->padding.right;
            if (block->bound->border) {
                border_h = block->bound->border->width.left + block->bound->border->width.right;
                // subtract the UA default 1px borders already implicit in the layout
                border_h = border_h > 2.0f ? border_h - 2.0f : 0.0f;
            }
        }
        form->intrinsic_width += pad_h + border_h;

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
            pad_v = block->bound->padding.top + block->bound->padding.bottom;
            if (block->bound->border) {
                border_v = block->bound->border->width.top + block->bound->border->width.bottom;
            }
        }
        form->intrinsic_height = content_h + pad_v + border_v;
    }

    // Update given_width only if CSS didn't specify an explicit width
    if (block->blk && block->blk->given_width < 0) {
        block->blk->given_width = form->intrinsic_width;
    }
    // Always update given_height to match computed intrinsic height (may differ from default)
    if (block->blk) {
        block->blk->given_height = form->intrinsic_height;
    }
}

/**
 * Layout a form control element.
 * Called from layout_block when element has item_prop_type == ITEM_PROP_FORM.
 */
void layout_form_control(LayoutContext* lycon, ViewBlock* block) {
    log_info("[FORM] layout_form_control ENTRY: block=%p, prop_type=%d, form=%p, tag=%s",
             block, block ? block->item_prop_type : -1, block ? block->form : nullptr,
             (block && block->tag_name) ? block->tag_name : "?");
    if (!block || block->item_prop_type != DomElement::ITEM_PROP_FORM || !block->form) {
        log_info("[FORM] layout_form_control SKIP: block=%p, prop_type=%d, form=%p",
                 block, block ? block->item_prop_type : -1, block ? block->form : nullptr);
        return;
    }

    FormControlProp* form = block->form;
    FontProp* font = block->font ? block->font : lycon->font.style;
    float pr = lycon->ui_context->pixel_ratio;

    log_debug("[FORM] layout_form_control: type=%d, tag=%s",
              form->control_type, block->tag_name ? block->tag_name : "?");

    // Calculate intrinsic size based on control type
    switch (form->control_type) {
    case FORM_CONTROL_TEXT:
        calc_text_input_size(lycon, form, font);
        break;

    case FORM_CONTROL_TEXTAREA:
        calc_textarea_size(lycon, block, form, font);
        break;

    case FORM_CONTROL_BUTTON:
        calc_button_size(lycon, block, form, font);
        break;

    case FORM_CONTROL_SELECT:
        calc_select_size(lycon, block, form, font);
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

    // Calculate border and padding sizes
    float border_h = 0, border_v = 0;
    float padding_h = 0, padding_v = 0;

    if (block->bound) {
        if (block->bound->border) {
            border_h = block->bound->border->width.left + block->bound->border->width.right;
            border_v = block->bound->border->width.top + block->bound->border->width.bottom;
        }
        padding_h = block->bound->padding.left + block->bound->padding.right;
        padding_v = block->bound->padding.top + block->bound->padding.bottom;
    }

    // Check box-sizing model (default is content-box per CSS spec)
    bool is_border_box = (block->blk && block->blk->box_sizing == CSS_VALUE_BORDER_BOX);

    // Apply CSS width/height if specified, otherwise use intrinsic
    // Note: intrinsic sizes are CONTENT-AREA (no border/padding included).
    // CSS width/height follows box-sizing model.
    float content_width = form->intrinsic_width;
    float content_height = form->intrinsic_height;
    float width, height;

    if (block->blk && block->blk->given_width >= 0) {
        // CSS specifies width
        if (is_border_box) {
            width = block->blk->given_width;
            content_width = width - border_h - padding_h;
        } else {
            content_width = block->blk->given_width;
            width = content_width + border_h + padding_h;
        }
    } else {
        // Use intrinsic content size + actual CSS border/padding
        width = content_width + border_h + padding_h;
    }

    if (block->blk && block->blk->given_height >= 0) {
        // CSS specifies height
        if (is_border_box) {
            height = block->blk->given_height;
            content_height = height - border_v - padding_v;
        } else {
            content_height = block->blk->given_height;
            height = content_height + border_v + padding_v;
        }
    } else {
        // Use intrinsic content size + actual CSS border/padding
        height = content_height + border_v + padding_v;
    }

    log_debug("[FORM] layout: intrinsic=%.1fx%.1f, given=%.1fx%.1f, border=%.1f/%.1f, padding=%.1f/%.1f, box_sizing=%s",
              form->intrinsic_width, form->intrinsic_height,
              block->blk ? block->blk->given_width : -1, block->blk ? block->blk->given_height : -1,
              border_h, border_v, padding_h, padding_v, is_border_box ? "border-box" : "content-box");

    // Set final dimensions
    // Apply CSS min-width/max-width constraints (e.g., max-width: 100px on textarea).
    // Per CSS spec: for content-box, min/max-width constrains content area;
    // for border-box, min/max-width constrains the border-box (already handled correctly).
    if (block->blk) {
        if (is_border_box) {
            width = adjust_min_max_width(block, width);
            content_width = width - border_h - padding_h;
            height = adjust_min_max_height(block, height);
            content_height = height - border_v - padding_v;
        } else {
            content_width = adjust_min_max_width(block, content_width);
            content_height = adjust_min_max_height(block, content_height);
            width = content_width + border_h + padding_h;
            height = content_height + border_v + padding_v;
        }
    }
    block->width = width;
    block->height = height;
    block->content_width = content_width;
    block->content_height = content_height;

    if (block->content_width < 0) block->content_width = 0;
    if (block->content_height < 0) block->content_height = 0;

    // Set internal text baseline for inline-block baseline alignment.
    // Form controls with text (input, textarea, button) have a virtual internal
    // baseline where their text content would sit. Without this, the parent's
    // inline layout treats them as replaced elements (baseline at bottom margin edge),
    // which causes the line box to be taller than expected.
    // SELECT uses bottom-margin-edge baseline per CSS 2.1 §10.8.1 (no in-flow CSS
    // line boxes), consistent with replaced elements. This ensures correct vertical
    // alignment when selects of different heights appear on the same line.
    if (form->control_type == FORM_CONTROL_TEXT ||
        form->control_type == FORM_CONTROL_TEXTAREA ||
        form->control_type == FORM_CONTROL_BUTTON) {
        float border_top = (block->bound && block->bound->border) ? block->bound->border->width.top : 0;
        float pad_top = block->bound ? block->bound->padding.top : 0;
        // Use the font ascender from font metrics (hhea_ascender).
        // This ensures the form control's internal text baseline aligns correctly
        // with surrounding inline text, keeping line box height minimal.
        float font_ascender = (font && font->ascender > 0) ? font->ascender : (font ? font->font_size * 0.8f : 13.0f);
        lycon->block.last_line_ascender = border_top + pad_top + font_ascender;
        lycon->block.last_line_max_ascender = lycon->block.last_line_ascender;
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

        float border_left = (block->bound && block->bound->border) ? block->bound->border->width.left : 0;
        float border_top  = (block->bound && block->bound->border) ? block->bound->border->width.top  : 0;
        float border_right= (block->bound && block->bound->border) ? block->bound->border->width.right : 0;
        float option_width = block->width - border_left - border_right - block->bound->padding.left - block->bound->padding.right;
        if (option_width < 0) option_width = 0;

        // row_height matches Chrome's listbox option row (17px per CSS spec / Chrome UA)
        const float row_height = 17.0f;
        // hr margin-top per UA stylesheet: 0.5em (HTML spec §10 / Chrome UA)
        float fs = (font && font->font_size > 0) ? font->font_size : 13.333f;
        const float hr_margin_top = fs * 0.5f;
        float current_y = border_top;  // tracks y offset for next item in listbox

        for (DomNode* child = block->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;
            DomElement* celem = child->as_element();
            uintptr_t ctag = celem->tag();

            if (ctag == HTM_TAG_OPTION) {
                celem->view_type = RDT_VIEW_BLOCK;
                if (is_listbox) {
                    celem->x = border_left;
                    celem->y = current_y;
                    celem->width = option_width;
                    celem->height = row_height;
                    celem->content_width = option_width;
                    celem->content_height = row_height;
                    current_y += row_height;
                } else {
                    celem->x = 0;  celem->y = 0;
                    celem->width = 0;  celem->height = 0;
                    celem->content_width = 0;  celem->content_height = 0;
                }
            } else if (ctag == HTM_TAG_HR) {
                celem->view_type = RDT_VIEW_BLOCK;
                if (is_listbox) {
                    // hr inside listbox: zero height, margin-top = 0.5em (UA stylesheet)
                    current_y += hr_margin_top;
                    celem->x = border_left;
                    celem->y = current_y;
                    celem->width = option_width;
                    celem->height = 0;
                    celem->content_width = option_width;
                    celem->content_height = 0;
                } else {
                    celem->x = 0;  celem->y = 0;
                    celem->width = 0;  celem->height = 0;
                    celem->content_width = 0;  celem->content_height = 0;
                }
            } else if (ctag == HTM_TAG_OPTGROUP) {
                celem->view_type = RDT_VIEW_BLOCK;
                celem->x = 0;  celem->y = 0;
                celem->width = 0;  celem->height = 0;
                celem->content_width = 0;  celem->content_height = 0;
                // Recurse into optgroup children
                for (DomNode* gc = celem->first_child; gc; gc = gc->next_sibling) {
                    if (!gc->is_element()) continue;
                    DomElement* gcelem = gc->as_element();
                    uintptr_t gctag = gcelem->tag();
                    if (gctag == HTM_TAG_OPTION) {
                        gcelem->view_type = RDT_VIEW_BLOCK;
                        if (is_listbox) {
                            gcelem->x = border_left;
                            gcelem->y = current_y;
                            gcelem->width = option_width;
                            gcelem->height = row_height;
                            gcelem->content_width = option_width;
                            gcelem->content_height = row_height;
                            current_y += row_height;
                        } else {
                            gcelem->x = 0;  gcelem->y = 0;
                            gcelem->width = 0;  gcelem->height = 0;
                            gcelem->content_width = 0;  gcelem->content_height = 0;
                        }
                    } else if (gctag == HTM_TAG_OPTGROUP) {
                        gcelem->view_type = RDT_VIEW_BLOCK;
                        gcelem->x = 0;  gcelem->y = 0;
                        gcelem->width = 0;  gcelem->height = 0;
                        gcelem->content_width = 0;  gcelem->content_height = 0;
                    }
                }
            }
        }
    } else if (block->is_element()) {
        // Non-select form controls: mark any stray children as 0×0
        for (DomNode* child = block->first_child; child; child = child->next_sibling) {
            if (child->is_element()) {
                uintptr_t ctag = child->as_element()->tag();
                if (ctag == HTM_TAG_OPTION || ctag == HTM_TAG_OPTGROUP) {
                    DomElement* celem = child->as_element();
                    celem->view_type = RDT_VIEW_BLOCK;
                    celem->x = 0;  celem->y = 0;
                    celem->width = 0;  celem->height = 0;
                    celem->content_width = 0;  celem->content_height = 0;
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
    case HTM_TAG_INPUT:
    case HTM_TAG_BUTTON:
    case HTM_TAG_SELECT:
    case HTM_TAG_TEXTAREA:
        return true;
    default:
        return false;
    }
}

/**
 * Get intrinsic min-content width for a form control.
 */
float form_control_min_content_width(ViewBlock* block) {
    if (!block || block->item_prop_type != DomElement::ITEM_PROP_FORM || !block->form) {
        return 0;
    }

    // Form controls have fixed intrinsic size
    return block->form->intrinsic_width;
}

/**
 * Get intrinsic max-content width for a form control.
 */
float form_control_max_content_width(ViewBlock* block) {
    if (!block || block->item_prop_type != DomElement::ITEM_PROP_FORM || !block->form) {
        return 0;
    }

    // Form controls have fixed intrinsic size
    return block->form->intrinsic_width;
}

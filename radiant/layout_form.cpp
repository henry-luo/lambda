#include "layout.hpp"
#include "form_control.hpp"
#include "../lib/log.h"
#include <string.h>
#include <math.h>

/**
 * Layout support for HTML form controls.
 * Form controls are replaced elements with intrinsic dimensions.
 */

/**
 * Calculate intrinsic size for a text input based on size attribute and font.
 */
static void calc_text_input_size(LayoutContext* lycon, FormControlProp* form, FontProp* font) {
    float pr = lycon->ui_context->pixel_ratio;

    // Width: size attribute * average char width
    // Browser default: ~153px for 20 chars at 16px = ~0.48 em per char
    int size = form->size > 0 ? form->size : FormDefaults::TEXT_SIZE_CHARS;  // default 20 chars
    if (font && font->font_size > 0) {
        form->intrinsic_width = size * font->font_size * 0.48f;
    } else {
        form->intrinsic_width = FormDefaults::TEXT_WIDTH * pr;
    }

    // Height: browser default is 21px (at 16px font)
    // Compute as font_size + extra vertical space
    if (font && font->font_size > 0) {
        form->intrinsic_height = font->font_size + 2 * FormDefaults::TEXT_PADDING_V * pr;
    } else {
        form->intrinsic_height = FormDefaults::TEXT_HEIGHT * pr;
    }
}

/**
 * Calculate intrinsic size for a textarea based on cols/rows and font.
 */
static void calc_textarea_size(LayoutContext* lycon, FormControlProp* form, FontProp* font) {
    float pr = lycon->ui_context->pixel_ratio;

    int cols = form->cols > 0 ? form->cols : FormDefaults::TEXTAREA_COLS;
    int rows = form->rows > 0 ? form->rows : FormDefaults::TEXTAREA_ROWS;

    if (font && font->font_size > 0) {
        // Width: cols * average char width
        form->intrinsic_width = cols * font->font_size * 0.55f + 2 * FormDefaults::TEXTAREA_PADDING * pr;
        // Height: rows * line-height
        form->intrinsic_height = rows * font->font_size * 1.2f + 2 * FormDefaults::TEXTAREA_PADDING * pr;
    } else {
        form->intrinsic_width = FormDefaults::TEXT_WIDTH * pr;
        form->intrinsic_height = FormDefaults::TEXT_HEIGHT * rows * pr;
    }
}

/**
 * Calculate intrinsic size for a button based on content/value.
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
        // Approximate text width
        size_t len = strlen(text);
        form->intrinsic_width = len * font->font_size * 0.55f + 2 * FormDefaults::BUTTON_PADDING_H * pr;
    } else {
        form->intrinsic_width = FormDefaults::BUTTON_MIN_WIDTH * pr;
    }

    // Ensure minimum width
    if (form->intrinsic_width < FormDefaults::BUTTON_MIN_WIDTH * pr) {
        form->intrinsic_width = FormDefaults::BUTTON_MIN_WIDTH * pr;
    }

    // Height based on font
    if (font && font->font_size > 0) {
        form->intrinsic_height = font->font_size + 2 * FormDefaults::BUTTON_PADDING_V * pr;
    } else {
        form->intrinsic_height = FormDefaults::TEXT_HEIGHT * pr;
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
        calc_textarea_size(lycon, form, font);
        break;

    case FORM_CONTROL_BUTTON:
        calc_button_size(lycon, block, form, font);
        break;

    case FORM_CONTROL_SELECT:
        // Select uses default intrinsic size set in resolve_htm_style
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
    // Note: intrinsic sizes are border-box (include default border/padding)
    // CSS width/height follows box-sizing model
    float width = form->intrinsic_width;
    float height = form->intrinsic_height;
    float content_width, content_height;

    if (block->blk && block->blk->given_width >= 0) {
        // CSS specifies width
        if (is_border_box) {
            // border-box: CSS width is total border-box width
            width = block->blk->given_width;
            content_width = width - border_h - padding_h;
        } else {
            // content-box (default): CSS width is content width only
            content_width = block->blk->given_width;
            width = content_width + border_h + padding_h;
        }
    } else {
        // Use intrinsic - treat as border-box
        content_width = width - border_h - padding_h;
    }

    if (block->blk && block->blk->given_height >= 0) {
        // CSS specifies height
        if (is_border_box) {
            // border-box: CSS height is total border-box height
            height = block->blk->given_height;
            content_height = height - border_v - padding_v;
        } else {
            // content-box (default): CSS height is content height only
            content_height = block->blk->given_height;
            height = content_height + border_v + padding_v;
        }
    } else {
        // Use intrinsic - treat as border-box
        content_height = height - border_v - padding_v;
    }

    log_debug("[FORM] layout: intrinsic=%.1fx%.1f, given=%.1fx%.1f, border=%.1f/%.1f, padding=%.1f/%.1f, box_sizing=%s",
              form->intrinsic_width, form->intrinsic_height,
              block->blk ? block->blk->given_width : -1, block->blk ? block->blk->given_height : -1,
              border_h, border_v, padding_h, padding_v, is_border_box ? "border-box" : "content-box");

    // Set final dimensions
    block->width = width;
    block->height = height;
    block->content_width = content_width;
    block->content_height = content_height;

    if (block->content_width < 0) block->content_width = 0;
    if (block->content_height < 0) block->content_height = 0;

    log_debug("[FORM] layout complete: w=%.1f h=%.1f cw=%.1f ch=%.1f",
              block->width, block->height, block->content_width, block->content_height);
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

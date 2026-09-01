#include "layout.hpp"
#include "view.hpp"
#include "rdt_video.h"
#include "event.hpp"
#include "render.hpp"
#include "../lib/str.h"
#include "../lib/strview.h"
#include "../lib/memtrack.h"
#include "../lib/color.h"
#include "../lib/tagged.hpp"
#include "../lib/utf.h"
#include <cstdlib>  // for strtol
#include <new>      // for placement new


static void media_state_changed(RdtVideo* video, RdtVideoState state, void* userdata) {
    (void)video;
    DocState* doc_state = (DocState*)userdata;
    if (doc_state && state == RDT_VIDEO_STATE_PLAYING) {
        doc_state->has_active_video = true;
    }
    radiant_video_notify_frame_ready(doc_state);
}

static void media_frame_ready(RdtVideo* video, void* userdata) {
    (void)video;
    radiant_video_notify_frame_ready((DocState*)userdata);
}

static void media_duration_known(RdtVideo* video, double seconds, void* userdata) {
    (void)video;
    (void)seconds;
    radiant_video_notify_frame_ready((DocState*)userdata);
}

static void media_video_size_known(RdtVideo* video, int width, int height, void* userdata) {
    (void)video;
    (void)width;
    (void)height;
    radiant_video_notify_frame_ready((DocState*)userdata);
}

static RdtVideoCallbacks media_callbacks = {
    media_state_changed,
    media_frame_ready,
    media_duration_known,
    media_video_size_known
};

static FormControlProp* ensure_form_control_prop(LayoutContext* lycon, ViewBlock* block,
                                                 FormControlType control_type,
                                                 bool* out_created) {
    if (out_created) *out_created = false;
    if (!lycon || !block) return nullptr;
    if (block->form_control()) {
        return block->form;
    }

    FormControlProp* form = block->ensure_form(lycon->doc->view_tree);
    form_control_prop_init(form);
    form->control_type = control_type;

    // F8/ES19: a control just became live, so the document owes an init phase.
    // The gate is doc-scoped and the per-control ViewState bit decides who
    // actually runs, so re-arming on a prop the view pool re-created costs one
    // walk that finds nothing to do.
    if (lycon->doc) lycon->doc->behavior_init_pending = true;
    if (out_created) *out_created = true;
    return form;
}

static void apply_html_form_state_attributes(LayoutContext* lycon, ViewBlock* block,
                                             bool read_only, bool checked, bool required) {
    if (!lycon || !block || !block->form || !lycon->doc) return;
    DocState* state = (DocState*)lycon->doc->state;
    if (block->has_attribute("disabled")) form_control_set_disabled(state, block, true);
    if (read_only && block->has_attribute("readonly")) form_control_set_readonly(state, block, true);
    if (checked && block->has_attribute("checked")) form_control_set_checked(state, block, true);
    if (required && block->has_attribute("required")) form_control_set_required(state, block, true);
}

static Color parse_html_color(const char* color_str) {
    Color result;
    result.r = 0; result.g = 0; result.b = 0; result.a = 255;
    if (!color_str || !*color_str) return result;

    uint8_t r, g, b, a;
    if (color_parse_hex(color_str, &r, &g, &b, &a)) {
        result.r = r; result.g = g; result.b = b; result.a = a;
    }
    return result;
}

struct HtmlDimensionAttr {
    float value;
    bool is_percent;
};

static bool parse_html_dimension_attr(const char* attr, bool allow_percent,
                                      bool require_digit_start,
                                      HtmlDimensionAttr* out_dim) {
    if (!attr || !out_dim) return false;
    size_t attr_len = strlen(attr);
    if (attr_len == 0) return false;

    out_dim->value = -1.0f;
    out_dim->is_percent = false;
    if (allow_percent && attr[attr_len - 1] == '%') {
        StrView view = strview_init(attr, attr_len - 1);
        float percent = (float)strview_to_int(&view);
        if (percent < 0.0f) return false;
        out_dim->value = percent;
        out_dim->is_percent = true;
        return true;
    }

    if (require_digit_start && (attr[0] < '0' || attr[0] > '9')) return false;
    StrView view = strview_init(attr, attr_len);
    float value = (float)strview_to_int(&view);
    if (value < 0.0f) return false;
    out_dim->value = value;
    return true;
}

static void apply_html_background_color(LayoutContext* lycon, ViewBlock* block, Color color) {
    // HTML bgcolor hints and form UA defaults share one allocation invariant.
    layout_ensure_background(lycon, block)->color = color;
}

static void apply_html_font_weight(FontProp* font, CssEnum weight, int numeric) {
    if (!font) return;
    font->font_weight = weight;
    font->font_weight_numeric = numeric;
}

static void apply_html_font_size(FontProp* font, float size, bool from_medium) {
    font->font_size = size;
    font->font_size_from_medium = from_medium;
}

static CssEnum html_align_value(const char* align) {
    if (!align) return CSS_VALUE_UNSET;
    size_t length = strlen(align);
    if (str_ieq_const(align, length, "left")) return CSS_VALUE_LEFT;
    if (str_ieq_const(align, length, "right")) return CSS_VALUE_RIGHT;
    if (str_ieq_const(align, length, "center")) return CSS_VALUE_CENTER;
    if (str_ieq_const(align, length, "justify")) return CSS_VALUE_JUSTIFY;
    return CSS_VALUE_UNSET;
}

static void apply_html_align_attribute(LayoutContext* lycon, DomNode* element,
                                       ViewBlock* block, bool legacy_block_alignment) {
    CssEnum value = html_align_value(element ? element->get_attribute("align") : nullptr);
    if (value == CSS_VALUE_UNSET) return;
    BlockProp* prop = block->ensure_block(lycon);
    prop->text_align = value;
    if (legacy_block_alignment && value != CSS_VALUE_JUSTIFY) {
        prop->legacy_block_align = value;
        prop->legacy_align_center_blocks = value == CSS_VALUE_CENTER;
    }
}

static void apply_html_text_align_attribute(LayoutContext* lycon, DomNode* element,
                                            ViewBlock* block) {
    apply_html_align_attribute(lycon, element, block, false);
}

static void apply_html_form_control_font(LayoutContext* lycon, ViewBlock* block) {
    // Chrome UA form controls share this Arial medium-derived override.
    FontProp* font = block->ensure_font(lycon);
    // Keep the UA font at its computed size; FontProp::used_zoom applies CSS
    // Viewport zoom once when the native control is measured and painted.
    apply_html_font_size(font, 13.3333f, false);
    radiant_retain_font_family(font, lam::GcPtr<char>((char*)"Arial"));
}

static void apply_html_textarea_font(LayoutContext* lycon, ViewBlock* block) {
    // Textarea keeps its own monospace/normal-weight UA font policy.
    FontProp* font = block->ensure_font(lycon);
    radiant_retain_font_family(font, lam::GcPtr<char>((char*)"monospace"));
    apply_html_font_size(font, 13.333333f, true);
    font->font_style = CSS_VALUE_NORMAL;
    apply_html_font_weight(font, CSS_VALUE_NORMAL, 400);
}

static BorderProp* apply_html_uniform_border(LayoutContext* lycon, ViewBlock* block,
                                             float width, CssEnum style,
                                             const Color* color,
                                             bool set_specificity = true) {
    BorderProp* border = layout_ensure_border(lycon, block);
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        radiant_border_side_set(radiant_border_side(border, (CssBoxSide)side),
                                width, style, color, set_specificity);
    }
    return border;
}

static BorderProp* apply_html_inset_border_colors(LayoutContext* lycon,
                                                 ViewBlock* block, float width) {
    BorderProp* border = apply_html_uniform_border(
        lycon, block, width, CSS_VALUE_INSET, nullptr, true);
    Color dark = {}; dark.r = dark.g = dark.b = 128; dark.a = 255;
    Color light = {}; light.r = light.g = light.b = 192; light.a = 255;
    Color colors[4] = {dark, light, light, dark};
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        RadiantBorderSide refs = radiant_border_side(border, (CssBoxSide)side);
        *refs.color = colors[side];
    }
    return border;
}

static void apply_html_button_box_defaults(LayoutContext* lycon, ViewBlock* block) {
    block->ensure_boundary(lycon);
    // Native button padding and border are UA lengths, so they must follow the
    // same effective zoom as the control's authored used dimensions.
    float zoom = layout_effective_zoom((View*)block);
    radiant_spacing_set_pair(&block->boundary_mut()->padding,
        CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, FormDefaults::BUTTON_PADDING_V * zoom);
    radiant_spacing_set_pair(&block->boundary_mut()->padding,
        CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, FormDefaults::BUTTON_PADDING_H * zoom);
    apply_html_uniform_border(
        lycon, block, FormDefaults::BUTTON_BORDER * zoom, CSS_VALUE_OUTSET, nullptr);
}

static void apply_html_fixed_replaced_size(LayoutContext* lycon, ViewBlock* block,
                                           float width, float height) {
    block->display.outer = CSS_VALUE_INLINE_BLOCK;
    block->display.inner = RDT_DISPLAY_REPLACED;
    block->ensure_block(lycon);
    block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
    block->blk->given_width = lycon->block.given_width = width;
    block->blk->given_height = lycon->block.given_height = height;
}

void layout_refresh_html_em_replaced_size(LayoutContext* lycon, DomElement* element) {
    if (!lycon || !element) return;
    if (element->tag() != MARKUP_NAME_METER &&
        element->tag() != MARKUP_NAME_PROGRESS) return;

    ViewBlock* block = lam::unsafe_view_block_api_span(
        lam::view_require_element(static_cast<View*>(element)));
    if (!block || !block->blk) return;

    bool has_width = layout_specified_physical_size_declaration(element, true) != nullptr;
    bool has_height = layout_specified_physical_size_declaration(element, false) != nullptr;
    if (!has_width) {
        float em = element->tag() == MARKUP_NAME_METER
            ? FormDefaults::METER_INLINE_SIZE_EM
            : FormDefaults::PROGRESS_INLINE_SIZE_EM;
        block->blk->given_width = lycon->block.given_width =
            form_control_em_size(lycon, block, em);
    }
    if (!has_height) {
        block->blk->given_height = lycon->block.given_height =
            form_control_em_size(lycon, block, FormDefaults::FORM_WIDGET_BLOCK_SIZE_EM);
    }
}

static void apply_html_context_default_size(LayoutContext* lycon, float width, float height) {
    lycon->block.given_width = width;
    lycon->block.given_height = height;
}

static void apply_html_body_margin_attribute(LayoutContext* lycon, ViewBlock* block,
                                             const char* attribute, CssBoxSide first,
                                             CssBoxSide second, bool pair) {
    const char* raw = block->as_element()->get_attribute(attribute);
    if (!raw || raw[0] < '0' || raw[0] > '9') return;
    StrView view = strview_init(raw, strlen(raw));
    float value = strview_to_int(&view);
    if (value < 0.0f) return;
    BoundaryProp* boundary = block->boundary_mut();
    if (pair) {
        radiant_spacing_set_pair(&boundary->margin, first, second, value);
    } else {
        *radiant_spacing_value(&boundary->margin, first) = value;
        *radiant_spacing_specificity(&boundary->margin, first) = -1;
    }
}

static constexpr float HTML_BODY_DEFAULT_MARGIN = 8.0f;

void layout_refresh_html_body_ua_margin(LayoutContext* lycon, DomElement* dom_elem) {
    if (!lycon || !dom_elem || dom_elem->tag() != MARKUP_NAME_BODY || !dom_elem->bound) {
        return;
    }
    ViewBlock* block = lam::unsafe_view_block_api_span(
        lam::view_require_element(static_cast<View*>(dom_elem)));
    float body_margin = HTML_BODY_DEFAULT_MARGIN *
        layout_effective_zoom(static_cast<View*>(block));
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        CssBoxSide box_side = (CssBoxSide)side;
        if (*radiant_spacing_specificity(&block->boundary_mut()->margin, box_side) < 0) {
            *radiant_spacing_value(&block->boundary_mut()->margin, box_side) = body_margin;
        }
    }
}

static float html_pixel_dimension_or_default(DomNode* element, const char* attribute,
                                             float default_value, bool require_digit_start) {
    const char* value = element->get_attribute(attribute);
    HtmlDimensionAttr dimension;
    if (parse_html_dimension_attr(value, false, require_digit_start, &dimension)) {
        return dimension.value;
    }
    return default_value;
}

// Replaced elements differ only in their attribute policy; the axis write and
// percentage bookkeeping must remain one path so later intrinsic sizing sees
// the same hints for every HTML replaced element.
struct HtmlDimensionPolicy {
    float default_width;
    float default_height;
    float width_percent_base;
    float height_percent_base;
    bool allow_percent;
    bool require_digit_start;
    bool default_when_missing;
    bool default_when_invalid;
    bool positive_percent_only;
    bool persist;
    bool positive_value = false;
    bool percent_at_most_100 = false;
    bool clear_unresolved_percent = true;
    bool store_resolved_percent = false;
};

static void apply_html_axis_value(LayoutContext* lycon, ViewBlock* block,
                                  LayoutAxis axis, float value, bool is_percent,
                                  float percent_base, bool clear_unresolved_percent,
                                  bool store_resolved_percent) {
    float resolved = is_percent
        ? (percent_base > 0.0f ? percent_base * value / 100.0f : -1.0f)
        : value;
    if (block) {
        BlockProp* prop = block->ensure_block(lycon);
        LayoutAxisRefs refs(prop, axis == LAYOUT_AXIS_X);
        // table percentage hints can be unresolved during intrinsic passes; clearing
        // either provisional width here changes a 50% table into an auto-sized table.
        if (!is_percent || resolved >= 0.0f || clear_unresolved_percent) {
            *refs.given = is_percent
                ? (store_resolved_percent ? resolved : -1.0f) : value;
        }
        *refs.given_percent = is_percent ? value : NAN;
    }
    LayoutAxisRefs context(&lycon->block, axis);
    if (!is_percent || resolved >= 0.0f || clear_unresolved_percent) {
        *context.given = resolved;
    }
}

static void apply_html_dimension_default(LayoutContext* lycon, ViewBlock* block,
                                         LayoutAxis axis, float value,
                                         const HtmlDimensionPolicy* policy) {
    apply_html_axis_value(lycon, policy->persist ? block : nullptr, axis, value,
                          false, 0.0f, policy->clear_unresolved_percent,
                          policy->store_resolved_percent);
}

static void apply_html_dimension_attribute(LayoutContext* lycon, DomNode* element,
                                           ViewBlock* block, LayoutAxis axis,
                                           const char* attribute, float default_value,
                                           const HtmlDimensionPolicy* policy) {
    const char* attr = element ? element->get_attribute(attribute) : nullptr;
    if (!attr) {
        if (policy->default_when_missing) {
            apply_html_dimension_default(lycon, block, axis, default_value, policy);
        }
        return;
    }

    HtmlDimensionAttr dimension;
    if (!parse_html_dimension_attr(attr, policy->allow_percent,
                                   policy->require_digit_start, &dimension)) {
        if (policy->default_when_invalid) {
            apply_html_dimension_default(lycon, block, axis, default_value, policy);
        }
        return;
    }

    if ((policy->positive_value && dimension.value <= 0.0f) ||
        (policy->percent_at_most_100 && dimension.is_percent && dimension.value > 100.0f)) {
        return;
    }

    if (dimension.is_percent) {
        if (policy->positive_percent_only && dimension.value <= 0.0f) return;
        float base = axis == LAYOUT_AXIS_X
            ? policy->width_percent_base : policy->height_percent_base;
        apply_html_axis_value(lycon, policy->persist ? block : nullptr,
                              axis, dimension.value, true, base,
                              policy->clear_unresolved_percent,
                              policy->store_resolved_percent);
    } else {
        apply_html_axis_value(lycon, policy->persist ? block : nullptr,
                              axis, dimension.value, false, 0.0f,
                              policy->clear_unresolved_percent,
                              policy->store_resolved_percent);
    }
}

static void apply_html_dimension_attributes(LayoutContext* lycon, DomNode* element,
                                            ViewBlock* block,
                                            const HtmlDimensionPolicy* policy) {
    apply_html_dimension_attribute(lycon, element, block, LAYOUT_AXIS_X, "width",
                                   policy->default_width, policy);
    apply_html_dimension_attribute(lycon, element, block, LAYOUT_AXIS_Y, "height",
                                   policy->default_height, policy);
}

static void apply_html_table_dimension_attribute(LayoutContext* lycon, DomNode* element,
                                                 ViewBlock* block, LayoutAxis axis) {
    HtmlDimensionPolicy policy = {
        0.0f, 0.0f,
        lycon->block.content_width > 0.0f
            ? lycon->block.content_width : lycon->line.right - lycon->line.left,
        0.0f, axis == LAYOUT_AXIS_X, false, false, false, false, true
    };
    policy.positive_value = true;
    policy.percent_at_most_100 = true;
    policy.clear_unresolved_percent = false;
    policy.store_resolved_percent = true;
    apply_html_dimension_attribute(lycon, element, block, axis,
        axis == LAYOUT_AXIS_X ? "width" : "height", 0.0f, &policy);
}

bool layout_canvas_natural_size(ViewBlock* block, float* out_width, float* out_height) {
    if (!block || block->tag() != MARKUP_NAME_CANVAS || !out_width || !out_height) {
        return false;
    }

    DomElement* element = block->as_element();
    if (!element) return false;

    // Canvas attributes define its bitmap coordinate space, which remains its
    // natural size when CSS leaves the corresponding box size automatic.
    *out_width = html_pixel_dimension_or_default(element, "width", 300.0f, true);
    *out_height = html_pixel_dimension_or_default(element, "height", 150.0f, true);
    return true;
}

static void apply_html_replaced_default(LayoutContext* lycon, DomNode* element,
                                        ViewBlock* block, bool require_digit_start,
                                        bool persist_in_block, bool context_default,
                                        bool default_invalid, bool ensure_block,
                                        bool default_when_missing = true) {
    block->display.inner = RDT_DISPLAY_REPLACED;
    if (context_default) apply_html_context_default_size(lycon, 300.0f, 150.0f);
    if (ensure_block) block->ensure_block(lycon);
    HtmlDimensionPolicy policy = {
        300.0f, 150.0f, 0.0f, 0.0f, false, require_digit_start,
        !context_default && default_when_missing, default_invalid, false, persist_in_block
    };
    apply_html_dimension_attributes(lycon, element, block, &policy);
}

static const char* html_media_next_source(DomElement* element, DomNode** cursor) {
    if (!element || !cursor) return nullptr;
    if (!*cursor) {
        *cursor = element;
        const char* direct = element->get_attribute("src");
        if (direct && *direct) return direct;
    }
    DomNode* child = *cursor == element ? element->first_child : (*cursor)->next_sibling;
    while (child) {
        *cursor = child;
        if (child->is_element() && child->as_element()->tag() == MARKUP_NAME_SOURCE) {
            const char* src = child->as_element()->get_attribute("src");
            if (src && *src) return src;
        }
        child = child->next_sibling;
    }
    return nullptr;
}

static void initialize_html_media(LayoutContext* lycon, DomNode* element,
                                  ViewBlock* block, bool is_video) {
    DomElement* media_element = element && element->is_element()
        ? element->as_element() : nullptr;
    DomDocument* doc = lycon->ui_context ? lycon->ui_context->document : nullptr;
    if (!doc || !doc->url) return;

    if (!block->embed) {
        block->ensure_embed(lycon);
    }
    if (block->embedp()->video) return;

    const char* preload = is_video ? element->get_attribute("preload") : nullptr;
    bool preload_none = preload && strcmp(preload, "none") == 0;
    RdtVideo* media = nullptr;
    char* selected_file_path = nullptr;
    DomNode* source_cursor = nullptr;
    const char* src = nullptr;
    while ((src = html_media_next_source(media_element, &source_cursor))) {
        Url* abs_url = parse_url(doc->url, src);
        char* file_path = abs_url ? url_to_local_path(abs_url) : nullptr;
        if (abs_url) url_destroy(abs_url);
        if (!file_path) continue;

        RdtVideo* candidate = rdt_video_create(&media_callbacks, doc->state);
        if (!candidate) {
            mem_free(file_path);
            continue;
        }
        if (!preload_none && rdt_video_open_file(candidate, file_path) != 0) {
            rdt_video_destroy(candidate);
            mem_free(file_path);
            continue;
        }
        media = candidate;
        selected_file_path = file_path;
        break;
    }
    if (!media) return;

    {
        if (element->has_attribute("loop")) rdt_video_set_loop(media, true);
        if (element->has_attribute("muted")) rdt_video_set_muted(media, true);
        block->embed->video = media;

        if (is_video) {
            block->embed->has_controls = element->has_attribute("controls");
            const char* poster_src = element->get_attribute("poster");
            if (poster_src && *poster_src) {
                block->embed->poster = load_image(lycon->ui_context, poster_src);
            }
        }

        if (element->has_attribute("autoplay")) {
            if (preload_none) rdt_video_open_file(media, selected_file_path);
            rdt_video_play(media);
            if (doc->state) doc->state->has_active_video = true;
        }
    }
    mem_free(selected_file_path);
}

static DomElement* parent_table_element(DomNode* element) {
    for (DomNode* node = element ? element->parent : nullptr; node; node = node->parent) {
        if (node->is_element() && node->as_element()->tag_id == MARKUP_NAME_TABLE) {
            return node->as_element();
        }
    }
    return nullptr;
}

// table presentational hints all resolve from the nearest table ancestor.
static float get_parent_table_number(DomNode* element, const char* attribute_name) {
    DomElement* table = parent_table_element(element);
    const char* attr = table && attribute_name
        ? table->get_attribute(attribute_name) : nullptr;
    if (!attr) return -1.0f;
    StrView view = strview_init(attr, strlen(attr));
    float value = strview_to_int(&view);
    return value >= 0.0f ? value : -1.0f;
}

static float parse_html_table_border_width(const char* attribute, bool is_present) {
    if (!is_present) return -1.0f;
    if (!attribute) return 1.0f;
    const char* cursor = str_skip_ascii_space(attribute);
    if (*cursor < '0' || *cursor > '9') {
        // HTML's boolean border attribute defaults to one CSS pixel; only an
        // explicit numeric zero suppresses the legacy table and cell borders.
        return 1.0f;
    }
    StrView view = strview_init(cursor, strlen(cursor));
    float width = strview_to_int(&view);
    return width >= 0.0f ? width : 1.0f;
}

static float get_parent_table_border_width(DomNode* element) {
    DomElement* table = parent_table_element(element);
    return parse_html_table_border_width(table ? table->get_attribute("border") : nullptr,
                                         table && table->has_attribute("border"));
}

static float html_font_size_for_level(int level) {
    if (level < 1) level = 1;
    if (level > 7) level = 7;
    static const float sizes[] = {10.0f, 13.0f, 16.0f, 18.0f, 24.0f, 32.0f, 48.0f};
    return sizes[level - 1];
}

static bool parse_legacy_font_size_level(const char* input, int* out_level) {
    const char* cursor = str_skip_ascii_space(input);
    bool relative_plus = false;
    bool relative_minus = false;
    if (*cursor == '+' || *cursor == '-') {
        relative_plus = *cursor == '+';
        relative_minus = *cursor == '-';
        cursor++;
    }
    if (*cursor < '0' || *cursor > '9') return false;

    int value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        // only the 1..7 range matters; saturating also avoids integer overflow
        value = value > 7 ? 8 : value * 10 + (*cursor - '0');
        cursor++;
    }
    if (relative_plus) value += 3;
    if (relative_minus) value = 3 - value;
    if (value < 1) value = 1;
    if (value > 7) value = 7;
    *out_level = value;
    return true;
}

static void apply_table_cell_dimension_attribute(DomElement* elmt, ViewBlock* block,
                                                 LayoutAxis axis) {
    if (!elmt || !block) return;
    const char* attribute = axis == LAYOUT_AXIS_X ? "width" : "height";
    const char* raw = elmt->get_attribute(attribute);
    if (!raw || !block->blk) return;

    HtmlDimensionAttr dimension;
    if (!parse_html_dimension_attr(raw, axis == LAYOUT_AXIS_X, true, &dimension)) return;
    LayoutAxisRefs refs(block->blk, axis == LAYOUT_AXIS_X);
    if (dimension.is_percent) {
        *refs.given = -1.0f;
        *refs.given_percent = dimension.value;
    } else {
        *refs.given = dimension.value;
        *refs.given_percent = NAN;
    }
}

// Get the rules attribute from the parent TABLE element.
// HTML rules presentational hints affect table border conflict resolution and
// per-cell borders (e.g. rules="cols" creates vertical cell rules).
static const char* get_parent_table_rules(DomNode* elmt) {
    DomElement* table = parent_table_element(elmt);
    return table ? table->get_attribute("rules") : nullptr;
}

static void apply_html_table_rules_cell_border(LayoutContext* lycon, ViewBlock* block,
                                               const char* rules_attr, bool is_header) {
    if (!rules_attr) return;

    size_t rules_len = strlen(rules_attr);
    bool rules_cols = str_ieq_const(rules_attr, rules_len, "cols");
    bool rules_rows = str_ieq_const(rules_attr, rules_len, "rows");
    bool rules_all = str_ieq_const(rules_attr, rules_len, "all");
    if (!rules_cols && !rules_rows && !rules_all) return;

    BorderProp* border = layout_ensure_border(lycon, block);
    Color grey = {0};
    grey.r = 128;
    grey.g = 128;
    grey.b = 128;
    grey.a = 255;
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        bool is_column_rule = side == CSS_BOX_SIDE_LEFT || side == CSS_BOX_SIDE_RIGHT;
        bool is_row_rule = side == CSS_BOX_SIDE_TOP || side == CSS_BOX_SIDE_BOTTOM;
        if (!(rules_all || (is_column_rule && rules_cols) || (is_row_rule && rules_rows))) continue;
        radiant_border_side_set(radiant_border_side(border, (CssBoxSide)side),
                                1.0f, CSS_VALUE_SOLID, &grey, true);
    }
}

static void apply_html_table_rules_group_border(LayoutContext* lycon, ViewBlock* block,
                                                const char* rules_attr) {
    if (!rules_attr || !block) return;
    size_t rules_len = strlen(rules_attr);
    if (!str_ieq_const(rules_attr, rules_len, "groups") &&
        !str_ieq_const(rules_attr, rules_len, "all")) return;
    // HTML rules=groups supplies the inter-row-group border; author CSS may
    // override its width or color through the normal cascade.
    BorderProp* border = layout_ensure_border(lycon, block);
    RadiantBorderSide refs = radiant_border_side(border, CSS_BOX_SIDE_BOTTOM);
    *refs.width = 1.0f;
    *refs.width_specificity = -1;
    *refs.style = CSS_VALUE_SOLID;
    *refs.color = (Color){ .r=128, .g=128, .b=128, .a=255 };
}

// get parent TR's valign attribute (for TD/TH cells)
static const char* get_parent_tr_valign(DomNode* elmt) {
    // TD/TH -> TR, check TR's valign attribute
    DomNode* node = elmt->parent;
    if (node && node->is_element()) {
        DomElement* elem = node->as_element();
        if (elem->tag_id == MARKUP_NAME_TR) {
            return elem->get_attribute("valign");
        }
    }
    return nullptr;
}

static void apply_html_table_cell_defaults(LayoutContext* lycon, DomNode* cell_node,
                                           ViewBlock* block, bool is_header) {
    DomElement* cell = cell_node->as_element();
    if (is_header) {
        apply_html_font_weight(block->ensure_font(lycon), CSS_VALUE_BOLD, 700);
    }

    BlockProp* block_prop = block->ensure_block(lycon);
    bool cell_is_rtl = false;
    for (DomElement* current = cell; current;) {
        CssEnum specified_direction = layout_specified_keyword(
            current, CSS_PROPERTY_DIRECTION, CSS_VALUE__UNDEF);
        const char* dir = current->get_attribute("dir");
        if (specified_direction == CSS_VALUE_RTL ||
            (dir && str_ieq_const(dir, strlen(dir), "rtl"))) {
            cell_is_rtl = true;
            break;
        }
        if (specified_direction == CSS_VALUE_LTR ||
            (dir && str_ieq_const(dir, strlen(dir), "ltr"))) {
            break;
        }
        DomNode* parent = current->parent;
        current = parent && parent->is_element() ? parent->as_element() : nullptr;
    }
    // CSS Text initial 'start' alignment follows the cell's direction; retain
    // the established LTR HTML geometry while correcting RTL table cells.
    block_prop->text_align = is_header ? CSS_VALUE_CENTER :
        (cell_is_rtl ? CSS_VALUE_START : CSS_VALUE_LEFT);
    apply_table_cell_dimension_attribute(cell, block, LAYOUT_AXIS_X);
    apply_table_cell_dimension_attribute(cell, block, LAYOUT_AXIS_Y);

    block->ensure_inline(lycon);
    block->in_line->vertical_align = CSS_VALUE_MIDDLE;

    float cellpadding = get_parent_table_number(cell_node, "cellpadding");
    float padding = cellpadding >= 0.0f ? cellpadding : 1.0f;
    block->ensure_boundary(lycon);
    radiant_spacing_set_all(&block->boundary_mut()->padding, padding);

    const char* align_attr = cell->get_attribute("align");
    if (align_attr) {
        size_t align_len = strlen(align_attr);
        if (str_ieq_const(align_attr, align_len, "left")) {
            block_prop->text_align = CSS_VALUE_LEFT;
            block_prop->legacy_block_align = CSS_VALUE_LEFT;
        } else if (str_ieq_const(align_attr, align_len, "right")) {
            block_prop->text_align = CSS_VALUE_RIGHT;
            block_prop->legacy_block_align = CSS_VALUE_RIGHT;
        } else if (str_ieq_const(align_attr, align_len, "center")) {
            block_prop->text_align = CSS_VALUE_CENTER;
            block_prop->legacy_align_center_blocks = true;
            block_prop->legacy_block_align = CSS_VALUE_CENTER;
        }
    }

    const char* valign_attr = cell->get_attribute("valign");
    if (!valign_attr) valign_attr = get_parent_tr_valign(cell_node);
    if (valign_attr) {
        size_t valign_len = strlen(valign_attr);
        if (str_ieq_const(valign_attr, valign_len, "top")) {
            block->in_line->vertical_align = CSS_VALUE_TOP;
        } else if (str_ieq_const(valign_attr, valign_len, "middle")) {
            block->in_line->vertical_align = CSS_VALUE_MIDDLE;
        } else if (str_ieq_const(valign_attr, valign_len, "bottom")) {
            block->in_line->vertical_align = CSS_VALUE_BOTTOM;
        }
    }

    if (cell->get_attribute("nowrap")) {
        block_prop->white_space = CSS_VALUE_NOWRAP;
    }

    const char* bgcolor_attr = cell->get_attribute("bgcolor");
    if (bgcolor_attr) {
        Color bg_color = parse_html_color(bgcolor_attr);
        apply_html_background_color(lycon, block, bg_color);
    }

    if (get_parent_table_border_width(cell_node) > 0.0f) {
        Color grey = (Color){ .r=128, .g=128, .b=128, .a=255 };
        apply_html_uniform_border(lycon, block, 1.0f, CSS_VALUE_INSET, &grey);
    }
    apply_html_table_rules_cell_border(
        lycon, block, get_parent_table_rules(cell_node), is_header);
}

// Resolve dir="auto" by finding the first strong directional character.
// Returns CSS_VALUE_RTL or CSS_VALUE_LTR.
static CssEnum resolve_dir_auto(DomElement* elmt) {
    for (DomNode* child = elmt->first_child; child; child = child->next_sibling) {
        int result = layout_find_first_strong_direction(child, true);
        if (result > 0) return CSS_VALUE_RTL;
        if (result < 0) return CSS_VALUE_LTR;
    }
    return CSS_VALUE_LTR;  // default to LTR if no strong character found
}

static void apply_html_heading_default(LayoutContext* lycon, DomNode* element,
                                       ViewBlock* block, NameId tag) {
    // HTML heading defaults are a data table: the same font/margin pipeline
    // applies to every level, so only the level-specific scales stay indexed.
    static const float font_scales[] = {2.0f, 1.5f, 1.17f, 1.0f, 0.83f, 0.67f};
    static const float margin_scales[] = {0.67f, 0.83f, 1.0f, 1.33f, 1.67f, 2.33f};
    size_t level = (size_t)(tag - MARKUP_NAME_H1);
    if (level >= sizeof(font_scales) / sizeof(font_scales[0])) return;

    FontProp* font = block->ensure_font(lycon);
    float heading_size = lycon->font.style->font_size * font_scales[level];
    apply_html_font_size(font, heading_size, false);
    apply_html_font_weight(font, CSS_VALUE_BOLD, 700);
    block->ensure_boundary(lycon);
    radiant_spacing_set_pair(&block->boundary_mut()->margin,
        CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, heading_size * margin_scales[level]);
    apply_html_text_align_attribute(lycon, element, block);
}

static bool apply_html_inline_text_default(LayoutContext* lycon, ViewSpan* span, NameId tag) {
    CssEnum style = CSS_VALUE__UNDEF;
    CssEnum weight = CSS_VALUE__UNDEF;
    CssEnum decoration = CSS_VALUE__UNDEF;
    CssEnum vertical_align = CSS_VALUE__UNDEF;
    float size_scale = 1.0f;
    switch (tag) {
        case MARKUP_NAME_B: case MARKUP_NAME_STRONG: weight = CSS_VALUE_BOLD; break;
        case MARKUP_NAME_I: case MARKUP_NAME_EM: case MARKUP_NAME_CITE:
        case MARKUP_NAME_DFN: case MARKUP_NAME_VAR: case MARKUP_NAME_Q:
            style = CSS_VALUE_ITALIC; break;
        case MARKUP_NAME_U: case MARKUP_NAME_INS: case MARKUP_NAME_ABBR:
        case MARKUP_NAME_ACRONYM: decoration = CSS_VALUE_UNDERLINE; break;
        case MARKUP_NAME_S: case MARKUP_NAME_DEL: case MARKUP_NAME_STRIKE:
            decoration = CSS_VALUE_LINE_THROUGH; break;
        case MARKUP_NAME_SMALL: size_scale = 0.83f; break;
        case MARKUP_NAME_BIG: size_scale = 1.17f; break;
        case MARKUP_NAME_RT:
            // css ruby: an HTML rt element generates a ruby-text box.
            span->display = {CSS_VALUE_INLINE, CSS_VALUE_RUBY_TEXT};
            size_scale = 0.5f;
            break;
        case MARKUP_NAME_SUB: size_scale = 0.83f; vertical_align = CSS_VALUE_SUB; break;
        case MARKUP_NAME_SUP: size_scale = 0.83f; vertical_align = CSS_VALUE_SUPER; break;
        case MARKUP_NAME_RUBY:
            // Ruby's inner formatting context is a display default, not a font default.
            span->display = {CSS_VALUE_INLINE, CSS_VALUE_RUBY};
            return true;
        default: return false;
    }
    FontProp* font = span->ensure_font(lycon);
    if (tag == MARKUP_NAME_RT) {
        // CSS Text Decoration 4 §3.1: the UA disables emphasis inheritance on
        // ruby text so marks remain attached to the ruby base.
        font->text_emphasis_enabled = false;
    }
    if (style != CSS_VALUE__UNDEF) font->font_style = style;
    if (weight == CSS_VALUE_BOLD) apply_html_font_weight(font, CSS_VALUE_BOLD, 700);
    if (decoration != CSS_VALUE__UNDEF) font->text_deco = decoration;
    if (size_scale != 1.0f) {
        apply_html_font_size(font, lycon->font.style->font_size * size_scale, false);
    }
    if (vertical_align != CSS_VALUE__UNDEF) {
        span->ensure_inline(lycon)->vertical_align = vertical_align;
    }
    return true;
}

static void apply_html_q_quote_defaults(LayoutContext* lycon, DomElement* element) {
    if (!lycon || !element || element->tag() != MARKUP_NAME_Q || !element->doc) return;
    Pool* pool = element->doc->document_pool;
    const PseudoStyleKind kinds[] = {PSEUDO_STYLE_BEFORE, PSEUDO_STYLE_AFTER};
    const char* quote_names[] = {"open-quote", "close-quote"};
    for (int i = 0; i < 2; i++) {
        if (dom_element_get_pseudo_element_value(
                element, CSS_PROPERTY_CONTENT, i + 1)) continue;
        StyleTree** style_slot = element->pseudo_style_slot(kinds[i]);
        if (!style_slot) continue;
        if (!*style_slot) *style_slot = style_tree_create(pool);
        if (!*style_slot) continue;
        CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
        if (!value) continue;
        value->type = CSS_VALUE_TYPE_CUSTOM;
        value->data.custom_property.name = pool_strdup(pool, quote_names[i]);
        CssSpecificity specificity = {0, 0, 0, 0, false};
        CssDeclaration* declaration = css_declaration_create(
            CSS_PROPERTY_CONTENT, value, specificity, CSS_ORIGIN_USER_AGENT, pool);
        if (declaration) style_tree_apply_declaration(*style_slot, declaration);
    }
}

static void apply_html_centered_popup_style(LayoutContext* lycon, ViewBlock* block,
                                            float padding_em) {
    block->ensure_position(lycon);
    block->position->position = CSS_VALUE_FIXED;
    block->position->top = block->position->right =
        block->position->bottom = block->position->left = 0.0f;
    block->position->has_top = block->position->has_right =
        block->position->has_bottom = block->position->has_left = true;
    block->ensure_boundary(lycon);
    radiant_margin_set_type_all(&block->boundary_mut()->margin, CSS_VALUE_AUTO);
    radiant_spacing_set_all(&block->boundary_mut()->padding,
                            lycon->font.style->font_size * padding_em);
    apply_html_uniform_border(lycon, block, 3.0f, CSS_VALUE_SOLID, nullptr, true);
    block->ensure_block(lycon);
    block->blk->given_width = block->blk->given_height = -1.0f;
    block->blk->given_width_type = block->blk->given_height_type = CSS_VALUE_FIT_CONTENT;
    block->ensure_scroll(lycon);
    block->scroller->overflow_x = block->scroller->overflow_y = CSS_VALUE_AUTO;
}

void apply_element_default_style(LayoutContext* lycon, DomNode* elmt) {
    ViewSpan* span = lam::view_require_element(static_cast<View*>(elmt));
    // Default-style resolution runs for both block and inline elements,
    // including generated pseudo-elements like ::after with display:inline.
    // Use the shared element storage view so inline elements can still receive
    // boundary/font defaults without asserting on their current view tag.
    ViewBlock* block = lam::unsafe_view_block_api_span(span);
    NameId elmt_name = elmt->tag();
    if (elmt->is_element()) {
        DomElement* dom_elem = elmt->as_element();
        const char* namespace_uri = dom_elem->get_attribute("__lambda_ns_uri");
        if (namespace_uri &&
            strcmp(namespace_uri, "http://www.w3.org/1999/xhtml") != 0) {
            // HTML rendering rules apply only in the XHTML namespace.
            return;
        }
        bool popover_has_display_override = dom_elem->specified_style &&
            style_tree_get_declaration(dom_elem->specified_style, CSS_PROPERTY_DISPLAY);
        if (dom_elem->has_attribute("popover") &&
            (dom_elem->is_popover_open() || popover_has_display_override)) {
            // popover UA styling supplies the fit-content box even when author display keeps it in flow
            apply_html_centered_popup_style(lycon, block, 0.25f);
            if (dom_elem->is_popover_open() && dom_elem->tag_id == MARKUP_NAME_OBJECT &&
                !dom_elem->get_attribute(MARKUP_NAME_DATA)) {
                // an open object fallback keeps the replaced default size while fit-content includes popup chrome
                apply_html_replaced_default(lycon, elmt, block, true, true, true, false, true);
            }
        }
    }
    if (elmt_name == MARKUP_NAME_Q && elmt->is_element()) {
        // HTML §4.5.11: q uses UA open/close quotes, which must participate in layout.
        apply_html_q_quote_defaults(lycon, elmt->as_element());
    }
    if (apply_html_inline_text_default(lycon, span, elmt_name)) return;
    switch (elmt_name) {
    case MARKUP_NAME_BODY: {
        block->ensure_boundary(lycon);
        // the UA margin is a CSS length and must follow the ancestor zoom;
        // leaving this literal unscaled shortens auto-height roots under zoom.
        float body_margin = HTML_BODY_DEFAULT_MARGIN *
            layout_effective_zoom(static_cast<View*>(block));
        radiant_spacing_set_all(&block->boundary_mut()->margin, body_margin);
        // Handle HTML bgcolor attribute (e.g., <body bgcolor="#fff">)
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            apply_html_background_color(lycon, block, bg_color);
        }
        // Legacy HTML body margin attributes are still honored by browsers.
        apply_html_body_margin_attribute(lycon, block, "marginwidth",
            CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, true);
        apply_html_body_margin_attribute(lycon, block, "marginheight",
            CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, true);
        apply_html_body_margin_attribute(lycon, block, "leftmargin",
            CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_LEFT, false);
        apply_html_body_margin_attribute(lycon, block, "topmargin",
            CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_TOP, false);
        // overflow: visible (CSS default - no special overflow handling for body)
        break;
    }
    case MARKUP_NAME_H1:
    case MARKUP_NAME_H2:
    case MARKUP_NAME_H3:
    case MARKUP_NAME_H4:
    case MARKUP_NAME_H5:
    case MARKUP_NAME_H6:
        apply_html_heading_default(lycon, elmt, block, elmt_name);
        break;
    case MARKUP_NAME_P: {
        block->ensure_boundary(lycon);
        // margin: 1em 0;
        radiant_spacing_set_pair(&block->boundary_mut()->margin,
            CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, lycon->font.style->font_size);
        apply_html_text_align_attribute(lycon, elmt, block);
        break;
    }
    case MARKUP_NAME_UL:  case MARKUP_NAME_OL:  case MARKUP_NAME_MENU:
    {
        block->ensure_block(lycon);
        block->ensure_boundary(lycon);
        // margin: 1em 0; padding: 0 0 0 40px;
        // UA stylesheet: nested lists have margin: 0
        // Chrome: :is(ul, ol, dir, menu, dl) ul, :is(ul, ol, dir, menu, dl) ol { margin-block: 0 }
        // Also: ul ul { list-style-type: circle }, ul ul ul { list-style-type: square }
        {
            bool is_nested = false;
            int ul_ancestor_count = 0;
            DomNode* ancestor = elmt->parent;
            while (ancestor) {
                if (ancestor->is_element()) {
                    uintptr_t atag = ancestor->tag();
                    if (atag == MARKUP_NAME_UL || atag == MARKUP_NAME_OL ||
                        atag == MARKUP_NAME_MENU || atag == MARKUP_NAME_DIR ||
                        atag == MARKUP_NAME_DL) {
                        is_nested = true;
                    }
                    if (atag == MARKUP_NAME_UL) {
                        ul_ancestor_count++;
                    }
                }
                ancestor = ancestor->parent;
            }
            if (elmt_name == MARKUP_NAME_UL) {
                // Browser UA: ul=disc, ul ul=circle, ul ul ul (and deeper)=square
                block->blk->list_style_type =
                    (ul_ancestor_count == 0) ? CSS_VALUE_DISC :
                    (ul_ancestor_count == 1) ? CSS_VALUE_CIRCLE :
                                               CSS_VALUE_SQUARE;
            } else {
                block->blk->list_style_type = CSS_VALUE_DECIMAL;
            }
            if (!is_nested) {
                float ua_zoom = layout_effective_zoom((View*)block);
                radiant_spacing_set_pair(&block->boundary_mut()->margin,
                    CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM,
                    lycon->font.style->font_size * ua_zoom);
            }
        }
        CssEnum direction = layout_specified_keyword(
            elmt->as_element(), CSS_PROPERTY_DIRECTION, CSS_VALUE__UNDEF);
        const char* dir_attr = elmt->get_attribute("dir");
        if (dir_attr && str_ieq_const(dir_attr, strlen(dir_attr), "rtl")) {
            // HTML §3.2.6: dir=rtl maps to direction before the UA list
            // padding-inline-start rule is applied.
            direction = CSS_VALUE_RTL;
        } else if (dir_attr && str_ieq_const(dir_attr, strlen(dir_attr), "ltr")) {
            direction = CSS_VALUE_LTR;
        }
        if (direction != CSS_VALUE_RTL) direction = CSS_VALUE_LTR;
        CssBoxSide inline_start = direction == CSS_VALUE_RTL
            ? CSS_BOX_SIDE_RIGHT : CSS_BOX_SIDE_LEFT;
        // CSS Writing Modes: the list UA rule is padding-inline-start; a
        // physical left default would remain in RTL and inflate the list box.
        *radiant_spacing_value(&block->boundary_mut()->padding, inline_start) =
            40.0f * layout_effective_zoom((View*)block);
        *radiant_spacing_specificity(&block->boundary_mut()->padding, inline_start) = -1;
        break;
    }
    case MARKUP_NAME_CENTER:
        block->ensure_block(lycon);
        block->blk->text_align = CSS_VALUE_CENTER;
        break;
    case MARKUP_NAME_DIV: {
        // HTML spec §14.3.3: <div align> maps to text-align
        apply_html_align_attribute(lycon, elmt, block, true);
        break;
    }
    case MARKUP_NAME_IMG: { // get html width and height (before the css styles)
        HtmlDimensionPolicy policy = {
            0.0f, 0.0f,
            lycon->block.content_width > 0.0f ? lycon->block.content_width : 0.0f,
            lycon->block.content_height > 0.0f ? lycon->block.content_height : 0.0f,
            true, true, false, false, false, true
        };
        apply_html_dimension_attributes(lycon, elmt, block, &policy);
        // HTML spec §14.3.3: <img align="left|right"> maps to float: left|right
        {
            const char* align_attr = elmt->get_attribute("align");
            if (align_attr) {
                size_t align_len = strlen(align_attr);
                if (str_ieq_const(align_attr, align_len, "left")) {
                    block->ensure_position(lycon);
                    block->position->float_prop = CSS_VALUE_LEFT;
                } else if (str_ieq_const(align_attr, align_len, "right")) {
                    block->ensure_position(lycon);
                    block->position->float_prop = CSS_VALUE_RIGHT;
                }
            }
        }
        break;
    }
    case MARKUP_NAME_IFRAME: {
        // HTML spec §15.5.14: iframe { border: 2px inset; }
        apply_html_inset_border_colors(lycon, block, 2.0f);
        const char* frameborder_attr = elmt->get_attribute("frameborder");
        if (frameborder_attr) {
            size_t frameborder_len = strlen(frameborder_attr);
            bool no_frameborder = str_ieq_const(frameborder_attr, frameborder_len, "0") ||
                str_ieq_const(frameborder_attr, frameborder_len, "no");
            if (no_frameborder) {
                // Legacy frameborder=0 suppresses the iframe UA border before author CSS overrides.
                BorderProp* border = block->boundary_mut()->border;
                for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                    RadiantBorderSide refs = radiant_border_side(border, (CssBoxSide)side);
                    *refs.width = 0.0f;
                    *refs.style = CSS_VALUE_NONE;
                }
            }
        }
        block->ensure_scroll(lycon);
        block->scroller->overflow_x = CSS_VALUE_AUTO;
        block->scroller->overflow_y = CSS_VALUE_AUTO;
        block->ensure_block(lycon);
        // Parse HTML width/height attributes; default 300x150 per HTML spec.
        HtmlDimensionPolicy policy = {
            300.0f, 150.0f, 0.0f, 0.0f,
            true, false, true, false, true, true
        };
        apply_html_dimension_attributes(lycon, elmt, block, &policy);
        break;
    }
    case MARKUP_NAME_EMBED: {
        apply_html_replaced_default(lycon, elmt, block, true, true, true, false, false);
        break;
    }
    case MARKUP_NAME_WEBVIEW: {
        apply_html_replaced_default(lycon, elmt, block, false, true, false, false, true);
        break;
    }
    case MARKUP_NAME_AUDIO: {
        // HTML §4.8.9: <audio> without controls is not rendered
        // <audio controls> is a replaced inline-block element with intrinsic size 300×54
        // (Chrome default audio player dimensions)
        if (elmt->has_attribute("controls")) {
            block->display.inner = RDT_DISPLAY_REPLACED;
            block->ensure_block(lycon);
            lycon->block.given_width = 300;
            block->blk->given_width = 300;
            lycon->block.given_height = 54;
            block->blk->given_height = 54;
        }
        // audio-only playback uses the same media ownership path as <video>.
        initialize_html_media(lycon, elmt, block, false);
        break;
    }
    case MARKUP_NAME_VIDEO: {
        apply_html_replaced_default(lycon, elmt, block, false, false, false, true, false);
        initialize_html_media(lycon, elmt, block, true);
        break;
    }
    case MARKUP_NAME_CANVAS:
        apply_html_replaced_default(lycon, elmt, block, true, false, false, true, false);
        break;
    case MARKUP_NAME_OBJECT:
        if (elmt->get_attribute("data")) {
            // A loadable object uses the resource's intrinsic size when its
            // HTML dimensions are omitted; the 300x150 size is only fallback.
            apply_html_replaced_default(lycon, elmt, block, true, true, false, true, false, false);
        }
        break;
    case MARKUP_NAME_HR: {
        // hr default: 1px border on all sides (creates 2px height from border-top + border-bottom)
        // This matches browser UA stylesheet behavior (CSS logical pixels)
        apply_html_inset_border_colors(lycon, block, 1.0f);
        // 8px margin top/bottom, auto left/right for horizontal centering (browser default)
        radiant_spacing_set_pair(&block->boundary_mut()->margin,
            CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, 8);
        radiant_spacing_set_pair(&block->boundary_mut()->margin,
            CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, 0);
        *radiant_margin_type(&block->boundary_mut()->margin, CSS_BOX_SIDE_LEFT) = CSS_VALUE_AUTO;
        *radiant_margin_type(&block->boundary_mut()->margin, CSS_BOX_SIDE_RIGHT) = CSS_VALUE_AUTO;
        HtmlDimensionPolicy policy = {
            0.0f, 0.0f, 0.0f, 0.0f, true, true, false, false, false, true
        };
        apply_html_dimension_attribute(lycon, elmt, block, LAYOUT_AXIS_X,
                                       "width", 0.0f, &policy);
        break;
    }
    case MARKUP_NAME_FONT: {
        // parse font style
        // Get color attribute using DomNode interface
        const char* color_attr = span->get_attribute("color");
        if (color_attr) {
            span->ensure_inline(lycon);
            span->in_line->color = parse_html_color(color_attr);
            span->in_line->has_color = true;
        }
        // Handle font size attribute (deprecated HTML but still supported)
        // size="1" = x-small (10px), size="2" = small (13px), size="3" = medium (16px, default)
        // size="4" = large (18px), size="5" = x-large (24px), size="6" = xx-large (32px), size="7" = 48px
        const char* size_attr = span->get_attribute("size");
        if (size_attr) {
            int level = 0;
            if (parse_legacy_font_size_level(size_attr, &level)) {
                // HTML relative font sizes are offsets from legacy level 3, not inherited CSS size
                float font_size = html_font_size_for_level(level);
                apply_html_font_size(span->ensure_font(lycon), font_size, false);  // CSS logical pixels
            }
        }
        // Handle font face attribute
        const char* face_attr = span->get_attribute("face");
        if (face_attr) {
            radiant_retain_font_family(span->ensure_font(lycon), lam::PoolPtr<char>((char*)face_attr));  // store font family name
        }
        break;
    }
    case MARKUP_NAME_A: {
        // anchor style
        span->ensure_inline(lycon);
        span->in_line->cursor = CSS_VALUE_POINTER;
        span->in_line->color = color_name_to_rgb(CSS_VALUE_BLUE);
        span->in_line->has_color = true;
        span->ensure_font(lycon)->text_deco = CSS_VALUE_UNDERLINE;
        break;
    }
    case MARKUP_NAME_CODE:  case MARKUP_NAME_KBD:  case MARKUP_NAME_SAMP:  case MARKUP_NAME_TT: {
        // monospace font family
        bool had_monospace_family = span->font && span->fontp()->family &&
            str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace");
        radiant_retain_font_family(span->ensure_font(lycon), lam::GcPtr<char>((char*)"monospace"));
        // Browser quirk (Chromium CheckForGenericFamilyChange): when font-family
        // transitions to monospace and no explicit font-size on this element,
        // scale inherited size by 13/16. Only applies when the inherited font-size
        // originates from the CSS 'medium' keyword (initial value), not from an
        // explicit font-size declaration like '12px'.
        bool parent_is_mono = lycon->font.style && lycon->font.style->family &&
            str_ieq_const(lycon->font.style->family, strlen(lycon->font.style->family), "monospace");
        // Default-style resolution can run again after intrinsic measurement;
        // do not scale a UA monospace size that was already established.
        if (!had_monospace_family && !parent_is_mono &&
            span->fontp()->font_size > 0 && span->fontp()->font_size_from_medium) {
            span->font->font_size = span->font->font_size * 13.0f / 16.0f;
        }
        break;
    }
    // ========== Block elements ==========
    case MARKUP_NAME_PRE:  case MARKUP_NAME_LISTING:  case MARKUP_NAME_XMP: {
        // preformatted: monospace, preserve whitespace, margin 1em 0
        radiant_retain_font_family(block->ensure_font(lycon), lam::GcPtr<char>((char*)"monospace"));
        // Browser quirk (Chromium CheckForGenericFamilyChange): when font-family
        // transitions to monospace and no explicit font-size on this element,
        // scale inherited size by 13/16. Only applies when the inherited font-size
        // originates from the CSS 'medium' keyword (initial value).
        float pre_font_size = lycon->font.style->font_size;
        {
            bool parent_is_mono = lycon->font.style && lycon->font.style->family &&
                str_ieq_const(lycon->font.style->family, strlen(lycon->font.style->family), "monospace");
            if (!parent_is_mono && block->fontp()->font_size > 0 && block->fontp()->font_size_from_medium) {
                block->font->font_size = block->font->font_size * 13.0f / 16.0f;
            }
            if (!parent_is_mono && pre_font_size > 0 && block->fontp()->font_size_from_medium) {
                pre_font_size = pre_font_size * 13.0f / 16.0f;
            }
        }
        block->ensure_block(lycon);
        block->blk->white_space = CSS_VALUE_PRE;
        block->ensure_boundary(lycon);
        radiant_spacing_set_pair(&block->boundary_mut()->margin,
            CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, pre_font_size);
        break;
    }
    case MARKUP_NAME_BLOCKQUOTE:
    case MARKUP_NAME_FIGURE:
        // margin: 1em 40px
        block->ensure_boundary(lycon);
        radiant_spacing_set_pair(&block->boundary_mut()->margin,
            CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, lycon->font.style->font_size);
        radiant_spacing_set_pair(&block->boundary_mut()->margin,
            CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, 40);
        break;
    case MARKUP_NAME_ADDRESS:
        // italic, block display
        block->ensure_font(lycon)->font_style = CSS_VALUE_ITALIC;
        break;
    case MARKUP_NAME_DL:
        // definition list: margin 1em 0
        block->ensure_boundary(lycon);
        radiant_spacing_set_pair(&block->boundary_mut()->margin,
            CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, lycon->font.style->font_size);
        break;
    case MARKUP_NAME_DD:
        // definition description: margin-left 40px
        block->ensure_boundary(lycon);
        *radiant_spacing_value(&block->boundary_mut()->margin, CSS_BOX_SIDE_LEFT) = 40;
        *radiant_spacing_specificity(&block->boundary_mut()->margin, CSS_BOX_SIDE_LEFT) = -1;
        break;
    case MARKUP_NAME_SUMMARY: {
        // UA default: list-style: inside disclosure-closed/-open.
        // summary elements use inside marker position (disclosure triangle before text)
        block->ensure_block(lycon);
        block->blk->list_style_position = (CssEnum)1;  // 1 = inside
        // The marker direction is the parent details' `open` attribute, which
        // is the entire open/close state (HTML 4.11.1 defines no separate
        // internal openness). Pinning it to `closed` left the triangle facing
        // right after a toggle: layout re-runs on the attribute mutation, but
        // a constant cannot follow it.
        DomNode* summary_parent = elmt->parent;
        DomElement* summary_details = summary_parent && summary_parent->is_element()
            ? summary_parent->as_element() : nullptr;
        bool details_open = summary_details &&
            summary_details->tag() == MARKUP_NAME_DETAILS &&
            summary_details->has_attribute(MARKUP_NAME_OPEN);
        block->blk->list_style_type = details_open
            ? CSS_VALUE_DISCLOSURE_OPEN : CSS_VALUE_DISCLOSURE_CLOSED;
        break;
    }
    // ========== Table elements ==========
    case MARKUP_NAME_TABLE: {
        // HTML UA default: border-spacing: 2px (CSS spec default is 0, but HTML tables use 2px)
        // This is applied at the TableProp level in layout_table.cpp, not here in block props

        apply_html_table_dimension_attribute(lycon, elmt, block, LAYOUT_AXIS_X);
        apply_html_table_dimension_attribute(lycon, elmt, block, LAYOUT_AXIS_Y);
        // Handle HTML bgcolor attribute (e.g., bgcolor="#f6f6ef")
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            apply_html_background_color(lycon, block, bg_color);
        }
        // Handle HTML border attribute (e.g., border="5")
        // Per WHATWG 15.3.10: table[border] { border-style: outset; border-color: grey; }
        // border-width is the attribute value in pixels
        const char* border_attr = elmt->get_attribute("border");
        if (elmt->has_attribute("border")) {
            float border_width = parse_html_table_border_width(border_attr, true);
            if (border_width >= 0) {
                // border-color: grey (128, 128, 128)
                Color grey = (Color){ .r=128, .g=128, .b=128, .a=255 };
                apply_html_uniform_border(lycon, block, border_width, CSS_VALUE_OUTSET, &grey);
            }
        }
        // HTML spec §14.3.3: <table align="center"> maps to margin-left: auto; margin-right: auto
        // <table align="left|right"> maps to float: left|right
        const char* align_attr = elmt->get_attribute("align");
        if (align_attr) {
            size_t alen = strlen(align_attr);
            if (str_ieq_const(align_attr, alen, "center")) {
        block->ensure_boundary(lycon);
                *radiant_margin_type(&block->boundary_mut()->margin, CSS_BOX_SIDE_LEFT) = CSS_VALUE_AUTO;
                *radiant_margin_type(&block->boundary_mut()->margin, CSS_BOX_SIDE_RIGHT) = CSS_VALUE_AUTO;
                *radiant_spacing_specificity(&block->boundary_mut()->margin, CSS_BOX_SIDE_LEFT) = -1;
                *radiant_spacing_specificity(&block->boundary_mut()->margin, CSS_BOX_SIDE_RIGHT) = -1;
            } else if (str_ieq_const(align_attr, alen, "left")) {
                block->ensure_position(lycon);
                block->position->float_prop = CSS_VALUE_LEFT;
            } else if (str_ieq_const(align_attr, alen, "right")) {
                block->ensure_position(lycon);
                block->position->float_prop = CSS_VALUE_RIGHT;
            }
        }
        break;
    }
    case MARKUP_NAME_TR: {
        // Browser UA stylesheet: tbody/thead/tfoot default to vertical-align:
        // middle, and table rows inherit it. Preserve that computed value even
        // when author CSS changes a <tr> to an inline box.
        block->ensure_inline(lycon);
        block->in_line->vertical_align = CSS_VALUE_MIDDLE;

        // Handle HTML bgcolor attribute for table rows
        const char* bgcolor_attr = elmt->get_attribute("bgcolor");
        if (bgcolor_attr) {
            Color bg_color = parse_html_color(bgcolor_attr);
            apply_html_background_color(lycon, block, bg_color);
        }
        break;
    }
    case MARKUP_NAME_THEAD:
    case MARKUP_NAME_TBODY:
    case MARKUP_NAME_TFOOT: {
        apply_html_table_rules_group_border(
            lycon, block, get_parent_table_rules(elmt));
        break;
    }
    case MARKUP_NAME_TH: {
        apply_html_table_cell_defaults(lycon, elmt, block, true);
        break;
    }
    case MARKUP_NAME_TD: {
        apply_html_table_cell_defaults(lycon, elmt, block, false);
        break;
    }
    case MARKUP_NAME_CAPTION:
        // Chromium's caption UA rule uses -webkit-center, which centers block
        // descendants as well as inline content; preserve that semantic here.
        block->ensure_block(lycon);
        block->blk->text_align = CSS_VALUE_CENTER;
        block->blk->legacy_align_center_blocks = true;
        block->blk->legacy_block_align = CSS_VALUE_CENTER;
        break;
    // ========== Form elements ==========
    case MARKUP_NAME_FIELDSET: {
        // fieldset: border and padding (CSS logical pixels)
        BlockProp* fieldset = block->ensure_block(lycon);
        block->ensure_boundary(lycon);
        // Chrome uses groove style with gray color for fieldset borders
        Color fieldset_border_color = (Color){ .r=192, .g=192, .b=192, .a=255 };
        apply_html_uniform_border(lycon, block, 2.0f, CSS_VALUE_GROOVE,
            &fieldset_border_color);
        *radiant_spacing_value(&block->boundary_mut()->padding, CSS_BOX_SIDE_TOP) =
            0.35f * lycon->font.style->font_size;
        *radiant_spacing_value(&block->boundary_mut()->padding, CSS_BOX_SIDE_BOTTOM) =
            0.625f * lycon->font.style->font_size;
        radiant_spacing_set_pair(&block->boundary_mut()->padding,
            CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, 0.75f * lycon->font.style->font_size);
        *radiant_spacing_specificity(&block->boundary_mut()->padding, CSS_BOX_SIDE_TOP) = -1;
        *radiant_spacing_specificity(&block->boundary_mut()->padding, CSS_BOX_SIDE_BOTTOM) = -1;
        // HTML Rendering §15.3.12 defines the UA margin as margin-inline;
        // physical left/right is the wrong axis for vertical fieldsets.
        bool vertical_inline_axis =
            layout_element_inline_axis_is_vertical(block->as_element());
        if (vertical_inline_axis) {
            radiant_spacing_set_pair(&block->boundary_mut()->margin,
                CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, 2);
        } else {
            radiant_spacing_set_pair(&block->boundary_mut()->margin,
                CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, 2);
        }
        if (!layout_specified_physical_minmax_size_declaration(
                block->as_element(), !vertical_inline_axis, true)) {
            // HTML Rendering §15.3.12 gives fieldsets a min-content inline minimum.
            if (vertical_inline_axis) {
                fieldset->given_min_height_type = CSS_VALUE_MIN_CONTENT;
            } else {
                fieldset->given_min_width_type = CSS_VALUE_MIN_CONTENT;
            }
        }
        break;
    }
    case MARKUP_NAME_LEGEND:
        // HTML Rendering §15.3.12 defines this as padding-inline; storing it
        // on physical left/right makes vertical legends wider instead of taller.
        block->ensure_boundary(lycon);
        if (layout_element_inline_axis_is_vertical(block->as_element())) {
            radiant_spacing_set_pair(&block->boundary_mut()->padding,
                CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, 2);
        } else {
            radiant_spacing_set_pair(&block->boundary_mut()->padding,
                CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, 2);
        }
        break;
    case MARKUP_NAME_BUTTON: {
        // button: centered text, some padding, inline-block display with flow inner
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_BUTTON, &form_created);
        if (form_created) apply_html_form_state_attributes(lycon, block, false, false, false);

        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = CSS_VALUE_FLOW;  // button has flow children
        // Button sizing is determined by content - will be shrink-to-fit

        block->ensure_block(lycon);
        block->blk->text_align = CSS_VALUE_CENTER;
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        // HTML Rendering gives buttons a normal line-height, preventing an
        // inherited author line-height from inflating their anonymous content box.
        block->blk->line_height = css_value_create_keyword(
            lycon->doc->view_tree->prop_pool, "normal");
        // Chrome UA: font-size 13.3333px, font-family Arial for form controls
        apply_html_form_control_font(lycon, block);
        apply_html_button_box_defaults(lycon, block);
        break;
    }
    case MARKUP_NAME_INPUT: {
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_TEXT, &form_created);
        if (form_created) {
            // Parse state attributes and seed canonical StateStore state.
            DocState* state = (DocState*)lycon->doc->state;
            block->form->state_ref = state;
            apply_html_form_state_attributes(lycon, block, true, true, true);
        }

        // JS DOM helpers may create FormControlProp before HTML style resolution
        // runs. Refresh attribute-backed metadata every pass so the form control
        // type and default value reflect the current DOM attributes.
        const char* type = block->get_attribute("type");
        block->form->input_type = type;
        block->form->control_type = get_input_control_type(type);
        block->form->value = block->get_attribute("value");
        block->form->placeholder = block->get_attribute("placeholder");
        block->form->name = block->get_attribute("name");

        const char* size_attr = block->get_attribute("size");
        block->form->size = FormDefaults::TEXT_SIZE_CHARS;
        if (size_attr) {
            block->form->size = (int)str_to_int64_default(size_attr, strlen(size_attr), 0); // INT_CAST_OK: HTML size attribute is a character count
            if (block->form->size <= 0) block->form->size = FormDefaults::TEXT_SIZE_CHARS;
        }

        block->ensure_block(lycon);

        // Chrome UA: font-size 13.3333px, font-family Arial for all form controls
        apply_html_form_control_font(lycon, block);

        switch (block->form->control_type) {
        case FORM_CONTROL_HIDDEN:
            block->display.outer = CSS_VALUE_NONE;
            break;
        case FORM_CONTROL_CHECKBOX:
        case FORM_CONTROL_RADIO:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            block->form->intrinsic_width = FormDefaults::CHECK_SIZE;
            block->form->intrinsic_height = FormDefaults::CHECK_SIZE;
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
        block->ensure_boundary(lycon);
            radiant_spacing_set_specificity_all(&block->boundary_mut()->margin);
            *radiant_spacing_value(&block->boundary_mut()->margin, CSS_BOX_SIDE_TOP) = 3;
            *radiant_spacing_value(&block->boundary_mut()->margin, CSS_BOX_SIDE_RIGHT) = FormDefaults::CHECK_MARGIN;
            *radiant_spacing_value(&block->boundary_mut()->margin, CSS_BOX_SIDE_BOTTOM) =
                (block->form->control_type == FORM_CONTROL_RADIO) ? 0 : FormDefaults::CHECK_MARGIN;
            *radiant_spacing_value(&block->boundary_mut()->margin, CSS_BOX_SIDE_LEFT) =
                (block->form->control_type == FORM_CONTROL_RADIO) ? FormDefaults::RADIO_MARGIN_LEFT : FormDefaults::CHECKBOX_MARGIN_LEFT;
            break;
        case FORM_CONTROL_BUTTON:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            // Button intrinsic size depends on value text - computed in layout
            // Default padding: 1px 6px (Chrome UA stylesheet)
            apply_html_button_box_defaults(lycon, block);
            break;
        case FORM_CONTROL_IMAGE:
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            // Image button is a replaced element; width/height content attributes
            // override the missing-image fallback dimensions.
            {
                float image_width = FormDefaults::IMAGE_INPUT_WIDTH;
                float image_height = FormDefaults::IMAGE_INPUT_HEIGHT;
                const char* width_attr = block->get_attribute("width");
                const char* height_attr = block->get_attribute("height");
                bool has_width_attr = false;
                bool has_height_attr = false;
                if (width_attr) {
                    float parsed_width = (float)str_to_double_default(width_attr, strlen(width_attr), -1.0);
                    if (parsed_width >= 0.0f) {
                        image_width = parsed_width;
                        has_width_attr = true;
                    }
                }
                if (height_attr) {
                    float parsed_height = (float)str_to_double_default(height_attr, strlen(height_attr), -1.0);
                    if (parsed_height >= 0.0f) {
                        image_height = parsed_height;
                        has_height_attr = true;
                    }
                }
                if (has_width_attr && !has_height_attr) {
                    // Chrome gives a width-constrained broken image submit control
                    // a square fallback glyph plus 1px control reserve.
                    image_height = image_width + 1.0f;
                }
                block->form->intrinsic_width = image_width;
                block->form->intrinsic_height = image_height;
            }
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
            break;
        case FORM_CONTROL_RANGE: {
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            block->form->intrinsic_width = FormDefaults::RANGE_WIDTH;
            // On macOS Chrome, a range with a list attribute (tick marks) renders taller (22px) than one without (16px)
            const char* list_attr = block->get_attribute("list");
            block->form->intrinsic_height = list_attr ? FormDefaults::RANGE_HEIGHT_WITH_LIST : FormDefaults::RANGE_HEIGHT;
            lycon->block.given_width = block->form->intrinsic_width;
            lycon->block.given_height = block->form->intrinsic_height;
        block->ensure_boundary(lycon);
            radiant_spacing_set_all(&block->boundary_mut()->margin, 2);
            const char* min_attr = block->get_attribute("min");
            const char* max_attr = block->get_attribute("max");
            const char* step_attr = block->get_attribute("step");
            if (min_attr) block->form->range_min = str_to_double_default(min_attr, strlen(min_attr), 0.0);
            if (max_attr) block->form->range_max = str_to_double_default(max_attr, strlen(max_attr), 0.0);
            if (step_attr) block->form->range_step = str_to_double_default(step_attr, strlen(step_attr), 0.0);
            if (block->form->value) {
                float val = (float)str_to_double_default(block->form->value, strlen(block->form->value), 0.0);
                float normalized = (val - block->form->range_min) /
                    (block->form->range_max - block->form->range_min);
                DocState* state = lycon && lycon->doc ? (DocState*)lycon->doc->state : nullptr;
                form_control_set_range_value(state, (View*)block, normalized);
            }
            break;
        }
        default:  // FORM_CONTROL_TEXT
            block->display.outer = CSS_VALUE_INLINE_BLOCK;
            // File inputs: Chrome renders as 253×21 border-box with no external border/padding
            // (the internal "Choose File" button + label text are shadow DOM)
            if (block->form->input_type && strcmp(block->form->input_type, "file") == 0) {
                block->form->intrinsic_width = 253.0f;
                block->form->intrinsic_height = FormDefaults::TEXT_HEIGHT;
                block->ensure_block(lycon);
                block->blk->given_width = 253.0f;
                block->blk->given_height = FormDefaults::TEXT_HEIGHT;
                block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
                break;
            }
            block->form->intrinsic_width = FormDefaults::TEXT_CONTENT_WIDTH;
            // Content-area height: TEXT_HEIGHT (21 border-box) minus default border+padding.
            // Chrome uses fixed 21px border-box for all text inputs regardless of font-size.
            block->form->intrinsic_height = FormDefaults::TEXT_HEIGHT
                - 2 * (FormDefaults::TEXT_BORDER + FormDefaults::TEXT_PADDING_V);
        block->ensure_boundary(lycon);
            Color text_border_color = (Color){ .r=118, .g=118, .b=118, .a=255 };
            const char* input_type = block->form->input_type;
            float border_width = input_type && strcmp(input_type, "color") == 0
                ? FormDefaults::COLOR_BORDER : FormDefaults::TEXT_BORDER;
            apply_html_uniform_border(lycon, block, border_width, CSS_VALUE_SOLID,
                &text_border_color);
            // Chrome UA: date/time inputs have padding=0; text-like inputs have padding=1
            {
                const char* itype = block->form->input_type;
                bool is_date_time = itype && (
                    strcmp(itype, "date") == 0 || strcmp(itype, "time") == 0 ||
                    strcmp(itype, "datetime-local") == 0 || strcmp(itype, "month") == 0 ||
                    strcmp(itype, "week") == 0);
                if (is_date_time) {
                    radiant_spacing_set_all(&block->boundary_mut()->padding, 0);
                } else {
                    bool vertical_inline_axis = layout_element_inline_axis_is_vertical(
                        block->as_element());
                    // HTML's text-control padding is logical: vertical writing
                    // modes exchange the inline and block padding pairs.
                    radiant_spacing_set_pair(&block->boundary_mut()->padding,
                        CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM,
                        vertical_inline_axis ? FormDefaults::TEXT_PADDING_H :
                            FormDefaults::TEXT_PADDING_V);
                    radiant_spacing_set_pair(&block->boundary_mut()->padding,
                        CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT,
                        vertical_inline_axis ? FormDefaults::TEXT_PADDING_V :
                            FormDefaults::TEXT_PADDING_H);
                }
            }
            break;
        }
        break;
    }
    case MARKUP_NAME_SELECT: {
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_SELECT, &form_created);
        if (form_created) {
            block->form->name = block->get_attribute("name");
            apply_html_form_state_attributes(lycon, block, false, false, false);
            // Count options and find selected index
            int option_count = 0;
            int selected_idx = -1;
            for (DomElement* option = dom_select_next_option(block, nullptr); option;
                 option = dom_select_next_option(block, option)) {
                // Script writes to option.selected are live IDL state, not a
                // selected attribute; preserve that state when DOM mutation
                // reconciliation rebuilds this form control.
                if (dom_option_is_selected(option) && selected_idx < 0) {
                    selected_idx = option_count;
                }
                option_count++;
            }
            block->form->option_count = option_count;
            bool is_multiple = block->has_attribute("multiple");
            int init_index = (selected_idx >= 0) ? selected_idx :
                (is_multiple ? -1 : (option_count > 0 ? 0 : -1));
            DocState* state = lycon && lycon->doc ? (DocState*)lycon->doc->state : nullptr;
            form_control_set_selected_index(state, (View*)block, init_index);
        }
        // Intrinsic measurement can allocate FormControlProp before HTML defaults
        // resolve, so derive listbox mode from the current markup on every pass.
        block->form->multiple = block->has_attribute("multiple");
        block->form->select_size = 0;
        // HTML §4.10.7: size attr specifies visible rows in listbox mode.
        const char* size_attr = block->get_attribute("size");
        if (size_attr) {
            int size_val = (int)str_to_int64_default(size_attr, strlen(size_attr), 0);
            if (size_val > 0) block->form->select_size = size_val;
        }
        // Read CSS `appearance` property — affects intrinsic width and UA-rendered chrome.
        {
            CssDeclaration* ap_decl = dom_element_get_specified_value(block, CSS_PROPERTY_APPEARANCE);
            CssEnum appearance = ap_decl && ap_decl->value &&
                ap_decl->value->type == CSS_VALUE_TYPE_KEYWORD
                ? ap_decl->value->data.keyword : CSS_VALUE_AUTO;
            block->form->appearance_none = appearance == CSS_VALUE_NONE;
            block->form->appearance_base_select = appearance == CSS_VALUE_BASE_SELECT;
        }
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->display.inner = RDT_DISPLAY_REPLACED;
        if (block->form->multiple || block->form->select_size > 1) {
            block->ensure_inline(lycon);
            // HTML rendering gives native listboxes the text-bottom inline alignment.
            block->in_line->vertical_align = CSS_VALUE_TEXT_BOTTOM;
        }
        block->ensure_block(lycon);
        block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
        if (block->form->appearance_base_select) {
            // Base appearance is CSS-rendered and inherits author font metrics.
            block->ensure_boundary(lycon);
            radiant_spacing_set_pair(&block->boundary_mut()->padding,
                CSS_BOX_SIDE_TOP, CSS_BOX_SIDE_BOTTOM, FormDefaults::BASE_SELECT_PADDING_V);
            radiant_spacing_set_pair(&block->boundary_mut()->padding,
                CSS_BOX_SIDE_LEFT, CSS_BOX_SIDE_RIGHT, FormDefaults::BASE_SELECT_PADDING_H);
        } else {
            // Native select controls use the platform system font.
            apply_html_form_control_font(lycon, block);
        }
        // Set intrinsic_width to UA default only on first resolve. Later passes
        // (e.g. intrinsic sizing in flex/grid) measure option text and write a
        // larger value here; we must not clobber it.
        if (block->form->intrinsic_width <= 0) {
            block->form->intrinsic_width = FormDefaults::SELECT_WIDTH;
        }
        // Content-area height (border-box minus default border). User CSS
        // padding/border get added by calc_select_size to produce the final
        // border-box height. Don't set given_height here — let
        // layout_form_control compute it after CSS cascade applies padding/border.
        block->form->intrinsic_height = FormDefaults::SELECT_HEIGHT - 2.0f;
        // Default border (UA): 1px solid #767676, 2px border-radius (Chrome-like)
        block->ensure_boundary(lycon);
        Color select_border_color = (Color){ .r=118, .g=118, .b=118, .a=255 };
        apply_html_uniform_border(lycon, block, 1.0f, CSS_VALUE_SOLID,
            &select_border_color, false);
        // Border-radius 2px on all four corners (Chrome UA)
        block->boundary_mut()->border->radius.top_left = block->boundary_mut()->border->radius.top_right =
            block->boundary_mut()->border->radius.bottom_left = block->boundary_mut()->border->radius.bottom_right = 2.0f;
        block->boundary_mut()->border->radius.top_left_y = block->boundary_mut()->border->radius.top_right_y =
            block->boundary_mut()->border->radius.bottom_left_y = block->boundary_mut()->border->radius.bottom_right_y = 2.0f;
        // UA background: white normally, light grey when disabled (Chrome ~rgb(235,235,228))
        DocState* state = block->doc ? block->doc->state : NULL;
        Color bg_color = form_control_is_disabled(state, (View*)block)
            ? (Color){ .r=235, .g=235, .b=228, .a=255 }
            : (Color){ .r=255, .g=255, .b=255, .a=255 };
        apply_html_background_color(lycon, block, bg_color);
        break;
    }
    case MARKUP_NAME_TEXTAREA: {
        bool form_created = false;
        ensure_form_control_prop(lycon, block, FORM_CONTROL_TEXTAREA, &form_created);
        if (form_created) {
            block->form->name = block->get_attribute("name");
            block->form->placeholder = block->get_attribute("placeholder");
            apply_html_form_state_attributes(lycon, block, true, false, false);
            // Parse cols/rows
            const char* cols_attr = block->get_attribute("cols");
            const char* rows_attr = block->get_attribute("rows");
            if (cols_attr) block->form->cols = (int)str_to_int64_default(cols_attr, strlen(cols_attr), 0);
            if (rows_attr) block->form->rows = (int)str_to_int64_default(rows_attr, strlen(rows_attr), 0);
        }
        block->display.outer = CSS_VALUE_INLINE_BLOCK;
        block->ensure_block(lycon);
        // HTML form controls use their UA normal line-height unless authored CSS overrides it.
        block->blk->line_height = css_value_create_keyword(
            lycon->doc->view_tree->prop_pool, "normal");
        apply_html_textarea_font(lycon, block);
        // html rendering defaults textarea overflow to auto; baseline synthesis
        // needs the used overflow state, not only the serialized computed value.
        block->ensure_scroll(lycon);
        block->scroller->overflow_x = block->scroller->overflow_y = CSS_VALUE_AUTO;
        // Note: textarea uses content-box (CSS default), same as Chrome UA
        // Intrinsic size: Chrome default 182x36 border-box (20 cols, 2 rows)
        block->form->intrinsic_width = 182.0f;
        block->form->intrinsic_height = 36.0f;
        // Don't set given_width/given_height — layout_form_control computes
        // intrinsic size dynamically from cols/rows and font metrics
        // Default border and padding
        block->ensure_boundary(lycon);
        Color textarea_border_color = (Color){ .r=118, .g=118, .b=118, .a=255 };
        apply_html_uniform_border(lycon, block, FormDefaults::TEXTAREA_BORDER, CSS_VALUE_SOLID,
            &textarea_border_color, false);
            radiant_spacing_set_all(&block->boundary_mut()->padding, FormDefaults::TEXTAREA_PADDING);
        break;
    }
    case MARKUP_NAME_METER:
        // Native meter sizing follows the HTML em-based default.
        apply_html_fixed_replaced_size(lycon, block,
            form_control_em_size(lycon, block, FormDefaults::METER_INLINE_SIZE_EM),
            form_control_em_size(lycon, block, FormDefaults::FORM_WIDGET_BLOCK_SIZE_EM));
        break;
    case MARKUP_NAME_PROGRESS:
        // Native progress sizing follows the HTML em-based default.
        apply_html_fixed_replaced_size(lycon, block,
            form_control_em_size(lycon, block, FormDefaults::PROGRESS_INLINE_SIZE_EM),
            form_control_em_size(lycon, block, FormDefaults::FORM_WIDGET_BLOCK_SIZE_EM));
        break;
    case MARKUP_NAME_OPTION:
    case MARKUP_NAME_OPTGROUP: {
        // HTML spec: option/optgroup inside select/datalist are 0×0 (UA rendered).
        // Outside select/datalist, they render as normal block flow content.
        if (elmt_name == MARKUP_NAME_OPTGROUP) {
            // HTML UA styles render optgroup labels with bolder inherited text.
            apply_html_font_weight(block->ensure_font(lycon), CSS_VALUE_BOLD, 700);
        }
        uintptr_t parent_tag = elmt->parent ? elmt->parent->tag() : 0;
        if (parent_tag == MARKUP_NAME_SELECT || parent_tag == MARKUP_NAME_DATALIST ||
            parent_tag == MARKUP_NAME_OPTGROUP) {
            block->display.outer = CSS_VALUE_BLOCK;
            block->display.inner = CSS_VALUE_FLOW;
            block->ensure_block(lycon);
            block->blk->given_width = 0;
            block->blk->given_height = 0;
        }
        break;
    }
    case MARKUP_NAME_DATALIST:
        // Datalist should be completely hidden (display:none)
        block->display.outer = CSS_VALUE_NONE;
        block->display.inner = CSS_VALUE_NONE;
        break;
    case MARKUP_NAME_DIALOG:
        // HTML §4.12.4: <dialog> without the 'open' attribute is not rendered.
        // Chrome UA: dialog:not([open]) { display: none; }
        if (!elmt->has_attribute("open")) {
            block->display.outer = CSS_VALUE_NONE;
            block->display.inner = CSS_VALUE_NONE;
        } else if (elmt->is_element() && elmt->as_element()->is_dialog_modal()) {
            // modal dialogs use the fixed, centered fit-content UA box before authored insets apply
            apply_html_centered_popup_style(lycon, block, 1.0f);
        }
        break;
    }

    // Handle HTML 'dir' attribute (global, applies to all elements)
    // CSS 2.1 §9.10: The 'dir' attribute maps to the CSS 'direction' property
    const char* dir_attr = elmt->get_attribute("dir");
    if (dir_attr) {
        block->ensure_block(lycon);
        if (str_ieq_const(dir_attr, strlen(dir_attr), "rtl")) {
            block->blk->direction = CSS_VALUE_RTL;
            // HTML rendering §15.3.5: recognized dir values isolate the element
            // from the surrounding bidi paragraph.
            block->blk->unicode_bidi = CSS_VALUE_ISOLATE;
        } else if (str_ieq_const(dir_attr, strlen(dir_attr), "ltr")) {
            block->blk->direction = CSS_VALUE_LTR;
            // HTML rendering §15.3.5: recognized dir values isolate the element
            // from the surrounding bidi paragraph.
            block->blk->unicode_bidi = CSS_VALUE_ISOLATE;
        } else if (str_ieq_const(dir_attr, strlen(dir_attr), "auto")) {
            // HTML5 §14.3.4: dir="auto" — resolve direction from first strong character
            CssEnum resolved = resolve_dir_auto(lam::dom_require_element(elmt));
            block->blk->direction = resolved;
            // HTML rendering §15.3.5: dir=auto is isolated unless plaintext
            // handling is required for preformatted or textarea content.
            block->blk->unicode_bidi = CSS_VALUE_ISOLATE;
            if (elmt->tag() == MARKUP_NAME_PRE || elmt->tag() == MARKUP_NAME_TEXTAREA) {
                // html rendering maps pre/textarea dir=auto to plaintext bidi.
                block->blk->unicode_bidi = CSS_VALUE_PLAINTEXT;
            }
        }
    }
}

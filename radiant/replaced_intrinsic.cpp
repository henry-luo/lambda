#include "layout.hpp"
#include "view.hpp"
#include "render.hpp"
#include "rdt_video.h"
#include "../lib/tagged.hpp"

static void replaced_facts_set_axis(ReplacedIntrinsicFacts* facts, bool horizontal,
                                    float value, bool use_as_current) {
    if (!facts || value <= 0.0f) return;
    if (horizontal) {
        facts->natural_width = value;
        facts->has_natural_width = true;
        if (use_as_current) facts->width = value;
    } else {
        facts->natural_height = value;
        facts->has_natural_height = true;
        if (use_as_current) facts->height = value;
    }
}

static void replaced_facts_set_pair(ReplacedIntrinsicFacts* facts,
                                    float width, float height,
                                    bool use_as_current) {
    replaced_facts_set_axis(facts, true, width, use_as_current);
    replaced_facts_set_axis(facts, false, height, use_as_current);
    if (facts && facts->has_natural_width && facts->has_natural_height) {
        facts->natural_aspect_ratio = facts->natural_width / facts->natural_height;
        facts->has_natural_aspect_ratio = facts->natural_aspect_ratio > 0.0f;
    }
}

bool layout_replaced_default_size(NameId tag, float* width, float* height) {
    float default_width = 0.0f;
    float default_height = 0.0f;
    switch (tag) {
        case MARKUP_NAME_IFRAME:
        case MARKUP_NAME_VIDEO:
        case MARKUP_NAME_CANVAS:
        case MARKUP_NAME_OBJECT:
        case MARKUP_NAME_EMBED:
        case MARKUP_NAME_SVG:
            default_width = 300.0f;
            default_height = 150.0f;
            break;
        case MARKUP_NAME_AUDIO:
            default_width = 300.0f;
            default_height = 54.0f;
            break;
        default:
            return false;
    }
    if (width) *width = default_width;
    if (height) *height = default_height;
    return true;
}

bool layout_canvas_intrinsic_size(LayoutContext* lycon, ViewBlock* block,
                                 float* out_width, float* out_height,
                                 bool* out_view_box_changed) {
    if (out_width) *out_width = 0.0f;
    if (out_height) *out_height = 0.0f;
    if (out_view_box_changed) *out_view_box_changed = false;
    if (!block || block->tag() != MARKUP_NAME_CANVAS) return false;
    float width = 0.0f;
    float height = 0.0f;
    if (!layout_canvas_natural_size(block, &width, &height) ||
        width <= 0.0f || height <= 0.0f) return false;
    if (block->is_element()) {
        bool changed = layout_apply_object_view_box_intrinsic_size(
            lycon, block->as_element(), &width, &height);
        if (out_view_box_changed) *out_view_box_changed = changed;
    }
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return true;
}

ReplacedIntrinsicFacts layout_replaced_intrinsic_facts(LayoutContext* lycon,
                                                      ViewBlock* block) {
    ReplacedIntrinsicFacts facts = {};
    if (!block) return facts;

    facts.width = block->width > 0.0f ? block->width : 0.0f;
    facts.height = block->height > 0.0f ? block->height : 0.0f;
    if (block->tag() == MARKUP_NAME_IMG && lycon && block->is_element()) {
        layout_ensure_replaced_image_surface(
            lycon, block, block->as_element());
    }
    if (block->embed && block->embedp()->img) {
        ImageSurface* image = block->embedp()->img;
        if (image->has_intrinsic_size && image->width > 0 && image->height > 0) {
            replaced_facts_set_pair(&facts, (float)image->width,
                                    (float)image->height, true);
        } else if (image->has_intrinsic_aspect_ratio &&
                   image->width > 0 && image->height > 0) {
            // SVG's 300x150 fallback supplies usable axes, but only a viewBox
            // contributes a natural ratio to `aspect-ratio: auto <ratio>`.
            facts.natural_aspect_ratio = (float)image->width / (float)image->height;
            facts.has_natural_aspect_ratio = true;
        }
    }

    if (block->tag() == MARKUP_NAME_VIDEO && block->embed && block->embedp()->video) {
        float natural_width = (float)rdt_video_get_width(block->embedp()->video);
        float natural_height = (float)rdt_video_get_height(block->embedp()->video);
        if (natural_width > 0.0f && natural_height > 0.0f) {
            replaced_facts_set_pair(&facts, natural_width, natural_height, true);
        }
    }

    if (block->tag() == MARKUP_NAME_SVG && block->is_element()) {
        Element* svg = dom_element_backing(lam::dom_require_element(block));
        SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(svg);
        if (intrinsic.has_intrinsic_width) {
            replaced_facts_set_axis(&facts, true, intrinsic.width, true);
        }
        if (intrinsic.has_intrinsic_height) {
            replaced_facts_set_axis(&facts, false, intrinsic.height, true);
        }
        // A viewBox supplies a natural ratio even when neither viewport axis is
        // intrinsic; flex and `aspect-ratio:auto` need that distinction.
        if (intrinsic.has_intrinsic_aspect_ratio) {
            facts.natural_aspect_ratio = intrinsic.aspect_ratio;
            facts.has_natural_aspect_ratio = intrinsic.has_intrinsic_aspect_ratio;
        }
    }

    if (block->tag() == MARKUP_NAME_CANVAS &&
        (!facts.has_natural_width || !facts.has_natural_height)) {
        float natural_width = 0.0f;
        float natural_height = 0.0f;
        if (layout_canvas_intrinsic_size(lycon, block, &natural_width,
                                         &natural_height)) {
            replaced_facts_set_pair(&facts, natural_width, natural_height, false);
            if (facts.width <= 0.0f) {
                facts.width = facts.height > 0.0f
                    ? facts.height * natural_width / natural_height
                    : natural_width;
            }
            if (facts.height <= 0.0f) {
                facts.height = facts.width > 0.0f
                    ? facts.width * natural_height / natural_width
                    : natural_height;
            }
        }
    }

    if (facts.width <= 0.0f || facts.height <= 0.0f) {
        float default_width = 0.0f;
        float default_height = 0.0f;
        if (layout_replaced_default_size(block->tag(), &default_width,
                &default_height)) {
            facts.has_default_size = true;
            if (facts.width <= 0.0f) facts.width = default_width;
            if (facts.height <= 0.0f) facts.height = default_height;
        }
    }
    return facts;
}

bool layout_replaced_flex_intrinsic_dimensions(ViewBlock* block,
                                               FlexContainerLayout* flex_layout,
                                               const ReplacedIntrinsicFacts* facts,
                                               float* width, float* height) {
    if (!block || !flex_layout || !facts || !width || !height) return false;
    *width = facts->has_natural_width ? facts->natural_width : facts->width;
    *height = facts->has_natural_height ? facts->natural_height : facts->height;
    if (block->tag() == MARKUP_NAME_IMG && facts->has_natural_width &&
        facts->has_natural_height) {
        float ratio = layout_used_preferred_aspect_ratio(block);
        if (ratio <= 0.0f) ratio = facts->natural_width / facts->natural_height;
        if (layout_axis_has_given_size(block, true)) {
            *width = block->block()->given_width;
            *height = layout_axis_has_given_size(block, false)
                ? block->block()->given_height : *width / ratio;
        } else if (layout_axis_has_given_size(block, false)) {
            *height = block->block()->given_height;
            *width = *height * ratio;
        } else {
            float max_width = layout_positive_max_axis_or(block, true, -1.0f);
            if (max_width > 0.0f && max_width < *width) {
                *height *= max_width / *width;
                *width = max_width;
            }
        }
    }
    float ratio = facts->has_natural_aspect_ratio
        ? facts->natural_aspect_ratio : 0.0f;
    if (ratio > 0.0f) {
        if (facts->has_natural_width && !facts->has_natural_height) {
            *height = *width / ratio;
        } else if (facts->has_natural_height && !facts->has_natural_width) {
            *width = *height * ratio;
        } else if (!facts->has_natural_width && !facts->has_natural_height) {
            bool horizontal = is_main_axis_horizontal(flex_layout);
            float available = horizontal ? flex_layout->main_axis_size
                                         : flex_layout->cross_axis_size;
            bool definite = horizontal
                ? flex_layout->main_axis_available_size_is_definite
                : flex_layout->has_definite_cross_size;
            if (definite && available > 0.0f) {
                float border_size = layout_stretch_fit_border_box_size(
                    block, available, horizontal);
                float content_size = layout_content_size_from_border_box(
                    block, border_size, horizontal);
                if (horizontal) {
                    *width = content_size;
                    *height = content_size / ratio;
                } else {
                    *height = content_size;
                    *width = content_size * ratio;
                }
            } else {
                float default_width = 0.0f;
                layout_replaced_default_size(block->tag(), &default_width, nullptr);
                *width = default_width;
                *height = default_width / ratio;
            }
        }
    }
    if (*width <= 0.0f || *height <= 0.0f) return false;

    bool horizontal = is_main_axis_horizontal(flex_layout);
    bool cross_horizontal = !horizontal;
    CssEnum cross_type = cross_horizontal
        ? block->block()->given_width_type : block->block()->given_height_type;
    if (ratio > 0.0f && cross_type == CSS_VALUE_STRETCH &&
        flex_layout->has_definite_cross_size) {
        float border_size = layout_stretch_fit_border_box_size(
            block, flex_layout->cross_axis_size, cross_horizontal);
        float content_size = layout_content_size_from_border_box(
            block, border_size, cross_horizontal);
        if (cross_horizontal) {
            *width = content_size;
            *height = content_size / ratio;
        } else {
            *height = content_size;
            *width = content_size * ratio;
        }
    }
    return true;
}

bool layout_replaced_intrinsic_axis_size(LayoutContext* lycon, ViewBlock* block,
                                         DomElement* element, bool horizontal,
                                         float opposite_size, float* out_size) {
    if (!out_size || !element) return false;
    ReplacedIntrinsicFacts facts = layout_replaced_intrinsic_facts(lycon, block);
    if (facts.has_natural_width && facts.has_natural_height &&
        facts.natural_width > 0.0f && facts.natural_height > 0.0f) {
        float width = facts.natural_width;
        float height = facts.natural_height;
        if (opposite_size > 0.0f) {
            *out_size = horizontal
                ? opposite_size * width / height
                : opposite_size * height / width;
        } else {
            *out_size = horizontal ? width : height;
        }
        return *out_size > 0.0f;
    }

    float default_size = 0.0f;
    if (!layout_replaced_default_size(element->tag(), horizontal ? &default_size : nullptr,
                                      horizontal ? nullptr : &default_size)) return false;
    *out_size = default_size;
    return *out_size > 0.0f;
}

ReplacedSvgIntrinsicSize layout_replaced_svg_intrinsic_size(DomElement* element,
                                                            float constrained_width) {
    ReplacedSvgIntrinsicSize result = {300.0f, 150.0f};
    if (!element) return result;
    Element* svg = dom_element_backing(lam::dom_require_element(element));
    SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(svg);
    if (intrinsic.has_intrinsic_width) result.width = intrinsic.width;
    if (intrinsic.has_intrinsic_height) result.height = intrinsic.height;
    if (intrinsic.has_intrinsic_aspect_ratio) {
        if (intrinsic.has_intrinsic_width && !intrinsic.has_intrinsic_height) {
            result.height = result.width / intrinsic.aspect_ratio;
        } else if (!intrinsic.has_intrinsic_width && intrinsic.has_intrinsic_height) {
            result.width = result.height * intrinsic.aspect_ratio;
        } else if (!intrinsic.has_intrinsic_width && !intrinsic.has_intrinsic_height &&
                   constrained_width > 0.0f) {
            result.width = constrained_width;
            result.height = constrained_width / intrinsic.aspect_ratio;
        }
    }
    return result;
}

bool layout_measure_replaced_flex_intrinsic(LayoutContext* lycon,
                                            ViewElement* item,
                                            FlexContainerLayout* flex_layout,
                                            IntrinsicSize* out) {
    if (!lycon || !item || !out) return false;
    NameId tag = item->tag();
    if (tag != MARKUP_NAME_IMG && tag != MARKUP_NAME_SVG &&
        tag != MARKUP_NAME_CANVAS) return false;
    ViewBlock* block = lam::view_as_block(item);
    float width = 0.0f;
    float height = 0.0f;
    ReplacedIntrinsicFacts facts = layout_replaced_intrinsic_facts(lycon, block);
    if (facts.has_natural_width && facts.has_natural_height) {
        if (!layout_replaced_flex_intrinsic_dimensions(
                block, flex_layout, &facts, &width, &height)) return false;
    } else if (tag == MARKUP_NAME_IMG && item->get_attribute("src")) {
        // Broken image data keeps the established fallback contribution.
        width = height = 16.0f;
    } else if (tag == MARKUP_NAME_IMG) {
        const char* width_attr = item->get_attribute("width");
        CssDeclaration* css_width = item->specified_style
            ? style_tree_get_declaration(item->specified_style, CSS_PROPERTY_WIDTH) : nullptr;
        bool has_html_width = width_attr && !css_width && item->blk &&
            item->block()->given_width >= 0.0f && isnan(item->block()->given_width_percent);
        width = has_html_width
            ? min(item->block()->given_width, MAX_LAYOUT_DIMENSION) : 0.0f;
    } else if (!layout_replaced_flex_intrinsic_dimensions(
                   block, flex_layout, &facts, &width, &height)) {
        return false;
    }
    out->min_width = out->max_width = width;
    out->min_height = out->max_height = height;
    return true;
}

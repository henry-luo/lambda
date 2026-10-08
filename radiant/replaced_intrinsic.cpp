#include "layout.hpp"
#include "view.hpp"
#include "render.hpp"
#include "svg_animation.hpp"
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

float layout_replaced_image_density(const DomElement* block) {
    return block && block->embed && block->embedp()->content_image_resolution > 0.0f
        ? block->embedp()->content_image_resolution : 1.0f;
}

void layout_replaced_image_facts(ReplacedIntrinsicFacts* facts, ImageSurface* image,
        bool from_image, float density) {
    if (!facts || !image) return;
    if (!isfinite(density) || density <= 0.0f) return;
    if (image->format == IMAGE_FORMAT_SVG) {
        if (image->has_natural_width) replaced_facts_set_axis(facts, true, image->natural_width / density, true);
        if (image->has_natural_height) replaced_facts_set_axis(facts, false, image->natural_height / density, true);
        if (image->has_intrinsic_aspect_ratio) {
            facts->natural_aspect_ratio = image->natural_aspect_ratio;
            facts->has_natural_aspect_ratio = image->natural_aspect_ratio > 0.0f;
        }
        // intrinsic consumers need concrete fallback axes without promoting them to natural metadata.
        layout_replaced_default_object_size(facts, 300.0f, 150.0f, &facts->width, &facts->height);
        facts->has_default_size = !facts->has_natural_width || !facts->has_natural_height;
        return;
    }
    float width = from_image || image->encoded_width <= 0 ? (float)image->width : (float)image->encoded_width;
    float height = from_image || image->encoded_height <= 0 ? (float)image->height : (float)image->encoded_height;
    if (width <= 0.0f || height <= 0.0f) return;
    if (image->has_intrinsic_size) replaced_facts_set_pair(facts, width / density, height / density, true);
    else if (image->has_intrinsic_aspect_ratio) {
        facts->natural_aspect_ratio = width / height; facts->has_natural_aspect_ratio = true;
    }
}

void layout_replaced_default_object_size(const ReplacedIntrinsicFacts* facts,
        float default_width, float default_height, float* width, float* height) {
    if (!facts || !width || !height) return;
    float ratio = facts->has_natural_aspect_ratio ? facts->natural_aspect_ratio : 0.0f;
    float w = facts->has_natural_width ? facts->natural_width : default_width;
    float h = facts->has_natural_height ? facts->natural_height : default_height;
    // CSS Images 3 section 4.3.1: fill missing natural axes, or contain a ratio-only object in the default rectangle.
    if (ratio > 0.0f) {
        if (facts->has_natural_width && !facts->has_natural_height) h = w / ratio;
        else if (!facts->has_natural_width && facts->has_natural_height) w = h * ratio;
        else if (!facts->has_natural_width && !facts->has_natural_height) {
            w = fminf(default_width, default_height * ratio); h = w / ratio;
        }
    }
    *width = w; *height = h;
}

bool layout_replaced_content_size(const ReplacedIntrinsicFacts* facts,
        const ReplacedSizeConstraints* constraints, float available_width, float* width, float* height) {
    if (!facts || !constraints || !width || !height) return false;
    const ReplacedSizeConstraints& c = *constraints;
    bool auto_width = isnan(c.width), auto_height = isnan(c.height);
    float ratio = c.preferred_ratio;
    float edge_x = c.ratio_content_box ? 0.0f : c.horizontal_edges;
    float edge_y = c.ratio_content_box ? 0.0f : c.vertical_edges;
    float min_w = fmaxf(0.0f, c.min_width), min_h = fmaxf(0.0f, c.min_height);
    float max_w = fmaxf(min_w, c.max_width), max_h = fmaxf(min_h, c.max_height);
    float w = auto_width ? facts->has_natural_width ? facts->natural_width
        : facts->has_natural_height && ratio > 0.0f ? facts->natural_height * ratio
        : fminf(300.0f, available_width) : c.width;
    float h = auto_height ? facts->has_natural_height ? facts->natural_height
        : ratio > 0.0f ? fmaxf(0.0f, (w + edge_x) / ratio - edge_y) : 150.0f : c.height;
    if (auto_width && !auto_height && ratio > 0.0f)
        w = fmaxf(0.0f, (fmaxf(min_h, fminf(h, max_h)) + edge_y) * ratio - edge_x);
    if (auto_height && !auto_width && ratio > 0.0f)
        h = fmaxf(0.0f, (fmaxf(min_w, fminf(w, max_w)) + edge_x) / ratio - edge_y);
    if (auto_width && auto_height && ratio > 0.0f) {
        // CSS 2.2 section 10.4: preserve the ratio where both constraint intervals permit it.
        // conflicting width/height intervals use independent bounds, as in the violation table.
        h = fmaxf(0.0f, (w + edge_x) / ratio - edge_y);
        float ratio_w = w + edge_x, ratio_h = h + edge_y;
        if (ratio_w > 0.0f && ratio_h > 0.0f) {
            float lower = fmaxf((min_w + edge_x) / ratio_w, (min_h + edge_y) / ratio_h);
            float upper = fminf((max_w + edge_x) / ratio_w, (max_h + edge_y) / ratio_h);
            if (lower <= upper) {
                float scale = fmaxf(lower, fminf(1.0f, upper));
                w = fmaxf(0.0f, ratio_w * scale - edge_x);
                h = fmaxf(0.0f, ratio_h * scale - edge_y);
            }
        }
    }
    *width = fmaxf(min_w, fminf(w, max_w)); *height = fmaxf(min_h, fminf(h, max_h));
    return isfinite(*width) && isfinite(*height) && *width >= 0.0f && *height >= 0.0f;
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
        case MARKUP_NAME_SCENE3D:
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

    // ViewBlock stores used border-box geometry. Replaced intrinsic facts are
    // content-box dimensions, so a later intrinsic query never counts chrome twice.
    facts.width = block->width > 0.0f
        ? layout_content_size_from_border_box(block, block->width, true) : 0.0f;
    facts.height = block->height > 0.0f
        ? layout_content_size_from_border_box(block, block->height, false) : 0.0f;
    if (block->tag() == MARKUP_NAME_IMG && lycon && block->is_element()) {
        layout_ensure_replaced_image_surface(
            lycon, block, block->as_element());
    }
    // EmbedProp also carries flex/grid state, so only replaced boxes may
    // interpret its image slot as intrinsic content.
    if (layout_replaced_image_surface_contributes(block) &&
        block->embed && block->embedp()->img) {
        layout_replaced_image_facts(&facts, block->embedp()->img, true, layout_replaced_image_density(block));
    }

    if (block->tag() == MARKUP_NAME_VIDEO && block->embed && block->embedp()->video) {
        float natural_width = (float)rdt_video_get_width(block->embedp()->video);
        float natural_height = (float)rdt_video_get_height(block->embedp()->video);
        if (natural_width > 0.0f && natural_height > 0.0f) {
            replaced_facts_set_pair(&facts, natural_width, natural_height, true);
        }
    }

    if (layout_is_svg_viewport(block->tag()) && block->is_element()) {
        SvgAnimationSourceScope animation_sources(block->as_element()->doc);
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

    float default_width = 0.0f;
    float default_height = 0.0f;
    if (layout_replaced_default_size(block->tag(), &default_width,
            &default_height)) {
        // Keep fallback provenance after a prior layout populated used axes.
        // Intrinsic sizing must still use the fallback contribution on relayout.
        facts.has_default_size = true;
        if (facts.width <= 0.0f) facts.width = default_width;
        if (facts.height <= 0.0f) facts.height = default_height;
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
    bool image = block->tag() == MARKUP_NAME_IMG;
    bool specified_image_axis = image && (layout_axis_has_given_size(block, true) || layout_axis_has_given_size(block, false));
    if (image && *width > 0.0f && *height > 0.0f) {
        float ratio = layout_used_preferred_aspect_ratio(block);
        if (ratio <= 0.0f && facts->has_natural_aspect_ratio) ratio = facts->natural_aspect_ratio;
        if (layout_axis_has_given_size(block, true)) {
            *width = block->block()->given_width;
            *height = layout_axis_has_given_size(block, false)
                ? block->block()->given_height : ratio > 0.0f ? *width / ratio : *height;
        } else if (layout_axis_has_given_size(block, false)) {
            *height = block->block()->given_height;
            if (ratio > 0.0f) *width = *height * ratio;
        } else {
            float max_width = layout_positive_max_axis_or(block, true, -1.0f);
            if (max_width > 0.0f && max_width < *width) {
                if (ratio > 0.0f) *height = max_width / ratio;
                *width = max_width;
            }
        }
    }
    float ratio = facts->has_natural_aspect_ratio
        ? facts->natural_aspect_ratio : 0.0f;
    if (ratio > 0.0f && !specified_image_axis) {
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
    if (tag != MARKUP_NAME_IMG && !layout_is_svg_viewport(tag) &&
        tag != MARKUP_NAME_CANVAS) return false;
    ViewBlock* block = lam::view_as_block(item);
    float width = 0.0f;
    float height = 0.0f;
    ReplacedIntrinsicFacts facts = layout_replaced_intrinsic_facts(lycon, block);
    // a loaded SVG with partial or absent natural axes still has a valid concrete default size.
    bool loaded_image_fallback = tag == MARKUP_NAME_IMG && facts.has_default_size &&
        block->embed && block->embedp()->img;
    if ((facts.has_natural_width && facts.has_natural_height) || loaded_image_fallback) {
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

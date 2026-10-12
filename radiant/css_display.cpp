#include "layout.hpp"
#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/str.h"
#include "../lib/stringbuf.h"
#include <string.h>

static bool css_legacy_box_is_vertical(DomElement* element) {
    Pool* scratch = pool_create();
    if (!scratch) return false;
    StringBuf* output = stringbuf_new(scratch);
    bool vertical = output && css_prop_serialize_cascaded_value(element,
        CSS_PROPERTY_WEBKIT_BOX_ORIENT, 0, output) && output->str &&
        strcmp(output->str->chars, "vertical") == 0;
    pool_destroy(scratch);
    return vertical;
}

DisplayValue css_legacy_box_display(DomElement* element, uint8_t kind) {
    DisplayValue display = {kind == 2 ? CSS_VALUE_INLINE_BLOCK : CSS_VALUE_BLOCK, CSS_VALUE_FLEX};
    display.legacy_box = kind;
    display.legacy_vertical = css_legacy_box_is_vertical(element);
    if (!display.legacy_vertical) return display;
    CssPropertyCode property;
    if (!layout_cascaded_line_clamp_declaration(element, &property)) return display;
    Pool* scratch = pool_create();
    if (!scratch) return display;
    StringBuf* output = stringbuf_new(scratch);
    bool clamped = output && css_prop_serialize_cascaded_value(element, property, 0, output) &&
        output->str && strtod(output->str->chars, nullptr) > 0.0;
    if (clamped) {
        // css overflow 4 §5.3 converts a vertical clamped legacy box to a flow formatting context.
        display.inner = CSS_VALUE_FLOW_ROOT;
        display.legacy_box = 0;
        display.legacy_clamp = true;
    }
    pool_destroy(scratch);
    return display;
}

DisplayValue css_inherit_display_value(DomElement* element, DisplayValue parent) {
    if (parent.legacy_box) return css_legacy_box_display(element, parent.legacy_box);
    // inherit copies computed flow-root/inline-block, not the parent's legacy applicability.
    parent.legacy_clamp = false;
    parent.legacy_vertical = false;
    return parent;
}

const char* css_display_special_name(DisplayValue display) {
    if (display.legacy_box) return display.legacy_box == 2 ? "-webkit-inline-box" : "-webkit-box";
    if (display.list_item && display.outer == CSS_VALUE_INLINE &&
        (display.inner == CSS_VALUE_FLOW || display.inner == CSS_VALUE_FLOW_ROOT)) {
        return display.inner == CSS_VALUE_FLOW_ROOT ? "inline flow-root list-item" : "inline list-item";
    }
    if (display.inner == CSS_VALUE_FLOW_ROOT) {
        if (display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_INLINE_BLOCK) return "inline-block";
        return display.list_item ? "flow-root list-item" : "flow-root";
    }
    return nullptr;
}

bool css_content_value_has_image_url(const CssValue* value) {
    if (!value) return false;
    if (css_value_is_image_set(value)) return css_image_set_valid(value);
    if (value->type == CSS_VALUE_TYPE_URL) return true;
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        value->data.function->name) {
        const char* fn = value->data.function->name;
        size_t fn_len = strlen(fn);
        if (str_ieq_const(fn, fn_len, "url")) return true;
    }
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            if (css_content_value_has_image_url(value->data.list.values[i])) return true;
        }
    }
    return false;
}

bool css_display_contents_suppresses_element(DomElement* element) {
    // CSS Display 3: `display: contents` computes to `none` for replaced
    // elements and for HTML's no-box line-break elements.
    if (!element) return false;
    NameId tag_id = element->tag_id;
    switch (tag_id) {
    case MARKUP_NAME_BR:
    case MARKUP_NAME_WBR:
    case MARKUP_NAME_METER:
    case MARKUP_NAME_PROGRESS:
    case MARKUP_NAME_CANVAS:
    case MARKUP_NAME_SCENE3D:
    case MARKUP_NAME_GEOMAP:
    case MARKUP_NAME_EMBED:
    case MARKUP_NAME_OBJECT:
    case MARKUP_NAME_AUDIO:
    case MARKUP_NAME_IFRAME:
    case MARKUP_NAME_IMG:
    case MARKUP_NAME_VIDEO:
    case MARKUP_NAME_INPUT:
    case MARKUP_NAME_TEXTAREA:
    case MARKUP_NAME_SELECT:
        return true;
    case MARKUP_NAME_SVG:
        // An outermost SVG viewport cannot be unboxed; nested SVG viewports
        // and SVG graphics containers can participate in the parent tree.
        for (DomNode* ancestor = element->parent; ancestor;
             ancestor = ancestor->parent) {
            if (ancestor->is_element() && ancestor->tag() == MARKUP_NAME_SVG) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

bool css_display_element_is_replaced(DomElement* dom_elem) {
    if (!dom_elem) return false;
    // HTML §4.8.7/§4.8.9: these elements use replaced display internals when
    // their HTML conditions make them replaced.
    NameId tag_id = dom_elem->tag_id;
    bool is_replaced = layout_tag_is_css_replaced(tag_id) ||
        layout_is_svg_viewport(tag_id) ||
        (tag_id == MARKUP_NAME_OBJECT && dom_elem->get_attribute(MARKUP_NAME_DATA)) ||
        (tag_id == MARKUP_NAME_AUDIO && dom_elem->has_attribute(MARKUP_NAME_CONTROLS));
    if (dom_elem->specified_style) {
        CssDeclaration* content_decl = style_tree_get_declaration(
            dom_elem->specified_style, CSS_PROPERTY_CONTENT);
        if (content_decl && css_content_value_has_image_url(content_decl->value)) {
            // CSS Content 3: selected image-set content has the same replaced-box sizing as url().
            is_replaced = true;
        }
    }
    return is_replaced;
}

bool css_is_mathml_element(const DomElement* element) {
    if (!element || !element->tag_name) return false;
    // MathML Core §4.1: the math inner display type applies only to MathML elements.
    static const char* mathml_tags[] = {
        "math", "maction", "maligngroup", "malignmark", "menclose",
        "merror", "mfenced", "mfrac", "mglyph", "mi", "mlabeledtr",
        "mlongdiv", "mmultiscripts", "mn", "mo", "mover", "mpadded",
        "mphantom", "mprescripts", "mroot", "mrow", "ms", "mscarries",
        "mscarry", "msgroup", "msline", "mspace", "msqrt", "mstack",
        "mstyle", "msub", "msup", "msubsup", "mtable", "mtd", "mtext",
        "mtr", "munder", "munderover", "semantics", "annotation",
        "annotation-xml"};
    for (size_t i = 0; i < sizeof(mathml_tags) / sizeof(*mathml_tags); i++) {
        if (strcmp(element->tag_name, mathml_tags[i]) == 0) return true;
    }
    return false;
}

struct CssDisplayKeywordResult {
    DisplayValue display;
    bool handled;
    bool blockify;
};

struct CssDisplayKeywordSpec {
    CssEnum keyword;
    CssEnum outer;
    CssEnum inner;
    bool blockify;
    bool replaced_inner;
    bool list_item;
};

static CssDisplayKeywordResult css_display_keyword_result(CssEnum keyword,
                                                           bool is_replaced,
                                                           bool is_mathml) {
    static const CssDisplayKeywordSpec specs[] = {
        {CSS_VALUE_FLEX, CSS_VALUE_BLOCK, CSS_VALUE_FLEX, false, false, false},
        {CSS_VALUE_INLINE_FLEX, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_FLEX, false, false, false},
        {CSS_VALUE_GRID, CSS_VALUE_BLOCK, CSS_VALUE_GRID, false, false, false},
        {CSS_VALUE_INLINE_GRID, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_GRID, false, false, false},
        {CSS_VALUE_BLOCK, CSS_VALUE_BLOCK, CSS_VALUE_FLOW, false, true, false},
        {CSS_VALUE_INLINE, CSS_VALUE_INLINE, CSS_VALUE_FLOW, true, true, false},
        {CSS_VALUE_INLINE_BLOCK, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_FLOW, true, true, false},
        {CSS_VALUE_LIST_ITEM, CSS_VALUE_LIST_ITEM, CSS_VALUE_FLOW, false, false, true},
        {CSS_VALUE_NONE, CSS_VALUE_NONE, CSS_VALUE_NONE, false, false, false},
        {CSS_VALUE_CONTENTS, CSS_VALUE_CONTENTS, CSS_VALUE_CONTENTS, false, false, false},
        {CSS_VALUE_MATH, CSS_VALUE_INLINE, CSS_VALUE_MATH, false, true, false},
        {CSS_VALUE_FLOW_ROOT, CSS_VALUE_BLOCK, CSS_VALUE_FLOW_ROOT, false, false, false},
        {CSS_VALUE_TABLE, CSS_VALUE_BLOCK, CSS_VALUE_TABLE, false, false, false},
        {CSS_VALUE_INLINE_TABLE, CSS_VALUE_INLINE, CSS_VALUE_TABLE, true, false, false},
        {CSS_VALUE_RUBY, CSS_VALUE_INLINE, CSS_VALUE_RUBY, true, false, false},
        {CSS_VALUE_RUBY_BASE, CSS_VALUE_INLINE, CSS_VALUE_RUBY_BASE, false, false, false},
        {CSS_VALUE_RUBY_TEXT, CSS_VALUE_INLINE, CSS_VALUE_RUBY_TEXT, false, false, false},
        {CSS_VALUE_RUBY_BASE_CONTAINER, CSS_VALUE_INLINE, CSS_VALUE_RUBY_BASE_CONTAINER, false, false, false},
        {CSS_VALUE_RUBY_TEXT_CONTAINER, CSS_VALUE_INLINE, CSS_VALUE_RUBY_TEXT_CONTAINER, false, false, false},
        {CSS_VALUE_TABLE_ROW, CSS_VALUE_BLOCK, CSS_VALUE_TABLE_ROW, true, false, false},
        {CSS_VALUE_TABLE_CELL, CSS_VALUE_TABLE_CELL, CSS_VALUE_TABLE_CELL, true, false, false},
        {CSS_VALUE_TABLE_ROW_GROUP, CSS_VALUE_BLOCK, CSS_VALUE_TABLE_ROW_GROUP, true, false, false},
        {CSS_VALUE_TABLE_HEADER_GROUP, CSS_VALUE_BLOCK, CSS_VALUE_TABLE_HEADER_GROUP, true, false, false},
        {CSS_VALUE_TABLE_FOOTER_GROUP, CSS_VALUE_BLOCK, CSS_VALUE_TABLE_FOOTER_GROUP, true, false, false},
        {CSS_VALUE_TABLE_COLUMN, CSS_VALUE_BLOCK, CSS_VALUE_TABLE_COLUMN, true, false, false},
        {CSS_VALUE_TABLE_COLUMN_GROUP, CSS_VALUE_BLOCK, CSS_VALUE_TABLE_COLUMN_GROUP, true, false, false},
        {CSS_VALUE_TABLE_CAPTION, CSS_VALUE_BLOCK, CSS_VALUE_TABLE_CAPTION, true, false, false},
    };
    for (const CssDisplayKeywordSpec& spec : specs) {
        if (spec.keyword != keyword) continue;
        CssDisplayKeywordResult result = {
            {spec.outer, spec.replaced_inner && is_replaced
                ? RDT_DISPLAY_REPLACED :
                (spec.inner == CSS_VALUE_MATH && !is_mathml
                    ? (is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW)
                    : spec.inner)}, false, spec.blockify};
        result.display.list_item = spec.list_item;
        result.handled = true;
        return result;
    }
    return {{CSS_VALUE_BLOCK, CSS_VALUE_FLOW}, false, false};
}

static CssEnum css_display_list_inner(CssEnum keyword, bool is_replaced,
                                      bool is_mathml) {
    if (keyword == CSS_VALUE_FLOW) {
        return is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
    }
    if (keyword == CSS_VALUE_MATH && !is_mathml) {
        // CSS Display 3: non-MathML `math` computes to flow while replaced
        // elements retain their intrinsic replaced principal box.
        return is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
    }
    if (keyword == CSS_VALUE_FLOW_ROOT || keyword == CSS_VALUE_FLEX ||
        keyword == CSS_VALUE_GRID || keyword == CSS_VALUE_TABLE ||
        keyword == CSS_VALUE_RUBY ||
        (keyword == CSS_VALUE_MATH && is_mathml)) {
        return keyword;
    }
    return CSS_VALUE_FLOW;
}

static bool css_display_list_value(const CssValue* value, bool is_replaced,
                                   bool is_mathml,
                                   DisplayValue* out_display) {
    if (!value || value->type != CSS_VALUE_TYPE_LIST || !out_display) return false;
    CssValue** values = value->data.list.values;
    int count = value->data.list.count;
    bool has_list_item = false;
    CssEnum outer = CSS_VALUE__UNDEF;
    CssEnum inner = CSS_VALUE__UNDEF;
    for (int i = 0; i < count; i++) {
        if (!values[i] || values[i]->type != CSS_VALUE_TYPE_KEYWORD) continue;
        CssEnum keyword = values[i]->data.keyword;
        if (keyword == CSS_VALUE_LIST_ITEM) has_list_item = true;
        else if (keyword == CSS_VALUE_BLOCK || keyword == CSS_VALUE_INLINE ||
                 keyword == CSS_VALUE_RUN_IN) outer = keyword;
        else if (keyword == CSS_VALUE_FLOW || keyword == CSS_VALUE_FLOW_ROOT ||
                 keyword == CSS_VALUE_FLEX || keyword == CSS_VALUE_GRID ||
                 keyword == CSS_VALUE_TABLE || keyword == CSS_VALUE_RUBY ||
                 keyword == CSS_VALUE_MATH) {
            inner = keyword;
        }
    }

    if (has_list_item) {
        out_display->list_item = true;
        // CSS Display 3: `inline list-item` remains an inline-level principal
        // box; treating it as inline-block changes line participation and breaks
        // marker placement for every following sibling.
        out_display->outer = outer == CSS_VALUE_INLINE
            ? CSS_VALUE_INLINE : CSS_VALUE_LIST_ITEM;
        out_display->inner = inner == CSS_VALUE_FLOW_ROOT
            ? CSS_VALUE_FLOW_ROOT
            : (is_replaced && inner == CSS_VALUE_FLOW
                ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW);
        return true;
    }
    if (count >= 2 && outer != CSS_VALUE__UNDEF && inner != CSS_VALUE__UNDEF) {
        out_display->outer = outer == CSS_VALUE_INLINE
            ? CSS_VALUE_INLINE : CSS_VALUE_BLOCK;
        out_display->inner = css_display_list_inner(
            inner, is_replaced, is_mathml);
        return true;
    }
    if (count == 1 && values[0] && values[0]->type == CSS_VALUE_TYPE_KEYWORD) {
        CssDisplayKeywordResult result = css_display_keyword_result(
            values[0]->data.keyword, is_replaced, is_mathml);
        if (result.handled) {
            *out_display = result.display;
            return true;
        }
    }
    return false;
}

bool css_resolve_display_css_value(DomElement* element, const CssValue* value,
                                   DisplayValue* out_display) {
    if (!element || !value || !out_display) return false;
    if (value->type == CSS_VALUE_TYPE_CUSTOM &&
        css_display_legacy_keyword_supported(value->data.custom_property.name)) {
        *out_display = css_legacy_box_display(element,
            strcmp(value->data.custom_property.name, "-webkit-inline-box") == 0 ? 2 : 1);
        return true;
    }
    bool is_replaced = css_display_element_is_replaced(element);
    bool is_mathml = css_is_mathml_element(element);
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssDisplayKeywordResult result = css_display_keyword_result(
            value->data.keyword, is_replaced, is_mathml);
        if (!result.handled) return false;
        *out_display = result.display;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_LIST) {
        return css_display_list_value(value, is_replaced, is_mathml, out_display);
    }
    return false;
}

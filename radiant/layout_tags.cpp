#include "layout.hpp"

bool layout_tag_is_replaced_content(NameId tag) {
    switch (tag) {
        case MARKUP_NAME_IMG:
        case MARKUP_NAME_VIDEO:
        case MARKUP_NAME_IFRAME:
        case MARKUP_NAME_CANVAS:
        case MARKUP_NAME_SVG:
        case MARKUP_NAME_SCENE3D:
        case MARKUP_NAME_GEOMAP:
        case MARKUP_NAME_EMBED:
            return true;
        default:
            return false;
    }
}

bool layout_tag_is_replaced_widget(NameId tag) {
    switch (tag) {
        case MARKUP_NAME_INPUT:
        case MARKUP_NAME_SELECT:
        case MARKUP_NAME_TEXTAREA:
        case MARKUP_NAME_METER:
        case MARKUP_NAME_PROGRESS:
        case MARKUP_NAME_WEBVIEW:
        case MARKUP_NAME_HR:
            return true;
        default:
            return false;
    }
}

bool layout_tag_is_css_replaced(NameId tag) {
    return tag == MARKUP_NAME_IMG || tag == MARKUP_NAME_VIDEO ||
        tag == MARKUP_NAME_INPUT || tag == MARKUP_NAME_SELECT ||
        tag == MARKUP_NAME_TEXTAREA || tag == MARKUP_NAME_IFRAME ||
        tag == MARKUP_NAME_HR || tag == MARKUP_NAME_METER ||
        tag == MARKUP_NAME_PROGRESS || tag == MARKUP_NAME_CANVAS ||
        tag == MARKUP_NAME_WEBVIEW || tag == MARKUP_NAME_EMBED ||
        tag == MARKUP_NAME_SCENE3D || tag == MARKUP_NAME_GEOMAP;
}

bool layout_tag_is_non_caret_container(NameId tag, bool include_button) {
    if (tag == MARKUP_NAME_BR || tag == MARKUP_NAME_HR ||
        layout_tag_is_replaced_content(tag) ||
        tag == MARKUP_NAME_INPUT || tag == MARKUP_NAME_SELECT ||
        tag == MARKUP_NAME_TEXTAREA || tag == MARKUP_NAME_OBJECT ||
        tag == MARKUP_NAME_AUDIO) return true;
    return include_button && tag == MARKUP_NAME_BUTTON;
}

bool layout_tag_is_default_inline(NameId tag) {
    static const NameId inline_tags[] = {
        MARKUP_NAME_A, MARKUP_NAME_SPAN, MARKUP_NAME_EM, MARKUP_NAME_STRONG,
        MARKUP_NAME_B, MARKUP_NAME_I, MARKUP_NAME_U, MARKUP_NAME_S,
        MARKUP_NAME_SMALL, MARKUP_NAME_BIG, MARKUP_NAME_SUB, MARKUP_NAME_SUP,
        MARKUP_NAME_ABBR, MARKUP_NAME_ACRONYM, MARKUP_NAME_CITE, MARKUP_NAME_DFN,
        MARKUP_NAME_Q, MARKUP_NAME_VAR, MARKUP_NAME_TIME, MARKUP_NAME_MARK,
        MARKUP_NAME_BDI, MARKUP_NAME_BDO, MARKUP_NAME_CODE, MARKUP_NAME_TT,
        MARKUP_NAME_KBD, MARKUP_NAME_SAMP, MARKUP_NAME_BR, MARKUP_NAME_LABEL,
        MARKUP_NAME_IMG, MARKUP_NAME_VIDEO, MARKUP_NAME_AUDIO, MARKUP_NAME_CANVAS,
        MARKUP_NAME_IFRAME, MARKUP_NAME_EMBED, MARKUP_NAME_OBJECT, MARKUP_NAME_SVG, MARKUP_NAME_SCENE3D, MARKUP_NAME_GEOMAP,
        MARKUP_NAME_METER, MARKUP_NAME_PROGRESS, MARKUP_NAME_BUTTON,
        MARKUP_NAME_INPUT, MARKUP_NAME_SELECT, MARKUP_NAME_TEXTAREA
    };
    // custom HTML elements have no UA display override and retain initial inline.
    return tag == NAME_ID_NONE || layout_tag_in_list(tag, inline_tags,
                              sizeof(inline_tags) / sizeof(*inline_tags));
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

bool is_table_internal_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_CELL ||
           display == CSS_VALUE_TABLE_ROW ||
           display == CSS_VALUE_TABLE_ROW_GROUP ||
           display == CSS_VALUE_TABLE_HEADER_GROUP ||
           display == CSS_VALUE_TABLE_FOOTER_GROUP ||
           display == CSS_VALUE_TABLE_COLUMN ||
           display == CSS_VALUE_TABLE_COLUMN_GROUP ||
           display == CSS_VALUE_TABLE_CAPTION;
}

bool view_size_containment_applies(DisplayValue display) {
    if (display.outer == CSS_VALUE_NONE || display.outer == CSS_VALUE_CONTENTS ||
        (display.outer == CSS_VALUE_INLINE &&
            (display.inner == CSS_VALUE_FLOW || display.inner == CSS_VALUE_RUBY))) return false;
    if (display.inner == CSS_VALUE_TABLE || (is_table_internal_display(display.inner) &&
        display.inner != CSS_VALUE_TABLE_CAPTION)) return false;
    return display.inner != CSS_VALUE_RUBY_BASE && display.inner != CSS_VALUE_RUBY_TEXT &&
        display.inner != CSS_VALUE_RUBY_BASE_CONTAINER && display.inner != CSS_VALUE_RUBY_TEXT_CONTAINER;
}

bool view_contents_are_skipped(const View* view) {
    const DomElement* element = view && view->is_element() ? view->as_element() : nullptr;
    return element && element->blk && element->block()->content_visibility_skipped &&
        view_size_containment_applies(element->display);
}

bool view_content_visibility_hides(DomElement* element, ContentVisibilityCheck check,
                                    bool include_self) {
    // the containing element remains available; only its flat-tree contents are hidden.
    for (DomElement* parent = include_self ? element : element ? dom_flat_tree_parent(element) : nullptr;
         parent; parent = dom_flat_tree_parent(parent)) {
        bool hidden = parent->blk && (check == CONTENT_VISIBILITY_USER_AGENT
            ? parent->block()->content_visibility == CSS_VALUE_HIDDEN
            : parent->block()->content_visibility_skipped);
        if (hidden && view_size_containment_applies(parent->display)) return true;
    }
    return false;
}

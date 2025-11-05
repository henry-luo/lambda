#include "dom.hpp"

extern "C" {
#include "../lambda/input/css/dom_element.h"
#include "../lambda/element_reader.h"
}

// DomNode member function implementations

char* DomNode::name() {
    if (type == MARK_ELEMENT && dom_element) {
        return (char*)dom_element->tag_name;
    }
    else if (type == MARK_TEXT && dom_text) {
        return (char*)"#text";
    }
    else if (type == MARK_COMMENT && dom_comment) {
        return (char*)"#comment";
    }
    // debug: log what went wrong
    log_error("[DOM] #null node detected - type=%d, dom_element=%p, dom_text=%p, this=%p",
            type, (void*)dom_element, (void*)dom_text, (void*)this);
    return (char*)"#null";
}

unsigned char* DomNode::text_data() {
    if (type == MARK_TEXT && dom_text) {
        return (unsigned char*)dom_text->text;
    }
    return nullptr;
}

uintptr_t DomNode::tag() {
    if (type == MARK_ELEMENT && dom_element) {
        // Convert Lambda CSS element tag name to Lexbor tag enum
        return tag_name_to_lexbor_id(dom_element->tag_name);
    }
    return 0;
}

// Helper function to convert tag name string to Lexbor tag ID
uintptr_t DomNode::tag_name_to_lexbor_id(const char* tag_name) {
    if (!tag_name) return 0;

    // Use case-insensitive comparison for HTML tags
    // Map common HTML tags to their Lexbor constants
    if (strcasecmp(tag_name, "img") == 0) return LXB_TAG_IMG;
    if (strcasecmp(tag_name, "div") == 0) return LXB_TAG_DIV;
    if (strcasecmp(tag_name, "span") == 0) return LXB_TAG_SPAN;
    if (strcasecmp(tag_name, "p") == 0) return LXB_TAG_P;
    if (strcasecmp(tag_name, "h1") == 0) return LXB_TAG_H1;
    if (strcasecmp(tag_name, "h2") == 0) return LXB_TAG_H2;
    if (strcasecmp(tag_name, "h3") == 0) return LXB_TAG_H3;
    if (strcasecmp(tag_name, "h4") == 0) return LXB_TAG_H4;
    if (strcasecmp(tag_name, "h5") == 0) return LXB_TAG_H5;
    if (strcasecmp(tag_name, "h6") == 0) return LXB_TAG_H6;
    if (strcasecmp(tag_name, "a") == 0) return LXB_TAG_A;
    if (strcasecmp(tag_name, "body") == 0) return LXB_TAG_BODY;
    if (strcasecmp(tag_name, "head") == 0) return LXB_TAG_HEAD;
    if (strcasecmp(tag_name, "html") == 0) return LXB_TAG_HTML;
    if (strcasecmp(tag_name, "title") == 0) return LXB_TAG_TITLE;
    if (strcasecmp(tag_name, "meta") == 0) return LXB_TAG_META;
    if (strcasecmp(tag_name, "link") == 0) return LXB_TAG_LINK;
    if (strcasecmp(tag_name, "style") == 0) return LXB_TAG_STYLE;
    if (strcasecmp(tag_name, "script") == 0) return LXB_TAG_SCRIPT;
    if (strcasecmp(tag_name, "br") == 0) return LXB_TAG_BR;
    if (strcasecmp(tag_name, "hr") == 0) return LXB_TAG_HR;
    if (strcasecmp(tag_name, "ul") == 0) return LXB_TAG_UL;
    if (strcasecmp(tag_name, "ol") == 0) return LXB_TAG_OL;
    if (strcasecmp(tag_name, "li") == 0) return LXB_TAG_LI;
    if (strcasecmp(tag_name, "table") == 0) return LXB_TAG_TABLE;
    if (strcasecmp(tag_name, "tr") == 0) return LXB_TAG_TR;
    if (strcasecmp(tag_name, "td") == 0) return LXB_TAG_TD;
    if (strcasecmp(tag_name, "th") == 0) return LXB_TAG_TH;
    if (strcasecmp(tag_name, "thead") == 0) return LXB_TAG_THEAD;
    if (strcasecmp(tag_name, "tbody") == 0) return LXB_TAG_TBODY;
    if (strcasecmp(tag_name, "tfoot") == 0) return LXB_TAG_TFOOT;
    if (strcasecmp(tag_name, "form") == 0) return LXB_TAG_FORM;
    if (strcasecmp(tag_name, "input") == 0) return LXB_TAG_INPUT;
    if (strcasecmp(tag_name, "button") == 0) return LXB_TAG_BUTTON;
    if (strcasecmp(tag_name, "select") == 0) return LXB_TAG_SELECT;
    if (strcasecmp(tag_name, "option") == 0) return LXB_TAG_OPTION;
    if (strcasecmp(tag_name, "textarea") == 0) return LXB_TAG_TEXTAREA;
    if (strcasecmp(tag_name, "label") == 0) return LXB_TAG_LABEL;
    if (strcasecmp(tag_name, "fieldset") == 0) return LXB_TAG_FIELDSET;
    if (strcasecmp(tag_name, "legend") == 0) return LXB_TAG_LEGEND;
    if (strcasecmp(tag_name, "iframe") == 0) return LXB_TAG_IFRAME;
    if (strcasecmp(tag_name, "embed") == 0) return LXB_TAG_EMBED;
    if (strcasecmp(tag_name, "object") == 0) return LXB_TAG_OBJECT;
    if (strcasecmp(tag_name, "param") == 0) return LXB_TAG_PARAM;
    if (strcasecmp(tag_name, "video") == 0) return LXB_TAG_VIDEO;
    if (strcasecmp(tag_name, "audio") == 0) return LXB_TAG_AUDIO;
    if (strcasecmp(tag_name, "source") == 0) return LXB_TAG_SOURCE;
    if (strcasecmp(tag_name, "track") == 0) return LXB_TAG_TRACK;
    if (strcasecmp(tag_name, "canvas") == 0) return LXB_TAG_CANVAS;
    if (strcasecmp(tag_name, "svg") == 0) return LXB_TAG_SVG;
    if (strcasecmp(tag_name, "lineargradient") == 0) return LXB_TAG_LINEARGRADIENT;
    if (strcasecmp(tag_name, "radialgradient") == 0) return LXB_TAG_RADIALGRADIENT;
    if (strcasecmp(tag_name, "animatetransform") == 0) return LXB_TAG_ANIMATETRANSFORM;
    if (strcasecmp(tag_name, "animatemotion") == 0) return LXB_TAG_ANIMATEMOTION;
    if (strcasecmp(tag_name, "strong") == 0) return LXB_TAG_STRONG;
    if (strcasecmp(tag_name, "em") == 0) return LXB_TAG_EM;
    if (strcasecmp(tag_name, "b") == 0) return LXB_TAG_B;
    if (strcasecmp(tag_name, "i") == 0) return LXB_TAG_I;
    if (strcasecmp(tag_name, "u") == 0) return LXB_TAG_U;
    if (strcasecmp(tag_name, "s") == 0) return LXB_TAG_S;
    if (strcasecmp(tag_name, "small") == 0) return LXB_TAG_SMALL;
    if (strcasecmp(tag_name, "mark") == 0) return LXB_TAG_MARK;
    if (strcasecmp(tag_name, "del") == 0) return LXB_TAG_DEL;
    if (strcasecmp(tag_name, "ins") == 0) return LXB_TAG_INS;
    if (strcasecmp(tag_name, "sub") == 0) return LXB_TAG_SUB;
    if (strcasecmp(tag_name, "sup") == 0) return LXB_TAG_SUP;
    if (strcasecmp(tag_name, "q") == 0) return LXB_TAG_Q;
    if (strcasecmp(tag_name, "cite") == 0) return LXB_TAG_CITE;
    if (strcasecmp(tag_name, "abbr") == 0) return LXB_TAG_ABBR;
    if (strcasecmp(tag_name, "dfn") == 0) return LXB_TAG_DFN;
    if (strcasecmp(tag_name, "time") == 0) return LXB_TAG_TIME;
    if (strcasecmp(tag_name, "code") == 0) return LXB_TAG_CODE;
    if (strcasecmp(tag_name, "var") == 0) return LXB_TAG_VAR;
    if (strcasecmp(tag_name, "samp") == 0) return LXB_TAG_SAMP;
    if (strcasecmp(tag_name, "kbd") == 0) return LXB_TAG_KBD;
    if (strcasecmp(tag_name, "address") == 0) return LXB_TAG_ADDRESS;
    if (strcasecmp(tag_name, "main") == 0) return LXB_TAG_MAIN;
    if (strcasecmp(tag_name, "section") == 0) return LXB_TAG_SECTION;
    if (strcasecmp(tag_name, "article") == 0) return LXB_TAG_ARTICLE;
    if (strcasecmp(tag_name, "aside") == 0) return LXB_TAG_ASIDE;
    if (strcasecmp(tag_name, "nav") == 0) return LXB_TAG_NAV;
    if (strcasecmp(tag_name, "header") == 0) return LXB_TAG_HEADER;
    if (strcasecmp(tag_name, "footer") == 0) return LXB_TAG_FOOTER;
    if (strcasecmp(tag_name, "hgroup") == 0) return LXB_TAG_HGROUP;
    if (strcasecmp(tag_name, "figure") == 0) return LXB_TAG_FIGURE;
    if (strcasecmp(tag_name, "figcaption") == 0) return LXB_TAG_FIGCAPTION;
    if (strcasecmp(tag_name, "details") == 0) return LXB_TAG_DETAILS;
    if (strcasecmp(tag_name, "summary") == 0) return LXB_TAG_SUMMARY;
    if (strcasecmp(tag_name, "dialog") == 0) return LXB_TAG_DIALOG;
    if (strcasecmp(tag_name, "data") == 0) return LXB_TAG_DATA;
    if (strcasecmp(tag_name, "output") == 0) return LXB_TAG_OUTPUT;
    if (strcasecmp(tag_name, "progress") == 0) return LXB_TAG_PROGRESS;
    if (strcasecmp(tag_name, "meter") == 0) return LXB_TAG_METER;
    if (strcasecmp(tag_name, "menu") == 0) return LXB_TAG_MENU;
    if (strcasecmp(tag_name, "center") == 0) return LXB_TAG_CENTER;
    if (strcasecmp(tag_name, "pre") == 0) return LXB_TAG_PRE;
    if (strcasecmp(tag_name, "blockquote") == 0) return LXB_TAG_BLOCKQUOTE;
    if (strcasecmp(tag_name, "dd") == 0) return LXB_TAG_DD;
    if (strcasecmp(tag_name, "dt") == 0) return LXB_TAG_DT;
    if (strcasecmp(tag_name, "dl") == 0) return LXB_TAG_DL;

    // For unknown tags, return 0 (similar to Lexbor's LXB_TAG__UNDEF behavior)
    return 0;
}

const lxb_char_t* DomNode::get_attribute(const char* attr_name, size_t* value_len) {
    if (type == MARK_ELEMENT && dom_element) {
        // Use DOM element's attribute system
        const char* value = dom_element_get_attribute(dom_element, attr_name);
        if (value) {
            if (value_len) *value_len = strlen(value);
            return (const lxb_char_t*)value;
        }
    }
    return nullptr;
}

DomNode* DomNode::first_child() {
    if (_child) {
        fprintf(stderr, "[DOM DEBUG] Returning cached first_child - parent_type=%d, child_type=%d, child_ptr=%p\n",
                this->type, _child->type, _child->dom_element);
        return _child;
    }
    // Handle mark elements (now using DomElement)
    if (type == MARK_ELEMENT && dom_element) {
        // Navigate to first child through DomElement tree
        void* first = dom_element->first_child;

        // Skip comment nodes
        while (first && dom_node_get_type(first) == DOM_NODE_COMMENT) {
            if (dom_node_get_type(first) == DOM_NODE_ELEMENT) {
                first = ((DomElement*)first)->next_sibling;
            } else if (dom_node_get_type(first) == DOM_NODE_TEXT) {
                first = ((DomText*)first)->next_sibling;
            } else {
                first = ((DomComment*)first)->next_sibling;
            }
        }

        if (first) {
            DomNode* child_node = nullptr;
            DomNodeType node_type = dom_node_get_type(first);

            if (node_type == DOM_NODE_ELEMENT) {
                child_node = create_mark_element((DomElement*)first);
            } else if (node_type == DOM_NODE_TEXT) {
                child_node = create_mark_text((DomText*)first);
            }
            // Comments are skipped, so we don't create nodes for them

            if (child_node) {
                child_node->parent = this;
                child_node->style = (Style*)first;  // Link to DomElement/DomText for CSS
                fprintf(stderr, "[DOM DEBUG] Caching first_child - parent_type=%d, child_type=%d, child_ptr=%p\n",
                        this->type, child_node->type, child_node->dom_element);
                this->_child = child_node;
                return child_node;
            }
        }
    }
    return NULL;
}

DomNode* DomNode::next_sibling() {
    if (_next) {
        fprintf(stderr, "[DOM DEBUG] Returning cached next_sibling - parent_type=%d, sibling_type=%d, sibling_ptr=%p\n",
                this->type, _next->type, _next->dom_element);
        return _next;
    }

    // handle mark nodes (now using DomElement/DomText)
    if (type == MARK_ELEMENT || type == MARK_TEXT) {
        // Get next sibling from DomElement/DomText structure
        void* next = nullptr;
        if (type == MARK_ELEMENT && dom_element) {
            next = dom_element->next_sibling;
        } else if (type == MARK_TEXT && dom_text) {
            next = dom_text->next_sibling;
        }

        // Skip comment nodes
        while (next && dom_node_get_type(next) == DOM_NODE_COMMENT) {
            if (dom_node_get_type(next) == DOM_NODE_ELEMENT) {
                next = ((DomElement*)next)->next_sibling;
            } else if (dom_node_get_type(next) == DOM_NODE_TEXT) {
                next = ((DomText*)next)->next_sibling;
            } else {
                next = ((DomComment*)next)->next_sibling;
            }
        }

        if (next) {
            DomNode* sibling_node = nullptr;
            DomNodeType node_type = dom_node_get_type(next);

            if (node_type == DOM_NODE_ELEMENT) {
                sibling_node = create_mark_element((DomElement*)next);
            } else if (node_type == DOM_NODE_TEXT) {
                sibling_node = create_mark_text((DomText*)next);
            }
            // Comments are skipped

            if (sibling_node) {
                sibling_node->parent = parent;
                sibling_node->style = (Style*)next;  // Link to DomElement/DomText for CSS
                fprintf(stderr, "[DOM DEBUG] Caching next_sibling - parent_type=%d, sibling_type=%d, sibling_ptr=%p\n",
                        this->type, sibling_node->type, sibling_node->dom_element);
                this->_next = sibling_node;
                return sibling_node;
            }
        }
    }
    return NULL;
}

// Mark-specific method implementations

char* DomNode::mark_text_data() {
    if (type == MARK_TEXT && dom_text) {
        return (char*)dom_text->text;
    }
    return nullptr;
}

Item DomNode::mark_get_attribute(const char* attr_name) {
    if (type != MARK_ELEMENT || !dom_element) {
        return ItemNull;
    }

    // Use DOM element's attribute system
    const char* value = dom_element_get_attribute(dom_element, attr_name);
    if (value) {
        // Return as C string pointer wrapped in Item
        // Store as raw_pointer since pointer field is only 56-bit
        Item result;
        result.type_id = LMD_TYPE_STRING;
        result.raw_pointer = (void*)value;
        return result;
    }

    // Attribute not found - return item with no type set
    Item not_found;
    not_found.item = 0;
    not_found.type_id = 0;
    return not_found;
}

Item DomNode::mark_get_content() {
    if (type != MARK_ELEMENT || !dom_element) {
        return ItemNull;
    }

    // For DomElement, we could return a list of children, but that requires
    // traversing the DOM tree. For now, return NULL.
    // This method may need redesign for the new DomElement-based structure.
    return ItemNull;
}

// Static factory methods for creating mark nodes

DomNode* DomNode::create_mark_element(DomElement* element) {
    if (!element) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_ELEMENT;
    node->dom_element = element;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    fprintf(stderr, "[DOM DEBUG] create_mark_element - created node %p, type=%d, dom_element=%p, tag=%s\n",
            (void*)node, node->type, (void*)node->dom_element, element->tag_name);

    return node;
}

DomNode* DomNode::create_mark_text(DomText* text) {
    if (!text) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_TEXT;
    node->dom_text = text;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    return node;
}

DomNode* DomNode::create_mark_comment(DomComment* comment) {
    if (!comment) return nullptr;

    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_COMMENT;
    node->dom_comment = comment;
    node->style = nullptr;
    node->parent = nullptr;
    node->_next = nullptr;
    node->_child = nullptr;

    return node;
}

// Free DomNode tree recursively
void DomNode::free_tree(DomNode* node) {
    if (!node) return;

    // Free cached child and sibling nodes recursively
    if (node->_child) {
        free_tree(node->_child);
    }

    if (node->_next) {
        free_tree(node->_next);
    }

    // Note: We don't free mark_element, mark_text, lxb_elmt, or lxb_node
    // as they are managed by their respective systems (Pool or Lexbor)

    // Free the DomNode wrapper itself
    free(node);
}

// Clean up cached children for stack-allocated root nodes
void DomNode::free_cached_children() {
    if (_child) {
        free_tree(_child);
        _child = nullptr;
    }
    // Note: We don't free _next for root nodes as root typically has no siblings
}

#include "dom_node.hpp"
#include "dom_element.hpp"
#include "css_formatter.hpp"
#include "css_style_node.hpp"
#include "../../../lib/log.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/stringbuf.h"
#include "../../../lib/string.h"
#include "../../../radiant/view.hpp"  // For HTM_TAG_* constants
/**
 * Convert HTML tag name string to Lexbor tag ID
 * This is called once during element creation to populate the tag_id field
 */
uintptr_t DomNode::tag_name_to_id(const char* tag_name) {
    if (!tag_name) return 0;

    // Use case-insensitive comparison for HTML tags
    // Map common HTML tags to their Lexbor constants
    if (strcasecmp(tag_name, "img") == 0) return HTM_TAG_IMG;
    if (strcasecmp(tag_name, "div") == 0) return HTM_TAG_DIV;
    if (strcasecmp(tag_name, "span") == 0) return HTM_TAG_SPAN;
    if (strcasecmp(tag_name, "p") == 0) return HTM_TAG_P;
    if (strcasecmp(tag_name, "h1") == 0) return HTM_TAG_H1;
    if (strcasecmp(tag_name, "h2") == 0) return HTM_TAG_H2;
    if (strcasecmp(tag_name, "h3") == 0) return HTM_TAG_H3;
    if (strcasecmp(tag_name, "h4") == 0) return HTM_TAG_H4;
    if (strcasecmp(tag_name, "h5") == 0) return HTM_TAG_H5;
    if (strcasecmp(tag_name, "h6") == 0) return HTM_TAG_H6;
    if (strcasecmp(tag_name, "a") == 0) return HTM_TAG_A;
    if (strcasecmp(tag_name, "body") == 0) return HTM_TAG_BODY;
    if (strcasecmp(tag_name, "head") == 0) return HTM_TAG_HEAD;
    if (strcasecmp(tag_name, "html") == 0) return HTM_TAG_HTML;
    if (strcasecmp(tag_name, "title") == 0) return HTM_TAG_TITLE;
    if (strcasecmp(tag_name, "meta") == 0) return HTM_TAG_META;
    if (strcasecmp(tag_name, "link") == 0) return HTM_TAG_LINK;
    if (strcasecmp(tag_name, "style") == 0) return HTM_TAG_STYLE;
    if (strcasecmp(tag_name, "script") == 0) return HTM_TAG_SCRIPT;
    if (strcasecmp(tag_name, "br") == 0) return HTM_TAG_BR;
    if (strcasecmp(tag_name, "hr") == 0) return HTM_TAG_HR;
    if (strcasecmp(tag_name, "ul") == 0) return HTM_TAG_UL;
    if (strcasecmp(tag_name, "ol") == 0) return HTM_TAG_OL;
    if (strcasecmp(tag_name, "li") == 0) return HTM_TAG_LI;
    if (strcasecmp(tag_name, "table") == 0) return HTM_TAG_TABLE;
    if (strcasecmp(tag_name, "tr") == 0) return HTM_TAG_TR;
    if (strcasecmp(tag_name, "td") == 0) return HTM_TAG_TD;
    if (strcasecmp(tag_name, "th") == 0) return HTM_TAG_TH;
    if (strcasecmp(tag_name, "thead") == 0) return HTM_TAG_THEAD;
    if (strcasecmp(tag_name, "tbody") == 0) return HTM_TAG_TBODY;
    if (strcasecmp(tag_name, "tfoot") == 0) return HTM_TAG_TFOOT;
    if (strcasecmp(tag_name, "caption") == 0) return HTM_TAG_CAPTION;
    if (strcasecmp(tag_name, "colgroup") == 0) return HTM_TAG_COLGROUP;
    if (strcasecmp(tag_name, "col") == 0) return HTM_TAG_COL;
    if (strcasecmp(tag_name, "form") == 0) return HTM_TAG_FORM;
    if (strcasecmp(tag_name, "input") == 0) return HTM_TAG_INPUT;
    if (strcasecmp(tag_name, "button") == 0) return HTM_TAG_BUTTON;
    if (strcasecmp(tag_name, "select") == 0) return HTM_TAG_SELECT;
    if (strcasecmp(tag_name, "option") == 0) return HTM_TAG_OPTION;
    if (strcasecmp(tag_name, "textarea") == 0) return HTM_TAG_TEXTAREA;
    if (strcasecmp(tag_name, "label") == 0) return HTM_TAG_LABEL;
    if (strcasecmp(tag_name, "fieldset") == 0) return HTM_TAG_FIELDSET;
    if (strcasecmp(tag_name, "legend") == 0) return HTM_TAG_LEGEND;
    if (strcasecmp(tag_name, "iframe") == 0) return HTM_TAG_IFRAME;
    if (strcasecmp(tag_name, "embed") == 0) return HTM_TAG_EMBED;
    if (strcasecmp(tag_name, "object") == 0) return HTM_TAG_OBJECT;
    if (strcasecmp(tag_name, "param") == 0) return HTM_TAG_PARAM;
    if (strcasecmp(tag_name, "video") == 0) return HTM_TAG_VIDEO;
    if (strcasecmp(tag_name, "audio") == 0) return HTM_TAG_AUDIO;
    if (strcasecmp(tag_name, "source") == 0) return HTM_TAG_SOURCE;
    if (strcasecmp(tag_name, "track") == 0) return HTM_TAG_TRACK;
    if (strcasecmp(tag_name, "canvas") == 0) return HTM_TAG_CANVAS;
    if (strcasecmp(tag_name, "svg") == 0) return HTM_TAG_SVG;
    if (strcasecmp(tag_name, "lineargradient") == 0) return HTM_TAG_LINEARGRADIENT;
    if (strcasecmp(tag_name, "radialgradient") == 0) return HTM_TAG_RADIALGRADIENT;
    if (strcasecmp(tag_name, "animatetransform") == 0) return HTM_TAG_ANIMATETRANSFORM;
    if (strcasecmp(tag_name, "animatemotion") == 0) return HTM_TAG_ANIMATEMOTION;
    if (strcasecmp(tag_name, "strong") == 0) return HTM_TAG_STRONG;
    if (strcasecmp(tag_name, "em") == 0) return HTM_TAG_EM;
    if (strcasecmp(tag_name, "b") == 0) return HTM_TAG_B;
    if (strcasecmp(tag_name, "i") == 0) return HTM_TAG_I;
    if (strcasecmp(tag_name, "u") == 0) return HTM_TAG_U;
    if (strcasecmp(tag_name, "s") == 0) return HTM_TAG_S;
    if (strcasecmp(tag_name, "small") == 0) return HTM_TAG_SMALL;
    if (strcasecmp(tag_name, "mark") == 0) return HTM_TAG_MARK;
    if (strcasecmp(tag_name, "del") == 0) return HTM_TAG_DEL;
    if (strcasecmp(tag_name, "ins") == 0) return HTM_TAG_INS;
    if (strcasecmp(tag_name, "sub") == 0) return HTM_TAG_SUB;
    if (strcasecmp(tag_name, "sup") == 0) return HTM_TAG_SUP;
    if (strcasecmp(tag_name, "q") == 0) return HTM_TAG_Q;
    if (strcasecmp(tag_name, "cite") == 0) return HTM_TAG_CITE;
    if (strcasecmp(tag_name, "abbr") == 0) return HTM_TAG_ABBR;
    if (strcasecmp(tag_name, "dfn") == 0) return HTM_TAG_DFN;
    if (strcasecmp(tag_name, "time") == 0) return HTM_TAG_TIME;
    if (strcasecmp(tag_name, "code") == 0) return HTM_TAG_CODE;
    if (strcasecmp(tag_name, "var") == 0) return HTM_TAG_VAR;
    if (strcasecmp(tag_name, "samp") == 0) return HTM_TAG_SAMP;
    if (strcasecmp(tag_name, "kbd") == 0) return HTM_TAG_KBD;
    if (strcasecmp(tag_name, "address") == 0) return HTM_TAG_ADDRESS;
    if (strcasecmp(tag_name, "main") == 0) return HTM_TAG_MAIN;
    if (strcasecmp(tag_name, "section") == 0) return HTM_TAG_SECTION;
    if (strcasecmp(tag_name, "article") == 0) return HTM_TAG_ARTICLE;
    if (strcasecmp(tag_name, "aside") == 0) return HTM_TAG_ASIDE;
    if (strcasecmp(tag_name, "nav") == 0) return HTM_TAG_NAV;
    if (strcasecmp(tag_name, "header") == 0) return HTM_TAG_HEADER;
    if (strcasecmp(tag_name, "footer") == 0) return HTM_TAG_FOOTER;
    if (strcasecmp(tag_name, "hgroup") == 0) return HTM_TAG_HGROUP;
    if (strcasecmp(tag_name, "figure") == 0) return HTM_TAG_FIGURE;
    if (strcasecmp(tag_name, "figcaption") == 0) return HTM_TAG_FIGCAPTION;
    if (strcasecmp(tag_name, "details") == 0) return HTM_TAG_DETAILS;
    if (strcasecmp(tag_name, "summary") == 0) return HTM_TAG_SUMMARY;
    if (strcasecmp(tag_name, "dialog") == 0) return HTM_TAG_DIALOG;
    if (strcasecmp(tag_name, "data") == 0) return HTM_TAG_DATA;
    if (strcasecmp(tag_name, "output") == 0) return HTM_TAG_OUTPUT;
    if (strcasecmp(tag_name, "progress") == 0) return HTM_TAG_PROGRESS;
    if (strcasecmp(tag_name, "meter") == 0) return HTM_TAG_METER;
    if (strcasecmp(tag_name, "menu") == 0) return HTM_TAG_MENU;
    if (strcasecmp(tag_name, "center") == 0) return HTM_TAG_CENTER;
    if (strcasecmp(tag_name, "pre") == 0) return HTM_TAG_PRE;
    if (strcasecmp(tag_name, "blockquote") == 0) return HTM_TAG_BLOCKQUOTE;
    if (strcasecmp(tag_name, "dd") == 0) return HTM_TAG_DD;
    if (strcasecmp(tag_name, "dt") == 0) return HTM_TAG_DT;
    if (strcasecmp(tag_name, "dl") == 0) return HTM_TAG_DL;

    // For unknown tags, return 0 (similar to Lexbor's HTM_TAG__UNDEF behavior)
    return 0;
}

// Get tag ID for element nodes
uintptr_t DomNode::tag() const {
    const DomElement* elem = as_element();
    return elem ? elem->tag_id : 0;
}

// Get text content for text nodes
unsigned char* DomNode::text_data() const {
    const DomText* text = as_text();
    return text ? (unsigned char*)text->text : nullptr;
}

const char* DomNode::get_attribute(const char* attr_name) const {
    const DomElement* elem = as_element();
    return elem ? dom_element_get_attribute(const_cast<DomElement*>(elem), attr_name) : nullptr;
}

const char* DomNode::node_name() const {
    // Dispatch based on node type
    switch (node_type) {
        case DOM_NODE_ELEMENT: {
            const DomElement* elem = static_cast<const DomElement*>(this);
            return elem->tag_name ? elem->tag_name : "#unnamed";
        }
        case DOM_NODE_TEXT:
            return "#text";
        case DOM_NODE_COMMENT:
        case DOM_NODE_DOCTYPE: {
            const DomComment* comment = static_cast<const DomComment*>(this);
            return comment->tag_name ? comment->tag_name : "#comment";
        }
        case DOM_NODE_DOCUMENT:
            return "#document";
        default:
            return "#unknown";
    }
}

// ============================================================================
// Tree Manipulation Implementation
// ============================================================================

bool DomNode::append_child(DomNode* child) {
    if (!child) {
        log_error("DomNode::append_child: NULL child");
        return false;
    }

    // Only elements can have children
    if (!this->is_element()) {
        log_error("DomNode::append_child: Parent is not an element");
        return false;
    }

    // Set parent relationship
    child->parent = this;

    // Cast to DomElement to access first_child
    DomElement* element = static_cast<DomElement*>(this);

    // Add to parent's child list
    if (!element->first_child) {
        // First child
        element->first_child = child;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
    } else {
        // Find last child
        DomNode* last = element->first_child;
        while (last->next_sibling) {
            last = last->next_sibling;
        }

        // Append after last child
        last->next_sibling = child;
        child->prev_sibling = last;
        child->next_sibling = nullptr;
    }

    return true;
}

bool DomNode::remove_child(DomNode* child) {
    if (!child) {
        return false;
    }

    // Only elements can have children
    if (!this->is_element()) {
        log_error("DomNode::remove_child: Parent is not an element");
        return false;
    }

    // Verify parent relationship
    if (child->parent != this) {
        log_error("DomNode::remove_child: Child does not belong to this parent");
        return false;
    }

    // Cast to DomElement to access first_child
    DomElement* element = static_cast<DomElement*>(this);

    // Update sibling links
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        // Child was first child
        element->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    }

    // Clear child's relationships
    child->parent = nullptr;
    child->prev_sibling = nullptr;
    child->next_sibling = nullptr;

    return true;
}

bool DomNode::insert_before(DomNode* new_node, DomNode* ref_node) {
    if (!new_node) {
        return false;
    }

    // Only elements can have children
    if (!this->is_element()) {
        log_error("DomNode::insert_before: Parent is not an element");
        return false;
    }

    // If no reference node, append at end
    if (!ref_node) {
        return this->append_child(new_node);
    }

    // Verify reference node is a child of this parent
    if (ref_node->parent != this) {
        log_error("DomNode::insert_before: Reference node is not a child of this parent");
        return false;
    }

    // Cast to DomElement to access first_child
    DomElement* element = static_cast<DomElement*>(this);

    // Set parent relationship
    new_node->parent = this;

    // Insert before reference node
    new_node->next_sibling = ref_node;
    new_node->prev_sibling = ref_node->prev_sibling;

    if (ref_node->prev_sibling) {
        ref_node->prev_sibling->next_sibling = new_node;
    } else {
        // Reference node was first child
        element->first_child = new_node;
    }

    ref_node->prev_sibling = new_node;

    return true;
}

// ============================================================================
// Utility Methods Implementation
// ============================================================================

/**
 * Context for style property printing callback
 */
typedef struct {
    StrBuf* buf;
    bool* has_props;
} StylePrintContext;

/**
 * Callback function for printing each style property
 * Called by style_tree_foreach for each property in the style tree
 */
static bool print_style_property_callback(StyleNode* node, void* context) {
    if (!node || !node->winning_decl || !context) {
        return true;  // continue iteration
    }

    StylePrintContext* ctx = (StylePrintContext*)context;
    CssDeclaration* decl = node->winning_decl;

    if (!decl->value) {
        return true;  // skip properties without values
    }

    // Add comma separator if not first property
    if (*ctx->has_props) {
        strbuf_append_str(ctx->buf, ", ");
    }

    // Get property name using the property database
    const char* prop_name = css_get_property_name(decl->property_id);

    if (!prop_name) {
        // If no name available, print with property ID for debugging
        char prop_id_buf[32];
        snprintf(prop_id_buf, sizeof(prop_id_buf), "property-%d", (int)decl->property_id);
        strbuf_append_str(ctx->buf, prop_id_buf);
    } else {
        // Print property name
        strbuf_append_str(ctx->buf, prop_name);
    }
    strbuf_append_char(ctx->buf, ':');

    // Format the value using the CSS formatter
    CssValue* val = (CssValue*)decl->value;

    // Create a temporary pool and formatter for value formatting
    Pool* temp_pool = pool_create();
    if (temp_pool) {
        CssFormatter* formatter = css_formatter_create(temp_pool, CSS_FORMAT_COMPACT);
        if (formatter) {
            // Format the value
            css_format_value(formatter, val);

            // Get the formatted string from the formatter's output buffer
            String* result = stringbuf_to_string(formatter->output);
            if (result && result->len > 0) {
                strbuf_append_str(ctx->buf, result->chars);
            }

            css_formatter_destroy(formatter);
        }
        pool_destroy(temp_pool);
    }

    *ctx->has_props = true;
    return true;  // continue iteration
}

void DomNode::print(StrBuf* buf, int indent) const {
    // If no buffer provided, print to console (legacy behavior)
    if (!buf) {
        for (int i = 0; i < indent; i++) printf("  ");

        const char* node_name = this->node_name();
        printf("<%s", node_name);

        // Print additional info for elements
        if (this->is_element()) {
            const DomElement* elem = this->as_element();
            if (elem->id) {
                printf(" id=\"%s\"", elem->id);
            }
            if (elem->class_count > 0) {
                printf(" class=\"");
                for (int i = 0; i < elem->class_count; i++) {
                    if (i > 0) printf(" ");
                    printf("%s", elem->class_names[i]);
                }
                printf("\"");
            }
        } else if (this->is_text()) {
            const DomText* text = this->as_text();
            if (text->text && text->length > 0) {
                // Print truncated text content
                printf(" \"");
                size_t max_len = 40;
                if (text->length <= max_len) {
                    printf("%.*s", (int)text->length, text->text);
                } else {
                    printf("%.*s...", (int)(max_len - 3), text->text);
                }
                printf("\"");
            }
        }

        printf(">\n");

        // Recursively print children (only for elements)
        if (this->is_element()) {
            const DomElement* element = this->as_element();
            const DomNode* child = element->first_child;
            while (child) {
                child->print(nullptr, indent + 1);
                child = child->next_sibling;
            }
        }
        return;
    }

    // Buffer-based printing (detailed format from dom_element_print)
    if (this->is_element()) {
        const DomElement* element = this->as_element();
        
        // Add indentation
        strbuf_append_char_n(buf, ' ', indent);

        // Print opening tag
        strbuf_append_char(buf, '<');
        strbuf_append_str(buf, element->tag_name ? element->tag_name : "unknown");

        // Print id attribute first if present
        if (element->id && element->id[0] != '\0') {
            strbuf_append_str(buf, " id=\"");
            strbuf_append_str(buf, element->id);
            strbuf_append_char(buf, '"');
        }

        // Print class attribute if present
        if (element->class_count > 0 && element->class_names) {
            strbuf_append_str(buf, " class=\"");
            for (int i = 0; i < element->class_count; i++) {
                if (i > 0) {
                    strbuf_append_char(buf, ' ');
                }
                strbuf_append_str(buf, element->class_names[i]);
            }
            strbuf_append_char(buf, '"');
        }

        // Print other attributes
        int attr_count = 0;
        const char** attr_names = dom_element_get_attribute_names((DomElement*)element, &attr_count);
        if (attr_names) {
            for (int i = 0; i < attr_count; i++) {
                const char* name = attr_names[i];
                const char* value = dom_element_get_attribute((DomElement*)element, name);

                // Skip id and class as they're already printed above
                if (strcmp(name, "id") != 0 && strcmp(name, "class") != 0 && value) {
                    strbuf_append_char(buf, ' ');
                    strbuf_append_str(buf, name);
                    strbuf_append_str(buf, "=\"");
                    strbuf_append_str(buf, value);
                    strbuf_append_char(buf, '"');
                }
            }
        }

        // Print pseudo-state information if any (for testing/debugging)
        if (element->pseudo_state != 0) {
            strbuf_append_str(buf, " [pseudo:");
            if (element->pseudo_state & PSEUDO_STATE_HOVER) strbuf_append_str(buf, " hover");
            if (element->pseudo_state & PSEUDO_STATE_ACTIVE) strbuf_append_str(buf, " active");
            if (element->pseudo_state & PSEUDO_STATE_FOCUS) strbuf_append_str(buf, " focus");
            if (element->pseudo_state & PSEUDO_STATE_VISITED) strbuf_append_str(buf, " visited");
            if (element->pseudo_state & PSEUDO_STATE_CHECKED) strbuf_append_str(buf, " checked");
            if (element->pseudo_state & PSEUDO_STATE_DISABLED) strbuf_append_str(buf, " disabled");
            strbuf_append_char(buf, ']');
        }

        strbuf_append_char(buf, '>');

        // Print specified CSS styles if present
        if (element->id || element->class_count > 0 || element->specified_style) {
            int has_text = false;
            strbuf_append_str(buf, "[");

            // print id
            if (element->id && element->id[0] != '\0') {
                strbuf_append_format(buf, "id:'%s'", element->id);
                has_text = true;
            }

            // print classes
            if (element->class_count > 0 && element->class_names) {
                strbuf_append_str(buf, has_text ? ", classes:" : "classes:");
                strbuf_append_char(buf, '[');
                for (int i = 0; i < element->class_count; i++) {
                    strbuf_append_format(buf, "\"%s\"", element->class_names[i]);
                    if (i < element->class_count - 1) strbuf_append_char(buf, ',');
                }
                strbuf_append_char(buf, ']');
                has_text = true;
            }

            // print styles generically using style_tree_foreach
            if (element->specified_style && element->specified_style->tree) {
                strbuf_append_str(buf, has_text ? ", styles:{" : "styles:{");

                bool has_props = false;
                StylePrintContext ctx = { buf, &has_props };

                // Iterate through all properties in the style tree
                style_tree_foreach(element->specified_style, print_style_property_callback, &ctx);

                strbuf_append_str(buf, "}");
            }

            strbuf_append_char(buf, ']');
        }

        // Print children (only for elements)
        if (this->is_element()) {
            const DomElement* element = this->as_element();
            const DomNode* child = element->first_child;
            bool has_element_children = false;

            // Count and print children
            while (child) {
                if (child->is_element()) {
                    // Recursively print child elements with newline before each child
                    has_element_children = true;
                    strbuf_append_char(buf, '\n');
                    child->print(buf, indent + 2);
                } else if (child->is_text()) {
                    // Print text nodes (skip whitespace-only text nodes)
                    const DomText* text_node = (const DomText*)child;

                    if (text_node->text && text_node->length > 0) {
                        // Check if text node contains only whitespace
                        bool is_whitespace_only = true;
                        for (size_t i = 0; i < text_node->length; i++) {
                            char c = text_node->text[i];
                            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                                is_whitespace_only = false;
                                break;
                            }
                        }

                        if (!is_whitespace_only) {
                            strbuf_append_str(buf, "\n");
                            strbuf_append_char_n(buf, ' ', indent + 2);
                            strbuf_append_str(buf, "\"");
                            strbuf_append_str_n(buf, text_node->text, text_node->length);
                            strbuf_append_str(buf, "\"");
                        }
                    }
                } else if (child->is_comment()) {
                    // Print comment/DOCTYPE nodes
                    const DomComment* comment_node = (const DomComment*)child;
                    strbuf_append_char(buf, '\n');
                    strbuf_append_char_n(buf, ' ', indent + 2);
                    strbuf_append_str(buf, "<!-- ");
                    if (comment_node->content) {
                        strbuf_append_str(buf, comment_node->content);
                    }
                    strbuf_append_str(buf, " -->");
                }

                // Move to next sibling
                child = child->next_sibling;
            }

            // Print closing tag
            // Add newline and indentation before closing tag only if we had element children
            if (has_element_children) {
                strbuf_append_char(buf, '\n');
                strbuf_append_char_n(buf, ' ', indent);
            }
            strbuf_append_str(buf, "</");
            strbuf_append_str(buf, element->tag_name ? element->tag_name : "unknown");
            strbuf_append_char(buf, '>');

            // Add trailing newline only for root element (indent == 0)
            if (indent == 0) {
                strbuf_append_char(buf, '\n');
            }
        }
    } else if (this->is_text()) {
        // For standalone text node printing
        const DomText* text_node = this->as_text();
        if (text_node->text && text_node->length > 0) {
            strbuf_append_char_n(buf, ' ', indent);
            strbuf_append_str(buf, "\"");
            strbuf_append_str_n(buf, text_node->text, text_node->length);
            strbuf_append_str(buf, "\"");
            if (indent == 0) {
                strbuf_append_char(buf, '\n');
            }
        }
    } else if (this->is_comment()) {
        // For standalone comment/DOCTYPE printing
        const DomComment* comment_node = this->as_comment();
        strbuf_append_char_n(buf, ' ', indent);
        strbuf_append_str(buf, "<!-- ");
        if (comment_node->content) {
            strbuf_append_str(buf, comment_node->content);
        }
        strbuf_append_str(buf, " -->");
        if (indent == 0) {
            strbuf_append_char(buf, '\n');
        }
    }
}

void DomNode::free_tree() {
    // Recursively free all children (only for elements)
    if (this->is_element()) {
        DomElement* element = static_cast<DomElement*>(this);
        DomNode* child = element->first_child;
        while (child) {
            DomNode* next = child->next_sibling;
            child->free_tree();
            child = next;
        }

        // Clear first_child
        element->first_child = nullptr;
    }

    // Clear relationships
    this->parent = nullptr;
    this->next_sibling = nullptr;
    this->prev_sibling = nullptr;
}

#include <stdlib.h>
#include <string.h>
#include "../lib/strbuf.h"
#include "flexbox.h"
#include "../dom.h"

// Axis properties
typedef struct {
    bool is_horizontal;
    bool order_normal;
    int main_size_container;
    int cross_size_container;
} AxisInfo;

// Line structure for multi-line layouts
typedef struct {
    FlexNode** items;
    int num_items;
    int main_size;
    int max_cross_size;
} FlexLine;

// Create a new flex node
FlexNode* createFlexNode(void) {
    FlexNode* node = calloc(1, sizeof(FlexNode));
    if (!node) return NULL;
    node->width = -1;
    node->height = -1;
    node->flex_basis = -1;
    node->flex_grow = 0.0f;
    node->flex_shrink = 1.0f;
    node->content_cross_size = -1;
    node->direction = "row";
    node->justify = "flex-start";
    node->align_items = "stretch";
    node->align_content = "stretch";
    node->wrap = "nowrap";
    node->is_dirty = true;
    return node;
}

// Destroy a node and its children (does not free DOM elements)
void destroyFlexNode(FlexNode* node) {
    if (!node) return;
    for (int i = 0; i < node->num_children; i++) {
        destroyFlexNode(node->children[i]);
    }
    free(node->children);
    free(node);
}

// Add a child to a parent
void addChild(FlexNode* parent, FlexNode* child) {
    if (!parent || !child) return;
    parent->children = realloc(parent->children, (parent->num_children + 1) * sizeof(FlexNode*));
    if (!parent->children) return;
    parent->children[parent->num_children++] = child;
    markDirty(parent);
}

// Mark a node as dirty
void markDirty(FlexNode* node) {
    if (!node) return;
    node->is_dirty = true;
}

lxb_status_t style_print_callback(const lxb_char_t *data, size_t len, void *ctx) {
    // printf("style rule: %.*s\n", (int) len, (const char *) data);
    StrBuf* buf = (StrBuf*) ctx;
    strbuf_append_str_n(buf, (const char*)data, len);
    return LXB_STATUS_OK;
}

// Get CSS property value as string
static const char* getCSSProperty(lxb_dom_element_t* element, const char* property) {
    lxb_css_rule_declaration_t *style = lxb_dom_element_style_by_name(element, (lxb_char_t*)property, strlen(property));
    if (style) {
        StrBuf* buf = strbuf_new(32);
        
        const lxb_css_entry_data_t *data = lxb_css_property_by_id(style->type);
        lxb_status_t status = data->serialize(style->u.user, style_print_callback, buf);
        if (status != LXB_STATUS_OK) {
            strbuf_free(buf);
            return NULL;
        }
        if (buf->length) {
            lxb_char_t* value = buf->s;
            buf->s = NULL;  strbuf_free(buf);  // keep the string, free the buffer
            printf("property %s: %s\n", property, value);
            return value;
        }
    }
    return NULL;
}

// Build FlexNode from DOM element
static FlexNode* buildFlexNodeFromDOM(lxb_dom_element_t* element) {
    FlexNode* node = createFlexNode();
    if (!node) return NULL;

    node->dom_element = element;

    // Extract dimensions (assuming px units for simplicity)
    const char* width = getCSSProperty(element, "width");
    if (width) node->width = atoi(width); // Fallback to -1 if parsing fails
    const char* height = getCSSProperty(element, "height");
    if (height) node->height = atoi(height);

    // Flexbox properties
    const char* display = getCSSProperty(element, "display");
    if (display && strcmp(display, "flex") == 0) {
        node->direction = getCSSProperty(element, "flex-direction") ?: "row";
        node->justify = getCSSProperty(element, "justify-content") ?: "flex-start";
        node->align_items = getCSSProperty(element, "align-items") ?: "stretch";
        node->align_content = getCSSProperty(element, "align-content") ?: "stretch";
        node->wrap = getCSSProperty(element, "flex-wrap") ?: "nowrap";
    }
    else { // flex item properties
        const char* basis = getCSSProperty(element, "flex-basis");
        if (basis && strcmp(basis, "auto") != 0) node->flex_basis = atoi(basis);
        const char* grow = getCSSProperty(element, "flex-grow");
        if (grow) node->flex_grow = atof(grow);
        const char* shrink = getCSSProperty(element, "flex-shrink");
        if (shrink) node->flex_shrink = atof(shrink);
        if (height) {
            node->content_cross_size = atoi(height);
            printf("set content_cross_size: %d\n", node->content_cross_size);
        }        
    }

    // Recursively process children
    lxb_dom_node_t* child = lxb_dom_node_first_child(lxb_dom_interface_node(element));
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            FlexNode* child_node = buildFlexNodeFromDOM((lxb_dom_element_t*)child);
            if (child_node) addChild(node, child_node);
        }
        child = lxb_dom_node_next(child);
    }

    return node;
}

// Parse HTML and CSS into a FlexNode tree
FlexNode* parseHTMLandCSS(const char* html_source) {
    // create HTML document object
    lxb_html_document_t *document = lxb_html_document_create();
    if (!document) {
        fprintf(stderr, "Failed to create HTML document.\n");
        return NULL;
    }
    // init CSS on document, otherwise CSS declarations will not be parsed
    lxb_status_t status = lxb_html_document_css_init(document, true);
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to CSS initialization\n");
        return NULL;
    }

    status = lxb_html_document_parse(document, (const lxb_char_t *)html_source, strlen(html_source));
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to parse HTML.\n");
        lxb_html_document_destroy(document);
        return NULL;
    } 

    lxb_dom_element_t *body = (lxb_dom_element_t*)lxb_html_document_body_element(document);
    if (!body) { printf("missing body element\n");  return NULL; }
    lxb_dom_element_t *div = (lxb_dom_element_t*)lxb_dom_node_first_child(lxb_dom_interface_node(body));
    // build FlexNode tree
    FlexNode* root = buildFlexNodeFromDOM(div);

    return root;
}

// Get axis info
static bool getAxisInfo(const FlexNode* node, AxisInfo* info) {
    if (!node || !node->direction) return false;
    if (strcmp(node->direction, "row") == 0) {
        info->is_horizontal = true;
        info->order_normal = true;
    } else if (strcmp(node->direction, "row-reverse") == 0) {
        info->is_horizontal = true;
        info->order_normal = false;
    } else if (strcmp(node->direction, "column") == 0) {
        info->is_horizontal = false;
        info->order_normal = true;
    } else if (strcmp(node->direction, "column-reverse") == 0) {
        info->is_horizontal = false;
        info->order_normal = false;
    } else return false;
    info->main_size_container = info->is_horizontal ? node->width : node->height;
    info->cross_size_container = info->is_horizontal ? node->height : node->width;
    return info->main_size_container >= 0 && info->cross_size_container >= 0;
}

// Adjust sizes for a line
static void adjust_line_sizes(FlexLine* line, int main_size_container, MeasureFunc measure) {
    int total_initial_main_size = 0;
    float total_grow = 0.0f, total_shrink = 0.0f;

    printf("!! adjust line sizes\n");
    for (int i = 0; i < line->num_items; i++) {
        FlexNode* item = line->items[i];
        if (measure && item->flex_basis < 0) {
            measure(item, main_size_container, -1, &item->main_size, &item->cross_size);
        } else {
            item->main_size = item->flex_basis < 0 ? 0 : item->flex_basis;
        }
        item->cross_size = item->content_cross_size < 0 ? 0 : item->content_cross_size; // Set cross size
        total_initial_main_size += item->main_size;
        total_grow += item->flex_grow;
        total_shrink += item->flex_shrink;
        line->max_cross_size = (item->cross_size > line->max_cross_size) ? 
                               item->cross_size : line->max_cross_size;
    }
    line->main_size = total_initial_main_size;

    int extra_space = main_size_container - total_initial_main_size;
    if (extra_space > 0 && total_grow > 0) {
        for (int i = 0; i < line->num_items; i++) {
            FlexNode* item = line->items[i];
            if (item->flex_grow > 0) {
                item->main_size += (int)((extra_space * item->flex_grow) / total_grow);
            }
        }
        line->main_size = main_size_container;
    } else if (extra_space < 0 && total_shrink > 0) {
        int deficit = -extra_space;
        for (int i = 0; i < line->num_items; i++) {
            FlexNode* item = line->items[i];
            if (item->flex_shrink > 0) {
                int reduction = (int)((deficit * item->flex_shrink) / total_shrink);
                item->main_size = (item->main_size > reduction) ? item->main_size - reduction : 0;
            }
        }
        line->main_size = 0;
        for (int i = 0; i < line->num_items; i++) line->main_size += line->items[i]->main_size;
    }
}

// Position items in a line along main axis
static void position_line_main(FlexLine* line, const char* justify, int main_size_container, bool order_normal) {
    int remaining_space = main_size_container - line->main_size;
    int* order = malloc(line->num_items * sizeof(int));
    if (!order) return;
    for (int i = 0; i < line->num_items; i++) {
        order[i] = order_normal ? i : (line->num_items - 1 - i);
    }

    int pos = 0;
    if (strcmp(justify, "flex-start") == 0) pos = 0;
    else if (strcmp(justify, "flex-end") == 0) pos = remaining_space;
    else if (strcmp(justify, "center") == 0) pos = remaining_space / 2;
    else if (strcmp(justify, "space-between") == 0 && line->num_items > 1) {
        int space_each = remaining_space / (line->num_items - 1);
        for (int i = 0; i < line->num_items; i++) {
            FlexNode* item = line->items[order[i]];
            item->position_main = pos;
            pos += item->main_size + space_each;
        }
    } else if (strcmp(justify, "space-around") == 0 && line->num_items > 0) {
        int space_each = remaining_space / (line->num_items * 2);
        pos = space_each;
        for (int i = 0; i < line->num_items; i++) {
            FlexNode* item = line->items[order[i]];
            item->position_main = pos;
            pos += item->main_size + (2 * space_each);
        }
    }

    free(order);
}

// Layout a node
static void layout_flex_node(FlexNode* node, MeasureFunc measure) {
    AxisInfo axis;
    if (!getAxisInfo(node, &axis)) return;

    printf("Layout node: %s, wd: %d, hg: %d\n", lxb_dom_element_local_name(node->dom_element, NULL), 
        node->width, node->height);
    if (node->num_children == 0 && measure) {
        measure(node, axis.main_size_container, axis.cross_size_container, 
                &node->main_size, &node->cross_size);
        return;
    }

    FlexLine* lines = NULL;
    int num_lines = 0;
    bool wrap = strcmp(node->wrap, "nowrap") != 0;
    bool reverse_wrap = strcmp(node->wrap, "wrap-reverse") == 0;

    if (!wrap) {
        printf("single line layout without wrap\n");
        lines = malloc(sizeof(FlexLine));
        if (!lines) return;
        num_lines = 1;
        lines[0].items = node->children;
        lines[0].num_items = node->num_children;
        lines[0].main_size = 0;
        lines[0].max_cross_size = 0;
        adjust_line_sizes(&lines[0], axis.main_size_container, measure);
    } 
    else {
        printf("layout with wrap\n");
        int current_main = 0;
        int start_idx = 0;
        for (int i = 0; i <= node->num_children; i++) {
            if (i == node->num_children || current_main > axis.main_size_container) {
                if (i > start_idx) {
                    lines = realloc(lines, (num_lines + 1) * sizeof(FlexLine));
                    lines[num_lines].items = &node->children[start_idx];
                    lines[num_lines].num_items = i - start_idx;
                    lines[num_lines].main_size = 0;
                    lines[num_lines].max_cross_size = 0;
                    adjust_line_sizes(&lines[num_lines], axis.main_size_container, measure);
                    num_lines++;
                }
                start_idx = i;
                current_main = 0;
            }
            if (i < node->num_children) {
                FlexNode* child = node->children[i];
                int child_main = child->flex_basis < 0 && measure ? 
                                 child->main_size : (child->flex_basis < 0 ? 0 : child->flex_basis);
                printf("child main size: %d\n", child_main);
                current_main += child_main;
            }
        }
    }

    int total_cross_size = 0;
    for (int i = 0; i < num_lines; i++) total_cross_size += lines[i].max_cross_size;
    int remaining_cross = axis.cross_size_container - total_cross_size;
    int cross_pos = 0;
    printf("cross size container: %d, total cross size: %d, remaining cross: %d\n", 
          axis.cross_size_container, total_cross_size, remaining_cross);

    printf("align content: %s\n", node->align_content);
    if (strcmp(node->align_content, "flex-start") == 0) cross_pos = 0;
    else if (strcmp(node->align_content, "flex-end") == 0) cross_pos = remaining_cross;
    else if (strcmp(node->align_content, "center") == 0) cross_pos = remaining_cross / 2;
    else if (strcmp(node->align_content, "space-between") == 0 && num_lines > 1) {
        int space_each = remaining_cross / (num_lines - 1);
        for (int i = 0; i < num_lines; i++) {
            int line_idx = reverse_wrap ? (num_lines - 1 - i) : i;
            for (int j = 0; j < lines[line_idx].num_items; j++) {
                FlexNode* item = lines[line_idx].items[j];
                item->position_cross = cross_pos;
                item->cross_size = strcmp(node->align_items, "stretch") == 0 ? 
                                   lines[line_idx].max_cross_size : item->content_cross_size;
            }
            position_line_main(&lines[line_idx], node->justify, axis.main_size_container, axis.order_normal);
            cross_pos += lines[line_idx].max_cross_size + space_each;
        }
    } else if (strcmp(node->align_content, "space-around") == 0 && num_lines > 0) {
        int space_each = remaining_cross / (num_lines * 2);
        cross_pos = space_each;
        for (int i = 0; i < num_lines; i++) {
            int line_idx = reverse_wrap ? (num_lines - 1 - i) : i;
            for (int j = 0; j < lines[line_idx].num_items; j++) {
                FlexNode* item = lines[line_idx].items[j];
                item->position_cross = cross_pos;
                item->cross_size = strcmp(node->align_items, "stretch") == 0 ? 
                                   lines[line_idx].max_cross_size : item->content_cross_size;
            }
            position_line_main(&lines[line_idx], node->justify, axis.main_size_container, axis.order_normal);
            cross_pos += lines[line_idx].max_cross_size + (2 * space_each);
        }
    } else {
        int line_cross_size = num_lines > 0 ? axis.cross_size_container / num_lines : 0;
        for (int i = 0; i < num_lines; i++) {
            int line_idx = reverse_wrap ? (num_lines - 1 - i) : i;
            for (int j = 0; j < lines[line_idx].num_items; j++) {
                FlexNode* item = lines[line_idx].items[j];
                item->position_cross = cross_pos;
                item->cross_size = strcmp(node->align_items, "stretch") == 0 ? 
                                   line_cross_size : item->content_cross_size;
            }
            position_line_main(&lines[line_idx], node->justify, axis.main_size_container, axis.order_normal);
            cross_pos += line_cross_size;
        }
    }

    for (int i = 0; i < num_lines; i++) {
        int line_cross_size = lines[i].max_cross_size;
        for (int j = 0; j < lines[i].num_items; j++) {
            FlexNode* item = lines[i].items[j];
            printf("node align_items:%s: position_cross: %d, cross_size: %d, line_cross_size: %d\n",
                node->align_items, item->position_cross, item->cross_size, line_cross_size);
            if (strcmp(node->align_items, "flex-start") == 0) {}
            else if (strcmp(node->align_items, "center") == 0) 
                item->position_cross += (line_cross_size - item->cross_size) / 2;
            else if (strcmp(node->align_items, "flex-end") == 0) 
                item->position_cross += line_cross_size - item->cross_size;
            else if (strcmp(node->align_items, "stretch") == 0) 
                item->cross_size = line_cross_size;
        }
    }

    free(lines);
}

// Calculate layout
void calculateFlexLayout(FlexNode* root, MeasureFunc measure) {
    if (!root || !root->is_dirty) return;

    printf("calculate layout\n");
    for (int i = 0; i < root->num_children; i++) {
        calculateFlexLayout(root->children[i], measure);
    }

    layout_flex_node(root, measure);
    root->is_dirty = false;
}

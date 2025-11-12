# Lambda Layout Integration Plan

This document outlines the plan to integrate Lambda's HTML/CSS parsing and styling system with Radiant's layout engine, creating a unified layout pipeline.

## Overview

**Goal**: Create a `lambda layout` command that uses Lambda's HTML parser and CSS engine to build a styled document tree, then passes it to Radiant's layout engine for rendering.

**Current State**:
- ✅ Lambda HTML parser: Parses HTML → Lambda Element tree
- ✅ Lambda CSS engine: Parses CSS → Applies styles to DOM elements via AVL trees
- ✅ Radiant layout engine: Layouts Lexbor DOM → Produces positioned boxes
- ❌ **Gap**: Radiant is hardcoded to work with Lexbor structures

**Target State**:
- Lambda parses HTML/CSS → Styled Lambda tree → Radiant layout → Rendered output
- Radiant works with **both** Lexbor and Lambda DOM representations

---

## Phase 1: Enhance `dom.hpp` for Lambda Integration

### 1.1 Current DomNode Structure Analysis

The `DomNode` struct already has provisions for Lambda nodes:

```cpp
typedef struct DomNode {
    NodeType type;  // MARK_ELEMENT, MARK_TEXT, LEXBOR_ELEMENT, LEXBOR_NODE
    union {
        lxb_dom_node_t* lxb_node;
        lxb_html_element_t* lxb_elmt;
        Element* mark_element;     // ✓ Already exists
        String* mark_text;         // ✓ Already exists
    };
    struct Style* style;
    DomNode* parent;
    private:
        DomNode* _child;
        DomNode* _next;
    // ... methods
} DomNode;
```

**Status**: The union already supports Lambda nodes! But methods need implementation.

### 1.2 Required DomNode Enhancements

#### A. Complete Lambda Element Methods

**File**: `radiant/dom.hpp` and `radiant/dom.cpp`

```cpp
// In dom.hpp - Already declared, need implementation
class DomNode {
public:
    // Lambda-specific getters
    char* mark_text_data();              // Get text from mark_text
    Item mark_get_attribute(const char* attr_name);  // Get attribute from mark_element
    Item mark_get_content();             // Get element content/children

    // Factory methods
    static DomNode* create_mark_element(Element* element);
    static DomNode* create_mark_text(String* text);
};
```

**Implementation Details**:

```cpp
// dom.cpp implementations needed

char* DomNode::mark_text_data() {
    if (type != MARK_TEXT || !mark_text) return nullptr;
    return mark_text->chars;
}

Item DomNode::mark_get_attribute(const char* attr_name) {
    if (type != MARK_ELEMENT || !mark_element) {
        return (Item){.item = ITEM_NULL};
    }

    // Use elmt_get() to retrieve attribute from Lambda Element
    TypeElmt* elem_type = (TypeElmt*)mark_element->type;
    ShapeEntry* entry = elem_type->shape;

    while (entry) {
        if (strcmp(entry->name->str, attr_name) == 0) {
            void* field_ptr = (char*)mark_element->data + entry->byte_offset;

            // Return based on type
            if (entry->type->type_id == LMD_TYPE_STRING) {
                String* str = *(String**)field_ptr;
                return (Item){.item = s2it(str)};
            } else if (entry->type->type_id == LMD_TYPE_BOOL) {
                bool* val = (bool*)field_ptr;
                return (Item){.bool_val = *val, .type_id = LMD_TYPE_BOOL};
            } else if (entry->type->type_id == LMD_TYPE_NULL) {
                return (Item){.item = ITEM_NULL};
            }
        }
        entry = entry->next;
    }

    return (Item){.item = ITEM_NULL};
}

Item DomNode::mark_get_content() {
    if (type != MARK_ELEMENT || !mark_element) {
        return (Item){.item = ITEM_NULL};
    }

    // Lambda Elements are also Lists with children
    List* list = (List*)mark_element;
    return (Item){.list = list};
}

DomNode* DomNode::create_mark_element(Element* element) {
    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_ELEMENT;
    node->mark_element = element;
    node->style = nullptr;
    node->parent = nullptr;
    node->_child = nullptr;
    node->_next = nullptr;
    return node;
}

DomNode* DomNode::create_mark_text(String* text) {
    DomNode* node = (DomNode*)calloc(1, sizeof(DomNode));
    node->type = MARK_TEXT;
    node->mark_text = text;
    node->style = nullptr;
    node->parent = nullptr;
    node->_child = nullptr;
    node->_next = nullptr;
    return node;
}
```

#### B. Unified Navigation Methods

Enhance `first_child()` and `next_sibling()` to work with Lambda nodes:

```cpp
DomNode* DomNode::first_child() {
    // Return cached if available
    if (_child) return _child;

    if (type == LEXBOR_ELEMENT || type == LEXBOR_NODE) {
        // Existing Lexbor logic
        lxb_dom_node_t* child = lxb_dom_node_first_child(lxb_node);
        if (!child) return nullptr;

        // Create wrapper DomNode
        DomNode* child_node = (DomNode*)calloc(1, sizeof(DomNode));
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            child_node->type = LEXBOR_ELEMENT;
            child_node->lxb_elmt = lxb_dom_interface_element(child);
        } else {
            child_node->type = LEXBOR_NODE;
            child_node->lxb_node = child;
        }
        child_node->parent = this;
        _child = child_node;
        return child_node;

    } else if (type == MARK_ELEMENT) {
        // Lambda Element navigation
        List* list = (List*)mark_element;
        if (list->length == 0) return nullptr;

        Item first_item = list->items[0];
        DomNode* child_node = nullptr;

        if (first_item.type_id == LMD_TYPE_ELEMENT) {
            child_node = create_mark_element((Element*)first_item.pointer);
        } else if (first_item.type_id == LMD_TYPE_STRING) {
            child_node = create_mark_text((String*)first_item.pointer);
        }

        if (child_node) {
            child_node->parent = this;
            _child = child_node;
        }
        return child_node;
    }

    return nullptr;
}

DomNode* DomNode::next_sibling() {
    // Return cached if available
    if (_next) return _next;

    if (!parent) return nullptr;

    if (type == LEXBOR_ELEMENT || type == LEXBOR_NODE) {
        // Existing Lexbor logic
        lxb_dom_node_t* sibling = lxb_dom_node_next(lxb_node);
        if (!sibling) return nullptr;

        DomNode* sibling_node = (DomNode*)calloc(1, sizeof(DomNode));
        if (sibling->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            sibling_node->type = LEXBOR_ELEMENT;
            sibling_node->lxb_elmt = lxb_dom_interface_element(sibling);
        } else {
            sibling_node->type = LEXBOR_NODE;
            sibling_node->lxb_node = sibling;
        }
        sibling_node->parent = parent;
        _next = sibling_node;
        return sibling_node;

    } else if (type == MARK_ELEMENT || type == MARK_TEXT) {
        // Lambda navigation: find current index in parent's children
        if (parent->type != MARK_ELEMENT) return nullptr;

        List* parent_list = (List*)parent->mark_element;

        // Find our index
        int my_index = -1;
        for (int64_t i = 0; i < parent_list->length; i++) {
            Item item = parent_list->items[i];

            if (type == MARK_ELEMENT && item.type_id == LMD_TYPE_ELEMENT) {
                if ((Element*)item.pointer == mark_element) {
                    my_index = i;
                    break;
                }
            } else if (type == MARK_TEXT && item.type_id == LMD_TYPE_STRING) {
                if ((String*)item.pointer == mark_text) {
                    my_index = i;
                    break;
                }
            }
        }

        // Get next sibling
        if (my_index >= 0 && my_index + 1 < parent_list->length) {
            Item next_item = parent_list->items[my_index + 1];
            DomNode* sibling_node = nullptr;

            if (next_item.type_id == LMD_TYPE_ELEMENT) {
                sibling_node = create_mark_element((Element*)next_item.pointer);
            } else if (next_item.type_id == LMD_TYPE_STRING) {
                sibling_node = create_mark_text((String*)next_item.pointer);
            }

            if (sibling_node) {
                sibling_node->parent = parent;
                _next = sibling_node;
            }
            return sibling_node;
        }
    }

    return nullptr;
}
```

#### C. Unified Attribute Access

Extend `get_attribute()` to work with Lambda:

```cpp
const lxb_char_t* DomNode::get_attribute(const char* attr_name, size_t* value_len) {
    if (type == LEXBOR_ELEMENT && lxb_elmt) {
        // Existing Lexbor implementation
        lxb_dom_element_t* element = lxb_dom_interface_element(lxb_elmt);
        lxb_dom_attr_t* attr = lxb_dom_element_attr_by_name(
            element, (const lxb_char_t*)attr_name, strlen(attr_name)
        );

        if (!attr) return nullptr;

        size_t len;
        const lxb_char_t* value = lxb_dom_attr_value(attr, &len);
        if (value_len) *value_len = len;
        return value;

    } else if (type == MARK_ELEMENT && mark_element) {
        // Lambda implementation
        Item attr_item = mark_get_attribute(attr_name);

        if (attr_item.type_id == LMD_TYPE_STRING) {
            String* str = (String*)attr_item.pointer;
            if (value_len) *value_len = str->len;
            return (const lxb_char_t*)str->chars;
        } else if (attr_item.type_id == LMD_TYPE_BOOL) {
            // Boolean attribute - return "true" or nullptr
            if (attr_item.bool_val) {
                if (value_len) *value_len = 4;
                return (const lxb_char_t*)"true";
            }
        } else if (attr_item.type_id == LMD_TYPE_NULL) {
            // Empty attribute - return empty string
            if (value_len) *value_len = 0;
            return (const lxb_char_t*)"";
        }
    }

    return nullptr;
}
```

#### D. Unified Tag Name Access

Enhance `name()` method:

```cpp
char* DomNode::name() {
    if (type == LEXBOR_ELEMENT && lxb_elmt) {
        const lxb_char_t* element_name = lxb_dom_element_local_name(
            lxb_dom_interface_element(lxb_elmt), NULL
        );
        return element_name ? (char*)element_name : (char*)"#element";
    }
    else if (type == LEXBOR_NODE && lxb_node) {
        return (char*)"#text";
    }
    else if (type == MARK_ELEMENT && mark_element) {
        TypeElmt* elem_type = (TypeElmt*)mark_element->type;
        return elem_type->name.str;
    }
    else if (type == MARK_TEXT && mark_text) {
        return (char*)"#text";
    }
    return (char*)"#null";
}
```

---

## Phase 2: Add `lambda layout` Subcommand

### 2.1 Command Interface

**File**: `lambda/main.cpp`

Add new subcommand handler:

```cpp
// In main.cpp command dispatch
if (argc >= 2 && strcmp(argv[1], "layout") == 0) {
    return cmd_layout(argc - 2, argv + 2);
}
```

### 2.2 Layout Command Implementation

**New File**: `lambda/cmd_layout.cpp`

```cpp
#include "lambda.h"
#include "../radiant/layout.hpp"
#include "../radiant/dom.hpp"
#include "../lambda/input/input.h"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/dom_element.hpp"

typedef struct LayoutOptions {
    const char* input_file;
    const char* output_file;
    const char* css_file;
    int viewport_width;
    int viewport_height;
    bool debug;
} LayoutOptions;

static void print_layout_usage() {
    printf("Usage: lambda layout [options] <input.html>\n");
    printf("\nOptions:\n");
    printf("  -o, --output <file>     Output file (default: stdout)\n");
    printf("  -c, --css <file>        External CSS file\n");
    printf("  -w, --width <pixels>    Viewport width (default: 800)\n");
    printf("  -h, --height <pixels>   Viewport height (default: 600)\n");
    printf("  --debug                 Enable debug output\n");
    printf("\nExamples:\n");
    printf("  lambda layout document.html\n");
    printf("  lambda layout -c styles.css -w 1024 document.html\n");
    printf("  lambda layout --debug -o layout.txt page.html\n");
}

static LayoutOptions parse_layout_options(int argc, char** argv) {
    LayoutOptions opts = {0};
    opts.viewport_width = 800;
    opts.viewport_height = 600;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) opts.output_file = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--css") == 0) {
            if (i + 1 < argc) opts.css_file = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) {
            if (i + 1 < argc) opts.viewport_width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--height") == 0) {
            if (i + 1 < argc) opts.viewport_height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--debug") == 0) {
            opts.debug = true;
        } else if (argv[i][0] != '-') {
            opts.input_file = argv[i];
        }
    }

    return opts;
}

// Convert Lambda Element tree to DomNode tree
static DomNode* lambda_to_domnode(Element* elem, Pool* pool) {
    if (!elem) return nullptr;

    DomNode* node = DomNode::create_mark_element(elem);

    // Recursively convert children
    List* list = (List*)elem;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];

        if (child.type_id == LMD_TYPE_ELEMENT) {
            DomNode* child_node = lambda_to_domnode((Element*)child.pointer, pool);
            if (child_node) {
                child_node->parent = node;
                // Children linked via first_child/next_sibling navigation
            }
        } else if (child.type_id == LMD_TYPE_STRING) {
            DomNode* text_node = DomNode::create_mark_text((String*)child.pointer);
            text_node->parent = node;
        }
    }

    return node;
}

// Apply Lambda CSS styles to DomNode tree
static void apply_lambda_styles(DomNode* root, DomElement* css_root, Pool* pool) {
    if (!root || !css_root) return;

    // Traverse DomNode tree
    DomNode* current = root;

    while (current) {
        if (current->type == MARK_ELEMENT) {
            // Find matching CSS element
            // For now, assume 1:1 correspondence from lambda_element_to_dom_element
            // In full implementation, need proper matching logic

            // Get specified styles from DomElement
            TypeElmt* elem_type = (TypeElmt*)current->mark_element->type;
            const char* tag_name = elem_type->name.str;

            // TODO: Implement style retrieval and application
            // For each CSS property, create Style struct entry
        }

        // Traverse children
        DomNode* child = current->first_child();
        if (child) {
            current = child;
        } else {
            // No children, try sibling
            DomNode* sibling = current->next_sibling();
            if (sibling) {
                current = sibling;
            } else {
                // Go up to parent's sibling
                current = current->parent;
                while (current && !current->next_sibling()) {
                    current = current->parent;
                }
                if (current) {
                    current = current->next_sibling();
                }
            }
        }
    }
}

int cmd_layout(int argc, char** argv) {
    LayoutOptions opts = parse_layout_options(argc, argv);

    if (!opts.input_file) {
        print_layout_usage();
        return 1;
    }

    // Step 1: Parse HTML with Lambda parser
    printf("Parsing HTML: %s\n", opts.input_file);

    Pool* pool = pool_create();
    Input* input = input_from_file(opts.input_file, "html", nullptr);

    if (!input) {
        fprintf(stderr, "Failed to parse HTML file: %s\n", opts.input_file);
        pool_destroy(pool);
        return 1;
    }

    // Step 2: Get root element (skip DOCTYPE)
    Element* root_elem = get_html_root_element(input);
    if (!root_elem) {
        fprintf(stderr, "No HTML root element found\n");
        pool_destroy(pool);
        return 1;
    }

    // Step 3: Parse CSS (from <style> tags or external file)
    printf("Parsing CSS...\n");

    // Extract inline CSS from <style> tags
    char* inline_css = extract_style_tags(root_elem);

    // Parse external CSS if provided
    char* external_css = nullptr;
    if (opts.css_file) {
        external_css = read_file_content(opts.css_file);
    }

    // Combine CSS sources
    StringBuf* css_sb = stringbuf_new(pool);
    if (inline_css) stringbuf_append_str(css_sb, inline_css);
    if (external_css) stringbuf_append_str(css_sb, external_css);

    String* css_content = stringbuf_to_string(css_sb);

    // Step 4: Build CSS DOM and apply styles
    DomElement* css_dom = lambda_element_to_dom_element(root_elem, pool);

    if (css_content && css_content->len > 0) {
        CssStylesheet* stylesheet = css_parse_stylesheet(css_content->chars, pool);
        if (stylesheet) {
            // Apply stylesheet rules to DOM elements
            css_apply_stylesheet(css_dom, stylesheet, pool);
        }
    }

    // Step 5: Convert to Radiant DomNode structure
    printf("Building layout tree...\n");
    DomNode* dom_root = lambda_to_domnode(root_elem, pool);

    // Step 6: Apply CSS styles to DomNode tree
    apply_lambda_styles(dom_root, css_dom, pool);

    // Step 7: Run Radiant layout engine
    printf("Running layout (viewport: %dx%d)...\n",
           opts.viewport_width, opts.viewport_height);

    // Create layout context
    // ViewTree* view_tree = run_layout(dom_root, opts.viewport_width, opts.viewport_height);

    // Step 8: Output results
    if (opts.output_file) {
        printf("Writing output to: %s\n", opts.output_file);
        // write_layout_output(view_tree, opts.output_file);
    } else {
        // print_layout_tree(view_tree, stdout);
    }

    // Cleanup
    pool_destroy(pool);

    printf("Layout complete!\n");
    return 0;
}
```

### 2.3 Helper Functions

**File**: `lambda/layout_helpers.cpp`

```cpp
// Extract root HTML element, skipping DOCTYPE
Element* get_html_root_element(Input* input) {
    void* root_ptr = (void*)input->root.pointer;
    List* root_list = (List*)root_ptr;

    if (root_list->type_id == LMD_TYPE_LIST) {
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];

            if (item.type_id() == LMD_TYPE_ELEMENT) {
                Element* elem = (Element*)item.pointer;
                TypeElmt* type = (TypeElmt*)elem->type;

                // Skip DOCTYPE and comments
                if (strcmp(type->name.str, "!DOCTYPE") != 0 &&
                    strcmp(type->name.str, "!--") != 0) {
                    return elem;
                }
            }
        }
    } else if (root_list->type_id == LMD_TYPE_ELEMENT) {
        return (Element*)root_ptr;
    }

    return nullptr;
}

// Extract CSS from <style> tags
char* extract_style_tags(Element* root) {
    StringBuf* sb = stringbuf_new(pool_get_current());
    extract_style_tags_recursive(root, sb);
    return stringbuf_to_cstring(sb);
}

static void extract_style_tags_recursive(Element* elem, StringBuf* sb) {
    if (!elem) return;

    TypeElmt* type = (TypeElmt*)elem->type;

    // Check if this is a <style> tag
    if (strcmp(type->name.str, "style") == 0) {
        // Extract text content
        List* list = (List*)elem;
        for (int64_t i = 0; i < list->length; i++) {
            Item child = list->items[i];
            if (child.type_id == LMD_TYPE_STRING) {
                String* text = (String*)child.pointer;
                stringbuf_append_str(sb, text->chars);
                stringbuf_append_char(sb, '\n');
            }
        }
    }

    // Recurse children
    List* list = (List*)elem;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        if (child.type_id == LMD_TYPE_ELEMENT) {
            extract_style_tags_recursive((Element*)child.pointer, sb);
        }
    }
}
```

---

## Phase 3: Adapt Radiant for Lambda CSS

### 3.1 Identify Lexbor Dependencies

**Files to audit**:
- `radiant/layout.cpp` - Main layout logic
- `radiant/style.cpp` - Style computation
- `radiant/box.cpp` - Box model
- `radiant/inline.cpp` - Inline layout
- `radiant/flex.cpp` - Flexbox layout
- `radiant/table.cpp` - Table layout

**Search patterns**:
```bash
grep -rn "lxb_css" radiant/
grep -rn "lxb_style" radiant/
grep -rn "lexbor_" radiant/
```

### 3.2 Style Abstraction Layer

**New File**: `radiant/style_adapter.hpp`

Create an abstraction that works with both Lexbor and Lambda CSS:

```cpp
#pragma once

#include "dom.hpp"

// Forward declarations
struct CssDeclaration;  // Lambda CSS
typedef struct lxb_css_property_s lxb_css_property_t;  // Lexbor CSS

typedef enum {
    STYLE_SOURCE_LEXBOR,
    STYLE_SOURCE_LAMBDA
} StyleSource;

// Unified style value representation
typedef struct StyleValue {
    StyleSource source;

    union {
        // Lexbor style
        struct {
            lxb_css_property_t* lxb_prop;
            void* lxb_value;
        };

        // Lambda style
        struct {
            CssDeclaration* lambda_decl;
            CssPropertyId lambda_prop_id;
        };
    };

    // Cached computed values (source-agnostic)
    struct {
        bool is_computed;

        // Common CSS property values
        float length_px;      // For lengths (width, height, margin, etc.)
        uint32_t color_rgba;  // For colors
        int keyword;          // For keywords (display, position, etc.)
        bool bool_val;        // For boolean-like properties
    } computed;

} StyleValue;

// Style adapter interface
class StyleAdapter {
public:
    static StyleValue* get_property(DomNode* node, const char* prop_name);
    static float get_length_px(StyleValue* value, float parent_size, float font_size);
    static uint32_t get_color(StyleValue* value);
    static int get_keyword(StyleValue* value);
    static const char* get_string(StyleValue* value);

private:
    static StyleValue* get_lexbor_property(DomNode* node, const char* prop_name);
    static StyleValue* get_lambda_property(DomNode* node, const char* prop_name);
};
```

**Implementation**: `radiant/style_adapter.cpp`

```cpp
#include "style_adapter.hpp"
#include "../lambda/input/css/css_style_node.h"

StyleValue* StyleAdapter::get_property(DomNode* node, const char* prop_name) {
    if (!node) return nullptr;

    if (node->type == LEXBOR_ELEMENT || node->type == LEXBOR_NODE) {
        return get_lexbor_property(node, prop_name);
    } else if (node->type == MARK_ELEMENT) {
        return get_lambda_property(node, prop_name);
    }

    return nullptr;
}

StyleValue* StyleAdapter::get_lambda_property(DomNode* node, const char* prop_name) {
    if (!node->mark_element) return nullptr;

    // Get DomElement for this node (needs to be cached or looked up)
    // For now, assume we have access to the CSS DomElement
    DomElement* css_elem = get_css_dom_element_for_node(node);
    if (!css_elem) return nullptr;

    // Map property name to CssPropertyId
    CssPropertyId prop_id = css_property_name_to_id(prop_name);

    // Get computed/specified value
    CssDeclaration* decl = dom_element_get_specified_value(css_elem, prop_id);
    if (!decl) return nullptr;

    // Create StyleValue wrapper
    StyleValue* value = (StyleValue*)calloc(1, sizeof(StyleValue));
    value->source = STYLE_SOURCE_LAMBDA;
    value->lambda_decl = decl;
    value->lambda_prop_id = prop_id;
    value->computed.is_computed = false;

    return value;
}

float StyleAdapter::get_length_px(StyleValue* value, float parent_size, float font_size) {
    if (!value) return 0.0f;

    // Return cached if available
    if (value->computed.is_computed) {
        return value->computed.length_px;
    }

    float result = 0.0f;

    if (value->source == STYLE_SOURCE_LAMBDA) {
        CssValue* css_val = value->lambda_decl->value;

        if (css_val->type == CSS_VALUE_LENGTH) {
            float num = css_val->data.length.value;
            const char* unit = css_val->data.length.unit;

            if (strcmp(unit, "px") == 0) {
                result = num;
            } else if (strcmp(unit, "em") == 0) {
                result = num * font_size;
            } else if (strcmp(unit, "rem") == 0) {
                result = num * 16.0f;  // Assume 16px root font size
            } else if (strcmp(unit, "%") == 0) {
                result = (num / 100.0f) * parent_size;
            }
            // Add more units as needed

        } else if (css_val->type == CSS_VALUE_NUMBER) {
            result = css_val->data.number;
        }

    } else if (value->source == STYLE_SOURCE_LEXBOR) {
        // Existing Lexbor logic
        // ... (use existing Radiant code)
    }

    // Cache result
    value->computed.is_computed = true;
    value->computed.length_px = result;

    return result;
}

uint32_t StyleAdapter::get_color(StyleValue* value) {
    if (!value) return 0x000000FF;  // Default black

    if (value->computed.is_computed) {
        return value->computed.color_rgba;
    }

    uint32_t result = 0x000000FF;

    if (value->source == STYLE_SOURCE_LAMBDA) {
        CssValue* css_val = value->lambda_decl->value;

        if (css_val->type == CSS_VALUE_COLOR) {
            // Extract RGBA from Lambda CSS color
            result = css_val->data.color_rgba;

        } else if (css_val->type == CSS_VALUE_KEYWORD) {
            // Map keyword to color (red, blue, etc.)
            const char* keyword = css_val->data.keyword;
            result = css_keyword_to_color(keyword);
        }

    } else if (value->source == STYLE_SOURCE_LEXBOR) {
        // Existing Lexbor color parsing
    }

    value->computed.is_computed = true;
    value->computed.color_rgba = result;

    return result;
}
```

### 3.3 Refactor Radiant Layout Code

**Example refactoring** in `radiant/layout.cpp`:

```cpp
// BEFORE (Lexbor-specific)
float get_element_width(DomNode* node) {
    lxb_html_element_t* elem = node->as_element();
    lxb_css_property_t* width_prop = lxb_style_get_property(elem, "width");
    return lxb_css_length_to_px(width_prop);
}

// AFTER (Unified with StyleAdapter)
float get_element_width(DomNode* node) {
    StyleValue* width_value = StyleAdapter::get_property(node, "width");
    if (!width_value) return 0.0f;

    float parent_width = get_parent_width(node);
    float font_size = get_font_size(node);

    return StyleAdapter::get_length_px(width_value, parent_width, font_size);
}
```

### 3.4 Files to Refactor

**Priority list**:

1. **`radiant/layout.cpp`** - Main layout logic
   - Replace `lxb_style_*` calls with `StyleAdapter::get_property()`
   - Replace `lxb_css_*` type checks with unified checks

2. **`radiant/box.cpp`** - Box model calculations
   - Refactor margin/padding/border retrieval
   - Use StyleAdapter for all dimension queries

3. **`radiant/inline.cpp`** - Inline layout
   - Font size, line height, text properties
   - Color, text-decoration, etc.

4. **`radiant/flex.cpp`** - Flexbox layout
   - flex-direction, justify-content, align-items
   - flex-grow, flex-shrink, flex-basis

5. **`radiant/table.cpp`** - Table layout
   - border-collapse, border-spacing
   - table-layout, vertical-align

**Refactoring pattern**:

```cpp
// For each Lexbor-specific call:
// 1. Identify property being accessed
// 2. Replace with StyleAdapter call
// 3. Test with both Lexbor and Lambda sources

// Example transformation:
// OLD:
lxb_css_property_t* prop = lxb_style_get_property(elem, "display");
if (prop && lxb_css_property_is_keyword(prop, "flex")) {
    do_flexbox_layout();
}

// NEW:
StyleValue* display = StyleAdapter::get_property(node, "display");
if (display && StyleAdapter::get_keyword(display) == CSS_DISPLAY_FLEX) {
    do_flexbox_layout();
}
```

---

## Phase 4: Integration Testing

### 4.1 Test Files

Create test HTML documents with various CSS features:

**File**: `test/layout/simple_box.html`
```html
<!DOCTYPE html>
<html>
<head>
    <style>
        body { margin: 20px; font-size: 16px; }
        .box {
            width: 200px;
            height: 100px;
            padding: 10px;
            margin: 5px;
            background-color: blue;
        }
    </style>
</head>
<body>
    <div class="box">Test Box</div>
</body>
</html>
```

**File**: `test/layout/flexbox.html`
```html
<!DOCTYPE html>
<html>
<head>
    <style>
        .container {
            display: flex;
            flex-direction: row;
            justify-content: space-between;
            width: 400px;
        }
        .item {
            width: 100px;
            height: 50px;
            background-color: red;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="item">1</div>
        <div class="item">2</div>
        <div class="item">3</div>
    </div>
</body>
</html>
```

### 4.2 Test Command

```bash
# Test basic layout
lambda layout test/layout/simple_box.html -o output/simple_box_layout.txt

# Test with external CSS
lambda layout test/layout/page.html -c test/layout/styles.css -o output/page_layout.txt

# Test with custom viewport
lambda layout test/layout/responsive.html -w 1024 -h 768 --debug
```

### 4.3 Comparison Testing

Create tests that compare Lexbor vs Lambda layouts:

**File**: `test/test_layout_equivalence.cpp`

```cpp
TEST(LayoutEquivalence, SimpleBoxLayout) {
    // Parse with Lexbor
    Document* lex_doc = load_html_doc_lexbor("test/layout/simple_box.html");
    ViewTree* lex_layout = run_layout(lex_doc->dom_tree, 800, 600);

    // Parse with Lambda
    Input* lambda_input = parse_html_lambda("test/layout/simple_box.html");
    DomNode* lambda_root = lambda_to_domnode(lambda_input->root);
    ViewTree* lambda_layout = run_layout(lambda_root, 800, 600);

    // Compare layouts
    EXPECT_LAYOUT_EQUIVALENT(lex_layout, lambda_layout);
}
```

---

## Phase 5: Optimization & Polish

### 5.1 Performance Considerations

- **Caching**: Cache DomElement lookups for Lambda nodes
- **Pool allocation**: Ensure all Lambda structures use pool allocator
- **AVL tree access**: Minimize CSS property lookups with caching

### 5.2 Error Handling

- Graceful fallback if CSS parsing fails
- Warning messages for unsupported CSS properties
- Debug mode to trace style application

### 5.3 Documentation

Update documentation:
- `doc/Lambda_Layout.md` - User guide for layout command
- `doc/Radiant_Integration.md` - Developer guide for style adapter
- `doc/CSS_Properties.md` - Supported CSS properties matrix

---

## Implementation Timeline

### Week 1: DomNode Enhancement
- [ ] Implement Lambda element methods
- [ ] Enhance navigation methods
- [ ] Add unified attribute access
- [ ] Unit tests for DomNode operations

### Week 2: Layout Command
- [ ] Implement `cmd_layout.cpp`
- [ ] Add helper functions
- [ ] Create test HTML files
- [ ] Basic end-to-end test

### Week 3: Style Adapter
- [ ] Design StyleAdapter interface
- [ ] Implement get_property methods
- [ ] Implement value conversion functions
- [ ] Unit tests for StyleAdapter

### Week 4: Radiant Refactoring
- [ ] Audit all Lexbor dependencies
- [ ] Refactor layout.cpp
- [ ] Refactor box.cpp
- [ ] Refactor inline.cpp

### Week 5: Integration & Testing
- [ ] Create comprehensive test suite
- [ ] Compare Lexbor vs Lambda layouts
- [ ] Performance testing
- [ ] Bug fixes

### Week 6: Polish & Documentation
- [ ] Error handling improvements
- [ ] Debug output enhancements
- [ ] Write user documentation
- [ ] Code cleanup and comments

---

## Success Criteria

- [ ] `lambda layout` command successfully layouts HTML with embedded CSS
- [ ] External CSS file support working
- [ ] Layout output identical (or semantically equivalent) between Lexbor and Lambda
- [ ] All existing Radiant tests still passing
- [ ] New tests covering Lambda CSS path
- [ ] Performance within 10% of Lexbor path
- [ ] Documentation complete and clear

---

## Future Enhancements

1. **CSS Cascade**: Full cascade resolution including inheritance
2. **CSS Selectors**: Complete selector matching (currently simplified)
3. **Media Queries**: Responsive layout support
4. **CSS Grid**: Add grid layout support
5. **Animations**: CSS transitions and animations
6. **Print Layout**: Page-based layout for PDF output
7. **SVG**: Inline SVG layout integration

---

## See Also

- `doc/Html_To_Mark.md` - HTML parsing and mapping
- `doc/CSS_Integration.md` - CSS system architecture
- `vibe/Radiant_CSS.md` - Radiant CSS overview
- `test/test_html_css_gtest.cpp` - HTML/CSS integration tests

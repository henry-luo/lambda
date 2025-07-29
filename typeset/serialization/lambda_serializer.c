#include "lambda_serializer.h"
#include "../../lambda/lambda.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Serialization options

SerializationOptions* serialization_options_create_default(void) {
    SerializationOptions* options = calloc(1, sizeof(SerializationOptions));
    if (!options) return NULL;
    
    options->pretty_print = true;
    options->indent_size = 2;
    options->include_metadata = true;
    options->include_positioning = true;
    options->include_styling = true;
    options->include_source_refs = false;
    options->serialize_text_runs = true;
    options->serialize_glyphs = false;
    options->merge_adjacent_text = false;
    options->expand_math_elements = true;
    options->include_math_metrics = true;
    options->include_geometry = true;
    options->simplify_paths = false;
    
    return options;
}

void serialization_options_destroy(SerializationOptions* options) {
    free(options);
}

// Lambda serializer

LambdaSerializer* lambda_serializer_create(Context* ctx, SerializationOptions* options) {
    LambdaSerializer* serializer = calloc(1, sizeof(LambdaSerializer));
    if (!serializer) return NULL;
    
    serializer->lambda_context = ctx;
    serializer->options = options ? options : serialization_options_create_default();
    serializer->current_indent = 0;
    serializer->output_buffer = strbuf_new();
    serializer->nodes_serialized = 0;
    serializer->warnings_generated = 0;
    
    return serializer;
}

void lambda_serializer_destroy(LambdaSerializer* serializer) {
    if (!serializer) return;
    
    strbuf_free(serializer->output_buffer);
    if (serializer->options) {
        serialization_options_destroy(serializer->options);
    }
    free(serializer);
}

// Main serialization functions

Item serialize_view_tree_to_lambda(LambdaSerializer* serializer, ViewTree* tree) {
    if (!serializer || !tree || !serializer->lambda_context) {
        return ITEM_NULL;
    }
    
    Context* ctx = serializer->lambda_context;
    
    // Create view-tree element
    Item tree_element = create_element(ctx, "view-tree");
    
    // Add metadata attributes
    if (serializer->options->include_metadata) {
        if (tree->title) {
            add_lambda_string_attribute(ctx, tree_element, "title", tree->title);
        }
        if (tree->author) {
            add_lambda_string_attribute(ctx, tree_element, "author", tree->author);
        }
        if (tree->creator) {
            add_lambda_string_attribute(ctx, tree_element, "creator", tree->creator);
        }
        if (tree->creation_date) {
            add_lambda_string_attribute(ctx, tree_element, "creation-date", tree->creation_date);
        }
        
        add_lambda_number_attribute(ctx, tree_element, "pages", tree->page_count);
    }
    
    // Add document size if available
    if (tree->document_size.width > 0 && tree->document_size.height > 0) {
        add_lambda_number_attribute(ctx, tree_element, "document-width", tree->document_size.width);
        add_lambda_number_attribute(ctx, tree_element, "document-height", tree->document_size.height);
    }
    
    // Serialize root node if it exists
    if (tree->root) {
        Item root_item = serialize_view_node_to_lambda(serializer, tree->root);
        if (root_item.type != ITEM_NULL) {
            add_child(ctx, tree_element, root_item);
        }
    }
    
    // Serialize pages
    for (int i = 0; i < tree->page_count; i++) {
        if (tree->pages[i]) {
            Item page_item = serialize_view_page_to_lambda(serializer, tree->pages[i]);
            if (page_item.type != ITEM_NULL) {
                add_child(ctx, tree_element, page_item);
            }
        }
    }
    
    serializer->nodes_serialized++;
    return tree_element;
}

Item serialize_view_node_to_lambda(LambdaSerializer* serializer, ViewNode* node) {
    if (!serializer || !node || !serializer->lambda_context) {
        return ITEM_NULL;
    }
    
    Context* ctx = serializer->lambda_context;
    const char* element_name = get_element_name_for_node_type(node->type);
    
    Item node_element = create_element(ctx, element_name);
    
    // Add positioning attributes if enabled
    if (serializer->options->include_positioning) {
        add_lambda_number_attribute(ctx, node_element, "x", node->position.x);
        add_lambda_number_attribute(ctx, node_element, "y", node->position.y);
        add_lambda_number_attribute(ctx, node_element, "width", node->size.width);
        add_lambda_number_attribute(ctx, node_element, "height", node->size.height);
    }
    
    // Add semantic role if present
    if (node->semantic_role) {
        add_lambda_string_attribute(ctx, node_element, "role", node->semantic_role);
    }
    
    // Add ID if present
    if (node->id) {
        add_lambda_string_attribute(ctx, node_element, "id", node->id);
    }
    
    // Add class if present
    if (node->class_name) {
        add_lambda_string_attribute(ctx, node_element, "class", node->class_name);
    }
    
    // Add visibility and opacity if not default
    if (!node->visible) {
        add_lambda_bool_attribute(ctx, node_element, "visible", false);
    }
    if (node->opacity != 1.0) {
        add_lambda_number_attribute(ctx, node_element, "opacity", node->opacity);
    }
    
    // Serialize content based on node type
    serialize_node_content_to_lambda(serializer, node, node_element);
    
    // Serialize children
    ViewNode* child = node->first_child;
    while (child) {
        Item child_item = serialize_view_node_to_lambda(serializer, child);
        if (child_item.type != ITEM_NULL) {
            add_child(ctx, node_element, child_item);
        }
        child = child->next_sibling;
    }
    
    serializer->nodes_serialized++;
    return node_element;
}

Item serialize_view_page_to_lambda(LambdaSerializer* serializer, ViewPage* page) {
    if (!serializer || !page || !serializer->lambda_context) {
        return ITEM_NULL;
    }
    
    Context* ctx = serializer->lambda_context;
    Item page_element = create_element(ctx, "page");
    
    // Add page attributes
    add_lambda_number_attribute(ctx, page_element, "number", page->page_number);
    add_lambda_number_attribute(ctx, page_element, "width", page->page_size.width);
    add_lambda_number_attribute(ctx, page_element, "height", page->page_size.height);
    
    if (page->is_landscape) {
        add_lambda_bool_attribute(ctx, page_element, "landscape", true);
    }
    
    if (page->page_label) {
        add_lambda_string_attribute(ctx, page_element, "label", page->page_label);
    }
    
    // Serialize page content node
    if (page->page_node) {
        Item page_content = serialize_view_node_to_lambda(serializer, page->page_node);
        if (page_content.type != ITEM_NULL) {
            add_child(ctx, page_element, page_content);
        }
    }
    
    return page_element;
}

// Content serialization

void serialize_node_content_to_lambda(LambdaSerializer* serializer, ViewNode* node, Item element) {
    if (!serializer || !node || !serializer->lambda_context) return;
    
    Context* ctx = serializer->lambda_context;
    
    switch (node->type) {
        case VIEW_NODE_TEXT_RUN:
            if (node->content.text_run && serializer->options->serialize_text_runs) {
                serialize_text_run_content(serializer, node->content.text_run, element);
            }
            break;
            
        case VIEW_NODE_MATH_ELEMENT:
            if (node->content.math_elem && serializer->options->expand_math_elements) {
                serialize_math_element_content(serializer, node->content.math_elem, element);
            }
            break;
            
        case VIEW_NODE_GROUP:
            if (node->content.group) {
                if (node->content.group->name) {
                    add_lambda_string_attribute(ctx, element, "name", node->content.group->name);
                }
            }
            break;
            
        default:
            // No special content serialization needed
            break;
    }
}

void serialize_text_run_content(LambdaSerializer* serializer, ViewTextRun* text_run, Item element) {
    if (!serializer || !text_run || !serializer->lambda_context) return;
    
    Context* ctx = serializer->lambda_context;
    
    // Add font information
    if (text_run->font_size > 0) {
        add_lambda_number_attribute(ctx, element, "font-size", text_run->font_size);
    }
    
    // Add color information
    if (text_run->color.r != 0.0 || text_run->color.g != 0.0 || 
        text_run->color.b != 0.0 || text_run->color.a != 1.0) {
        
        Item color_array = create_list(ctx);
        add_to_list(ctx, color_array, create_number(ctx, text_run->color.r));
        add_to_list(ctx, color_array, create_number(ctx, text_run->color.g));
        add_to_list(ctx, color_array, create_number(ctx, text_run->color.b));
        add_to_list(ctx, color_array, create_number(ctx, text_run->color.a));
        
        add_attribute(ctx, element, "color", color_array);
    }
    
    // Add text metrics
    if (text_run->total_width > 0) {
        add_lambda_number_attribute(ctx, element, "text-width", text_run->total_width);
    }
    if (text_run->ascent > 0) {
        add_lambda_number_attribute(ctx, element, "ascent", text_run->ascent);
    }
    if (text_run->descent > 0) {
        add_lambda_number_attribute(ctx, element, "descent", text_run->descent);
    }
    
    // Add the actual text content as child
    if (text_run->text) {
        Item text_content = create_string(ctx, text_run->text);
        add_child(ctx, element, text_content);
    }
}

void serialize_math_element_content(LambdaSerializer* serializer, ViewMathElement* math_elem, Item element) {
    if (!serializer || !math_elem || !serializer->lambda_context) return;
    
    Context* ctx = serializer->lambda_context;
    
    // Add math-specific attributes
    const char* math_style_name = get_math_style_name(math_elem->math_style);
    if (math_style_name) {
        add_lambda_string_attribute(ctx, element, "math-style", math_style_name);
    }
    
    const char* math_class_name = get_math_class_name(math_elem->math_class);
    if (math_class_name) {
        add_lambda_string_attribute(ctx, element, "math-class", math_class_name);
    }
    
    if (math_elem->is_cramped) {
        add_lambda_bool_attribute(ctx, element, "cramped", true);
    }
    
    // Add mathematical metrics if enabled
    if (serializer->options->include_math_metrics) {
        if (math_elem->width > 0) {
            add_lambda_number_attribute(ctx, element, "math-width", math_elem->width);
        }
        if (math_elem->height > 0) {
            add_lambda_number_attribute(ctx, element, "math-height", math_elem->height);
        }
        if (math_elem->depth > 0) {
            add_lambda_number_attribute(ctx, element, "math-depth", math_elem->depth);
        }
        if (math_elem->axis_height > 0) {
            add_lambda_number_attribute(ctx, element, "axis-height", math_elem->axis_height);
        }
    }
}

// Utility functions

const char* get_element_name_for_node_type(ViewNodeType type) {
    switch (type) {
        case VIEW_NODE_DOCUMENT: return "document";
        case VIEW_NODE_PAGE: return "page";
        case VIEW_NODE_BLOCK: return "block";
        case VIEW_NODE_INLINE: return "inline";
        case VIEW_NODE_TEXT_RUN: return "text-run";
        case VIEW_NODE_MATH_ELEMENT: return "math-element";
        case VIEW_NODE_GLYPH: return "glyph";
        case VIEW_NODE_LINE: return "line";
        case VIEW_NODE_RECTANGLE: return "rectangle";
        case VIEW_NODE_PATH: return "path";
        case VIEW_NODE_GROUP: return "group";
        case VIEW_NODE_TRANSFORM: return "transform";
        case VIEW_NODE_CLIPPING: return "clipping";
        default: return "unknown";
    }
}

const char* get_math_style_name(int math_style) {
    switch (math_style) {
        case VIEW_MATH_DISPLAY: return "display";
        case VIEW_MATH_TEXT: return "text";
        case VIEW_MATH_SCRIPT: return "script";
        case VIEW_MATH_SCRIPTSCRIPT: return "scriptscript";
        default: return "text";
    }
}

const char* get_math_class_name(int math_class) {
    switch (math_class) {
        case VIEW_MATH_ORD: return "ordinary";
        case VIEW_MATH_OP: return "operator";
        case VIEW_MATH_BIN: return "binary";
        case VIEW_MATH_REL: return "relation";
        case VIEW_MATH_OPEN: return "opening";
        case VIEW_MATH_CLOSE: return "closing";
        case VIEW_MATH_PUNCT: return "punctuation";
        case VIEW_MATH_INNER: return "inner";
        default: return "ordinary";
    }
}

void add_lambda_string_attribute(Context* ctx, Item element, const char* name, const char* value) {
    if (!ctx || !name || !value) return;
    Item string_value = create_string(ctx, value);
    add_attribute(ctx, element, name, string_value);
}

void add_lambda_number_attribute(Context* ctx, Item element, const char* name, double value) {
    if (!ctx || !name) return;
    Item number_value = create_number(ctx, value);
    add_attribute(ctx, element, name, number_value);
}

void add_lambda_bool_attribute(Context* ctx, Item element, const char* name, bool value) {
    if (!ctx || !name) return;
    Item bool_value = create_boolean(ctx, value);
    add_attribute(ctx, element, name, bool_value);
}

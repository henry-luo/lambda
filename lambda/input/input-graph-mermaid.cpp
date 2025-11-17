#include "input-graph.h"
#include "../mark_builder.hpp"
#include <ctype.h>
#include <string.h>

// Forward declarations for Mermaid parsing
static void skip_whitespace_and_comments_mermaid(const char **mermaid);
static String* parse_mermaid_identifier(Input* input, MarkBuilder* builder, const char **mermaid);
static String* parse_mermaid_label(Input* input, MarkBuilder* builder, const char **mermaid);
static void parse_mermaid_node_def(Input* input, MarkBuilder* builder, Element* graph, const char **mermaid);
static void parse_mermaid_edge_def(Input* input, MarkBuilder* builder, Element* graph, const char **mermaid);
static void parse_mermaid_class_def(Input* input, MarkBuilder* builder, Element* graph, const char **mermaid);
static String* parse_mermaid_node_shape(Input* input, MarkBuilder* builder, const char **mermaid, const char* node_id);

// Skip whitespace and comments in Mermaid
static void skip_whitespace_and_comments_mermaid(const char **mermaid) {
    while (**mermaid) {
        // Skip whitespace
        if (isspace(**mermaid)) {
            (*mermaid)++;
            continue;
        }

        // Skip %% comments
        if (**mermaid == '%' && *(*mermaid + 1) == '%') {
            while (**mermaid && **mermaid != '\n') {
                (*mermaid)++;
            }
            continue;
        }

        break;
    }
}

// Parse Mermaid identifier (alphanumeric + underscore + dash)
static String* parse_mermaid_identifier(Input* input, MarkBuilder* builder, const char **mermaid) {
    skip_whitespace_and_comments_mermaid(mermaid);

    const char* start = *mermaid;
    if (!isalpha(**mermaid) && **mermaid != '_') {
        return NULL;
    }

    while (isalnum(**mermaid) || **mermaid == '_' || **mermaid == '-') {
        (*mermaid)++;
    }

    int len = *mermaid - start;
    return builder->createString(start, len);
}

// Parse Mermaid node shape and extract label
static String* parse_mermaid_node_shape(Input* input, MarkBuilder* builder, const char **mermaid, const char* node_id) {
    skip_whitespace_and_comments_mermaid(mermaid);

    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);

    char open_char = **mermaid;
    char close_char = 0;
    const char* shape = "box"; // default shape

    // Determine shape based on opening character(s)
    if (open_char == '[') {
        close_char = ']';
        shape = "box";
        (*mermaid)++;
    } else if (open_char == '(' && *(*mermaid + 1) == '(') {
        close_char = ')';
        shape = "circle";
        *mermaid += 2;
        // Look for closing ))
    } else if (open_char == '(') {
        close_char = ')';
        shape = "ellipse";
        (*mermaid)++;
    } else if (open_char == '{') {
        close_char = '}';
        shape = "diamond";
        (*mermaid)++;
    } else if (open_char == '>') {
        close_char = ']';
        shape = "pentagon";
        (*mermaid)++;
    } else {
        // No shape specified, return node_id as label
        return builder->createString(node_id);
    }

    // Extract label text
    while (**mermaid && **mermaid != close_char) {
        if (**mermaid == '\\') {
            (*mermaid)++;
            if (**mermaid) {
                stringbuf_append_char(sb, **mermaid);
                (*mermaid)++;
            }
        } else {
            stringbuf_append_char(sb, **mermaid);
            (*mermaid)++;
        }
    }

    // Skip closing character(s)
    if (**mermaid == close_char) {
        (*mermaid)++;
        if (strcmp(shape, "circle") == 0 && **mermaid == ')') {
            (*mermaid)++; // Skip second ) for circle
        }
    }

    String* label = builder->createString(sb->str->chars, sb->length);
    return label;
}

// Parse Mermaid label (text in quotes or brackets)
static String* parse_mermaid_label(Input* input, MarkBuilder* builder, const char **mermaid) {
    skip_whitespace_and_comments_mermaid(mermaid);

    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);

    char quote_char = 0;
    if (**mermaid == '"' || **mermaid == '\'') {
        quote_char = **mermaid;
        (*mermaid)++;
    } else if (**mermaid == '|') {
        quote_char = '|';
        (*mermaid)++;
    } else {
        return NULL;
    }

    while (**mermaid && **mermaid != quote_char) {
        if (**mermaid == '\\') {
            (*mermaid)++;
            if (**mermaid) {
                stringbuf_append_char(sb, **mermaid);
                (*mermaid)++;
            }
        } else {
            stringbuf_append_char(sb, **mermaid);
            (*mermaid)++;
        }
    }

    if (**mermaid == quote_char) {
        (*mermaid)++;
    }

    return builder->createString(sb->str->chars, sb->length);
}

// Parse Mermaid node definition: nodeId[label] or nodeId(label) etc.
static void parse_mermaid_node_def(Input* input, MarkBuilder* builder, Element* graph, const char **mermaid) {
    skip_whitespace_and_comments_mermaid(mermaid);

    // Parse node ID
    String* node_id = parse_mermaid_identifier(input, builder, mermaid);
    if (!node_id) {
        return;
    }

    skip_whitespace_and_comments_mermaid(mermaid);

    // Parse node shape and label
    String* label = parse_mermaid_node_shape(input, builder, mermaid, node_id->chars);
    if (!label) {
        label = node_id; // Use ID as label if no shape specified
    }

    // Create node element
    Element* node = create_node_element(input, node_id->chars, label->chars);

    // Add to graph
    add_node_to_graph(input, graph, node);
}

// Parse Mermaid edge definition: nodeA --> nodeB or nodeA -.-> nodeB etc.
static void parse_mermaid_edge_def(Input* input, MarkBuilder* builder, Element* graph, const char **mermaid) {
    skip_whitespace_and_comments_mermaid(mermaid);

    // Parse from node
    String* from_id = parse_mermaid_identifier(input, builder, mermaid);
    if (!from_id) {
        return;
    }

    // Check if from node has shape definition
    const char* checkpoint = *mermaid;
    skip_whitespace_and_comments_mermaid(mermaid);
    if (**mermaid == '[' || **mermaid == '(' || **mermaid == '{' || **mermaid == '>') {
        // From node has shape, parse it
        String* from_label = parse_mermaid_node_shape(input, builder, mermaid, from_id->chars);
        Element* from_node = create_node_element(input, from_id->chars, from_label->chars);
        add_node_to_graph(input, graph, from_node);
    } else {
        *mermaid = checkpoint; // Reset if no shape found
    }

    skip_whitespace_and_comments_mermaid(mermaid);

    // Parse edge operator
    String* edge_style = builder->createString("solid");
    String* edge_label = NULL;

    // Detect edge type
    if (strncmp(*mermaid, "-->", 3) == 0) {
        *mermaid += 3;
        edge_style = builder->createString("solid");
    } else if (strncmp(*mermaid, "-.->", 4) == 0) {
        *mermaid += 4;
        edge_style = builder->createString("dashed");
    } else if (strncmp(*mermaid, "==>", 3) == 0) {
        *mermaid += 3;
        edge_style = builder->createString("bold");
    } else if (strncmp(*mermaid, "---", 3) == 0) {
        *mermaid += 3;
        edge_style = builder->createString("solid");
    } else if (strncmp(*mermaid, "-.-", 3) == 0) {
        *mermaid += 3;
        edge_style = builder->createString("dashed");
    } else if (strncmp(*mermaid, "===", 3) == 0) {
        *mermaid += 3;
        edge_style = builder->createString("bold");
    } else {
        return; // No valid edge operator
    }

    skip_whitespace_and_comments_mermaid(mermaid);

    // Check for edge label: |label|
    if (**mermaid == '|') {
        edge_label = parse_mermaid_label(input, builder, mermaid);
        skip_whitespace_and_comments_mermaid(mermaid);
    }

    // Parse to node
    String* to_id = parse_mermaid_identifier(input, builder, mermaid);
    if (!to_id) {
        return;
    }

    // Check if to node has shape definition
    checkpoint = *mermaid;
    skip_whitespace_and_comments_mermaid(mermaid);
    if (**mermaid == '[' || **mermaid == '(' || **mermaid == '{' || **mermaid == '>') {
        // To node has shape, parse it
        String* to_label = parse_mermaid_node_shape(input, builder, mermaid, to_id->chars);
        Element* to_node = create_node_element(input, to_id->chars, to_label->chars);
        add_node_to_graph(input, graph, to_node);
    } else {
        *mermaid = checkpoint; // Reset if no shape found
    }

    // Create edge element
    Element* edge = create_edge_element(input, from_id->chars, to_id->chars,
                                       edge_label ? edge_label->chars : NULL);

    // Add edge style with CSS-aligned naming
    // The add_graph_attribute function will convert "style" to "stroke-dasharray" for edges
    add_graph_attribute(input, edge, "style", edge_style->chars);

    // Add to graph
    add_edge_to_graph(input, graph, edge);
}

// Parse Mermaid class definition: classDef className fill:#color
static void parse_mermaid_class_def(Input* input, MarkBuilder* builder, Element* graph, const char **mermaid) {
    skip_whitespace_and_comments_mermaid(mermaid);

    // Skip "classDef"
    if (strncmp(*mermaid, "classDef", 8) == 0) {
        *mermaid += 8;
    } else {
        return;
    }

    skip_whitespace_and_comments_mermaid(mermaid);

    // Parse class name
    String* class_name = parse_mermaid_identifier(input, builder, mermaid);
    if (!class_name) {
        return;
    }

    skip_whitespace_and_comments_mermaid(mermaid);

    // Parse class properties (simplified - just collect as string)
    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);

    while (**mermaid && **mermaid != '\n' && **mermaid != ';') {
        stringbuf_append_char(sb, **mermaid);
        (*mermaid)++;
    }

    String* class_props = builder->createString(sb->str->chars, sb->length);

    // Store class definition as graph attribute
    StringBuf* class_key_sb = builder->stringBuf();
    stringbuf_reset(class_key_sb);
    stringbuf_append_str(class_key_sb, "classDef_");
    stringbuf_append_str(class_key_sb, class_name->chars);
    String* class_key = builder->createString(class_key_sb->str->chars, class_key_sb->length);

    add_graph_attribute(input, graph, class_key->chars, class_props->chars);
}

// Main Mermaid parser function
void parse_graph_mermaid(Input* input, const char* mermaid_string) {
    MarkBuilder builder(input);

    const char* mermaid = mermaid_string;
    skip_whitespace_and_comments_mermaid(&mermaid);

    // Detect diagram type
    String* diagram_type = NULL;

    if (strncmp(mermaid, "graph", 5) == 0) {
        diagram_type = builder.createString("flowchart");
        mermaid += 5;

        // Check for direction
        skip_whitespace_and_comments_mermaid(&mermaid);
        if (strncmp(mermaid, "TD", 2) == 0 || strncmp(mermaid, "TB", 2) == 0) {
            mermaid += 2;
        } else if (strncmp(mermaid, "LR", 2) == 0) {
            mermaid += 2;
        } else if (strncmp(mermaid, "RL", 2) == 0) {
            mermaid += 2;
        } else if (strncmp(mermaid, "BT", 2) == 0) {
            mermaid += 2;
        }
    } else if (strncmp(mermaid, "flowchart", 9) == 0) {
        diagram_type = builder.createString("flowchart");
        mermaid += 9;

        // Check for direction
        skip_whitespace_and_comments_mermaid(&mermaid);
        if (strncmp(mermaid, "TD", 2) == 0 || strncmp(mermaid, "TB", 2) == 0) {
            mermaid += 2;
        } else if (strncmp(mermaid, "LR", 2) == 0) {
            mermaid += 2;
        }
    } else if (strncmp(mermaid, "sequenceDiagram", 15) == 0) {
        diagram_type = builder.createString("sequence");
        mermaid += 15;
    } else {
        // Default to flowchart
        diagram_type = builder.createString("flowchart");
    }

    // Create main graph element with CSS-aligned attributes
    Element* graph = create_graph_element(input, "directed", "mermaid", "mermaid");
    add_graph_attribute(input, graph, "diagram-type", diagram_type->chars);
    add_graph_attribute(input, graph, "directed", "true"); // Mermaid flowcharts are typically directed

    // Parse diagram content
    while (*mermaid) {
        skip_whitespace_and_comments_mermaid(&mermaid);

        if (!*mermaid) {
            break;
        }

        const char* line_start = mermaid;

        // Check for classDef
        if (strncmp(mermaid, "classDef", 8) == 0) {
            parse_mermaid_class_def(input, &builder, graph, &mermaid);
            continue;
        }

        // Try to parse as edge (look for edge operators)
        const char* checkpoint = mermaid;
        String* potential_node = parse_mermaid_identifier(input, &builder, &mermaid);

        if (potential_node) {
            // Check if node has shape
            skip_whitespace_and_comments_mermaid(&mermaid);
            if (*mermaid == '[' || *mermaid == '(' || *mermaid == '{' || *mermaid == '>') {
                // Skip node shape
                String* temp_label = parse_mermaid_node_shape(input, &builder, &mermaid, potential_node->chars);
                skip_whitespace_and_comments_mermaid(&mermaid);
            }

            // Look for edge operator
            if (strncmp(mermaid, "-->", 3) == 0 || strncmp(mermaid, "-.->", 4) == 0 ||
                strncmp(mermaid, "==>", 3) == 0 || strncmp(mermaid, "---", 3) == 0 ||
                strncmp(mermaid, "-.-", 3) == 0 || strncmp(mermaid, "===", 3) == 0) {
                // This is an edge - reset and parse as edge
                mermaid = checkpoint;
                parse_mermaid_edge_def(input, &builder, graph, &mermaid);
            } else {
                // This is a standalone node - reset and parse as node
                mermaid = checkpoint;
                parse_mermaid_node_def(input, &builder, graph, &mermaid);
            }
        }

        // Skip to next line if we haven't made progress
        if (mermaid == line_start) {
            while (*mermaid && *mermaid != '\n') {
                mermaid++;
            }
            if (*mermaid == '\n') {
                mermaid++;
            }
        }
    }

    // Set graph as root
    input->root = {.element = graph};
}

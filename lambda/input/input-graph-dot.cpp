#include "input-graph.h"
#include <ctype.h>
#include <string.h>

// Forward declarations for internal functions
static void skip_whitespace_and_comments(const char **dot);
static String* parse_identifier(Input* input, const char **dot);
static String* parse_quoted_string(Input* input, const char **dot);
static String* parse_attribute_value(Input* input, const char **dot);
static void parse_attribute_list(Input* input, Element* element, const char **dot);
static Element* parse_node_statement(Input* input, const char **dot);
static Element* parse_edge_statement(Input* input, const char **dot);
static void parse_subgraph(Input* input, Element* graph, const char **dot);

// Skip whitespace and comments
static void skip_whitespace_and_comments(const char **dot) {
    while (**dot) {
        // Skip whitespace
        if (isspace(**dot)) {
            (*dot)++;
            continue;
        }
        
        // Skip // comments
        if (**dot == '/' && *(*dot + 1) == '/') {
            while (**dot && **dot != '\n') {
                (*dot)++;
            }
            continue;
        }
        
        // Skip /* */ comments
        if (**dot == '/' && *(*dot + 1) == '*') {
            *dot += 2;
            while (**dot && !(**dot == '*' && *(*dot + 1) == '/')) {
                (*dot)++;
            }
            if (**dot == '*' && *(*dot + 1) == '/') {
                *dot += 2;
            }
            continue;
        }
        
        // Skip # comments (also valid in DOT)
        if (**dot == '#') {
            while (**dot && **dot != '\n') {
                (*dot)++;
            }
            continue;
        }
        
        break;
    }
}

// Parse identifier (alphanumeric + underscore, can start with letter or underscore)
static String* parse_identifier(Input* input, const char **dot) {
    skip_whitespace_and_comments(dot);
    
    const char* start = *dot;
    if (!isalpha(**dot) && **dot != '_') {
        return NULL;
    }
    
    while (isalnum(**dot) || **dot == '_') {
        (*dot)++;
    }
    
    int len = *dot - start;
    return create_input_string(input, start, 0, len);
}

// Parse quoted string
static String* parse_quoted_string(Input* input, const char **dot) {
    skip_whitespace_and_comments(dot);
    
    if (**dot != '"') {
        return NULL;
    }
    
    StringBuf* sb = input->sb;
    stringbuf_reset(sb);
    
    (*dot)++; // Skip opening quote
    while (**dot && **dot != '"') {
        if (**dot == '\\') {
            (*dot)++;
            switch (**dot) {
                case '"': stringbuf_append_char(sb, '"'); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                default: 
                    stringbuf_append_char(sb, '\\');
                    stringbuf_append_char(sb, **dot);
                    break;
            }
        } else {
            stringbuf_append_char(sb, **dot);
        }
        (*dot)++;
    }
    
    if (**dot == '"') {
        (*dot)++; // Skip closing quote
    }
    
    return stringbuf_to_string(sb);
}

// Parse attribute value (identifier or quoted string)
static String* parse_attribute_value(Input* input, const char **dot) {
    skip_whitespace_and_comments(dot);
    
    if (**dot == '"') {
        return parse_quoted_string(input, dot);
    } else {
        return parse_identifier(input, dot);
    }
}

// Parse attribute list [attr1=value1, attr2=value2, ...]
static void parse_attribute_list(Input* input, Element* element, const char **dot) {
    skip_whitespace_and_comments(dot);
    
    if (**dot != '[') {
        return;
    }
    
    (*dot)++; // Skip [
    
    while (**dot && **dot != ']') {
        skip_whitespace_and_comments(dot);
        
        // Parse attribute name
        String* attr_name = parse_identifier(input, dot);
        if (!attr_name) {
            break;
        }
        
        skip_whitespace_and_comments(dot);
        
        // Expect =
        if (**dot != '=') {
            break;
        }
        (*dot)++;
        
        skip_whitespace_and_comments(dot);
        
        // Parse attribute value
        String* attr_value = parse_attribute_value(input, dot);
        if (!attr_value) {
            break;
        }
        
        // Add attribute to element
        add_graph_attribute(input, element, attr_name->chars, attr_value->chars);
        
        skip_whitespace_and_comments(dot);
        
        // Skip comma if present
        if (**dot == ',') {
            (*dot)++;
        }
    }
    
    if (**dot == ']') {
        (*dot)++; // Skip ]
    }
}

// Parse node statement: node_id [attributes]
static Element* parse_node_statement(Input* input, const char **dot) {
    skip_whitespace_and_comments(dot);
    
    // Parse node ID
    String* node_id = parse_identifier(input, dot);
    if (!node_id) {
        node_id = parse_quoted_string(input, dot);
    }
    if (!node_id) {
        return NULL;
    }
    
    // Create node element
    Element* node = create_node_element(input, node_id->chars, node_id->chars);
    
    // Parse optional attributes
    parse_attribute_list(input, node, dot);
    
    return node;
}

// Parse edge statement: node1 -> node2 [attributes] or node1 -- node2 [attributes]
static Element* parse_edge_statement(Input* input, const char **dot) {
    skip_whitespace_and_comments(dot);
    
    // Parse from node
    String* from_id = parse_identifier(input, dot);
    if (!from_id) {
        from_id = parse_quoted_string(input, dot);
    }
    if (!from_id) {
        return NULL;
    }
    
    skip_whitespace_and_comments(dot);
    
    // Parse edge operator (-> or --)
    bool is_directed = false;
    if (**dot == '-') {
        (*dot)++;
        if (**dot == '>') {
            (*dot)++;
            is_directed = true;
        } else if (**dot == '-') {
            (*dot)++;
            is_directed = false;
        } else {
            return NULL; // Invalid edge operator
        }
    } else {
        return NULL; // No edge operator found
    }
    
    skip_whitespace_and_comments(dot);
    
    // Parse to node
    String* to_id = parse_identifier(input, dot);
    if (!to_id) {
        to_id = parse_quoted_string(input, dot);
    }
    if (!to_id) {
        return NULL;
    }
    
    // Create edge element
    Element* edge = create_edge_element(input, from_id->chars, to_id->chars, NULL);
    
    // Add direction attribute with CSS-aligned naming
    add_graph_attribute(input, edge, "direction", is_directed ? "forward" : "none");
    
    // Parse optional attributes
    parse_attribute_list(input, edge, dot);
    
    return edge;
}

// Parse subgraph or cluster
static void parse_subgraph(Input* input, Element* graph, const char **dot) {
    skip_whitespace_and_comments(dot);
    
    // Look for "subgraph" or "cluster"
    if (strncmp(*dot, "subgraph", 8) == 0) {
        *dot += 8;
    } else if (strncmp(*dot, "cluster", 7) == 0) {
        *dot += 7;
    } else {
        return;
    }
    
    skip_whitespace_and_comments(dot);
    
    // Parse optional subgraph name
    String* subgraph_id = parse_identifier(input, dot);
    if (!subgraph_id) {
        subgraph_id = parse_quoted_string(input, dot);
    }
    
    // Default ID if none provided
    if (!subgraph_id) {
        subgraph_id = input_create_string(input, "subgraph");
    }
    
    skip_whitespace_and_comments(dot);
    
    // Expect {
    if (**dot != '{') {
        return;
    }
    (*dot)++;
    
    // Create cluster element
    Element* cluster = create_cluster_element(input, subgraph_id->chars, subgraph_id->chars);
    
    // Parse statements within subgraph
    while (**dot && **dot != '}') {
        skip_whitespace_and_comments(dot);
        
        if (**dot == '}') {
            break;
        }
        
        // Try to parse node or edge statement
        const char* checkpoint = *dot;
        
        // Look ahead to determine if this is an edge statement
        String* first_id = parse_identifier(input, dot);
        if (!first_id) {
            first_id = parse_quoted_string(input, dot);
        }
        
        if (first_id) {
            skip_whitespace_and_comments(dot);
            
            // Check for edge operator
            if ((**dot == '-' && (*(*dot + 1) == '>' || *(*dot + 1) == '-'))) {
                // This is an edge statement - reset and parse as edge
                *dot = checkpoint;
                Element* edge = parse_edge_statement(input, dot);
                if (edge) {
                    add_edge_to_graph(input, cluster, edge);
                }
            } else {
                // This is a node statement - reset and parse as node
                *dot = checkpoint;
                Element* node = parse_node_statement(input, dot);
                if (node) {
                    add_node_to_graph(input, cluster, node);
                }
            }
        }
        
        skip_whitespace_and_comments(dot);
        
        // Skip optional semicolon
        if (**dot == ';') {
            (*dot)++;
        }
        
        // Prevent infinite loop
        if (*dot == checkpoint) {
            (*dot)++;
        }
    }
    
    if (**dot == '}') {
        (*dot)++; // Skip }
    }
    
    // Add cluster to graph
    add_cluster_to_graph(input, graph, cluster);
}

// Main DOT parser function
void parse_graph_dot(Input* input, const char* dot_string) {
    const char* dot = dot_string;
    skip_whitespace_and_comments(&dot);
    
    // Parse graph type (strict? (di)graph name? { ... })
    bool is_strict = false;
    bool is_directed = false;
    String* graph_name = NULL;
    
    // Check for "strict"
    if (strncmp(dot, "strict", 6) == 0 && !isalnum(dot[6])) {
        is_strict = true;
        dot += 6;
        skip_whitespace_and_comments(&dot);
    }
    
    // Check for "digraph" or "graph"
    if (strncmp(dot, "digraph", 7) == 0 && !isalnum(dot[7])) {
        is_directed = true;
        dot += 7;
    } else if (strncmp(dot, "graph", 5) == 0 && !isalnum(dot[5])) {
        is_directed = false;
        dot += 5;
    } else {
        // Invalid graph declaration
        return;
    }
    
    skip_whitespace_and_comments(&dot);
    
    // Parse optional graph name
    graph_name = parse_identifier(input, &dot);
    if (!graph_name) {
        graph_name = parse_quoted_string(input, &dot);
    }
    
    skip_whitespace_and_comments(&dot);
    
    // Expect {
    if (*dot != '{') {
        return;
    }
    dot++;
    
    // Create main graph element
    Element* graph = create_graph_element(input, 
        is_directed ? "directed" : "undirected", 
        "dot", 
        "dot");
    
    // Add graph attributes with CSS-aligned naming
    if (graph_name) {
        add_graph_attribute(input, graph, "name", graph_name->chars);
    }
    if (is_strict) {
        add_graph_attribute(input, graph, "strict", "true");
    }
    // Add the directed attribute as a boolean
    add_graph_attribute(input, graph, "directed", is_directed ? "true" : "false");
    
    // Parse graph statements
    while (*dot && *dot != '}') {
        skip_whitespace_and_comments(&dot);
        
        if (*dot == '}') {
            break;
        }
        
        // Check for subgraph
        if (strncmp(dot, "subgraph", 8) == 0 || strncmp(dot, "cluster", 7) == 0) {
            parse_subgraph(input, graph, &dot);
            continue;
        }
        
        // Try to parse node or edge statement
        const char* checkpoint = dot;
        
        // Look ahead to determine statement type
        String* first_id = parse_identifier(input, &dot);
        if (!first_id) {
            first_id = parse_quoted_string(input, &dot);
        }
        
        if (first_id) {
            skip_whitespace_and_comments(&dot);
            
            // Check for edge operator
            if ((*dot == '-' && (*(dot + 1) == '>' || *(dot + 1) == '-'))) {
                // This is an edge statement - reset and parse as edge
                dot = checkpoint;
                Element* edge = parse_edge_statement(input, &dot);
                if (edge) {
                    add_edge_to_graph(input, graph, edge);
                }
            } else {
                // This is a node statement - reset and parse as node
                dot = checkpoint;
                Element* node = parse_node_statement(input, &dot);
                if (node) {
                    add_node_to_graph(input, graph, node);
                }
            }
        }
        
        skip_whitespace_and_comments(&dot);
        
        // Skip optional semicolon
        if (*dot == ';') {
            dot++;
        }
        
        // Prevent infinite loop
        if (dot == checkpoint) {
            dot++;
        }
    }
    
    if (*dot == '}') {
        dot++; // Skip }
    }
    
    // Set graph as root
    input->root.container = (Container*)graph;
    input->root.type_id = LMD_TYPE_ELEMENT;
}
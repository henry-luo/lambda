#include "input-graph.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// D2 parser implementation
// D2 (https://d2lang.com/) is a modern diagramming language with clean syntax
// Examples:
//   x -> y
//   x -> y: Label
//   x.shape: circle
//   x.style: {fill: red; stroke: blue}

typedef struct D2Parser {
    const char* input;
    const char* current;
    size_t length;
    size_t position;
} D2Parser;

// D2 parsing utility functions
static void d2_skip_whitespace(D2Parser* parser) {
    while (parser->position < parser->length &&
           (isspace(parser->current[0]) || parser->current[0] == '\n' || parser->current[0] == '\r')) {
        parser->current++;
        parser->position++;
    }
}

static void d2_skip_comment(D2Parser* parser) {
    if (parser->position < parser->length && parser->current[0] == '#') {
        // Skip until end of line
        while (parser->position < parser->length && parser->current[0] != '\n') {
            parser->current++;
            parser->position++;
        }
    }
}

static void d2_skip_whitespace_and_comments(D2Parser* parser) {
    while (parser->position < parser->length) {
        if (isspace(parser->current[0]) || parser->current[0] == '\n' || parser->current[0] == '\r') {
            d2_skip_whitespace(parser);
        } else if (parser->current[0] == '#') {
            d2_skip_comment(parser);
        } else {
            break;
        }
    }
}

static char* d2_parse_identifier(D2Parser* parser) {
    const char* start = parser->current;
    size_t len = 0;

    // D2 identifiers can contain letters, numbers, underscores, hyphens, and dots
    while (parser->position < parser->length &&
           (isalnum(parser->current[0]) || parser->current[0] == '_' ||
            parser->current[0] == '-' || parser->current[0] == '.')) {
        parser->current++;
        parser->position++;
        len++;
    }

    if (len == 0) return NULL;

    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    strncpy(result, start, len);
    result[len] = '\0';
    return result;
}

static char* d2_parse_quoted_string(D2Parser* parser) {
    if (parser->position >= parser->length || parser->current[0] != '"') {
        return NULL;
    }

    parser->current++; // Skip opening quote
    parser->position++;

    const char* start = parser->current;
    size_t len = 0;

    while (parser->position < parser->length && parser->current[0] != '"') {
        if (parser->current[0] == '\\' && parser->position + 1 < parser->length) {
            // Skip escaped character
            parser->current += 2;
            parser->position += 2;
            len += 2;
        } else {
            parser->current++;
            parser->position++;
            len++;
        }
    }

    if (parser->position >= parser->length) {
        return NULL; // Unterminated string
    }

    parser->current++; // Skip closing quote
    parser->position++;

    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    strncpy(result, start, len);
    result[len] = '\0';
    return result;
}

static char* d2_parse_label(D2Parser* parser) {
    d2_skip_whitespace_and_comments(parser);

    if (parser->position >= parser->length) {
        return NULL;
    }

    if (parser->current[0] == '"') {
        return d2_parse_quoted_string(parser);
    } else {
        // Parse until end of line or special character
        const char* start = parser->current;
        size_t len = 0;

        while (parser->position < parser->length &&
               parser->current[0] != '\n' && parser->current[0] != '\r' &&
               parser->current[0] != '{' && parser->current[0] != '}' &&
               parser->current[0] != '#') {
            parser->current++;
            parser->position++;
            len++;
        }

        // Trim trailing whitespace
        while (len > 0 && isspace(start[len - 1])) {
            len--;
        }

        if (len == 0) return NULL;

        char* result = (char*)malloc(len + 1);
        if (!result) return NULL;

        strncpy(result, start, len);
        result[len] = '\0';
        return result;
    }
}

static void d2_parse_style_block(D2Parser* parser, Element* element, Input* input) {
    if (parser->position >= parser->length || parser->current[0] != '{') {
        return;
    }

    parser->current++; // Skip opening brace
    parser->position++;

    while (parser->position < parser->length && parser->current[0] != '}') {
        d2_skip_whitespace_and_comments(parser);

        if (parser->position >= parser->length || parser->current[0] == '}') {
            break;
        }

        // Parse property: value
        char* property = d2_parse_identifier(parser);
        if (!property) break;

        d2_skip_whitespace_and_comments(parser);

        if (parser->position >= parser->length || parser->current[0] != ':') {
            free(property);
            break;
        }

        parser->current++; // Skip colon
        parser->position++;

        d2_skip_whitespace_and_comments(parser);

        char* value = d2_parse_label(parser);
        if (value) {
            // Convert D2 style properties to CSS-aligned attributes
            const char* css_property = property;
            if (strcmp(property, "fill") == 0) {
                css_property = "background-color";
            } else if (strcmp(property, "stroke") == 0) {
                css_property = "border-color";
            } else if (strcmp(property, "stroke-width") == 0) {
                css_property = "border-width";
            } else if (strcmp(property, "stroke-dash") == 0) {
                css_property = "stroke-dasharray";
            }

            add_graph_attribute(input, element, css_property, value);
            free(value);
        }

        free(property);

        d2_skip_whitespace_and_comments(parser);

        // Skip optional semicolon
        if (parser->position < parser->length && parser->current[0] == ';') {
            parser->current++;
            parser->position++;
        }
    }

    if (parser->position < parser->length && parser->current[0] == '}') {
        parser->current++; // Skip closing brace
        parser->position++;
    }
}

void parse_graph_d2(Input* input, const char* d2_string) {
    if (!input || !d2_string) return;

    // Create the main graph element
    Element* graph = create_graph_element(input, "directed", "hierarchical", "d2");
    if (!graph) return;

    // Set the graph as the root
    input->root = {.element = graph};

    D2Parser parser;
    parser.input = d2_string;
    parser.current = d2_string;
    parser.length = strlen(d2_string);
    parser.position = 0;

    while (parser.position < parser.length) {
        d2_skip_whitespace_and_comments(&parser);

        if (parser.position >= parser.length) break;

        // Parse node/edge statement
        char* first_id = d2_parse_identifier(&parser);
        if (!first_id) {
            // Skip to next line if identifier parsing failed
            while (parser.position < parser.length &&
                   parser.current[0] != '\n' && parser.current[0] != '\r') {
                parser.current++;
                parser.position++;
            }
            continue;
        }

        d2_skip_whitespace_and_comments(&parser);

        if (parser.position < parser.length && parser.current[0] == '.') {
            // Node property assignment: node.property: value
            parser.current++; // Skip dot
            parser.position++;

            char* property = d2_parse_identifier(&parser);
            if (!property) {
                free(first_id);
                continue;
            }

            d2_skip_whitespace_and_comments(&parser);

            if (parser.position >= parser.length || parser.current[0] != ':') {
                free(first_id);
                free(property);
                continue;
            }

            parser.current++; // Skip colon
            parser.position++;

            d2_skip_whitespace_and_comments(&parser);

            // Find or create the node
            Element* node = create_node_element(input, first_id, NULL);
            if (node) {
                add_node_to_graph(input, graph, node);

                if (parser.position < parser.length && parser.current[0] == '{') {
                    // Style block
                    d2_parse_style_block(&parser, node, input);
                } else {
                    // Single property value
                    char* value = d2_parse_label(&parser);
                    if (value) {
                        // Convert D2 properties to CSS-aligned attributes
                        const char* css_property = property;
                        if (strcmp(property, "shape") == 0) {
                            css_property = "shape";
                        } else if (strcmp(property, "label") == 0) {
                            css_property = "label";
                        } else if (strcmp(property, "style") == 0) {
                            css_property = "style";
                        }

                        add_graph_attribute(input, node, css_property, value);
                        free(value);
                    }
                }
            }

            free(first_id);
            free(property);

        } else if (parser.position + 1 < parser.length &&
                   parser.current[0] == '-' && parser.current[1] == '>') {
            // Edge: node1 -> node2 [: label]
            parser.current += 2; // Skip ->
            parser.position += 2;

            d2_skip_whitespace_and_comments(&parser);

            char* second_id = d2_parse_identifier(&parser);
            if (!second_id) {
                free(first_id);
                continue;
            }

            d2_skip_whitespace_and_comments(&parser);

            char* edge_label = NULL;
            if (parser.position < parser.length && parser.current[0] == ':') {
                parser.current++; // Skip colon
                parser.position++;

                d2_skip_whitespace_and_comments(&parser);
                edge_label = d2_parse_label(&parser);
            }

            // Create nodes if they don't exist
            Element* from_node = create_node_element(input, first_id, NULL);
            Element* to_node = create_node_element(input, second_id, NULL);
            Element* edge = create_edge_element(input, first_id, second_id, edge_label);

            if (from_node && to_node && edge) {
                add_node_to_graph(input, graph, from_node);
                add_node_to_graph(input, graph, to_node);
                add_edge_to_graph(input, graph, edge);
            }

            free(first_id);
            free(second_id);
            if (edge_label) free(edge_label);

        } else if (parser.position < parser.length && parser.current[0] == ':') {
            // Node with attributes block: node: { ... }
            parser.current++; // Skip colon
            parser.position++;

            d2_skip_whitespace_and_comments(&parser);

            Element* node = create_node_element(input, first_id, NULL);
            if (node) {
                add_node_to_graph(input, graph, node);

                if (parser.position < parser.length && parser.current[0] == '{') {
                    // Parse attribute block
                    d2_parse_style_block(&parser, node, input);
                }
            }

            free(first_id);

        } else {
            // Simple node declaration
            Element* node = create_node_element(input, first_id, NULL);
            if (node) {
                add_node_to_graph(input, graph, node);
            }

            free(first_id);
        }

        // Skip to next line or statement
        while (parser.position < parser.length &&
               parser.current[0] != '\n' && parser.current[0] != '\r') {
            parser.current++;
            parser.position++;
        }

        // Skip past newline characters to start of next line
        while (parser.position < parser.length &&
               (parser.current[0] == '\n' || parser.current[0] == '\r')) {
            parser.current++;
            parser.position++;
        }
    }
}

#include "format.h"
#include "../../lib/stringbuf.h"

// Forward declarations
static void format_graph_element(StringBuf* sb, Element* element, const char* flavor);
static void format_graph_node(StringBuf* sb, Element* node, const char* flavor);
static void format_graph_edge(StringBuf* sb, Element* edge, const char* flavor);
static void format_graph_cluster(StringBuf* sb, Element* cluster, const char* flavor);
static void format_graph_attributes(StringBuf* sb, Element* element, const char* flavor);
static void format_graph_attribute_value(StringBuf* sb, const char* key, const char* value, const char* flavor);

// Helper function to escape strings for different graph formats
static void format_graph_string(StringBuf* sb, const char* str, const char* flavor) {
    if (!str) return;
    
    if (strcmp(flavor, "dot") == 0) {
        // DOT format: escape quotes and backslashes, wrap in quotes if contains spaces/special chars
        bool needs_quotes = false;
        const char* p = str;
        while (*p) {
            if (*p == ' ' || *p == '-' || *p == '>' || *p == '{' || *p == '}' || *p == '"') {
                needs_quotes = true;
                break;
            }
            p++;
        }
        
        if (needs_quotes) stringbuf_append_char(sb, '"');
        
        p = str;
        while (*p) {
            if (*p == '"') {
                stringbuf_append_str(sb, "\\\"");
            } else if (*p == '\\') {
                stringbuf_append_str(sb, "\\\\");
            } else {
                stringbuf_append_char(sb, *p);
            }
            p++;
        }
        
        if (needs_quotes) stringbuf_append_char(sb, '"');
        
    } else if (strcmp(flavor, "mermaid") == 0) {
        // Mermaid format: wrap in quotes if contains spaces, escape quotes
        bool needs_quotes = strchr(str, ' ') != NULL || strchr(str, '-') != NULL;
        
        if (needs_quotes) stringbuf_append_char(sb, '"');
        
        const char* p = str;
        while (*p) {
            if (*p == '"') {
                stringbuf_append_str(sb, "\\\"");
            } else {
                stringbuf_append_char(sb, *p);
            }
            p++;
        }
        
        if (needs_quotes) stringbuf_append_char(sb, '"');
        
    } else if (strcmp(flavor, "d2") == 0) {
        // D2 format: wrap in quotes if contains spaces/special chars, escape quotes
        bool needs_quotes = false;
        const char* p = str;
        while (*p) {
            if (*p == ' ' || *p == ':' || *p == '{' || *p == '}' || *p == '"' || *p == '-' || *p == '>') {
                needs_quotes = true;
                break;
            }
            p++;
        }
        
        if (needs_quotes) stringbuf_append_char(sb, '"');
        
        p = str;
        while (*p) {
            if (*p == '"') {
                stringbuf_append_str(sb, "\\\"");
            } else {
                stringbuf_append_char(sb, *p);
            }
            p++;
        }
        
        if (needs_quotes) stringbuf_append_char(sb, '"');
        
    } else {
        // Default: just output the string as-is
        stringbuf_append_str(sb, str);
    }
}

// Helper function to get string attribute from element
static const char* get_element_attribute(Element* element, const char* attr_name) {
    if (!element || !element->data || !attr_name) return NULL;
    
    TypeElmt* elem_type = (TypeElmt*)element->type;
    if (!elem_type) return NULL;
    
    // Cast the element type to TypeMap to access attributes
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return NULL;
    
    // Search through element fields for the attribute
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->name->str && 
            field->name->length == strlen(attr_name) &&
            strncmp(field->name->str, attr_name, field->name->length) == 0) {
            
            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                void* data = ((char*)element->data) + field->byte_offset;
                String* str_val = *(String**)data;
                return str_val ? str_val->chars : NULL;
            }
        }
        field = field->next;
    }
    
    return NULL;
}

// Helper function to iterate through element children
static void format_graph_children(StringBuf* sb, Element* element, const char* flavor) {
    // Element inherits from List, so access items directly
    if (!element || !element->items || element->length == 0) {
        return;
    }
    
    for (int64_t i = 0; i < element->length; i++) {
        Item child_item = element->items[i];
        
        if (child_item.type_id == LMD_TYPE_ELEMENT) {
            Element* child = (Element*)child_item.container;
            if (child && child->type) {
                TypeElmt* child_type = (TypeElmt*)child->type;
                char child_type_name[256];
                snprintf(child_type_name, sizeof(child_type_name), "%.*s", 
                        (int)child_type->name.length, child_type->name.str);
                
                if (strcmp(child_type_name, "node") == 0) {
                    format_graph_node(sb, child, flavor);
                } else if (strcmp(child_type_name, "edge") == 0) {
                    format_graph_edge(sb, child, flavor);
                } else if (strcmp(child_type_name, "cluster") == 0) {
                    format_graph_cluster(sb, child, flavor);
                }
            }
        }
    }
}

// Format a graph node
static void format_graph_node(StringBuf* sb, Element* node, const char* flavor) {
    const char* id = get_element_attribute(node, "id");
    const char* label = get_element_attribute(node, "label");
    
    if (!id) return; // Node must have an ID
    
    if (strcmp(flavor, "dot") == 0) {
        stringbuf_append_str(sb, "    ");
        format_graph_string(sb, id, flavor);
        
        // Add attributes if present
        bool has_attrs = false;
        if (label) {
            stringbuf_append_str(sb, " [label=");
            format_graph_string(sb, label, flavor);
            has_attrs = true;
        }
        
        // Add other visual attributes
        format_graph_attributes(sb, node, flavor);
        
        if (has_attrs) {
            stringbuf_append_str(sb, "]");
        }
        
        stringbuf_append_str(sb, ";\n");
        
    } else if (strcmp(flavor, "mermaid") == 0) {
        stringbuf_append_str(sb, "    ");
        format_graph_string(sb, id, flavor);
        
        if (label) {
            stringbuf_append_str(sb, "[");
            format_graph_string(sb, label, flavor);
            stringbuf_append_str(sb, "]");
        }
        
        stringbuf_append_char(sb, '\n');
        
    } else if (strcmp(flavor, "d2") == 0) {
        // D2 nodes with properties
        format_graph_string(sb, id, flavor);
        
        if (label) {
            stringbuf_append_str(sb, ": ");
            format_graph_string(sb, label, flavor);
        }
        
        // Check for style attributes
        const char* shape = get_element_attribute(node, "shape");
        const char* fill = get_element_attribute(node, "fill");
        const char* stroke = get_element_attribute(node, "stroke");
        
        if (shape || fill || stroke) {
            stringbuf_append_str(sb, ": {\n");
            
            if (shape) {
                stringbuf_append_str(sb, "  shape: ");
                stringbuf_append_str(sb, shape);
                stringbuf_append_char(sb, '\n');
            }
            
            if (fill || stroke) {
                stringbuf_append_str(sb, "  style: {\n");
                if (fill) {
                    stringbuf_append_str(sb, "    fill: ");
                    stringbuf_append_str(sb, fill);
                    stringbuf_append_char(sb, '\n');
                }
                if (stroke) {
                    stringbuf_append_str(sb, "    stroke: ");
                    stringbuf_append_str(sb, stroke);
                    stringbuf_append_char(sb, '\n');
                }
                stringbuf_append_str(sb, "  }\n");
            }
            
            stringbuf_append_str(sb, "}\n");
        } else {
            stringbuf_append_char(sb, '\n');
        }
    }
}

// Format a graph edge
static void format_graph_edge(StringBuf* sb, Element* edge, const char* flavor) {
    const char* from = get_element_attribute(edge, "from");
    const char* to = get_element_attribute(edge, "to");
    const char* label = get_element_attribute(edge, "label");
    
    if (!from || !to) return; // Edge must have from and to
    
    if (strcmp(flavor, "dot") == 0) {
        stringbuf_append_str(sb, "    ");
        format_graph_string(sb, from, flavor);
        stringbuf_append_str(sb, " -> ");
        format_graph_string(sb, to, flavor);
        
        if (label) {
            stringbuf_append_str(sb, " [label=");
            format_graph_string(sb, label, flavor);
            stringbuf_append_str(sb, "]");
        }
        
        stringbuf_append_str(sb, ";\n");
        
    } else if (strcmp(flavor, "mermaid") == 0) {
        stringbuf_append_str(sb, "    ");
        format_graph_string(sb, from, flavor);
        stringbuf_append_str(sb, " --> ");
        format_graph_string(sb, to, flavor);
        
        if (label) {
            stringbuf_append_str(sb, " : ");
            format_graph_string(sb, label, flavor);
        }
        
        stringbuf_append_char(sb, '\n');
        
    } else if (strcmp(flavor, "d2") == 0) {
        format_graph_string(sb, from, flavor);
        stringbuf_append_str(sb, " -> ");
        format_graph_string(sb, to, flavor);
        
        if (label) {
            stringbuf_append_str(sb, ": ");
            format_graph_string(sb, label, flavor);
        }
        
        stringbuf_append_char(sb, '\n');
    }
}

// Format a graph cluster/subgraph
static void format_graph_cluster(StringBuf* sb, Element* cluster, const char* flavor) {
    const char* id = get_element_attribute(cluster, "id");
    const char* label = get_element_attribute(cluster, "label");
    
    if (strcmp(flavor, "dot") == 0) {
        stringbuf_append_str(sb, "    subgraph ");
        if (id) {
            format_graph_string(sb, id, flavor);
        } else {
            stringbuf_append_str(sb, "cluster_unnamed");
        }
        stringbuf_append_str(sb, " {\n");
        
        if (label) {
            stringbuf_append_str(sb, "        label=");
            format_graph_string(sb, label, flavor);
            stringbuf_append_str(sb, ";\n");
        }
        
        // Format cluster children with increased indentation
        format_graph_children(sb, cluster, flavor);
        
        stringbuf_append_str(sb, "    }\n");
        
    } else if (strcmp(flavor, "mermaid") == 0) {
        stringbuf_append_str(sb, "    subgraph ");
        if (id) {
            format_graph_string(sb, id, flavor);
        } else {
            stringbuf_append_str(sb, "cluster");
        }
        
        if (label) {
            stringbuf_append_str(sb, " [");
            format_graph_string(sb, label, flavor);
            stringbuf_append_str(sb, "]");
        }
        
        stringbuf_append_char(sb, '\n');
        format_graph_children(sb, cluster, flavor);
        stringbuf_append_str(sb, "    end\n");
        
    } else if (strcmp(flavor, "d2") == 0) {
        // D2 doesn't have explicit subgraphs like DOT, but we can group with containers
        if (id) {
            format_graph_string(sb, id, flavor);
        } else {
            stringbuf_append_str(sb, "container");
        }
        
        stringbuf_append_str(sb, ": {\n");
        
        if (label) {
            stringbuf_append_str(sb, "  label: ");
            format_graph_string(sb, label, flavor);
            stringbuf_append_char(sb, '\n');
        }
        
        format_graph_children(sb, cluster, flavor);
        stringbuf_append_str(sb, "}\n");
    }
}

// Format graph-specific attributes 
static void format_graph_attributes(StringBuf* sb, Element* element, const char* flavor) {
    // This function can be extended to handle more attributes
    // For now, it's a placeholder for future attribute formatting
}

// Format a graph element (main entry point for graph formatting)
static void format_graph_element(StringBuf* sb, Element* element, const char* flavor) {
    if (!element || !element->type) return;
    
    TypeElmt* elmt_type = (TypeElmt*)element->type;
    char element_type_name[256];
    snprintf(element_type_name, sizeof(element_type_name), "%.*s", 
            (int)elmt_type->name.length, elmt_type->name.str);
    
    // Check if this is a graph element
    if (strcmp(element_type_name, "graph") != 0) {
        printf("Warning: Expected graph element, got %s\n", element_type_name);
        return;
    }
    
    // Get graph attributes
    const char* graph_type = get_element_attribute(element, "type");
    const char* graph_layout = get_element_attribute(element, "layout");
    const char* graph_flavor = get_element_attribute(element, "flavor");
    const char* graph_name = get_element_attribute(element, "name");
    
    // Use provided flavor or fall back to graph's flavor attribute
    if (!flavor && graph_flavor) {
        flavor = graph_flavor;
    }
    if (!flavor) {
        flavor = "dot"; // Default flavor
    }
    
    printf("Formatting graph as %s (type: %s, layout: %s)\n", 
           flavor, graph_type ? graph_type : "unknown", graph_layout ? graph_layout : "unknown");
    
    if (strcmp(flavor, "dot") == 0) {
        // DOT format header
        bool is_directed = graph_type && strcmp(graph_type, "directed") == 0;
        stringbuf_append_str(sb, is_directed ? "digraph " : "graph ");
        
        if (graph_name) {
            format_graph_string(sb, graph_name, flavor);
        } else {
            stringbuf_append_str(sb, "G");
        }
        
        stringbuf_append_str(sb, " {\n");
        
        // Graph attributes
        if (graph_layout) {
            stringbuf_append_str(sb, "    layout=");
            format_graph_string(sb, graph_layout, flavor);
            stringbuf_append_str(sb, ";\n");
        }
        
        // Format children
        format_graph_children(sb, element, flavor);
        
        stringbuf_append_str(sb, "}\n");
        
    } else if (strcmp(flavor, "mermaid") == 0) {
        // Mermaid format header  
        bool is_directed = graph_type && strcmp(graph_type, "directed") == 0;
        stringbuf_append_str(sb, is_directed ? "flowchart TD\n" : "graph LR\n");
        
        // Format children
        format_graph_children(sb, element, flavor);
        
    } else if (strcmp(flavor, "d2") == 0) {
        // D2 format - no explicit header, just content
        
        // Add a comment if we have graph metadata
        if (graph_name || graph_type) {
            stringbuf_append_str(sb, "# Graph: ");
            if (graph_name) {
                stringbuf_append_str(sb, graph_name);
            }
            if (graph_type) {
                stringbuf_append_str(sb, " (");
                stringbuf_append_str(sb, graph_type);
                stringbuf_append_str(sb, ")");
            }
            stringbuf_append_char(sb, '\n');
        }
        
        // Format children
        format_graph_children(sb, element, flavor);
        
    } else {
        printf("Unsupported graph flavor: %s\n", flavor);
    }
}

// Main graph formatting function
String* format_graph(Pool* pool, Item root_item) {
    if (get_type_id(root_item) != LMD_TYPE_ELEMENT) {
        printf("format_graph: Root item is not an element\n");
        return NULL;
    }
    
    Element* element = root_item.element;
    if (!element) {
        printf("format_graph: Element is null\n");
        return NULL;
    }
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        printf("format_graph: Failed to create StringBuf\n");
        return NULL;
    }
    
    // Default to DOT format for backwards compatibility
    format_graph_element(sb, element, "dot");
    
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    
    return result;
}

// Graph formatting function with flavor support
String* format_graph_with_flavor(Pool* pool, Item root_item, const char* flavor) {
    if (get_type_id(root_item) != LMD_TYPE_ELEMENT) {
        printf("format_graph_with_flavor: Root item is not an element\n");
        return NULL;
    }
    
    Element* element = root_item.element;
    if (!element) {
        printf("format_graph_with_flavor: Element is null\n");
        return NULL;
    }
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        printf("format_graph_with_flavor: Failed to create StringBuf\n");
        return NULL;
    }
    
    format_graph_element(sb, element, flavor ? flavor : "dot");
    
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    
    return result;
}
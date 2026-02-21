#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"

// forward declarations
static void format_graph_element(StringBuf* sb, const ElementReader& element, const char* flavor);
static void format_graph_node(StringBuf* sb, const ElementReader& node, const char* flavor);
static void format_graph_edge(StringBuf* sb, const ElementReader& edge, const char* flavor);
static void format_graph_cluster(StringBuf* sb, const ElementReader& cluster, const char* flavor);
static void format_graph_children(StringBuf* sb, const ElementReader& element, const char* flavor);

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

// Main graph formatting function
String* format_graph(Pool* pool, Item root_item) {
    if (get_type_id(root_item) != LMD_TYPE_ELEMENT) {
        log_error("format_graph: Root item is not an element");
        return NULL;
    }

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        log_error("format_graph: Failed to create StringBuf");
        return NULL;
    }

    // default to DOT format for backwards compatibility
    ItemReader root(root_item.to_const());
    ElementReader elem = root.asElement();
    format_graph_element(sb, elem, "dot");

    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);

    return result;
}

// Graph formatting function with flavor support
String* format_graph_with_flavor(Pool* pool, Item root_item, const char* flavor) {
    if (get_type_id(root_item) != LMD_TYPE_ELEMENT) {
        log_error("format_graph_with_flavor: Root item is not an element");
        return NULL;
    }

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        log_error("format_graph_with_flavor: Failed to create StringBuf");
        return NULL;
    }

    ItemReader root(root_item.to_const());
    ElementReader elem = root.asElement();
    format_graph_element(sb, elem, flavor ? flavor : "dot");

    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);

    return result;
}

// helper to get string attribute from element
static const char* get_element_attribute(const ElementReader& elem, const char* attr_name) {
    ItemReader attr = elem.get_attr(attr_name);
    if (attr.isString()) {
        String* str = attr.asString();
        return str ? str->chars : nullptr;
    }
    return nullptr;
}

// format graph children using reader API
static void format_graph_children(StringBuf* sb, const ElementReader& element, const char* flavor) {
    auto it = element.children();
    ItemReader child_item;
    while (it.next(&child_item)) {
        if (child_item.isElement()) {
            ElementReader child = child_item.asElement();
            const char* child_type_name = child.tagName();

            if (child_type_name) {
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

// format graph node
static void format_graph_node(StringBuf* sb, const ElementReader& node, const char* flavor) {
    const char* id = get_element_attribute(node, "id");
    const char* label = get_element_attribute(node, "label");

    if (!id) return; // node must have an ID

    if (strcmp(flavor, "dot") == 0) {
        stringbuf_append_str(sb, "    ");
        format_graph_string(sb, id, flavor);

        // add attributes if present
        bool has_attrs = false;
        if (label) {
            stringbuf_append_str(sb, " [label=");
            format_graph_string(sb, label, flavor);
            has_attrs = true;
        }

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

        // check for style attributes
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

// format graph edge
static void format_graph_edge(StringBuf* sb, const ElementReader& edge, const char* flavor) {
    const char* from = get_element_attribute(edge, "from");
    const char* to = get_element_attribute(edge, "to");
    const char* label = get_element_attribute(edge, "label");

    if (!from || !to) return; // edge must have from and to

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

// format graph cluster
static void format_graph_cluster(StringBuf* sb, const ElementReader& cluster, const char* flavor) {
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

        // format cluster children with increased indentation
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

// format graph element
static void format_graph_element(StringBuf* sb, const ElementReader& element, const char* flavor) {
    const char* element_type_name = element.tagName();
    if (!element_type_name) return;

    // check if this is a graph element
    if (strcmp(element_type_name, "graph") != 0) {
        log_debug("graph: Expected graph element, got %s", element_type_name);
        return;
    }

    // get graph attributes
    const char* graph_type = get_element_attribute(element, "type");
    const char* graph_layout = get_element_attribute(element, "layout");
    const char* graph_flavor = get_element_attribute(element, "flavor");
    const char* graph_name = get_element_attribute(element, "name");

    // use provided flavor or fall back to graph's flavor attribute
    if (!flavor && graph_flavor) {
        flavor = graph_flavor;
    }
    if (!flavor) {
        flavor = "dot"; // default flavor
    }

    log_debug("graph: Formatting as %s (type: %s, layout: %s)",
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

        // graph attributes
        if (graph_layout) {
            stringbuf_append_str(sb, "    layout=");
            format_graph_string(sb, graph_layout, flavor);
            stringbuf_append_str(sb, ";\n");
        }

        // format children
        format_graph_children(sb, element, flavor);

        stringbuf_append_str(sb, "}\n");

    } else if (strcmp(flavor, "mermaid") == 0) {
        // Mermaid format header
        bool is_directed = graph_type && strcmp(graph_type, "directed") == 0;
        stringbuf_append_str(sb, is_directed ? "flowchart TD\n" : "graph LR\n");

        // format children
        format_graph_children(sb, element, flavor);

    } else if (strcmp(flavor, "d2") == 0) {
        // D2 format - no explicit header, just content

        // add a comment if we have graph metadata
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

        // format children
        format_graph_children(sb, element, flavor);

    } else {
        log_error("graph: Unsupported graph flavor: %s", flavor);
    }
}

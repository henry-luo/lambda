#include "format.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include <string.h>

// Forward declarations - old Element* API
static void format_inline_element(StringBuf* sb, Element* elem);
static void format_scheduling(StringBuf* sb, Element* elem);
static void format_timestamp(StringBuf* sb, Element* elem);
static void format_footnote_definition(StringBuf* sb, Element* elem);

// MarkReader-based forward declarations - new OrgContext& API
static void format_org_element_reader(OrgContext& ctx, const ElementReader& elem);
static void format_org_text_reader(OrgContext& ctx, const ItemReader& item);

// Simple helper to append a string to the buffer
static void append_string(StringBuf* sb, const char* str) {
    if (!sb || !str) return;
    stringbuf_append_str(sb, str);
}

// Helper to get element type name
static const char* get_element_type_name(Element* elem) {
    if (!elem || !elem->type) return NULL;
    TypeElmt* type = (TypeElmt*)elem->type;
    if (type->name.length == 0) return NULL;

    // Create a null-terminated string from StrView
    static char type_name_buffer[256];
    if (type->name.length >= sizeof(type_name_buffer)) return NULL;

    strncpy(type_name_buffer, type->name.str, type->name.length);
    type_name_buffer[type->name.length] = '\0';
    return type_name_buffer;
}

// Helper to extract string content from first child
static String* get_first_string_content(Element* elem) {
    if (!elem) return NULL;
    List* list = (List*)elem;
    if (list->length == 0) return NULL;

    Item first_item = list->items[0];
    TypeId type = get_type_id(first_item);
    if (type == LMD_TYPE_STRING) {
        return first_item.get_string();
    }
    else if (type == LMD_TYPE_ELEMENT) {
        Element* child_elem = first_item.element;
        return get_first_string_content(child_elem);
    }
    return NULL;
}

// Format a heading element
static void format_heading(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    int level = 1;  // default level
    String* title = NULL;
    String* todo = NULL;
    String* tags = NULL;

    // Extract level, TODO, title, and tags from children
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "level") == 0) {
                String* level_str = get_first_string_content(child);
                if (level_str) {
                    level = atoi(level_str->chars);
                }
            } else if (child_type && strcmp(child_type, "todo") == 0) {
                todo = get_first_string_content(child);
            } else if (child_type && strcmp(child_type, "title") == 0) {
                title = get_first_string_content(child);
            } else if (child_type && strcmp(child_type, "tags") == 0) {
                tags = get_first_string_content(child);
            }
        }
    }

    // Output heading with appropriate number of stars
    for (int i = 0; i < level; i++) {
        append_string(sb, "*");
    }
    append_string(sb, " ");

    // Add TODO keyword if present
    if (todo) {
        append_string(sb, todo->chars);
        append_string(sb, " ");
    }

    // Add title
    if (title) {
        append_string(sb, title->chars);
    }

    // Add tags if present
    if (tags) {
        append_string(sb, " ");
        append_string(sb, tags->chars);
    }

    append_string(sb, "\n");
}

// Format a paragraph element with inline formatting
static void format_paragraph(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        TypeId child_type_id = get_type_id(child_item);
        if (child_type_id == LMD_TYPE_STRING) {
            String* str = child_item.get_string();
            if (str && str) {
                append_string(sb, str->chars);
            }
        }
        else if (child_type_id == LMD_TYPE_ELEMENT) {
            Element* child_elem = child_item.element;
            format_inline_element(sb, child_elem);
        }
    }
    append_string(sb, "\n");
}

// Format inline elements (bold, italic, links, etc.)
static void format_inline_element(StringBuf* sb, Element* elem) {
    if (!elem) return;

    const char* type_name = get_element_type_name(elem);
    if (!type_name) return;

    if (strcmp(type_name, "plain_text") == 0) {
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            }
        }
    } else if (strcmp(type_name, "bold") == 0) {
        append_string(sb, "*");
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            }
        }
        append_string(sb, "*");
    } else if (strcmp(type_name, "italic") == 0) {
        append_string(sb, "/");
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            }
        }
        append_string(sb, "/");
    } else if (strcmp(type_name, "verbatim") == 0) {
        append_string(sb, "=");
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            }
        }
        append_string(sb, "=");
    } else if (strcmp(type_name, "code") == 0) {
        append_string(sb, "~");
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            }
        }
        append_string(sb, "~");
    } else if (strcmp(type_name, "strikethrough") == 0) {
        append_string(sb, "+");
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            }
        }
        append_string(sb, "+");
    } else if (strcmp(type_name, "underline") == 0) {
        append_string(sb, "_");
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            }
        }
        append_string(sb, "_");
    } else if (strcmp(type_name, "link") == 0) {
        append_string(sb, "[[");

        String* url = NULL;
        String* description = NULL;

        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                Element* child = child_item.element;
                const char* child_type = get_element_type_name(child);

                if (child_type && strcmp(child_type, "url") == 0) {
                    url = get_first_string_content(child);
                } else if (child_type && strcmp(child_type, "description") == 0) {
                    description = get_first_string_content(child);
                }
            }
        }

        if (url) {
            append_string(sb, url->chars);
        }

        if (description) {
            append_string(sb, "][");
            append_string(sb, description->chars);
        }

        append_string(sb, "]]");
    } else if (strcmp(type_name, "footnote_reference") == 0) {
        // Format footnote reference: [fn:name]
        append_string(sb, "[fn:");

        String* name = NULL;
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                Element* child = child_item.element;
                const char* child_type = get_element_type_name(child);

                if (child_type && strcmp(child_type, "name") == 0) {
                    name = get_first_string_content(child);
                    break;
                }
            }
        }

        if (name) {
            append_string(sb, name->chars);
        }
        append_string(sb, "]");
    } else if (strcmp(type_name, "inline_footnote") == 0) {
        // Format inline footnote: [fn:name:definition] or [fn::definition]
        append_string(sb, "[fn:");

        String* name = NULL;
        Element* definition_elem = NULL;

        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                Element* child = child_item.element;
                const char* child_type = get_element_type_name(child);

                if (child_type && strcmp(child_type, "name") == 0) {
                    name = get_first_string_content(child);
                } else if (child_type && strcmp(child_type, "definition") == 0) {
                    definition_elem = child;
                }
            }
        }

        if (name && strlen(name->chars) > 0) {
            append_string(sb, name->chars);
        }
        append_string(sb, ":");

        if (definition_elem) {
            // Format the definition content (may contain inline formatting)
            List* def_list = (List*)definition_elem;
            for (long i = 0; i < def_list->length; i++) {
                Item def_item = def_list->items[i];
                if (get_type_id(def_item) == LMD_TYPE_STRING) {
                    String* str = def_item.get_string();
                    if (str && str) {
                        append_string(sb, str->chars);
                    }
                } else if (get_type_id(def_item) == LMD_TYPE_ELEMENT) {
                    Element* def_child = def_item.element;
                    format_inline_element(sb, def_child);
                }
            }
        }
        append_string(sb, "]");
    } else if (strcmp(type_name, "inline_math") == 0) {
        // Format inline math: $content$
        String* raw_content = NULL;

        // Look for raw_content element
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                Element* child = child_item.element;
                const char* child_type = get_element_type_name(child);

                if (child_type && strcmp(child_type, "raw_content") == 0) {
                    raw_content = get_first_string_content(child);
                    break;
                }
            }
        }

        // Determine delimiter based on content (could be $ or \(...\))
        bool use_latex_style = false;
        if (raw_content && raw_content->chars) {
            // Simple heuristic: if it contains backslash commands, use LaTeX style
            if (strchr(raw_content->chars, '\\')) {
                use_latex_style = true;
            }
        }

        if (use_latex_style) {
            append_string(sb, "\\(");
            if (raw_content) {
                append_string(sb, raw_content->chars);
            }
            append_string(sb, "\\)");
        } else {
            append_string(sb, "$");
            if (raw_content) {
                append_string(sb, raw_content->chars);
            }
            append_string(sb, "$");
        }
    } else if (strcmp(type_name, "display_math") == 0) {
        // Format display math: $$content$$ or \[...\]
        String* raw_content = NULL;

        // Look for raw_content element
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                Element* child = child_item.element;
                const char* child_type = get_element_type_name(child);

                if (child_type && strcmp(child_type, "raw_content") == 0) {
                    raw_content = get_first_string_content(child);
                    break;
                }
            }
        }

        // Determine delimiter based on content
        bool use_latex_style = false;
        if (raw_content && raw_content->chars) {
            // Use LaTeX style if it contains backslash commands or is complex
            if (strchr(raw_content->chars, '\\') || strlen(raw_content->chars) > 20) {
                use_latex_style = true;
            }
        }

        if (use_latex_style) {
            append_string(sb, "\\[");
            if (raw_content) {
                append_string(sb, raw_content->chars);
            }
            append_string(sb, "\\]");
        } else {
            append_string(sb, "$$");
            if (raw_content) {
                append_string(sb, raw_content->chars);
            }
            append_string(sb, "$$");
        }
    } else if (strcmp(type_name, "timestamp") == 0) {
        format_timestamp(sb, elem);
    } else if (strcmp(type_name, "text_content") == 0) {
        // Handle container of inline elements
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                Element* child_elem = child_item.element;
                format_inline_element(sb, child_elem);
            }
        }
    } else {
        // Unknown inline element, treat as plain text
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            } else if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                Element* child_elem = child_item.element;
                format_inline_element(sb, child_elem);
            }
        }
    }
}

// Format a list item element
static void format_list_item(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_STRING) {
            String* str = child_item.get_string();
            if (str && str) {
                append_string(sb, str->chars);
            }
        }
    }
    append_string(sb, "\n");
}

// Format a code block element
static void format_code_block(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    String* language = NULL;

    // Extract language and content
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "language") == 0) {
                language = get_first_string_content(child);
            }
        }
    }

    // Output BEGIN_SRC
    append_string(sb, "#+BEGIN_SRC");
    if (language) {
        append_string(sb, " ");
        append_string(sb, language->chars);
    }
    append_string(sb, "\n");

    // Output content lines
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "content") == 0) {
                String* content = get_first_string_content(child);
                if (content) {
                    append_string(sb, content->chars);
                    append_string(sb, "\n");
                }
            }
        }
    }

    // Output END_SRC
    append_string(sb, "#+END_SRC\n");
}

// Format a quote block element
static void format_quote_block(StringBuf* sb, Element* elem) {
    if (!elem) return;

    // Output BEGIN_QUOTE
    append_string(sb, "#+BEGIN_QUOTE\n");

    // Output content paragraphs
    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "paragraph") == 0) {
                format_paragraph(sb, child);
            }
        }
    }

    // Output END_QUOTE
    append_string(sb, "#+END_QUOTE\n");
}

// Format an example block element
static void format_example_block(StringBuf* sb, Element* elem) {
    if (!elem) return;

    append_string(sb, "#+BEGIN_EXAMPLE\n");

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "content") == 0) {
                String* content = get_first_string_content(child);
                if (content) {
                    append_string(sb, content->chars);
                    append_string(sb, "\n");
                }
            }
        }
    }

    append_string(sb, "#+END_EXAMPLE\n");
}

// Format a verse block element
static void format_verse_block(StringBuf* sb, Element* elem) {
    if (!elem) return;

    append_string(sb, "#+BEGIN_VERSE\n");

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "content") == 0) {
                String* content = get_first_string_content(child);
                if (content) {
                    append_string(sb, content->chars);
                    append_string(sb, "\n");
                }
            }
        }
    }

    append_string(sb, "#+END_VERSE\n");
}

// Format a center block element
static void format_center_block(StringBuf* sb, Element* elem) {
    if (!elem) return;

    append_string(sb, "#+BEGIN_CENTER\n");

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "paragraph") == 0) {
                format_paragraph(sb, child);
            }
        }
    }

    append_string(sb, "#+END_CENTER\n");
}

// Format a drawer element
static void format_drawer(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    String* drawer_name = NULL;

    // Extract drawer name first
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "name") == 0) {
                drawer_name = get_first_string_content(child);
                break;
            }
        }
    }

    // Output drawer start
    append_string(sb, ":");
    if (drawer_name) {
        append_string(sb, drawer_name->chars);
    }
    append_string(sb, ":\n");

    // Output content lines
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "content") == 0) {
                String* content = get_first_string_content(child);
                if (content) {
                    append_string(sb, content->chars);
                    append_string(sb, "\n");
                }
            }
        }
    }

    // Output drawer end
    append_string(sb, ":END:\n");
}

// Format a scheduling element (SCHEDULED, DEADLINE, CLOSED)
static void format_scheduling(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    String* keyword = NULL;
    String* timestamp = NULL;

    // Extract keyword and timestamp
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "keyword") == 0) {
                keyword = get_first_string_content(child);
            } else if (child_type && strcmp(child_type, "timestamp") == 0) {
                timestamp = get_first_string_content(child);
            }
        }
    }

    // Output scheduling line
    append_string(sb, "  "); // Indent
    if (keyword) {
        // Convert keyword to uppercase for output
        if (strcmp(keyword->chars, "scheduled") == 0) {
            append_string(sb, "SCHEDULED: ");
        } else if (strcmp(keyword->chars, "deadline") == 0) {
            append_string(sb, "DEADLINE: ");
        } else if (strcmp(keyword->chars, "closed") == 0) {
            append_string(sb, "CLOSED: ");
        }
    }
    if (timestamp) {
        append_string(sb, timestamp->chars);
    }
    append_string(sb, "\n");
}

// Format a timestamp element
static void format_timestamp(StringBuf* sb, Element* elem) {
    if (!elem) return;

    String* timestamp = get_first_string_content(elem);
    if (timestamp) {
        append_string(sb, timestamp->chars);
    }
}

// Format a footnote definition element
static void format_footnote_definition(StringBuf* sb, Element* elem) {
    if (!elem) return;

    String* name = NULL;
    Element* content_elem = NULL;

    // Extract footnote name and content
    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child = child_item.element;
            const char* child_type = get_element_type_name(child);

            if (child_type && strcmp(child_type, "name") == 0) {
                name = get_first_string_content(child);
            } else if (child_type && strcmp(child_type, "content") == 0) {
                content_elem = child;
            }
        }
    }

    // Format as [fn:name] content
    append_string(sb, "[fn:");
    if (name) {
        append_string(sb, name->chars);
    }
    append_string(sb, "] ");

    // Format the content (may contain inline formatting)
    if (content_elem) {
        List* content_list = (List*)content_elem;
        for (long i = 0; i < content_list->length; i++) {
            Item content_item = content_list->items[i];
            if (get_type_id(content_item) == LMD_TYPE_STRING) {
                String* str = content_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            } else if (get_type_id(content_item) == LMD_TYPE_ELEMENT) {
                Element* content_child = content_item.element;
                format_inline_element(sb, content_child);
            }
        }
    }

    append_string(sb, "\n");
}

// Format a directive element
static void format_directive(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_STRING) {
            String* str = child_item.get_string();
            if (str && str) {
                append_string(sb, str->chars);
            }
        }
    }
    append_string(sb, "\n");
}

// Format a table cell element
static void format_table_cell(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_STRING) {
            String* str = child_item.get_string();
            if (str && str) {
                append_string(sb, str->chars);
            }
        }
    }
}

// Format a table row element
static void format_table_row(StringBuf* sb, Element* elem, bool is_header) {
    if (!elem) return;

    append_string(sb, "|");

    List* list = (List*)elem;
    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* cell = child_item.element;
            const char* cell_type = get_element_type_name(cell);

            if (cell_type && strcmp(cell_type, "table_cell") == 0) {
                append_string(sb, " ");
                format_table_cell(sb, cell);
                append_string(sb, " |");
            }
        }
    }
    append_string(sb, "\n");

    // If this is a header row, add separator
    if (is_header) {
        append_string(sb, "|");
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
                append_string(sb, "---------|");
            }
        }
        append_string(sb, "\n");
    }
}

// Format a table element
static void format_table(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* list = (List*)elem;
    bool first_row = true;

    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* row = child_item.element;
            const char* row_type = get_element_type_name(row);

            if (row_type && (strcmp(row_type, "table_row") == 0 ||
                            strcmp(row_type, "table_header_row") == 0)) {
                bool is_header = (strcmp(row_type, "table_header_row") == 0) || first_row;
                format_table_row(sb, row, is_header);
                first_row = false;
            }
        }
    }
}

// Format an org element based on its type
static void format_org_element(StringBuf* sb, Element* elem) {
    if (!elem) return;

    const char* type_name = get_element_type_name(elem);
    if (!type_name) return;

    if (strcmp(type_name, "heading") == 0) {
        format_heading(sb, elem);
    } else if (strcmp(type_name, "paragraph") == 0) {
        format_paragraph(sb, elem);
    } else if (strcmp(type_name, "list_item") == 0) {
        format_list_item(sb, elem);
    } else if (strcmp(type_name, "code_block") == 0) {
        format_code_block(sb, elem);
    } else if (strcmp(type_name, "quote_block") == 0) {
        format_quote_block(sb, elem);
    } else if (strcmp(type_name, "example_block") == 0) {
        format_example_block(sb, elem);
    } else if (strcmp(type_name, "verse_block") == 0) {
        format_verse_block(sb, elem);
    } else if (strcmp(type_name, "center_block") == 0) {
        format_center_block(sb, elem);
    } else if (strcmp(type_name, "drawer") == 0) {
        format_drawer(sb, elem);
    } else if (strcmp(type_name, "scheduling") == 0) {
        format_scheduling(sb, elem);
    } else if (strcmp(type_name, "timestamp") == 0) {
        format_timestamp(sb, elem);
    } else if (strcmp(type_name, "footnote_definition") == 0) {
        format_footnote_definition(sb, elem);
    } else if (strcmp(type_name, "display_math") == 0) {
        // Handle display math as block-level element
        format_inline_element(sb, elem);
        append_string(sb, "\n");
    } else if (strcmp(type_name, "directive") == 0) {
        format_directive(sb, elem);
    } else if (strcmp(type_name, "table") == 0) {
        format_table(sb, elem);
    } else if (strcmp(type_name, "table_row") == 0 || strcmp(type_name, "table_header_row") == 0) {
        bool is_header = (strcmp(type_name, "table_header_row") == 0);
        format_table_row(sb, elem, is_header);
    } else if (strcmp(type_name, "table_cell") == 0) {
        format_table_cell(sb, elem);
    } else if (strcmp(type_name, "text") == 0) {
        // Legacy text element - output content directly
        format_paragraph(sb, elem);
    } else {
        // For unknown elements, just output all string content
        List* list = (List*)elem;
        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            TypeId child_type = get_type_id(child_item);

            if (child_type == LMD_TYPE_STRING) {
                String* str = child_item.get_string();
                if (str && str) {
                    append_string(sb, str->chars);
                }
            } else if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = child_item.element;
                format_org_element(sb, child_elem);
            }
        }
    }
}

// Format a text or element item
static void format_org_text(StringBuf* sb, Item item) {
    if (item.item == ITEM_NULL) return;

    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_STRING) {
        String* str = item.get_string();
        if (str && str) {
            append_string(sb, str->chars);
        }
    } else if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        format_org_element(sb, elem);
    }
}

// MarkReader-based implementations
static void format_org_element_reader(OrgContext& ctx, const ElementReader& elem) {
    // Delegate to existing Element-based implementation
    Element* raw_elem = const_cast<Element*>(elem.element());
    format_org_element(ctx.output(), raw_elem);
}

static void format_org_text_reader(OrgContext& ctx, const ItemReader& item) {
    // Delegate to existing Item-based implementation
    Item raw_item = item.item();
    format_org_text(ctx.output(), raw_item);
}


// Main Org formatting function
void format_org(StringBuf* sb, Item root_item) {
    if (!sb || root_item.item == ITEM_NULL) return;

    // Create context for org formatting
    Pool* pool = pool_create();
    OrgContext ctx(pool, sb);

    // Use MarkReader API
    ItemReader root(root_item.to_const());

    if (root.isElement()) {
        ElementReader elem = root.asElement();
        Element* raw_elem = const_cast<Element*>(elem.element());
        const char* type_name = get_element_type_name(raw_elem);

        if (type_name && strcmp(type_name, "org_document") == 0) {
            // Format document children
            List* list = (List*)raw_elem;
            for (long i = 0; i < list->length; i++) {
                Item child_item = list->items[i];
                format_org_text_reader(ctx, ItemReader(child_item.to_const()));
            }
        } else {
            // Format single element
            format_org_element_reader(ctx, elem);
        }
    } else {
        // Fallback for other types
        format_org_text_reader(ctx, root);
    }

    pool_destroy(pool);
}

// String version of the formatter
String* format_org_string(Pool* pool, Item root_item) {
    if (root_item.item == ITEM_NULL) return NULL;

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    format_org(sb, root_item);

    return stringbuf_to_string(sb);
}

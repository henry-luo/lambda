#include "input.hpp"
#include "input-context.hpp"
#include "../mark_builder.hpp"
#include "../../lib/memtrack.h"

using namespace lambda;

static Item parse_yaml_content(InputContext* ctx, char** lines, int* current_line, int total_lines, int target_indent);

// Helper function to strip comments from YAML lines
static char* strip_yaml_comments(const char* line) {
    if (!line) return NULL;

    char* result = mem_strdup(line, MEM_CAT_INPUT_YAML);
    char* comment_pos = strchr(result, '#');

    if (comment_pos) {
        // Check if # is inside quotes
        bool in_single_quote = false;
        bool in_double_quote = false;
        char* pos = result;

        while (pos < comment_pos) {
            if (*pos == '\'' && !in_double_quote) {
                in_single_quote = !in_single_quote;
            } else if (*pos == '"' && !in_single_quote) {
                in_double_quote = !in_double_quote;
            }
            pos++;
        }

        // If # is not inside quotes, it's a comment
        if (!in_single_quote && !in_double_quote) {
            *comment_pos = '\0';
            // Trim trailing whitespace
            while (comment_pos > result && isspace(*(comment_pos - 1))) {
                *(--comment_pos) = '\0';
            }
        }
    }

    return result;
}

// Helper function to create String* from char*
static String* create_string_from_cstr(InputContext* ctx, const char* str) {
    if (!str) return NULL;
    return ctx->builder.createString(str);
}

// Utility functions
void trim_string_inplace(char* str) {
    // Trim leading whitespace
    char* start = str;
    while (*start && isspace(*start)) start++;

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    // Trim trailing whitespace
    int len = strlen(str);
    while (len > 0 && isspace(str[len - 1])) {
        str[--len] = '\0';
    }
}

Item parse_scalar_value(InputContext* ctx, const char* str) {
    if (!str) return ctx->builder.createNull();

    char* copy = mem_strdup(str, MEM_CAT_INPUT_YAML);
    trim_string_inplace(copy);

    if (strlen(copy) == 0) {
        mem_free(copy);
        return ctx->builder.createNull();
    }

    // Check for null
    if (strcmp(copy, "null") == 0 || strcmp(copy, "~") == 0) {
        mem_free(copy);
        return ctx->builder.createNull();
    }

    // Check for boolean
    if (strcmp(copy, "true") == 0 || strcmp(copy, "yes") == 0) {
        mem_free(copy);
        return ctx->builder.createBool(true);
    }
    if (strcmp(copy, "false") == 0 || strcmp(copy, "no") == 0) {
        mem_free(copy);
        return ctx->builder.createBool(false);
    }

    // Check for number
    char* end;
    int64_t int_val = strtol(copy, &end, 10);
    if (*end == '\0') {
        mem_free(copy);
        return ctx->builder.createInt(int_val);
    }

    double float_val = strtod(copy, &end);
    if (*end == '\0') {
        mem_free(copy);
        return ctx->builder.createFloat(float_val);
    }

    // Handle quoted strings
    if (copy[0] == '"' && copy[strlen(copy) - 1] == '"') {
        copy[strlen(copy) - 1] = '\0';
        String* str_result = ctx->builder.createString(copy + 1);
        mem_free(copy);
        return (Item){.item = s2it(str_result)};
    }

    // Default to string
    String* str_result = ctx->builder.createString(copy);
    mem_free(copy);
    return (Item){.item = s2it(str_result)};
}

// Parse flow array like [item1, item2, item3]
Array* parse_flow_array(InputContext* ctx, const char* str) {
    ArrayBuilder array_builder = ctx->builder.array();

    if (!str || strlen(str) < 2) {
        return array_builder.final().array;
    }

    // Make a copy and remove brackets
    char* copy = mem_strdup(str, MEM_CAT_INPUT_YAML);
    if (copy[0] == '[') copy++;
    if (copy[strlen(copy) - 1] == ']') copy[strlen(copy) - 1] = '\0';

    trim_string_inplace(copy);

    if (strlen(copy) == 0) {
        mem_free(copy - (str[0] == '[' ? 1 : 0));
        return array_builder.final().array;
    }

    // Split by comma
    char* token = copy;
    char* next = copy;

    while (next) {
        next = strchr(token, ',');
        if (next) {
            *next = '\0';
            next++;
        }

        trim_string_inplace(token);
        if (strlen(token) > 0) {
            Item item = parse_scalar_value(ctx, token);
            array_builder.append(item);
        }

        token = next;
    }

    mem_free(copy - (str[0] == '[' ? 1 : 0));
    return array_builder.final().array;
}

// Parse YAML content
static Item parse_yaml_content(InputContext* ctx, char** lines, int* current_line, int total_lines, int target_indent) {
    if (*current_line >= total_lines) {
        return ctx->builder.createNull();
    }

    char* line = lines[*current_line];

    // Get indentation level
    int indent = 0;
    while (line[indent] == ' ') indent++;

    // Check for tab indentation (invalid in YAML)
    for (int i = 0; i < indent; i++) {
        if (line[i] == '\t') {
            ctx->addError("YAML does not allow tab characters for indentation");
            // Convert tabs to spaces for recovery
            line[i] = ' ';
        }
    }

    // If we've dedented, return
    if (indent < target_indent) {
        return ctx->builder.createNull();
    }

    // Warn about inconsistent indentation
    if (indent > target_indent && (indent - target_indent) % 2 != 0) {
        ctx->addWarning("Inconsistent indentation detected (not a multiple of 2 spaces)");
    }

    char* content = line + indent;

    // Check for array item
    if (content[0] == '-' && (content[1] == ' ' || content[1] == '\0')) {
        ArrayBuilder array_builder = ctx->builder.array();

        while (*current_line < total_lines) {
            line = lines[*current_line];
            indent = 0;
            while (line[indent] == ' ') indent++;

            if (indent < target_indent) break;
            if (indent > target_indent) {
                (*current_line)++;
                continue;
            }

            content = line + indent;
            if (content[0] != '-' || (content[1] != ' ' && content[1] != '\0')) break;

            (*current_line)++;

            // Parse array item
            char* item_content = content + 1;
            while (*item_content == ' ') item_content++;

            Item item;
            if (strlen(item_content) > 0) {
                // Item on same line
                item = parse_scalar_value(ctx, item_content);
            } else {
                // Item on following lines
                item = parse_yaml_content(ctx, lines, current_line, total_lines, target_indent + 2);
            }

            array_builder.append(item);
        }

        return array_builder.final();
    }

    // Check for object (key: value)
    char* colon_pos = strstr(content, ":");
    if (colon_pos && (colon_pos[1] == ' ' || colon_pos[1] == '\0')) {
        MapBuilder map_builder = ctx->builder.map();

        while (*current_line < total_lines) {
            line = lines[*current_line];
            indent = 0;
            while (line[indent] == ' ') indent++;

            if (indent < target_indent) break;
            if (indent > target_indent) {
                (*current_line)++;
                continue;
            }

            content = line + indent;
            colon_pos = strstr(content, ":");
            if (!colon_pos || (colon_pos[1] != ' ' && colon_pos[1] != '\0')) {
                if (!colon_pos) {
                    ctx->addError("Expected key-value pair with colon separator");
                } else {
                    ctx->addError("Missing space after colon in key-value pair");
                }
                (*current_line)++;
                continue;  // Skip invalid line
            }

            (*current_line)++;

            // Extract key
            int key_len = colon_pos - content;
            char* key_str = (char*)mem_alloc(key_len + 1, MEM_CAT_INPUT_YAML);
            strncpy(key_str, content, key_len);
            key_str[key_len] = '\0';
            trim_string_inplace(key_str);

            // Validate key is not empty
            if (strlen(key_str) == 0) {
                ctx->addError("Empty key in YAML mapping");
                mem_free(key_str);
                continue;
            }

            // Create name object for map key (always pooled)
            String* key = ctx->builder.createName(key_str);
            mem_free(key_str);
            if (!key) continue;

            // Extract value
            char* value_content = colon_pos + 1;
            while (*value_content == ' ') value_content++;

            Item value;
            if (strlen(value_content) > 0) {
                // Value on same line
                if (value_content[0] == '[') {
                    // Flow array
                    Array* flow_array = parse_flow_array(ctx, value_content);
                    value = {.item = (uint64_t)flow_array};
                } else {
                    // Scalar value
                    value = parse_scalar_value(ctx, value_content);
                }
            } else {
                // Value on following lines
                value = parse_yaml_content(ctx, lines, current_line, total_lines, target_indent + 2);
            }

            // Add to map using builder
            map_builder.put(key, value);
        }
        return map_builder.final();
    }

    // Single scalar value
    (*current_line)++;
    return parse_scalar_value(ctx, content);
}

void parse_yaml(Input *input, const char* yaml_str) {
    InputContext ctx(input, yaml_str, strlen(yaml_str));

    // Split into lines
    char* yaml_copy = mem_strdup(yaml_str, MEM_CAT_INPUT_YAML);
    char** all_lines = (char**)mem_alloc(1000 * sizeof(char*), MEM_CAT_INPUT_YAML);
    int total_line_count = 0;

    char* saveptr;
    char* line = strtok_r(yaml_copy, "\n", &saveptr);
    while (line && total_line_count < 1000) {
        // Strip comments from line before storing
        char* clean_line = strip_yaml_comments(line);
        all_lines[total_line_count++] = clean_line;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    mem_free(yaml_copy);

    if (total_line_count == 0) {
        mem_free(all_lines);
        ctx.addWarning("Empty YAML document");
        return;
    }

    // Find document boundaries
    int* doc_starts = (int*)mem_alloc(100 * sizeof(int), MEM_CAT_INPUT_YAML);
    int doc_count = 0;
    bool has_doc_markers = false;

    // Check if there are any document markers
    for (int i = 0; i < total_line_count; i++) {
        if (strncmp(all_lines[i], "---", 3) == 0) {
            has_doc_markers = true;
            break;
        }
    }

    if (!has_doc_markers) {
        // No document markers - treat as single document
        doc_starts[doc_count++] = 0;
    } else {
        // Find all document starts
        bool in_document = false;
        for (int i = 0; i < total_line_count; i++) {
            if (strncmp(all_lines[i], "---", 3) == 0) {
                // Document marker found
                if (i + 1 < total_line_count && doc_count < 100) {
                    doc_starts[doc_count++] = i + 1; // Start after the marker
                    in_document = true;
                }
            } else if (!in_document && doc_count == 0) {
                // Content before first marker - treat as first document
                doc_starts[doc_count++] = 0;
                in_document = true;
            }
        }
    }

    // Parse each document
    ArrayBuilder documents_builder = ctx.builder.array();
    Item final_result = ctx.builder.createNull();
    int parsed_doc_count = 0;

    for (int doc_idx = 0; doc_idx < doc_count; doc_idx++) {
        int start_line = doc_starts[doc_idx];
        int end_line = (doc_idx + 1 < doc_count) ? doc_starts[doc_idx + 1] - 1 : total_line_count;

        // Skip if start line is at or beyond end
        if (start_line >= end_line) {
            ctx.addWarning("Empty YAML document found");
            continue;
        }

        // Create lines array for this document, excluding document markers and empty lines
        char** doc_lines = (char**)mem_alloc(1000 * sizeof(char*), MEM_CAT_INPUT_YAML);
        int doc_line_count = 0;

        for (int i = start_line; i < end_line; i++) {
            // Skip document markers and empty lines
            if (strlen(all_lines[i]) > 0 && strncmp(all_lines[i], "---", 3) != 0) {
                doc_lines[doc_line_count++] = mem_strdup(all_lines[i], MEM_CAT_INPUT_YAML);
            }
        }

        // Parse this document if it has content
        if (doc_line_count > 0) {
            int current_line = 0;
            Item doc_result = parse_yaml_content(&ctx, doc_lines, &current_line, doc_line_count, 0);

            if (parsed_doc_count == 0) {
                // Store first document
                final_result = doc_result;
                parsed_doc_count++;
            } else if (parsed_doc_count == 1) {
                // Second document found - add first document to array
                documents_builder.append(final_result);
                // Add current document to array
                documents_builder.append(doc_result);
                parsed_doc_count++;
            } else {
                // Third+ document - add to existing array
                documents_builder.append(doc_result);
                parsed_doc_count++;
            }
        } else {
            ctx.addWarning("Empty YAML document (only comments or whitespace)");
        }

        // cleanup document lines
        for (int i = 0; i < doc_line_count; i++) { mem_free(doc_lines[i]); }
        mem_free(doc_lines);
    }

    // Set the final result
    if (parsed_doc_count > 1) {
        input->root = documents_builder.final();
    } else {
        input->root = final_result;
    }

    // Log any errors that occurred during parsing
    ctx.logErrors();

    // cleanup
    for (int i = 0; i < total_line_count; i++) { mem_free(all_lines[i]); }
    mem_free(all_lines);
    mem_free(doc_starts);
}

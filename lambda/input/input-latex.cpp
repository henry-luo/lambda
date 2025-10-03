#include "input.h"
#include "input-common.h"

// Forward declaration for math parser integration
void parse_math(Input* input, const char* math_string, const char* flavor);

static Item parse_latex_element(Input *input, const char **latex);

// Use common utility functions from input.c and input-common.c
#define create_latex_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

static void skip_whitespace(const char **latex) {
    skip_common_whitespace(latex);
}

// LaTeX special characters that need escaping
static const char* latex_special_chars = "\\{}$&#^_%~";

static void skip_comment(const char **latex) {
    skip_latex_comment(latex);
}

static String* parse_latex_string_content(Input *input, const char **latex, char end_char) {
    StringBuf* sb = input->sb;
    int char_count = 0;
    const int max_string_chars = 10000; // Safety limit

    // Handle empty string case
    if (**latex == end_char) {
        return stringbuf_to_string(sb);
    }

    while (**latex && **latex != end_char && char_count < max_string_chars) {
        if (**latex == '\\') {
            // Handle LaTeX escape sequences
            (*latex)++; // Skip backslash

            if (**latex == '\0') break;

            // Handle common LaTeX escapes
            switch (**latex) {
                case '\\': stringbuf_append_char(sb, '\n'); break; // Line break
                case '{': stringbuf_append_char(sb, '{'); break;
                case '}': stringbuf_append_char(sb, '}'); break;
                case '$': stringbuf_append_char(sb, '$'); break;
                case '&': stringbuf_append_char(sb, '&'); break;
                case '#': stringbuf_append_char(sb, '#'); break;
                case '^': stringbuf_append_char(sb, '^'); break;
                case '_': stringbuf_append_char(sb, '_'); break;
                case '%': stringbuf_append_char(sb, '%'); break;
                case '~': stringbuf_append_char(sb, '~'); break;
                default:
                    // Unknown escape, keep both characters
                    stringbuf_append_char(sb, '\\');
                    stringbuf_append_char(sb, **latex);
                    break;
            }
            (*latex)++;
        } else if (**latex == '%') {
            // Skip LaTeX comments
            skip_comment(latex);
        } else {
            stringbuf_append_char(sb, **latex);
            (*latex)++;
        }
        char_count++;
    }

    return stringbuf_to_string(sb);
}

static String* parse_command_name(Input *input, const char **latex) {
    StringBuf* sb = input->sb;

    // Handle single-character control symbols (LaTeX-JS style)
    if (**latex && strchr("$%#&{}_\\-,/@^~", **latex)) {
        stringbuf_append_char(sb, **latex);
        (*latex)++;
        return stringbuf_to_string(sb);
    }

    // Command names can contain letters and sometimes numbers
    while (**latex && (isalpha(**latex) || (**latex == '*'))) {
        stringbuf_append_char(sb, **latex);
        (*latex)++;
    }

    return stringbuf_to_string(sb);
}

static Array* parse_command_arguments(Input *input, const char **latex) {
    printf("DEBUG: parse_command_arguments starting at: '%.30s'\n", *latex);
    Array* args = array_pooled(input->pool);
    if (!args) return NULL;

    skip_whitespace(latex);

    // Parse optional arguments [...]
    while (**latex == '[') {
        (*latex)++; // Skip [
        String* opt_arg = parse_latex_string_content(input, latex, ']');
        if (**latex == ']') (*latex)++; // Skip ]

        if (opt_arg && opt_arg->len > 0) {
            Item arg_item = {.item = s2it(opt_arg)};
            array_append(args, arg_item, input->pool);
        }
        skip_whitespace(latex);
    }

    // Parse required arguments {...}
    while (**latex == '{') {
        printf("DEBUG: Found opening brace, parsing argument from: '%.20s'\n", *latex);
        (*latex)++; // Skip {

        // For required arguments, we need to handle nested braces
        int brace_depth = 1;
        StringBuf* arg_sb = input->sb;
        stringbuf_reset(arg_sb);

        int char_count = 0;
        while (**latex && brace_depth > 0 && char_count < 100) { // Safety limit
            printf("DEBUG: Processing char '%c' (brace_depth=%d)\n", **latex, brace_depth);
            if (**latex == '{') {
                brace_depth++;
                stringbuf_append_char(arg_sb, **latex);
            } else if (**latex == '}') {
                brace_depth--;
                if (brace_depth > 0) {
                    stringbuf_append_char(arg_sb, **latex);
                }
                printf("DEBUG: Found closing brace, brace_depth now %d\n", brace_depth);
            } else if (**latex == '\\') {
                stringbuf_append_char(arg_sb, **latex);
                (*latex)++;
                if (**latex) {
                    stringbuf_append_char(arg_sb, **latex);
                }
            } else {
                stringbuf_append_char(arg_sb, **latex);
            }
            (*latex)++;
            char_count++;
        }
        printf("DEBUG: Finished parsing argument, processed %d chars\n", char_count);

        if (arg_sb->length > 0) {
            // Create a safe copy by extracting the content before StringBuf operations
            size_t content_len = arg_sb->length;
            char* content_chars = (char*)pool_calloc(input->pool, content_len + 1);
            if (content_chars) {
                memcpy(content_chars, arg_sb->str->chars, content_len);
                content_chars[content_len] = '\0';

                String *arg_string = input_create_string(input, content_chars);
                if (arg_string) {
                    printf("DEBUG: Parsed argument: '%.*s' (length: %u)\n", (int)arg_string->len, arg_string->chars, arg_string->len);
                    Item arg_item = {.item = s2it(arg_string)};
                    array_append(args, arg_item, input->pool);
                } else {
                    printf("DEBUG: input_create_string failed\n");
                }
            } else {
                printf("DEBUG: Failed to allocate memory for argument\n");
            }
            stringbuf_reset(arg_sb);
        } else {
            printf("DEBUG: Argument too short, skipping\n");
            stringbuf_reset(arg_sb);
        }

        skip_whitespace(latex);
    }

    return args;
}

static Item parse_latex_command(Input *input, const char **latex) {
    if (**latex != '\\') {
        return {.item = ITEM_ERROR};
    }

    (*latex)++; // Skip backslash

    String* cmd_name = parse_command_name(input, latex);
    if (!cmd_name || cmd_name->len == 0) {
        return {.item = ITEM_ERROR};
    }

    printf("DEBUG: Parsing command '%.*s' at position: '%.30s'\n", (int)cmd_name->len, cmd_name->chars, *latex);

    // Handle control symbols (LaTeX-JS style: escape c:[$%#&{}_\-,/@])
    if (cmd_name->len == 1) {
        char escaped_char = cmd_name->chars[0];
        printf("DEBUG: Processing control symbol: \\%c\n", escaped_char);

        // Handle control symbols that produce literal characters
        if (escaped_char == '$' || escaped_char == '%' || escaped_char == '#' ||
            escaped_char == '&' || escaped_char == '{' || escaped_char == '}' ||
            escaped_char == '_' || escaped_char == '^' || escaped_char == '~') {

            // Handle ^ and ~ which might have {} after them
            if (escaped_char == '^' || escaped_char == '~') {
                if (**latex == '{' && *(*latex + 1) == '}') {
                    (*latex) += 2; // Skip {}
                }
            }
            
            printf("DEBUG: Converting control symbol \\%c to literal '%c' element\n", escaped_char, escaped_char);
            
            // Create element with the character as content to avoid string merging
            Element* element = create_latex_element(input, "literal");
            if (!element) {
                return {.item = ITEM_ERROR};
            }
            
            // Add the literal character as content
            StringBuf* text_sb = input->sb;
            stringbuf_reset(text_sb);
            stringbuf_append_char(text_sb, escaped_char);
            String* char_str = stringbuf_to_string(text_sb);
            
            if (char_str) {
                list_push((List*)element, {.item = s2it(char_str)});
                ((TypeElmt*)element->type)->content_length = 1;
            }
            
            return {.item = (uint64_t)element};
        }
        // Handle special control symbols
        else if (escaped_char == ',') {
            // \, = thin space - create element to avoid string merging
            printf("DEBUG: Converting \\, to thin space element\n");
            Element* element = create_latex_element(input, "thinspace");
            if (!element) {
                return {.item = ITEM_ERROR};
            }
            return {.item = (uint64_t)element};
        }
        else if (escaped_char == '-') {
            // \- = soft hyphen
            printf("DEBUG: Converting \\- to soft hyphen\n");
            StringBuf* text_sb = input->sb;
            stringbuf_reset(text_sb);
            stringbuf_append_str(text_sb, "\u00AD"); // Unicode soft hyphen

            String* text_str = stringbuf_to_string(text_sb);
            return {.item = (uint64_t)text_str};
        }
        else if (escaped_char == '/') {
            // \/ = zero-width non-joiner
            printf("DEBUG: Converting \\/ to ZWNJ\n");
            StringBuf* text_sb = input->sb;
            stringbuf_reset(text_sb);
            stringbuf_append_str(text_sb, "\u200C"); // Unicode ZWNJ

            String* text_str = stringbuf_to_string(text_sb);
            return {.item = (uint64_t)text_str};
        }
        else if (escaped_char == '@') {
            // \@ = zero-width space (prevent space collapsing)
            printf("DEBUG: Converting \\@ to zero-width space\n");
            StringBuf* text_sb = input->sb;
            stringbuf_reset(text_sb);
            stringbuf_append_str(text_sb, "\u200B"); // Unicode zero-width space

            String* text_str = stringbuf_to_string(text_sb);
            return {.item = (uint64_t)text_str};
        }
        // Handle line break (\\)
        else if (escaped_char == '\\') {
            printf("DEBUG: Converting \\\\ to line break\n");
            // Create a line break element
            Element* element = create_latex_element(input, "linebreak");
            if (!element) {
                return {.item = ITEM_ERROR};
            }
            return {.item = (uint64_t)element};
        }
    }

    // Handle line break commands
    if (strcmp(cmd_name->chars, "\\") == 0 || strcmp(cmd_name->chars, "newline") == 0) {
        // printf("DEBUG: Processing line break command: %s\n", cmd_name->chars);
        // Create a line break element
        Element* element = create_latex_element(input, "linebreak");
        if (!element) {
            return {.item = ITEM_ERROR};
        }
        return {.item = (uint64_t)element};
    }

    if (strcmp(cmd_name->chars, "par") == 0) {
        // Create a paragraph break element
        Element* element = create_latex_element(input, "par");
        if (!element) {
            return {.item = ITEM_ERROR};
        }
        return {.item = (uint64_t)element};
    }

    // Handle verb command
    if (strcmp(cmd_name->chars, "verb") == 0) {
        printf("DEBUG: Processing verb command\n");
        // Parse \verb|text| or \verb/text/ etc.
        if (**latex) {
            char delimiter = **latex;
            (*latex)++; // Skip delimiter

            // Find closing delimiter
            StringBuf* verb_sb = input->sb;
            stringbuf_reset(verb_sb);

            while (**latex && **latex != delimiter) {
                stringbuf_append_char(verb_sb, **latex);
                (*latex)++;
            }

            if (**latex == delimiter) {
                (*latex)++; // Skip closing delimiter
            }

            String* verb_text = stringbuf_to_string(verb_sb);

            // Create verb element
            Element* element = create_latex_element(input, "verb");
            if (element && verb_text) {
                list_push((List*)element, {.item = s2it(verb_text)});
                ((TypeElmt*)element->type)->content_length = 1;
                return {.item = (uint64_t)element};
            }
        }
        return {.item = ITEM_ERROR};
    }

    // Handle special multi-character escape sequences
    if (strcmp(cmd_name->chars, "textbackslash") == 0) {
        // Create textbackslash element to avoid string merging
        Element* element = create_latex_element(input, "textbackslash");
        if (!element) {
            return {.item = ITEM_ERROR};
        }
        return {.item = (uint64_t)element};
    }

    // Handle \end{} commands - these are handled implicitly by environment parsing
    if (strcmp(cmd_name->chars, "end") == 0) {
        // Skip \end{} commands as they are handled by the environment parser
        Array* end_args = parse_command_arguments(input, latex);
        return {.item = ITEM_NULL}; // Don't create an element for \end commands
    }

    // Create element for the command
    Element* element = create_latex_element(input, cmd_name->chars);
    if (!element) {
        return {.item = ITEM_ERROR};
    }

    // Handle special commands that need custom parsing
    if (strcmp(cmd_name->chars, "item") == 0) {
        // \item commands don't take arguments in braces
        // Their content is everything until the next \item or \end{environment}
        printf("DEBUG: Parsing \\item command content\n");

        skip_whitespace(latex);

        // Parse content until next \item or \end
        StringBuf* content_sb = input->sb;
        stringbuf_reset(content_sb);

        while (**latex) {
            // Stop at next \item or \end
            if (strncmp(*latex, "\\item", 5) == 0 || strncmp(*latex, "\\end{", 5) == 0) {
                break;
            }

            // Handle escaped characters
            if (**latex == '\\') {
                // This might be a command within the item content
                // For now, just add the backslash and continue
                stringbuf_append_char(content_sb, **latex);
                (*latex)++;
                if (**latex) {
                    stringbuf_append_char(content_sb, **latex);
                    (*latex)++;
                }
            } else {
                stringbuf_append_char(content_sb, **latex);
                (*latex)++;
            }
        }

        // Add the content as a child of the item element
        if (content_sb->length > 0) {
            String *content_string = stringbuf_to_string(content_sb);
            if (content_string) {
                // Trim whitespace from content
                char* trimmed_content = (char*)pool_calloc(input->pool, content_string->len + 1);
                if (trimmed_content) {
                    strncpy(trimmed_content, content_string->chars, content_string->len);
                    trimmed_content[content_string->len] = '\0';

                    // Simple trim - remove leading/trailing whitespace
                    char* start = trimmed_content;
                    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;

                    char* end = start + strlen(start) - 1;
                    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
                    *(end + 1) = '\0';

                    if (strlen(start) > 0) {
                        String *trimmed_string = input_create_string(input, start);
                        if (trimmed_string) {
                            printf("DEBUG: Adding item content: '%s'\n", start);
                            Item content_item = {.item = s2it(trimmed_string)};
                            list_push((List*)element, content_item);
                        }
                    }
                }
            }
            stringbuf_reset(content_sb);
        }

        // Set content length
        ((TypeElmt*)element->type)->content_length = ((List*)element)->length;
        return {.item = (uint64_t)element};
    }

    // Parse arguments
    Array* args = parse_command_arguments(input, latex);

    // Handle special commands that have content blocks
    if (strcmp(cmd_name->chars, "begin") == 0) {
        // This is an environment begin - create element with environment name as tag
        if (args && args->length > 0) {
            printf("DEBUG: Processing \\begin command with %lld arguments\n", args->length);

            // Extract environment name from the successfully parsed argument
            const char* env_name = "itemize"; // Default

            // Try to access the first argument which contains the environment name
            if (args && args->length > 0 && args->items) {
                Item first_arg = args->items[0];
                TypeId arg_type = get_type_id(first_arg);

                if (arg_type == LMD_TYPE_STRING) {
                    String* env_string = (String*)first_arg.pointer;
                    if (env_string && env_string->chars && env_string->len > 0 && env_string->len < 50) {
                        // We have a valid environment name string
                        // printf("DEBUG: Found environment name from argument: '%s'\n", env_string->chars);
                        env_name = env_string->chars;
                    }
                }
            }

            // printf("DEBUG: Final environment name: '%s'\n", env_name);
            // printf("DEBUG: Detected environment: %s\n", env_name);
            // printf("DEBUG: Creating environment element for: '%s'\n", env_name);

            // Create a new element with the environment name instead
            element = create_latex_element(input, env_name);
            if (!element) {
                return ItemError;
            }

            // Don't add arguments to the element for environments
            // The environment name becomes the tag, not a child

            // Check if this is a math or raw text environment that should preserve content as-is
            bool is_math_env = is_math_environment(env_name);
            bool is_raw_text_env = is_raw_text_environment(env_name);

            // Parse content until \end{environment_name}
            skip_whitespace(latex);

            if (is_math_env || is_raw_text_env) {
                // For math and raw text environments, collect content as raw text
                StringBuf* content_sb = input->sb;
                stringbuf_reset(content_sb);

                int content_chars = 0;
                const int max_content_chars = 10000;

                while (**latex && content_chars < max_content_chars) {
                    // Check if we found the closing tag
                    if (strncmp(*latex, "\\end{", 5) == 0) {
                        const char* end_check = *latex + 5;
                        size_t env_name_len = strlen(env_name);
                        if (strncmp(end_check, env_name, env_name_len) == 0 &&
                            end_check[env_name_len] == '}') {
                            // Found matching \end, don't include it in content
                            *latex = end_check + env_name_len + 1;
                            break;
                        }
                    }

                    stringbuf_append_char(content_sb, **latex);
                    (*latex)++;
                    content_chars++;
                }

                // Process the content
                if (content_sb->length > 0) {
                    String *content_string = stringbuf_to_string(content_sb);
                    if (!content_string) {
                        stringbuf_reset(content_sb);
                        return ItemError;
                    }

                    if (content_string->len > 0) {
                        if (is_math_env) {
                            // Parse math content using our math parser
                            Input* math_input = input_new((Url*)input->url);
                            if (math_input) {
                                // Reset our StrBuf before calling math parser
                                stringbuf_reset(input->sb);

                                parse_math(math_input, content_string->chars, "latex");

                                // Reset our StrBuf after calling math parser
                                stringbuf_reset(input->sb);

                                if (math_input->root .item != ITEM_NULL) {
                                    // Add the parsed math as child
                                    list_push((List*)element, math_input->root);

                                    // Clean up temporary input (but preserve the parsed result)
                                    if (math_input->type_list) {
                                        arraylist_free(math_input->type_list);
                                    }
                                    pool_destroy(math_input->pool);
                                    free(math_input);
                                } else {
                                    // Fallback to raw text if math parsing fails
                                    Item content_item = {.item = s2it(content_string)};
                                    list_push((List*)element, content_item);

                                    // Clean up temporary input
                                    if (math_input->type_list) {
                                        arraylist_free(math_input->type_list);
                                    }
                                    pool_destroy(math_input->pool);
                                    free(math_input);
                                }
                            } else {
                                // Fallback to raw text if input creation fails
                                Item content_item = {.item = s2it(content_string)};
                                list_push((List*)element, content_item);
                            }
                        } else {
                            // For raw text environments, add as-is
                            Item content_item = {.item = s2it(content_string)};
                            list_push((List*)element, content_item);
                        }
                    }
                } else {
                    stringbuf_reset(content_sb);
                }
            } else {
                // For non-math/non-raw-text environments, parse content normally
                while (**latex) {
                    // Check for \end{environment_name}
                    if (strncmp(*latex, "\\end{", 5) == 0) {
                        const char* end_check = *latex + 5;
                        size_t env_name_len = strlen(env_name);
                        if (strncmp(end_check, env_name, env_name_len) == 0 &&
                            end_check[env_name_len] == '}') {
                            // Found matching \end, skip it
                            *latex = end_check + env_name_len + 1;
                            break;
                        }
                    }

                    // Parse content within the environment
                    if (**latex == '\\') {
                        Item child = parse_latex_command(input, latex);
                        if (child .item != ITEM_ERROR && child .item != ITEM_NULL) {
                            list_push((List*)element, child);
                        }
                    } else if (**latex == '%') {
                        skip_comment(latex);
                    } else {
                        // Parse text content with proper escape handling
                        StringBuf* text_sb = input->sb;
                        stringbuf_reset(text_sb);

                        int text_chars = 0;
                        const int max_text_chars = 5000;

                        while (**latex && text_chars < max_text_chars) {
                            // Check for end of environment pattern
                            if (strncmp(*latex, "\\end{", 5) == 0) {
                                break;
                            }

                            // Check for other LaTeX commands
                            if (**latex == '\\') {
                                // Peek ahead to see if this is a command or just an escape
                                const char* next_char = *latex + 1;
                                if (*next_char && strchr("{}$&#^_%~", *next_char)) {
                                    // This is an escaped character, handle it
                                    (*latex)++; // Skip backslash
                                    switch (**latex) {
                                        case '{': stringbuf_append_char(text_sb, '{'); break;
                                        case '}': stringbuf_append_char(text_sb, '}'); break;
                                        case '$': stringbuf_append_char(text_sb, '$'); break;
                                        case '&': stringbuf_append_char(text_sb, '&'); break;
                                        case '#': stringbuf_append_char(text_sb, '#'); break;
                                        case '^': stringbuf_append_char(text_sb, '^'); break;
                                        case '_': stringbuf_append_char(text_sb, '_'); break;
                                        case '%': stringbuf_append_char(text_sb, '%'); break;
                                        case '~': stringbuf_append_char(text_sb, '~'); break;
                                        default:
                                            stringbuf_append_char(text_sb, '\\');
                                            stringbuf_append_char(text_sb, **latex);
                                            break;
                                    }
                                    (*latex)++;
                                } else {
                                    // This is a LaTeX command, break to let the main parser handle it
                                    break;
                                }
                            } else if (**latex == '%') {
                                // This is a comment, break to let the comment handler deal with it
                                break;
                            } else {
                                stringbuf_append_char(text_sb, **latex);
                                (*latex)++;
                            }
                            text_chars++;
                        }

                        if (text_sb->length > 0) {
                            String *text_string = stringbuf_to_string(text_sb);
                            stringbuf_reset(text_sb);

                            if (!text_string) {
                                continue;
                            }

                            // Only add non-whitespace text
                            bool has_non_whitespace = false;
                            for (int i = 0; i < text_string->len; i++) {
                                if (!isspace(text_string->chars[i])) {
                                    has_non_whitespace = true;
                                    break;
                                }
                            }

                            if (has_non_whitespace) {
                                Item text_item = {.item = s2it(text_string)};
                                list_push((List*)element, text_item);
                            }
                        } else {
                            stringbuf_reset(text_sb);
                        }
                    }

                    skip_whitespace(latex);
                }
            }
        }
    } else {
        // For non-environment commands, add arguments as children
        if (args && args->length > 0) {
            for (size_t i = 0; i < args->length; i++) {
                list_push((List*)element, args->items[i]);
            }
        }
    }

    // Set content length based on element's list length
    ((TypeElmt*)element->type)->content_length = ((List*)element)->length;

    return {.item = (uint64_t)element};
}

static Item parse_latex_element(Input *input, const char **latex) {
    static int parse_depth = 0;
    parse_depth++;

    if (parse_depth > 20) {  // Reasonable depth limit for LaTeX
        parse_depth--;
        return {.item = ITEM_ERROR};
    }

    skip_whitespace(latex);

    if (!**latex) {
        parse_depth--;
        return {.item = ITEM_NULL};
    }

    // Skip comments
    if (**latex == '%') {
        skip_comment(latex);
        skip_whitespace(latex);
        if (**latex) {
            Item result = parse_latex_element(input, latex);
            parse_depth--;
            return result;
        }
        parse_depth--;
        return {.item = ITEM_NULL};
    }

    // Parse LaTeX commands
    if (**latex == '\\') {
        Item result = parse_latex_command(input, latex);
        parse_depth--;
        return result;
    }

    // Handle math mode delimiters
    if (**latex == '$') {
        bool display_math = false;
        (*latex)++; // Skip first $

        if (**latex == '$') {
            display_math = true;
            (*latex)++; // Skip second $
        }

        // Parse math content until closing delimiter
        StringBuf* math_sb = input->sb;
        stringbuf_reset(math_sb);

        while (**latex) {
            if (**latex == '$') {
                if (display_math) {
                    if (*(*latex + 1) == '$') {
                        (*latex) += 2; // Skip $$
                        break;
                    } else {
                        stringbuf_append_char(math_sb, **latex);
                        (*latex)++;
                    }
                } else {
                    (*latex)++; // Skip $
                    break;
                }
            } else {
                stringbuf_append_char(math_sb, **latex);
                (*latex)++;
            }
        }

        // Create a temporary Input for math parsing
        if (math_sb->length > 0) {
            String *math_string = stringbuf_to_string(math_sb);
            if (!math_string) {
                parse_depth--;
                return {.item = ITEM_ERROR};
            }

            if (math_string->len > 0) {
                // Create temporary input for math parsing
                Input* math_input = input_new((Url*)input->url);
                if (math_input) {
                    // Reset our StrBuf before calling math parser
                    stringbuf_reset(input->sb);

                    // Parse the math content using our math parser
                    parse_math(math_input, math_string->chars, "latex");

                    // Reset our StrBuf after calling math parser
                    stringbuf_reset(input->sb);

                    // Create wrapper element for the math
                    const char* math_name = display_math ? "displaymath" : "math";
                    Element* element = create_latex_element(input, math_name);
                    if (element && math_input->root .item != ITEM_NULL) {
                        // Add the parsed math as child
                        list_push((List*)element, math_input->root);
                        ((TypeElmt*)element->type)->content_length = ((List*)element)->length;

                        // Clean up temporary input (but preserve the parsed result)
                        // Note: We don't free math_input->root as it's now owned by element
                        if (math_input->type_list) {
                            arraylist_free(math_input->type_list);
                        }
                        pool_destroy(math_input->pool);
                        free(math_input);

                        stringbuf_reset(math_sb);
                        parse_depth--;
                        return {.item = (uint64_t)element};
                    }

                    // Cleanup on failure
                    if (math_input->type_list) {
                        arraylist_free(math_input->type_list);
                    }
                    pool_destroy(math_input->pool);
                    free(math_input);
                }
            }
        }

        parse_depth--;
        return {.item = ITEM_ERROR};
    }

    // Parse regular text content
    printf("DEBUG: Parsing text starting at: '%.20s'\n", *latex);
    StringBuf* text_sb = input->sb;
    stringbuf_reset(text_sb);
    int text_chars = 0;
    const int max_text_chars = 5000;

    while (**latex && text_chars < max_text_chars) {
        if (**latex == '\\') {
            printf("DEBUG: Found backslash at position %d\n", text_chars);
            // Check if this is an escaped character or command
            const char* next_char = *latex + 1;
            if (next_char && strchr("{}$&#^_%~", *next_char)) {
                // This is an escaped character
                (*latex)++; // Skip backslash
                switch (**latex) {
                    case '{': stringbuf_append_char(text_sb, '{'); break;
                    case '}': stringbuf_append_char(text_sb, '}'); break;
                    case '$': stringbuf_append_char(text_sb, '$'); break;
                    case '&': stringbuf_append_char(text_sb, '&'); break;
                    case '#': stringbuf_append_char(text_sb, '#'); break;
                    case '^': stringbuf_append_char(text_sb, '^'); break;
                    case '_': stringbuf_append_char(text_sb, '_'); break;
                    case '%': stringbuf_append_char(text_sb, '%'); break;
                    case '~': stringbuf_append_char(text_sb, '~'); break;
                    default:
                        stringbuf_append_char(text_sb, '\\');
                        stringbuf_append_char(text_sb, **latex);
                        break;
                }
                (*latex)++;
            } else {
                // This is a LaTeX command, break and process collected text
                printf("DEBUG: Found LaTeX command, breaking with %d chars collected\n", text_chars);
                break;
            }
        } else if (**latex == '-') {
            // Handle LaTeX dash ligatures: -- (en dash) and --- (em dash)
            if (*(*latex + 1) == '-') {
                if (*(*latex + 2) == '-') {
                    // Three dashes: --- → em dash (—)
                    stringbuf_append_str(text_sb, "—");
                    (*latex) += 3;
                    text_chars += 3;
                } else {
                    // Two dashes: -- → en dash (–)
                    stringbuf_append_str(text_sb, "–");
                    (*latex) += 2;
                    text_chars += 2;
                }
            } else {
                // Single dash: regular hyphen
                stringbuf_append_char(text_sb, **latex);
                (*latex)++;
                text_chars++;
            }
        } else if (**latex == '$' || **latex == '%') {
            // Math mode, break
            printf("DEBUG: Found math, breaking with %d chars collected\n", text_chars);
            break;
        } else if (**latex == '\n') {
            // Check for paragraph break (double newline)
            if (*(*latex + 1) == '\n') {
                // Found paragraph break, stop parsing text here
                printf("DEBUG: Found paragraph break at position %d\n", text_chars);
                break;
            } else {
                // Single newline, include in text
                stringbuf_append_char(text_sb, **latex);
                (*latex)++;
                text_chars++;
            }
        } else {
            stringbuf_append_char(text_sb, **latex);
            (*latex)++;
            text_chars++;
        }
    }

    printf("DEBUG: Text parsing finished, text_sb->length = %zu\n", text_sb->length);

    if (text_sb->length > 0) {
        printf("DEBUG: Processing collected text\n");
        String *text_string = stringbuf_to_string(text_sb);
        if (!text_string) {
            printf("DEBUG: stringbuf_to_string failed\n");
            stringbuf_reset(text_sb);
            parse_depth--;
            return {.item = ITEM_NULL};
        }

        // Only return non-whitespace text
        bool has_non_whitespace = false;
        for (int i = 0; i < text_string->len; i++) {
            if (!isspace(text_string->chars[i])) {
                has_non_whitespace = true;
                break;
            }
        }

        if (has_non_whitespace) {
            printf("DEBUG: Returning text node: '%.*s' (length: %u)\n", (int)text_string->len, text_string->chars, text_string->len);
            parse_depth--;
            return {.item = s2it(text_string)};
        } else {
            printf("DEBUG: Text node is whitespace-only, skipping: '%.*s'\n", (int)text_string->len, text_string->chars);
        }
    } else {
        stringbuf_reset(text_sb);
    }

    parse_depth--;
    return {.item = ITEM_NULL};
}

void parse_latex(Input* input, const char* latex_string) {
    printf("DEBUG: Starting LaTeX parsing...\n");
    // Reuse the StrBuf from input_new() - don't create a new one
    stringbuf_reset(input->sb);
    const char *latex = latex_string;

    // Create root document element
    Element* root_element = create_latex_element(input, "latex_document");
    if (!root_element) {
        printf("DEBUG: Failed to create root element\n");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Parse LaTeX content
    skip_whitespace(&latex);

    int element_count = 0;
    while (*latex && element_count < 1000) { // Safety limit
        printf("DEBUG: Parsing element %d, current position: '%.50s...'\n", element_count, latex);

        Item element = parse_latex_element(input, &latex);
        if (element .item != ITEM_NULL && element .item != ITEM_ERROR) {
            list_push((List*)root_element, element);
            printf("DEBUG: Added element %d to root\n", element_count);
        } else if (element.item == ITEM_ERROR) {
            printf("DEBUG: Error parsing element %d\n", element_count);
            break;
        } else {
            printf("DEBUG: Element %d was null (likely \\end{} or comment)\n", element_count);
        }
        // Check for paragraph breaks before skipping whitespace
        if (*latex == '\n' && *(latex + 1) == '\n') {
            printf("DEBUG: Creating paragraph break element\n");
            
            // Create a paragraph break element
            Element* par_element = create_latex_element(input, "par");
            if (par_element) {
                list_push((List*)root_element, {.item = (uint64_t)par_element});
                printf("DEBUG: Added paragraph break element to root\n");
            }
        }
        
        // Skip whitespace and paragraph breaks
        skip_whitespace(&latex);
        
        // Skip any remaining paragraph breaks
        while (*latex == '\n' && *(latex + 1) == '\n') {
            latex += 2; // Skip the double newline
            skip_whitespace(&latex); // Skip any additional whitespace
        }

        element_count++;
    }

    printf("DEBUG: Parsed %d elements total\n", element_count);
    printf("DEBUG: Root element list length: %lld\n", ((List*)root_element)->length);
    ((TypeElmt*)root_element->type)->content_length = ((List*)root_element)->length;
    printf("DEBUG: Set content_length to: %lld\n", ((TypeElmt*)root_element->type)->content_length);

    input->root = {.item = (uint64_t)root_element};
    printf("DEBUG: LaTeX parsing completed\n");
}

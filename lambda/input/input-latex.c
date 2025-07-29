#include "input.h"

// Forward declaration for math parser integration
void parse_math(Input* input, const char* math_string, const char* flavor);

static Item parse_latex_element(Input *input, const char **latex);

// Use common utility functions from input.c
#define create_latex_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

static void skip_whitespace(const char **latex) {
    int whitespace_count = 0;
    const int max_whitespace = 1000; // Safety limit
    
    while (**latex && (**latex == ' ' || **latex == '\n' || **latex == '\r' || **latex == '\t') && 
           whitespace_count < max_whitespace) {
        (*latex)++;
        whitespace_count++;
    }
    
    if (whitespace_count >= max_whitespace) {
        printf("WARNING: Hit whitespace limit, possible infinite loop in skip_whitespace\n");
    }
}

// LaTeX special characters that need escaping
static const char* latex_special_chars = "\\{}$&#^_%~";

// Common LaTeX commands
static const char* latex_commands[] = {
    // Document structure
    "documentclass", "usepackage", "begin", "end",
    "part", "chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph",
    
    // Text formatting
    "textbf", "textit", "texttt", "emph", "underline", "textsc", "textrm", "textsf",
    "large", "Large", "LARGE", "huge", "Huge", "small", "footnotesize", "scriptsize", "tiny",
    
    // Math mode
    "frac", "sqrt", "sum", "int", "prod", "lim", "sin", "cos", "tan", "log", "ln", "exp",
    "alpha", "beta", "gamma", "delta", "epsilon", "theta", "lambda", "mu", "pi", "sigma",
    "infty", "partial", "nabla", "cdot", "times", "div", "pm", "mp",
    
    // Lists and environments
    "item", "itemize", "enumerate", "description", "quote", "quotation", "verse",
    "center", "flushleft", "flushright", "verbatim", "tabular", "table", "figure",
    
    // References and citations
    "label", "ref", "cite", "bibliography", "footnote", "marginpar",
    
    // Special symbols
    "LaTeX", "TeX", "ldots", "vdots", "ddots", "quad", "qquad", "hspace", "vspace",
    
    NULL
};

// LaTeX environments that require special handling
static const char* latex_environments[] = {
    "document", "abstract", "itemize", "enumerate", "description", "quote", "quotation",
    "verse", "center", "flushleft", "flushright", "verbatim", "tabular", "array",
    "matrix", "pmatrix", "bmatrix", "vmatrix", "Vmatrix", "cases", "align", "equation",
    "eqnarray", "figure", "table", "minipage", "theorem", "proof", "definition",
    "example", "remark", "note", "warning", NULL
};

// Math environments
static const char* math_environments[] = {
    "equation", "eqnarray", "align", "alignat", "gather", "multline", "split",
    "cases", "matrix", "pmatrix", "bmatrix", "vmatrix", "Vmatrix", "smallmatrix", NULL
};

// Raw text environments (content should be preserved as-is)
static const char* raw_text_environments[] = {
    "verbatim", "lstlisting", "minted", "alltt", "Verbatim", "BVerbatim", 
    "LVerbatim", "SaveVerbatim", "VerbatimOut", "fancyvrb", NULL
};

static bool is_latex_command(const char* cmd_name) {
    for (int i = 0; latex_commands[i]; i++) {
        if (strcmp(cmd_name, latex_commands[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_latex_environment(const char* env_name) {
    for (int i = 0; latex_environments[i]; i++) {
        if (strcmp(env_name, latex_environments[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_math_environment(const char* env_name) {
    for (int i = 0; math_environments[i]; i++) {
        if (strcmp(env_name, math_environments[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_raw_text_environment(const char* env_name) {
    for (int i = 0; raw_text_environments[i]; i++) {
        if (strcmp(env_name, raw_text_environments[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void skip_comment(const char **latex) {
    if (**latex == '%') {
        // Skip to end of line
        while (**latex && **latex != '\n' && **latex != '\r') {
            (*latex)++;
        }
        if (**latex == '\r' && *(*latex + 1) == '\n') {
            (*latex) += 2; // Skip \r\n
        } else if (**latex == '\n' || **latex == '\r') {
            (*latex)++; // Skip \n or \r
        }
    }
}

static String* parse_latex_string_content(Input *input, const char **latex, char end_char) {
    StrBuf* sb = input->sb;
    int char_count = 0;
    const int max_string_chars = 10000; // Safety limit
    
    // Handle empty string case
    if (**latex == end_char) {
        return strbuf_to_string(sb);
    }
    
    while (**latex && **latex != end_char && char_count < max_string_chars) {
        if (**latex == '\\') {
            // Handle LaTeX escape sequences
            (*latex)++; // Skip backslash
            
            if (**latex == '\0') break;
            
            // Handle common LaTeX escapes
            switch (**latex) {
                case '\\': strbuf_append_char(sb, '\n'); break; // Line break
                case '{': strbuf_append_char(sb, '{'); break;
                case '}': strbuf_append_char(sb, '}'); break;
                case '$': strbuf_append_char(sb, '$'); break;
                case '&': strbuf_append_char(sb, '&'); break;
                case '#': strbuf_append_char(sb, '#'); break;
                case '^': strbuf_append_char(sb, '^'); break;
                case '_': strbuf_append_char(sb, '_'); break;
                case '%': strbuf_append_char(sb, '%'); break;
                case '~': strbuf_append_char(sb, '~'); break;
                default:
                    // Unknown escape, keep both characters
                    strbuf_append_char(sb, '\\');
                    strbuf_append_char(sb, **latex);
                    break;
            }
            (*latex)++;
        } else if (**latex == '%') {
            // Skip LaTeX comments
            skip_comment(latex);
        } else {
            strbuf_append_char(sb, **latex);
            (*latex)++;
        }
        char_count++;
    }

    return strbuf_to_string(sb);
}

static String* parse_command_name(Input *input, const char **latex) {
    StrBuf* sb = input->sb;
    
    // Command names can contain letters and sometimes numbers
    while (**latex && (isalpha(**latex) || (**latex == '*'))) {
        strbuf_append_char(sb, **latex);
        (*latex)++;
    }
    
    return strbuf_to_string(sb);
}

static Array* parse_command_arguments(Input *input, const char **latex) {
    Array* args = array_pooled(input->pool);
    if (!args) return NULL;
    
    skip_whitespace(latex);
    
    // Parse optional arguments [...]
    while (**latex == '[') {
        (*latex)++; // Skip [
        String* opt_arg = parse_latex_string_content(input, latex, ']');
        if (**latex == ']') (*latex)++; // Skip ]
        
        if (opt_arg && opt_arg->len > 0) {
            LambdaItem arg_item = (LambdaItem)s2it(opt_arg);
            array_append(args, arg_item, input->pool);
        }
        skip_whitespace(latex);
    }
    
    // Parse required arguments {...}
    while (**latex == '{') {
        (*latex)++; // Skip {
        
        // For required arguments, we need to handle nested braces
        int brace_depth = 1;
        StrBuf* arg_sb = input->sb;
        strbuf_full_reset(arg_sb);
        
        while (**latex && brace_depth > 0) {
            if (**latex == '{') {
                brace_depth++;
                strbuf_append_char(arg_sb, **latex);
            } else if (**latex == '}') {
                brace_depth--;
                if (brace_depth > 0) {
                    strbuf_append_char(arg_sb, **latex);
                }
            } else if (**latex == '\\') {
                strbuf_append_char(arg_sb, **latex);
                (*latex)++;
                if (**latex) {
                    strbuf_append_char(arg_sb, **latex);
                }
            } else {
                strbuf_append_char(arg_sb, **latex);
            }
            (*latex)++;
        }
        
        if (arg_sb->length > sizeof(uint32_t)) {
            String *arg_string = (String*)arg_sb->str;
            arg_string->len = arg_sb->length - sizeof(uint32_t);
            arg_string->ref_cnt = 0;
            strbuf_full_reset(arg_sb);
            
            LambdaItem arg_item = (LambdaItem)s2it(arg_string);
            array_append(args, arg_item, input->pool);
        } else {
            strbuf_full_reset(arg_sb);
        }
        
        skip_whitespace(latex);
    }
    
    return args;
}

static Item parse_latex_command(Input *input, const char **latex) {
    if (**latex != '\\') {
        return ITEM_ERROR;
    }
    
    (*latex)++; // Skip backslash
    
    String* cmd_name = parse_command_name(input, latex);
    if (!cmd_name || cmd_name->len == 0) {
        return ITEM_ERROR;
    }
    
    // Handle \end{} commands - these are handled implicitly by environment parsing
    if (strcmp(cmd_name->chars, "end") == 0) {
        // Skip \end{} commands as they are handled by the environment parser
        Array* end_args = parse_command_arguments(input, latex);
        return ITEM_NULL; // Don't create an element for \end commands
    }
    
    // Create element for the command
    Element* element = create_latex_element(input, cmd_name->chars);
    if (!element) {
        return ITEM_ERROR;
    }
    
    // Parse arguments
    Array* args = parse_command_arguments(input, latex);
    
    // Handle special commands that have content blocks
    if (strcmp(cmd_name->chars, "begin") == 0) {
        // This is an environment begin - create element with environment name as tag
        if (args && args->length > 0) {
            String* env_name = (String*)args->items[0];
            
            // Create a new element with the environment name instead
            element = create_latex_element(input, env_name->chars);
            if (!element) {
                return ITEM_ERROR;
            }
            
            // Don't add arguments to the element for environments
            // The environment name becomes the tag, not a child
            
            // Check if this is a math or raw text environment that should preserve content as-is
            bool is_math_env = is_math_environment(env_name->chars);
            bool is_raw_text_env = is_raw_text_environment(env_name->chars);
            
            // Parse content until \end{environment_name}
            skip_whitespace(latex);
            
            if (is_math_env || is_raw_text_env) {
                // For math and raw text environments, collect content as raw text
                StrBuf* content_sb = input->sb;
                strbuf_full_reset(content_sb);
                
                int content_chars = 0;
                const int max_content_chars = 10000;
                
                while (**latex && content_chars < max_content_chars) {
                    // Check if we found the closing tag
                    if (strncmp(*latex, "\\end{", 5) == 0) {
                        const char* end_check = *latex + 5;
                        if (strncmp(end_check, env_name->chars, env_name->len) == 0 &&
                            end_check[env_name->len] == '}') {
                            // Found matching \end, don't include it in content
                            *latex = end_check + env_name->len + 1;
                            break;
                        }
                    }
                    
                    strbuf_append_char(content_sb, **latex);
                    (*latex)++;
                    content_chars++;
                }
                
                // Process the content
                if (content_sb->length > sizeof(uint32_t)) {
                    String *content_string = (String*)content_sb->str;
                    content_string->len = content_sb->length - sizeof(uint32_t);
                    content_string->ref_cnt = 0;
                    
                    if (content_string->len > 0) {
                        if (is_math_env) {
                            // Parse math content using our math parser
                            Input* math_input = input_new(input->url);
                            if (math_input) {
                                parse_math(math_input, content_string->chars, "latex");
                                
                                if (math_input->root != ITEM_NULL) {
                                    // Add the parsed math as child
                                    list_push((List*)element, math_input->root);
                                    
                                    // Clean up temporary input (but preserve the parsed result)
                                    if (math_input->type_list) {
                                        arraylist_free(math_input->type_list);
                                    }
                                    pool_variable_destroy(math_input->pool);
                                    free(math_input);
                                } else {
                                    // Fallback to raw text if math parsing fails
                                    LambdaItem content_item = (LambdaItem)s2it(content_string);
                                    list_push((List*)element, content_item.item);
                                    
                                    // Clean up temporary input
                                    if (math_input->type_list) {
                                        arraylist_free(math_input->type_list);
                                    }
                                    pool_variable_destroy(math_input->pool);
                                    free(math_input);
                                }
                            } else {
                                // Fallback to raw text if input creation fails
                                LambdaItem content_item = (LambdaItem)s2it(content_string);
                                list_push((List*)element, content_item.item);
                            }
                        } else {
                            // For raw text environments, add as-is
                            LambdaItem content_item = (LambdaItem)s2it(content_string);
                            list_push((List*)element, content_item.item);
                        }
                    }
                } else {
                    strbuf_full_reset(content_sb);
                }
            } else {
                // For non-math/non-raw-text environments, parse content normally
                while (**latex) {
                    // Check for \end{environment_name}
                    if (strncmp(*latex, "\\end{", 5) == 0) {
                        const char* end_check = *latex + 5;
                        if (strncmp(end_check, env_name->chars, env_name->len) == 0 &&
                            end_check[env_name->len] == '}') {
                            // Found matching \end, skip it
                            *latex = end_check + env_name->len + 1;
                            break;
                        }
                    }
                    
                    // Parse content within the environment
                    if (**latex == '\\') {
                        Item child = parse_latex_command(input, latex);
                        if (child != ITEM_ERROR && child != ITEM_NULL) {
                            list_push((List*)element, child);
                        }
                    } else if (**latex == '%') {
                        skip_comment(latex);
                    } else {
                        // Parse text content with proper escape handling
                        StrBuf* text_sb = input->sb;
                        strbuf_full_reset(text_sb);
                        
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
                                        case '{': strbuf_append_char(text_sb, '{'); break;
                                        case '}': strbuf_append_char(text_sb, '}'); break;
                                        case '$': strbuf_append_char(text_sb, '$'); break;
                                        case '&': strbuf_append_char(text_sb, '&'); break;
                                        case '#': strbuf_append_char(text_sb, '#'); break;
                                        case '^': strbuf_append_char(text_sb, '^'); break;
                                        case '_': strbuf_append_char(text_sb, '_'); break;
                                        case '%': strbuf_append_char(text_sb, '%'); break;
                                        case '~': strbuf_append_char(text_sb, '~'); break;
                                        default: 
                                            strbuf_append_char(text_sb, '\\');
                                            strbuf_append_char(text_sb, **latex);
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
                                strbuf_append_char(text_sb, **latex);
                                (*latex)++;
                            }
                            text_chars++;
                        }
                        
                        if (text_sb->length > sizeof(uint32_t)) {
                            String *text_string = (String*)text_sb->str;
                            text_string->len = text_sb->length - sizeof(uint32_t);
                            text_string->ref_cnt = 0;
                            strbuf_full_reset(text_sb);
                            
                            // Only add non-whitespace text
                            bool has_non_whitespace = false;
                            for (int i = 0; i < text_string->len; i++) {
                                if (!isspace(text_string->chars[i])) {
                                    has_non_whitespace = true;
                                    break;
                                }
                            }
                            
                            if (has_non_whitespace) {
                                LambdaItem text_item = (LambdaItem)s2it(text_string);
                                list_push((List*)element, text_item.item);
                            }
                        } else {
                            strbuf_full_reset(text_sb);
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
    
    return (Item)element;
}

static Item parse_latex_element(Input *input, const char **latex) {
    static int parse_depth = 0;
    parse_depth++;
    
    if (parse_depth > 20) {  // Reasonable depth limit for LaTeX
        parse_depth--;
        return ITEM_ERROR;
    }
    
    skip_whitespace(latex);
    
    if (!**latex) {
        parse_depth--;
        return ITEM_NULL;
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
        return ITEM_NULL;
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
        StrBuf* math_sb = input->sb;
        strbuf_full_reset(math_sb);
        
        while (**latex) {
            if (**latex == '$') {
                if (display_math) {
                    if (*(*latex + 1) == '$') {
                        (*latex) += 2; // Skip $$
                        break;
                    } else {
                        strbuf_append_char(math_sb, **latex);
                        (*latex)++;
                    }
                } else {
                    (*latex)++; // Skip $
                    break;
                }
            } else {
                strbuf_append_char(math_sb, **latex);
                (*latex)++;
            }
        }
        
        // Create a temporary Input for math parsing
        if (math_sb->length > sizeof(uint32_t)) {
            String *math_string = (String*)math_sb->str;
            math_string->len = math_sb->length - sizeof(uint32_t);
            math_string->ref_cnt = 0;
            
            if (math_string->len > 0) {
                // Create temporary input for math parsing
                Input* math_input = input_new(input->url);
                if (math_input) {
                    // Parse the math content using our math parser
                    parse_math(math_input, math_string->chars, "latex");
                    
                    // Create wrapper element for the math
                    const char* math_name = display_math ? "displaymath" : "math";
                    Element* element = create_latex_element(input, math_name);
                    if (element && math_input->root != ITEM_NULL) {
                        // Add the parsed math as child
                        list_push((List*)element, math_input->root);
                        ((TypeElmt*)element->type)->content_length = ((List*)element)->length;
                        
                        // Clean up temporary input (but preserve the parsed result)
                        // Note: We don't free math_input->root as it's now owned by element
                        if (math_input->type_list) {
                            arraylist_free(math_input->type_list);
                        }
                        pool_variable_destroy(math_input->pool);
                        free(math_input);
                        
                        strbuf_full_reset(math_sb);
                        parse_depth--;
                        return (Item)element;
                    }
                    
                    // Cleanup on failure
                    if (math_input->type_list) {
                        arraylist_free(math_input->type_list);
                    }
                    pool_variable_destroy(math_input->pool);
                    free(math_input);
                }
            }
        }
        
        strbuf_full_reset(math_sb);
        parse_depth--;
        return ITEM_ERROR;
    }
    
    // Parse regular text content with escape handling
    StrBuf* text_sb = input->sb;
    strbuf_full_reset(text_sb);
    
    int text_chars = 0;
    const int max_text_chars = 5000;
    
    while (**latex && text_chars < max_text_chars) {
        if (**latex == '\\') {
            // Check if this is an escaped character or command
            const char* next_char = *latex + 1;
            if (*next_char && strchr("{}$&#^_%~", *next_char)) {
                // This is an escaped character
                (*latex)++; // Skip backslash
                switch (**latex) {
                    case '{': strbuf_append_char(text_sb, '{'); break;
                    case '}': strbuf_append_char(text_sb, '}'); break;
                    case '$': strbuf_append_char(text_sb, '$'); break;
                    case '&': strbuf_append_char(text_sb, '&'); break;
                    case '#': strbuf_append_char(text_sb, '#'); break;
                    case '^': strbuf_append_char(text_sb, '^'); break;
                    case '_': strbuf_append_char(text_sb, '_'); break;
                    case '%': strbuf_append_char(text_sb, '%'); break;
                    case '~': strbuf_append_char(text_sb, '~'); break;
                    default: 
                        strbuf_append_char(text_sb, '\\');
                        strbuf_append_char(text_sb, **latex);
                        break;
                }
                (*latex)++;
            } else {
                // This is a LaTeX command, break
                break;
            }
        } else if (**latex == '$' || **latex == '%') {
            // Math mode or comment, break
            break;
        } else {
            strbuf_append_char(text_sb, **latex);
            (*latex)++;
        }
        text_chars++;
    }
    
    if (text_sb->length > sizeof(uint32_t)) {
        String *text_string = (String*)text_sb->str;
        text_string->len = text_sb->length - sizeof(uint32_t);
        text_string->ref_cnt = 0;
        strbuf_full_reset(text_sb);
        
        // Only return non-whitespace text
        bool has_non_whitespace = false;
        for (int i = 0; i < text_string->len; i++) {
            if (!isspace(text_string->chars[i])) {
                has_non_whitespace = true;
                break;
            }
        }
        
        if (has_non_whitespace) {
            parse_depth--;
            return (Item)s2it(text_string);
        }
    } else {
        strbuf_full_reset(text_sb);
    }
    
    parse_depth--;
    return ITEM_NULL;
}

void parse_latex(Input* input, const char* latex_string) {
    printf("DEBUG: Starting LaTeX parsing...\n");
    input->sb = strbuf_new_pooled(input->pool);
    const char *latex = latex_string;
    
    // Create root document element
    Element* root_element = create_latex_element(input, "latex_document");
    if (!root_element) {
        printf("DEBUG: Failed to create root element\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    // Parse LaTeX content
    skip_whitespace(&latex);
    
    int element_count = 0;
    while (*latex && element_count < 1000) { // Safety limit
        printf("DEBUG: Parsing element %d, current position: '%.50s...'\n", element_count, latex);
        
        Item element = parse_latex_element(input, &latex);
        if (element != ITEM_NULL && element != ITEM_ERROR) {
            list_push((List*)root_element, element);
            printf("DEBUG: Added element %d to root\n", element_count);
        } else if (element == ITEM_ERROR) {
            printf("DEBUG: Error parsing element %d\n", element_count);
            break;
        } else {
            printf("DEBUG: Element %d was null (likely \\end{} or comment)\n", element_count);
        }
        skip_whitespace(&latex);
        element_count++;
    }
    
    printf("DEBUG: Parsed %d elements total\n", element_count);
    ((TypeElmt*)root_element->type)->content_length = ((List*)root_element)->length;
    
    input->root = (Item)root_element;
    printf("DEBUG: LaTeX parsing completed\n");
}

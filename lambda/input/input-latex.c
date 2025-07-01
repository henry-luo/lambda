#include "../transpiler.h"

static Item parse_latex_element(Input *input, const char **latex);

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
    
    // Create element for the command
    Element* element = elmt_pooled(input->pool);
    if (!element) {
        return ITEM_ERROR;
    }
    
    TypeElmt* elem_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!elem_type) {
        return ITEM_ERROR;
    }
    element->type = elem_type;
    
    // Set command name
    elem_type->name.str = cmd_name->chars;
    elem_type->name.length = cmd_name->len;
    
    // Parse arguments
    Array* args = parse_command_arguments(input, latex);
    if (args && args->length > 0) {
        for (size_t i = 0; i < args->length; i++) {
            list_push((List*)element, args->items[i]);
        }
    }
    
    // Handle special commands that have content blocks
    if (strcmp(cmd_name->chars, "begin") == 0) {
        // This is an environment begin - we need the environment name
        if (args && args->length > 0) {
            // Parse content until \end{environment_name}
            skip_whitespace(latex);
            
            while (**latex) {
                // Check for \end{environment_name}
                if (strncmp(*latex, "\\end{", 5) == 0) {
                    const char* end_check = *latex + 5;
                    // Extract environment name from args[0]
                    String* env_name = (String*)((LambdaItem*)&args->items[0])->item;
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
                    // Parse text content
                    StrBuf* text_sb = input->sb;
                    strbuf_full_reset(text_sb);
                    
                    while (**latex && **latex != '\\' && **latex != '%') {
                        strbuf_append_char(text_sb, **latex);
                        (*latex)++;
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
    
    elem_type->content_length = ((List*)element)->length;
    
    arraylist_append(input->type_list, elem_type);
    elem_type->type_index = input->type_list->length - 1;
    
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
        // Create math element
        Element* element = elmt_pooled(input->pool);
        if (!element) {
            parse_depth--;
            return ITEM_ERROR;
        }
        
        TypeElmt* elem_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
        if (!elem_type) {
            parse_depth--;
            return ITEM_ERROR;
        }
        element->type = elem_type;
        
        bool display_math = false;
        (*latex)++; // Skip first $
        
        if (**latex == '$') {
            display_math = true;
            (*latex)++; // Skip second $
        }
        
        // Set math mode name
        const char* math_name = display_math ? "displaymath" : "math";
        elem_type->name.str = math_name;
        elem_type->name.length = strlen(math_name);
        
        // Parse math content
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
        
        if (math_sb->length > sizeof(uint32_t)) {
            String *math_string = (String*)math_sb->str;
            math_string->len = math_sb->length - sizeof(uint32_t);
            math_string->ref_cnt = 0;
            strbuf_full_reset(math_sb);
            
            if (math_string->len > 0) {
                LambdaItem math_item = (LambdaItem)s2it(math_string);
                list_push((List*)element, math_item.item);
            }
        } else {
            strbuf_full_reset(math_sb);
        }
        
        elem_type->content_length = ((List*)element)->length;
        
        arraylist_append(input->type_list, elem_type);
        elem_type->type_index = input->type_list->length - 1;
        
        parse_depth--;
        return (Item)element;
    }
    
    // Parse regular text content
    StrBuf* text_sb = input->sb;
    strbuf_full_reset(text_sb);
    
    while (**latex && **latex != '\\' && **latex != '$' && **latex != '%') {
        strbuf_append_char(text_sb, **latex);
        (*latex)++;
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
    input->sb = strbuf_new_pooled(input->pool);
    const char *latex = latex_string;
    
    // Create root document element
    Element* root_element = elmt_pooled(input->pool);
    if (!root_element) {
        input->root = ITEM_ERROR;
        return;
    }
    
    TypeElmt* root_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!root_type) {
        input->root = ITEM_ERROR;
        return;
    }
    root_element->type = root_type;
    
    // Set root element name
    const char* root_name = "latex_document";
    root_type->name.str = root_name;
    root_type->name.length = strlen(root_name);
    
    // Parse LaTeX content
    skip_whitespace(&latex);
    
    while (*latex) {
        Item element = parse_latex_element(input, &latex);
        if (element != ITEM_NULL && element != ITEM_ERROR) {
            list_push((List*)root_element, element);
        }
        skip_whitespace(&latex);
    }
    
    root_type->content_length = ((List*)root_element)->length;
    
    arraylist_append(input->type_list, root_type);
    root_type->type_index = input->type_list->length - 1;
    
    input->root = (Item)root_element;
}

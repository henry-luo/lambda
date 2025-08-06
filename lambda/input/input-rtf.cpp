#include "input.h"

static Item parse_rtf_content(Input *input, const char **rtf);

// RTF color table entry
typedef struct {
    int red;
    int green;
    int blue;
} RTFColor;

// RTF font table entry
typedef struct {
    int font_number;
    String* font_name;
    String* font_family;
} RTFFont;

// RTF control word structure
typedef struct {
    String* keyword;
    int parameter;
    bool has_parameter;
} RTFControlWord;

static void skip_whitespace(const char **rtf) {
    while (**rtf && (**rtf == ' ' || **rtf == '\n' || **rtf == '\r' || **rtf == '\t')) {
        (*rtf)++;
    }
}

static void skip_to_brace(const char **rtf, char target_brace) {
    int brace_count = 0;
    while (**rtf) {
        if (**rtf == '{') {
            brace_count++;
        } else if (**rtf == '}') {
            brace_count--;
            if (brace_count == 0 && target_brace == '}') {
                break;
            }
        }
        (*rtf)++;
    }
}

static String* parse_rtf_string(Input *input, const char **rtf, char delimiter) {
    StrBuf* sb = input->sb;
    
    while (**rtf && **rtf != delimiter && **rtf != '{' && **rtf != '}') {
        if (**rtf == '\\') {
            (*rtf)++; // Skip backslash
            if (**rtf == '\\') {
                strbuf_append_char(sb, '\\');
            } else if (**rtf == '{') {
                strbuf_append_char(sb, '{');
            } else if (**rtf == '}') {
                strbuf_append_char(sb, '}');
            } else if (**rtf == 'n') {
                strbuf_append_char(sb, '\n');
            } else if (**rtf == 't') {
                strbuf_append_char(sb, '\t');
            } else if (**rtf == 'r') {
                strbuf_append_char(sb, '\r');
            } else if (strncmp(*rtf, "par", 3) == 0) {
                // Handle paragraph break
                strbuf_append_char(sb, '\n');
                *rtf += 2; // Skip 'ar' (we already skipped 'p')
            } else if (strncmp(*rtf, "line", 4) == 0) {
                // Handle line break
                strbuf_append_char(sb, '\n');
                *rtf += 3; // Skip 'ine'
            } else if (strncmp(*rtf, "tab", 3) == 0) {
                // Handle tab
                strbuf_append_char(sb, '\t');
                *rtf += 2; // Skip 'ab'
            } else if (**rtf == 'u') {
                // Unicode escape sequence \uNNNN
                (*rtf)++; // Skip 'u'
                int unicode_value = 0;
                int digits = 0;
                while (**rtf && **rtf >= '0' && **rtf <= '9' && digits < 5) {
                    unicode_value = unicode_value * 10 + (**rtf - '0');
                    (*rtf)++;
                    digits++;
                }
                // Convert Unicode to UTF-8
                if (unicode_value < 0x80) {
                    strbuf_append_char(sb, (char)unicode_value);
                } else if (unicode_value < 0x800) {
                    strbuf_append_char(sb, (char)(0xC0 | (unicode_value >> 6)));
                    strbuf_append_char(sb, (char)(0x80 | (unicode_value & 0x3F)));
                } else {
                    strbuf_append_char(sb, (char)(0xE0 | (unicode_value >> 12)));
                    strbuf_append_char(sb, (char)(0x80 | ((unicode_value >> 6) & 0x3F)));
                    strbuf_append_char(sb, (char)(0x80 | (unicode_value & 0x3F)));
                }
                (*rtf)--; // Compensate for the increment at the end
            } else if (**rtf == '\'') {
                // Hex escape sequence \'HH
                (*rtf)++; // Skip quote
                char hex[3] = {0};
                if (**rtf && *(*rtf + 1)) {
                    hex[0] = **rtf;
                    hex[1] = *(*rtf + 1);
                    int char_code = (int)strtol(hex, NULL, 16);
                    strbuf_append_char(sb, (char)char_code);
                    (*rtf)++; // Skip second hex digit
                }
            } else if (isalpha(**rtf)) {
                // Handle other control words by skipping them
                while (**rtf && (isalnum(**rtf) || **rtf == '-')) {
                    (*rtf)++;
                }
                // Skip optional space after control word
                if (**rtf == ' ') {
                    (*rtf)++;
                }
                (*rtf)--; // Compensate for the increment at the end
            } else {
                // Unknown escape, just add the character
                strbuf_append_char(sb, **rtf);
            }
        } else {
            strbuf_append_char(sb, **rtf);
        }
        (*rtf)++;
    }
    
    return strbuf_to_string(sb);
}

static RTFControlWord parse_control_word(Input *input, const char **rtf) {
    RTFControlWord control_word = {0};
    
    if (**rtf != '\\') {
        return control_word;
    }
    
    (*rtf)++; // Skip backslash
    
    StrBuf* sb = input->sb;
    
    // Parse keyword (letters only)
    while (**rtf && isalpha(**rtf)) {
        strbuf_append_char(sb, **rtf);
        (*rtf)++;
    }
    
    control_word.keyword = strbuf_to_string(sb);
    
    // Parse optional parameter (digits with optional minus sign)
    if (**rtf == '-' || (**rtf >= '0' && **rtf <= '9')) {
        control_word.has_parameter = true;
        int sign = 1;
        
        if (**rtf == '-') {
            sign = -1;
            (*rtf)++;
        }
        
        int value = 0;
        while (**rtf >= '0' && **rtf <= '9') {
            value = value * 10 + (**rtf - '0');
            (*rtf)++;
        }
        
        control_word.parameter = sign * value;
    }
    
    // Skip optional space delimiter
    if (**rtf == ' ') {
        (*rtf)++;
    }
    
    return control_word;
}

static Array* parse_color_table(Input *input, const char **rtf) {
    Array* colors = array_pooled(input->pool);
    if (!colors) return NULL;
    
    // Skip the \colortbl keyword
    while (**rtf && **rtf != ';' && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, rtf);
            if (cw.keyword) {
                RTFColor* color;
                MemPoolError err = pool_variable_alloc(input->pool, sizeof(RTFColor), (void**)&color);
                if (err != MEM_POOL_ERR_OK) break;
                
                color->red = 0;
                color->green = 0;
                color->blue = 0;
                
                if (strcmp(cw.keyword->chars, "red") == 0 && cw.has_parameter) {
                    color->red = cw.parameter;
                } else if (strcmp(cw.keyword->chars, "green") == 0 && cw.has_parameter) {
                    color->green = cw.parameter;
                } else if (strcmp(cw.keyword->chars, "blue") == 0 && cw.has_parameter) {
                    color->blue = cw.parameter;
                }
                
                LambdaItem color_item = {.item = (Item)color};
                array_append(colors, color_item, input->pool);
            }
        } else if (**rtf == ';') {
            (*rtf)++; // Next color
        } else {
            (*rtf)++;
        }
    }
    
    return colors;
}

static Array* parse_font_table(Input *input, const char **rtf) {
    Array* fonts = array_pooled(input->pool);
    if (!fonts) return NULL;
    
    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, rtf);
            if (cw.keyword && strcmp(cw.keyword->chars, "f") == 0 && cw.has_parameter) {
                // Start of font definition
                RTFFont* font;
                MemPoolError err = pool_variable_alloc(input->pool, sizeof(RTFFont), (void**)&font);
                if (err != MEM_POOL_ERR_OK) break;
                
                font->font_number = cw.parameter;
                
                // Parse font family
                skip_whitespace(rtf);
                if (**rtf == '\\') {
                    RTFControlWord family_cw = parse_control_word(input, rtf);
                    font->font_family = family_cw.keyword;
                }
                
                // Parse font name (until semicolon)
                skip_whitespace(rtf);
                font->font_name = parse_rtf_string(input, rtf, ';');
                
                if (**rtf == ';') {
                    (*rtf)++; // Skip semicolon
                }
                
                LambdaItem font_item = {.item = (Item)font};
                array_append(fonts, font_item, input->pool);
            }
        } else {
            (*rtf)++;
        }
    }
    
    return fonts;
}

static Map* parse_document_properties(Input *input, const char **rtf) {
    Map* props = map_pooled(input->pool);
    if (!props) return NULL;
    
    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, rtf);
            if (cw.keyword) {
                LambdaItem value;
                
                if (cw.has_parameter) {
                    double *dval;
                    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
                    if (err == MEM_POOL_ERR_OK) {
                        *dval = (double)cw.parameter;
                        value = (LambdaItem)d2it(dval);
                    } else {
                        value = (LambdaItem)ITEM_NULL;
                    }
                } else {
                    value = (LambdaItem)b2it(true);
                }
                map_put(props, cw.keyword, value, input);
            }
        } else {
            (*rtf)++;
        }
    }
    return props;
}

static Item parse_rtf_group(Input *input, const char **rtf) {
    if (**rtf != '{') {
        return ITEM_ERROR;
    }
    
    (*rtf)++; // Skip opening brace
    skip_whitespace(rtf);
    
    // Create a group object (map)
    Map* group = map_pooled(input->pool);
    if (!group) return ITEM_ERROR;
    
    Array* content = array_pooled(input->pool);
    if (!content) return (Item)group;
    
    Map* formatting = map_pooled(input->pool);
    if (!formatting) return (Item)group;
    
    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, rtf);
            if (cw.keyword) {
                // Handle various RTF control words
                if (strcmp(cw.keyword->chars, "colortbl") == 0) {
                    Array* colors = parse_color_table(input, rtf);
                    if (colors) {
                        LambdaItem color_table = {.item = (Item)colors};
                        
                        String* key;
                        MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 12, (void**)&key);
                        if (err == MEM_POOL_ERR_OK) {
                            strcpy(key->chars, "color_table");
                            key->len = 11;
                            key->ref_cnt = 0;
                            map_put(group, key, color_table, input);
                        }
                    }
                } else if (strcmp(cw.keyword->chars, "fonttbl") == 0) {
                    Array* fonts = parse_font_table(input, rtf);
                    if (fonts) {
                        LambdaItem font_table = {.item = (Item)fonts};
                        
                        String* key;
                        MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 11, (void**)&key);
                        if (err == MEM_POOL_ERR_OK) {
                            strcpy(key->chars, "font_table");
                            key->len = 10;
                            key->ref_cnt = 0;
                            map_put(group, key, font_table, input);
                        }
                    }
                } else {
                    // Store formatting information
                    LambdaItem value;
                    if (cw.has_parameter) {
                        double *dval;
                        MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
                        if (err == MEM_POOL_ERR_OK) {
                            *dval = (double)cw.parameter;
                            value = (LambdaItem)d2it(dval);
                        } else {
                            value = (LambdaItem)ITEM_NULL;
                        }
                    } else {
                        value = (LambdaItem)b2it(true);
                    }
                    map_put(formatting, cw.keyword, value, input);
                }
            }
        } else if (**rtf == '{') {
            // Nested group
            Item nested = parse_rtf_group(input, rtf);
            if (nested != ITEM_ERROR && nested != ITEM_NULL) {
                LambdaItem nested_item = {.item = nested};
                array_append(content, nested_item, input->pool);
            }
        } else {
            // Text content
            String* text = parse_rtf_string(input, rtf, '{');
            if (text && text->len > 0) {
                LambdaItem text_item = {.item = s2it(text)};
                array_append(content, text_item, input->pool);
            }
        }
        
        skip_whitespace(rtf);
    }
    
    if (**rtf == '}') {
        (*rtf)++; // Skip closing brace
    }
    
    // Add content and formatting to group
    if (content->length > 0) {
        LambdaItem content_item = {.item = (Item)content};
        
        String* content_key;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&content_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(content_key->chars, "content");
            content_key->len = 7;
            content_key->ref_cnt = 0;
            map_put(group, content_key, content_item, input);
        }
    }
    
    if (((TypeMap*)formatting->type)->length > 0) {
        LambdaItem format_item = {.item = (Item)formatting};
        
        String* format_key;
        MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 11, (void**)&format_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(format_key->chars, "formatting");
            format_key->len = 10;
            format_key->ref_cnt = 0;
            map_put(group, format_key, format_item, input);
        }
    }
    return (Item)group;
}

static Item parse_rtf_content(Input *input, const char **rtf) {
    skip_whitespace(rtf);
    
    if (**rtf == '{') {
        return parse_rtf_group(input, rtf);
    }
    
    return ITEM_ERROR;
}

void parse_rtf(Input* input, const char* rtf_string) {
    printf("rtf_parse\n");
    input->sb = strbuf_new_pooled(input->pool);
    
    const char* rtf = rtf_string;
    skip_whitespace(&rtf);
    
    // RTF documents must start with {\rtf
    if (strncmp(rtf, "{\\rtf", 5) != 0) {
        printf("Error: Invalid RTF format - must start with {\\rtf\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    // Create document root to hold all groups
    Array* document = array_pooled(input->pool);
    if (!document) {
        input->root = ITEM_ERROR;
        return;
    }
    
    // Parse all groups in the document
    while (*rtf && *rtf != '\0') {
        skip_whitespace(&rtf);
        if (*rtf == '\0') break;
        
        if (*rtf == '{') {
            Item group = parse_rtf_group(input, &rtf);
            if (group != ITEM_ERROR && group != ITEM_NULL) {
                LambdaItem group_item = {.item = group};
                array_append(document, group_item, input->pool);
            }
        } else {
            // Skip unknown content
            rtf++;
        }
    }
    
    input->root = (Item)document;
}

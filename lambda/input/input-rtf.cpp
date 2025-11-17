#include "input.hpp"
#include "../mark_builder.hpp"
#include "input_context.hpp"
#include "source_tracker.hpp"

using namespace lambda;

static Item parse_rtf_content(Input *input, MarkBuilder* builder, const char **rtf);

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

static String* parse_rtf_string(Input *input, MarkBuilder* builder, const char **rtf, char delimiter) {
    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);

    while (**rtf && **rtf != delimiter && **rtf != '{' && **rtf != '}') {
        if (**rtf == '\\') {
            (*rtf)++; // Skip backslash
            if (**rtf == '\\') {
                stringbuf_append_char(sb, '\\');
            } else if (**rtf == '{') {
                stringbuf_append_char(sb, '{');
            } else if (**rtf == '}') {
                stringbuf_append_char(sb, '}');
            } else if (**rtf == 'n') {
                stringbuf_append_char(sb, '\n');
            } else if (**rtf == 't') {
                stringbuf_append_char(sb, '\t');
            } else if (**rtf == 'r') {
                stringbuf_append_char(sb, '\r');
            } else if (strncmp(*rtf, "par", 3) == 0) {
                // Handle paragraph break
                stringbuf_append_char(sb, '\n');
                *rtf += 2; // Skip 'ar' (we already skipped 'p')
            } else if (strncmp(*rtf, "line", 4) == 0) {
                // Handle line break
                stringbuf_append_char(sb, '\n');
                *rtf += 3; // Skip 'ine'
            } else if (strncmp(*rtf, "tab", 3) == 0) {
                // Handle tab
                stringbuf_append_char(sb, '\t');
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
                    stringbuf_append_char(sb, (char)unicode_value);
                } else if (unicode_value < 0x800) {
                    stringbuf_append_char(sb, (char)(0xC0 | (unicode_value >> 6)));
                    stringbuf_append_char(sb, (char)(0x80 | (unicode_value & 0x3F)));
                } else {
                    stringbuf_append_char(sb, (char)(0xE0 | (unicode_value >> 12)));
                    stringbuf_append_char(sb, (char)(0x80 | ((unicode_value >> 6) & 0x3F)));
                    stringbuf_append_char(sb, (char)(0x80 | (unicode_value & 0x3F)));
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
                    stringbuf_append_char(sb, (char)char_code);
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
                stringbuf_append_char(sb, **rtf);
            }
        } else {
            stringbuf_append_char(sb, **rtf);
        }
        (*rtf)++;
    }

    return builder->createString(sb->str->chars, sb->length);
}

static RTFControlWord parse_control_word(Input *input, MarkBuilder* builder, const char **rtf) {
    RTFControlWord control_word = {0};

    if (**rtf != '\\') {
        return control_word;
    }

    (*rtf)++; // Skip backslash

    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);

    // Parse keyword (letters only)
    while (**rtf && isalpha(**rtf)) {
        stringbuf_append_char(sb, **rtf);
        (*rtf)++;
    }

    control_word.keyword = builder->createString(sb->str->chars, sb->length);

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

static Array* parse_color_table(Input *input, MarkBuilder* builder, const char **rtf) {
    Array* colors = array_pooled(input->pool);
    if (!colors) return NULL;

    // Skip the \colortbl keyword
    while (**rtf && **rtf != ';' && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, builder, rtf);
            if (cw.keyword) {
                RTFColor* color;
                color = (RTFColor*)pool_calloc(input->pool, sizeof(RTFColor));
                if (color == NULL) break;

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

                Item color_item = {.item = (uint64_t)color};
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

static Array* parse_font_table(Input *input, MarkBuilder* builder, const char **rtf) {
    Array* fonts = array_pooled(input->pool);
    if (!fonts) return NULL;

    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, builder, rtf);
            if (cw.keyword && strcmp(cw.keyword->chars, "f") == 0 && cw.has_parameter) {
                // Start of font definition
                RTFFont* font;
                font = (RTFFont*)pool_calloc(input->pool, sizeof(RTFFont));
                if (font == NULL) break;

                font->font_number = cw.parameter;

                // Parse font family
                skip_whitespace(rtf);
                if (**rtf == '\\') {
                    RTFControlWord family_cw = parse_control_word(input, builder, rtf);
                    font->font_family = family_cw.keyword;
                }

                // Parse font name (until semicolon)
                skip_whitespace(rtf);
                font->font_name = parse_rtf_string(input, builder, rtf, ';');

                if (**rtf == ';') {
                    (*rtf)++; // Skip semicolon
                }

                Item font_item = {.item = (uint64_t)font};
                array_append(fonts, font_item, input->pool);
            }
        } else {
            (*rtf)++;
        }
    }

    return fonts;
}

static Map* parse_document_properties(Input *input, MarkBuilder* builder, const char **rtf) {
    Map* props = map_pooled(input->pool);
    if (!props) return NULL;

    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, builder, rtf);
            if (cw.keyword) {
                Item value;

                if (cw.has_parameter) {
                    double *dval;
                    dval = (double*)pool_calloc(input->pool, sizeof(double));
                    if (dval != NULL) {
                        *dval = (double)cw.parameter;
                        value = {.item = d2it(dval)};
                    } else {
                        value = {.item = ITEM_NULL};
                    }
                } else {
                    value = {.item = b2it(true)};
                }
                map_put(props, cw.keyword, value, input);
            }
        } else {
            (*rtf)++;
        }
    }
    return props;
}

static Item parse_rtf_group(Input *input, MarkBuilder* builder, const char **rtf) {
    if (**rtf != '{') {
        return {.item = ITEM_ERROR};
    }

    (*rtf)++; // Skip opening brace
    skip_whitespace(rtf);

    // Create a group object (map)
    Map* group = map_pooled(input->pool);
    if (!group) return {.item = ITEM_ERROR};

    Array* content = array_pooled(input->pool);
    if (!content) return {.item = (uint64_t)group};

    Map* formatting = map_pooled(input->pool);
    if (!formatting) return {.item = (uint64_t)group};

    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(input, builder, rtf);
            if (cw.keyword) {
                // Handle various RTF control words
                if (strcmp(cw.keyword->chars, "colortbl") == 0) {
                    Array* colors = parse_color_table(input, builder, rtf);
                    if (colors) {
                        Item color_table = {.item = (uint64_t)colors};

                        String* key;
                        key = (String*)pool_calloc(input->pool, sizeof(String) + 12);
                        if (key != NULL) {
                            strcpy(key->chars, "color_table");
                            key->len = 11;
                            key->ref_cnt = 0;
                            map_put(group, key, color_table, input);
                        }
                    }
                } else if (strcmp(cw.keyword->chars, "fonttbl") == 0) {
                    Array* fonts = parse_font_table(input, builder, rtf);
                    if (fonts) {
                        Item font_table = {.item = (uint64_t)fonts};

                        String* key;
                        key = (String*)pool_calloc(input->pool, sizeof(String) + 11);
                        if (key != NULL) {
                            strcpy(key->chars, "font_table");
                            key->len = 10;
                            key->ref_cnt = 0;
                            map_put(group, key, font_table, input);
                        }
                    }
                } else {
                    // Store formatting information
                    Item value;
                    if (cw.has_parameter) {
                        double *dval;
                        dval = (double*)pool_calloc(input->pool, sizeof(double));
                        if (dval != NULL) {
                            *dval = (double)cw.parameter;
                            value = {.item = d2it(dval)};
                        } else {
                            value = {.item = ITEM_NULL};
                        }
                    } else {
                        value = {.item = b2it(true)};
                    }
                    map_put(formatting, cw.keyword, value, input);
                }
            }
        } else if (**rtf == '{') {
            // Nested group
            Item nested = parse_rtf_group(input, builder, rtf);
            if (nested .item != ITEM_ERROR && nested .item != ITEM_NULL) {
                array_append(content, nested, input->pool);
            }
        } else {
            // Text content
            String* text = parse_rtf_string(input, builder, rtf, '{');
            if (text && text->len > 0) {
                Item text_item = {.item = s2it(text)};
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
        Item content_item = {.item = (uint64_t)content};

        String* content_key;
        content_key = (String*)pool_calloc(input->pool, sizeof(String) + 8);
        if (content_key != NULL) {
            strcpy(content_key->chars, "content");
            content_key->len = 7;
            content_key->ref_cnt = 0;
            map_put(group, content_key, content_item, input);
        }
    }

    if (((TypeMap*)formatting->type)->length > 0) {
        Item format_item = {.item = (uint64_t)formatting};

        String* format_key;
        format_key = (String*)pool_calloc(input->pool, sizeof(String) + 11);
        if (format_key != NULL) {
            strcpy(format_key->chars, "formatting");
            format_key->len = 10;
            format_key->ref_cnt = 0;
            map_put(group, format_key, format_item, input);
        }
    }
    return {.item = (uint64_t)group};
}

static Item parse_rtf_content(Input *input, MarkBuilder* builder, const char **rtf) {
    skip_whitespace(rtf);

    if (**rtf == '{') {
        return parse_rtf_group(input, builder, rtf);
    }

    return {.item = ITEM_ERROR};
}

void parse_rtf(Input* input, const char* rtf_string) {
    printf("rtf_parse\n");
    InputContext ctx(input);
    SourceTracker tracker(rtf_string, strlen(rtf_string));
    MarkBuilder* builder = &ctx.builder();

    const char* rtf = rtf_string;
    skip_whitespace(&rtf);

    // RTF documents must start with {\rtf
    if (strncmp(rtf, "{\\rtf", 5) != 0) {
        ctx.addError(tracker.location(), "Invalid RTF format: document must start with '{\\rtf'");
        printf("Error: Invalid RTF format - must start with {\\rtf\n");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Create document root to hold all groups
    Array* document = array_pooled(input->pool);
    if (!document) {
        ctx.addError(tracker.location(), "Memory allocation failed for RTF document array");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Parse all groups in the document
    while (*rtf && *rtf != '\0') {
        skip_whitespace(&rtf);
        if (*rtf == '\0') break;

        if (*rtf == '{') {
            Item group = parse_rtf_group(input, builder, &rtf);
            if (group.item != ITEM_ERROR && group.item != ITEM_NULL) {
                array_append(document, group, input->pool);
            } else if (group.item == ITEM_ERROR) {
                ctx.addWarning(tracker.location(), "Failed to parse RTF group, skipping");
            }
        } else {
            // Skip unknown content
            ctx.addWarning(tracker.location(), "Unexpected character '%c' (0x%02X) outside group, skipping", *rtf, (unsigned char)*rtf);
            rtf++;
        }
    }

    // Report completion status
    if (ctx.hasErrors()) {
        ctx.addError(SourceLocation{0, 1, 1}, "RTF parsing completed with errors");
    }

    input->root = {.item = (uint64_t)document};
}

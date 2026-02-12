#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "lib/log.h"

using namespace lambda;

static const int RTF_MAX_DEPTH = 512;

static Item parse_rtf_content(InputContext& ctx, const char **rtf, int depth = 0);

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

static String* parse_rtf_string(InputContext& ctx, const char **rtf, char delimiter) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
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

    return builder.createString(sb->str->chars, sb->length);
}

static RTFControlWord parse_control_word(InputContext& ctx, const char **rtf) {
    MarkBuilder& builder = ctx.builder;
    RTFControlWord control_word = {0};

    if (**rtf != '\\') {
        return control_word;
    }

    (*rtf)++; // Skip backslash

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    // Parse keyword (letters only)
    while (**rtf && isalpha(**rtf)) {
        stringbuf_append_char(sb, **rtf);
        (*rtf)++;
    }

    control_word.keyword = builder.createString(sb->str->chars, sb->length);

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

static Array* parse_color_table(InputContext& ctx, const char **rtf) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;
    Array* colors = array_pooled(input->pool);
    if (!colors) return NULL;

    // Skip the \colortbl keyword
    while (**rtf && **rtf != ';' && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(ctx, rtf);
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

static Array* parse_font_table(InputContext& ctx, const char **rtf) {
    Input* input = ctx.input();
    Array* fonts = array_pooled(input->pool);
    if (!fonts) return NULL;

    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(ctx, rtf);
            if (cw.keyword && strcmp(cw.keyword->chars, "f") == 0 && cw.has_parameter) {
                // Start of font definition
                RTFFont* font;
                font = (RTFFont*)pool_calloc(input->pool, sizeof(RTFFont));
                if (font == NULL) break;

                font->font_number = cw.parameter;

                // Parse font family
                skip_whitespace(rtf);
                if (**rtf == '\\') {
                    RTFControlWord family_cw = parse_control_word(ctx, rtf);
                    font->font_family = family_cw.keyword;
                }

                // Parse font name (until semicolon)
                skip_whitespace(rtf);
                font->font_name = parse_rtf_string(ctx, rtf, ';');

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

static Map* parse_document_properties(InputContext& ctx, const char **rtf) {
    Input* input = ctx.input();
    Map* props = map_pooled(input->pool);
    if (!props) return NULL;

    while (**rtf && **rtf != '}') {
        if (**rtf == '\\') {
            RTFControlWord cw = parse_control_word(ctx, rtf);
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
                ctx.builder.putToMap(props, cw.keyword, value);
            }
        } else {
            (*rtf)++;
        }
    }
    return props;
}

static Item parse_rtf_group(InputContext& ctx, const char **rtf, int depth = 0) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;
    if (**rtf != '{') {
        return {.item = ITEM_ERROR};
    }
    if (depth >= RTF_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum RTF nesting depth (%d) exceeded", RTF_MAX_DEPTH);
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
            RTFControlWord cw = parse_control_word(ctx, rtf);
            if (cw.keyword) {
                // Handle various RTF control words
                if (strcmp(cw.keyword->chars, "colortbl") == 0) {
                    Array* colors = parse_color_table(ctx, rtf);
                    if (colors) {
                        Item color_table = {.item = (uint64_t)colors};

                        String* key;
                        key = (String*)pool_calloc(input->pool, sizeof(String) + 12);
                        if (key != NULL) {
                            strcpy(key->chars, "color_table");
                            key->len = 11;
                            key->ref_cnt = 0;
                            ctx.builder.putToMap(group, key, color_table);
                        }
                    }
                } else if (strcmp(cw.keyword->chars, "fonttbl") == 0) {
                    Array* fonts = parse_font_table(ctx, rtf);
                    if (fonts) {
                        Item font_table = {.item = (uint64_t)fonts};

                        String* key;
                        key = (String*)pool_calloc(input->pool, sizeof(String) + 11);
                        if (key != NULL) {
                            strcpy(key->chars, "font_table");
                            key->len = 10;
                            key->ref_cnt = 0;
                            ctx.builder.putToMap(group, key, font_table);
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
                    ctx.builder.putToMap(formatting, cw.keyword, value);
                }
            }
        } else if (**rtf == '{') {
            // Nested group
            Item nested = parse_rtf_group(ctx, rtf, depth + 1);
            if (nested .item != ITEM_ERROR && nested .item != ITEM_NULL) {
                array_append(content, nested, input->pool);
            }
        } else {
            // Text content
            String* text = parse_rtf_string(ctx, rtf, '{');
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
            ctx.builder.putToMap(group, content_key, content_item);
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
            ctx.builder.putToMap(group, format_key, format_item);
        }
    }
    return {.item = (uint64_t)group};
}

static Item parse_rtf_content(InputContext& ctx, const char **rtf, int depth) {
    skip_whitespace(rtf);

    if (**rtf == '{') {
        return parse_rtf_group(ctx, rtf, depth);
    }

    return {.item = ITEM_ERROR};
}

void parse_rtf(Input* input, const char* rtf_string) {
    if (!rtf_string || !*rtf_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    log_debug("rtf_parse\n");
    InputContext ctx(input, rtf_string, strlen(rtf_string));
    MarkBuilder& builder = ctx.builder;

    const char* rtf = rtf_string;
    skip_whitespace(&rtf);

    // RTF documents must start with {\rtf
    if (strncmp(rtf, "{\\rtf", 5) != 0) {
        ctx.addError(ctx.tracker.location(), "Invalid RTF format: document must start with '{\\rtf'");
        log_debug("Error: Invalid RTF format - must start with {\\rtf\n");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Create document root to hold all groups
    Array* document = array_pooled(input->pool);
    if (!document) {
        ctx.addError(ctx.tracker.location(), "Memory allocation failed for RTF document array");
        input->root = {.item = ITEM_ERROR};
        return;
    }

    // Parse all groups in the document
    while (*rtf && *rtf != '\0') {
        skip_whitespace(&rtf);
        if (*rtf == '\0') break;

        if (*rtf == '{') {
            Item group = parse_rtf_group(ctx, &rtf, 0);
            if (group.item != ITEM_ERROR && group.item != ITEM_NULL) {
                array_append(document, group, input->pool);
            } else if (group.item == ITEM_ERROR) {
                ctx.addWarning(ctx.tracker.location(), "Failed to parse RTF group, skipping");
            }
        } else {
            // Skip unknown content
            ctx.addWarning(ctx.tracker.location(), "Unexpected character '%c' (0x%02X) outside group, skipping", *rtf, (unsigned char)*rtf);
            rtf++;
        }
    }

    // Report completion status
    if (ctx.hasErrors()) {
        ctx.addError(SourceLocation{0, 1, 1}, "RTF parsing completed with errors");
    }

    input->root = {.item = (uint64_t)document};
}

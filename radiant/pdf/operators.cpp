// radiant/pdf/operators.cpp
// PDF operator parsing and graphics state management

#include "operators.h"
#include "../../lib/log.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// Helper: skip whitespace and comments
static void skip_whitespace(PDFStreamParser* parser) {
    while (parser->stream < parser->stream_end) {
        char c = *parser->stream;

        // Whitespace characters
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0') {
            parser->stream++;
            continue;
        }

        // Comment (% to end of line)
        if (c == '%') {
            while (parser->stream < parser->stream_end && *parser->stream != '\r' && *parser->stream != '\n') {
                parser->stream++;
            }
            continue;
        }

        break;
    }
}

// Helper: parse a number (integer or real)
static double parse_number(PDFStreamParser* parser) {
    skip_whitespace(parser);

    const char* start = parser->stream;

    // Handle sign
    if (*parser->stream == '+' || *parser->stream == '-') {
        parser->stream++;
    }

    // Parse digits before decimal point
    while (parser->stream < parser->stream_end && isdigit(*parser->stream)) {
        parser->stream++;
    }

    // Parse decimal point and fractional part
    if (parser->stream < parser->stream_end && *parser->stream == '.') {
        parser->stream++;
        while (parser->stream < parser->stream_end && isdigit(*parser->stream)) {
            parser->stream++;
        }
    }

    // Convert to double
    char* end;
    double value = strtod(start, &end);

    return value;
}

// Helper: parse a string (literal or hex)
static String* parse_string(PDFStreamParser* parser) {
    skip_whitespace(parser);

    if (parser->stream >= parser->stream_end) {
        return nullptr;
    }

    // Literal string: (text)
    if (*parser->stream == '(') {
        parser->stream++; // skip '('

        StrBuf* sb = strbuf_new();
        int paren_depth = 1;

        while (parser->stream < parser->stream_end && paren_depth > 0) {
            char c = *parser->stream++;

            if (c == '(') {
                paren_depth++;
                strbuf_append_char(sb, c);
            } else if (c == ')') {
                paren_depth--;
                if (paren_depth > 0) {
                    strbuf_append_char(sb, c);
                }
            } else if (c == '\\') {
                // Escape sequence
                if (parser->stream < parser->stream_end) {
                    char next = *parser->stream++;
                    switch (next) {
                        case 'n': strbuf_append_char(sb, '\n'); break;
                        case 'r': strbuf_append_char(sb, '\r'); break;
                        case 't': strbuf_append_char(sb, '\t'); break;
                        case 'b': strbuf_append_char(sb, '\b'); break;
                        case 'f': strbuf_append_char(sb, '\f'); break;
                        case '(': strbuf_append_char(sb, '('); break;
                        case ')': strbuf_append_char(sb, ')'); break;
                        case '\\': strbuf_append_char(sb, '\\'); break;
                        default:
                            // Octal escape: \ddd
                            if (next >= '0' && next <= '7') {
                                int octal = next - '0';
                                if (parser->stream < parser->stream_end && *parser->stream >= '0' && *parser->stream <= '7') {
                                    octal = octal * 8 + (*parser->stream++ - '0');
                                    if (parser->stream < parser->stream_end && *parser->stream >= '0' && *parser->stream <= '7') {
                                        octal = octal * 8 + (*parser->stream++ - '0');
                                    }
                                }
                                strbuf_append_char(sb, (char)octal);
                            } else {
                                strbuf_append_char(sb, next);
                            }
                            break;
                    }
                }
            } else {
                strbuf_append_char(sb, c);
            }
        }

        // Convert StrBuf to String
        String* result = (String*)pool_alloc(parser->pool, sizeof(String) + sb->length + 1);
        result->len = sb->length;
        memcpy(result->chars, sb->str, sb->length);
        result->chars[sb->length] = '\0';
        strbuf_free(sb);

        return result;
    }

    // Hex string: <hex>
    if (*parser->stream == '<') {
        parser->stream++; // skip '<'

        StrBuf* sb = strbuf_new();

        while (parser->stream < parser->stream_end && *parser->stream != '>') {
            char c1 = *parser->stream++;
            if (isspace(c1)) continue;

            char c2 = (parser->stream < parser->stream_end) ? *parser->stream++ : '0';
            while (isspace(c2) && parser->stream < parser->stream_end) {
                c2 = *parser->stream++;
            }

            int digit1 = isdigit(c1) ? (c1 - '0') : (tolower(c1) - 'a' + 10);
            int digit2 = isdigit(c2) ? (c2 - '0') : (tolower(c2) - 'a' + 10);

            strbuf_append_char(sb, (char)((digit1 << 4) | digit2));
        }

        if (parser->stream < parser->stream_end) {
            parser->stream++; // skip '>'
        }

        // Convert StrBuf to String
        String* result = (String*)pool_alloc(parser->pool, sizeof(String) + sb->length + 1);
        result->len = sb->length;
        memcpy(result->chars, sb->str, sb->length);
        result->chars[sb->length] = '\0';
        strbuf_free(sb);

        return result;
    }

    return nullptr;
}

// Helper: parse a name (/Name)
static String* parse_name(PDFStreamParser* parser) {
    skip_whitespace(parser);

    if (parser->stream >= parser->stream_end || *parser->stream != '/') {
        return nullptr;
    }

    parser->stream++; // skip '/'

    const char* start = parser->stream;

    while (parser->stream < parser->stream_end) {
        char c = *parser->stream;
        if (isspace(c) || c == '/' || c == '[' || c == ']' || c == '(' || c == ')' || c == '<' || c == '>') {
            break;
        }
        parser->stream++;
    }

    int len = parser->stream - start;
    String* name = (String*)pool_alloc(parser->pool, sizeof(String) + len + 1);
    name->len = len;
    memcpy(name->chars, start, len);
    name->chars[len] = '\0';

    return name;
}

// Helper: parse operator name
static const char* parse_operator_name(PDFStreamParser* parser) {
    skip_whitespace(parser);

    const char* start = parser->stream;

    while (parser->stream < parser->stream_end) {
        char c = *parser->stream;
        if (isspace(c) || c == '/' || c == '[' || c == ']' || c == '(' || c == ')' || c == '<' || c == '>') {
            break;
        }
        parser->stream++;
    }

    int len = parser->stream - start;
    char* name = (char*)pool_alloc(parser->pool, len + 1);
    memcpy(name, start, len);
    name[len] = '\0';

    return name;
}

// Create parser
PDFStreamParser* pdf_stream_parser_create(const char* stream, int length, Pool* pool, Input* input) {
    PDFStreamParser* parser = (PDFStreamParser*)pool_calloc(pool, sizeof(PDFStreamParser));

    parser->stream = stream;
    parser->stream_end = stream + length;
    parser->pool = pool;
    parser->input = input;

    pdf_graphics_state_init(&parser->state, pool);

    return parser;
}

// Destroy parser
void pdf_stream_parser_destroy(PDFStreamParser* parser) {
    // Nothing to do - pool handles memory
}

// Initialize graphics state
void pdf_graphics_state_init(PDFGraphicsState* state, Pool* pool) {
    state->pool = pool;
    state->char_spacing = 0.0;
    state->word_spacing = 0.0;
    state->horizontal_scaling = 100.0;
    state->leading = 0.0;
    state->font_name = nullptr;
    state->font_size = 0.0;
    state->text_rendering_mode = 0;
    state->text_rise = 0.0;

    // Identity matrices
    state->tm[0] = 1.0; state->tm[1] = 0.0;
    state->tm[2] = 0.0; state->tm[3] = 1.0;
    state->tm[4] = 0.0; state->tm[5] = 0.0;

    state->tlm[0] = 1.0; state->tlm[1] = 0.0;
    state->tlm[2] = 0.0; state->tlm[3] = 1.0;
    state->tlm[4] = 0.0; state->tlm[5] = 0.0;

    state->ctm[0] = 1.0; state->ctm[1] = 0.0;
    state->ctm[2] = 0.0; state->ctm[3] = 1.0;
    state->ctm[4] = 0.0; state->ctm[5] = 0.0;

    // Default colors (black)
    state->stroke_color[0] = 0.0;
    state->stroke_color[1] = 0.0;
    state->stroke_color[2] = 0.0;

    state->fill_color[0] = 0.0;
    state->fill_color[1] = 0.0;
    state->fill_color[2] = 0.0;

    state->current_x = 0.0;
    state->current_y = 0.0;

    state->saved_states = nullptr;
}

// Save graphics state (q operator)
void pdf_graphics_state_save(PDFGraphicsState* state) {
    PDFSavedState* saved = (PDFSavedState*)pool_alloc(state->pool, sizeof(PDFSavedState));

    memcpy(saved->tm, state->tm, sizeof(state->tm));
    memcpy(saved->tlm, state->tlm, sizeof(state->tlm));
    memcpy(saved->ctm, state->ctm, sizeof(state->ctm));

    saved->char_spacing = state->char_spacing;
    saved->word_spacing = state->word_spacing;
    saved->horizontal_scaling = state->horizontal_scaling;
    saved->leading = state->leading;
    saved->font_name = state->font_name;
    saved->font_size = state->font_size;
    saved->text_rendering_mode = state->text_rendering_mode;
    saved->text_rise = state->text_rise;

    memcpy(saved->stroke_color, state->stroke_color, sizeof(state->stroke_color));
    memcpy(saved->fill_color, state->fill_color, sizeof(state->fill_color));

    saved->current_x = state->current_x;
    saved->current_y = state->current_y;

    // Push onto stack
    saved->next = state->saved_states;
    state->saved_states = saved;
}

// Restore graphics state (Q operator)
void pdf_graphics_state_restore(PDFGraphicsState* state) {
    if (!state->saved_states) {
        log_warn("PDF: Attempt to restore state with empty stack");
        return;
    }

    PDFSavedState* saved = state->saved_states;

    memcpy(state->tm, saved->tm, sizeof(state->tm));
    memcpy(state->tlm, saved->tlm, sizeof(state->tlm));
    memcpy(state->ctm, saved->ctm, sizeof(state->ctm));

    state->char_spacing = saved->char_spacing;
    state->word_spacing = saved->word_spacing;
    state->horizontal_scaling = saved->horizontal_scaling;
    state->leading = saved->leading;
    state->font_name = saved->font_name;
    state->font_size = saved->font_size;
    state->text_rendering_mode = saved->text_rendering_mode;
    state->text_rise = saved->text_rise;

    memcpy(state->stroke_color, saved->stroke_color, sizeof(state->stroke_color));
    memcpy(state->fill_color, saved->fill_color, sizeof(state->fill_color));

    state->current_x = saved->current_x;
    state->current_y = saved->current_y;

    // Pop from stack
    state->saved_states = saved->next;
}

// Update text position
void pdf_update_text_position(PDFGraphicsState* state, double tx, double ty) {
    state->tm[4] += tx * state->tm[0] + ty * state->tm[2];
    state->tm[5] += tx * state->tm[1] + ty * state->tm[3];

    state->tlm[4] = state->tm[4];
    state->tlm[5] = state->tm[5];
}

// Apply text matrix
void pdf_apply_text_matrix(PDFGraphicsState* state, double a, double b, double c, double d, double e, double f) {
    state->tm[0] = a;
    state->tm[1] = b;
    state->tm[2] = c;
    state->tm[3] = d;
    state->tm[4] = e;
    state->tm[5] = f;

    // Also update text line matrix
    memcpy(state->tlm, state->tm, sizeof(state->tm));
}

// Parse next operator
PDFOperator* pdf_parse_next_operator(PDFStreamParser* parser) {
    skip_whitespace(parser);

    if (parser->stream >= parser->stream_end) {
        return nullptr;
    }

    // Parse operands (numbers, strings, names, arrays)
    // Store up to 16 operands (enough for most operators)
    double numbers[16];
    String* strings[16];
    int num_count = 0;
    int str_count = 0;

    while (parser->stream < parser->stream_end) {
        skip_whitespace(parser);

        if (parser->stream >= parser->stream_end) break;

        char c = *parser->stream;

        // Check for operator (alphabetic character)
        if (isalpha(c) || c == '\'' || c == '"' || c == '*') {
            break;
        }

        // Parse operand
        if (c == '(' || c == '<') {
            // String
            if (str_count < 16) {
                strings[str_count++] = parse_string(parser);
            }
        } else if (c == '/') {
            // Name
            if (str_count < 16) {
                strings[str_count++] = parse_name(parser);
            }
        } else if (c == '[') {
            // Array - TODO: handle arrays properly
            parser->stream++;
            skip_whitespace(parser);
        } else if (c == ']') {
            parser->stream++;
            skip_whitespace(parser);
        } else if (isdigit(c) || c == '-' || c == '+' || c == '.') {
            // Number
            if (num_count < 16) {
                numbers[num_count++] = parse_number(parser);
            }
        } else {
            parser->stream++;
        }
    }

    // Parse operator name
    const char* op_name = parse_operator_name(parser);
    if (!op_name || strlen(op_name) == 0) {
        return nullptr;
    }

    // Create operator
    PDFOperator* op = (PDFOperator*)pool_calloc(parser->pool, sizeof(PDFOperator));
    op->name = op_name;
    op->type = PDF_OP_UNKNOWN;

    // Identify operator type and extract operands
    if (strcmp(op_name, "BT") == 0) {
        op->type = PDF_OP_BT;
    } else if (strcmp(op_name, "ET") == 0) {
        op->type = PDF_OP_ET;
    } else if (strcmp(op_name, "Tf") == 0) {
        op->type = PDF_OP_Tf;
        if (str_count >= 1 && num_count >= 1) {
            op->operands.set_font.font_name = strings[0];
            op->operands.set_font.size = numbers[0];
            parser->state.font_name = strings[0];
            parser->state.font_size = numbers[0];
        }
    } else if (strcmp(op_name, "Tm") == 0) {
        op->type = PDF_OP_Tm;
        if (num_count >= 6) {
            op->operands.text_matrix.a = numbers[0];
            op->operands.text_matrix.b = numbers[1];
            op->operands.text_matrix.c = numbers[2];
            op->operands.text_matrix.d = numbers[3];
            op->operands.text_matrix.e = numbers[4];
            op->operands.text_matrix.f = numbers[5];
            pdf_apply_text_matrix(&parser->state, numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5]);
        }
    } else if (strcmp(op_name, "Td") == 0) {
        op->type = PDF_OP_Td;
        if (num_count >= 2) {
            op->operands.text_position.tx = numbers[0];
            op->operands.text_position.ty = numbers[1];
            pdf_update_text_position(&parser->state, numbers[0], numbers[1]);
        }
    } else if (strcmp(op_name, "TD") == 0) {
        op->type = PDF_OP_TD;
        if (num_count >= 2) {
            op->operands.text_position.tx = numbers[0];
            op->operands.text_position.ty = numbers[1];
            parser->state.leading = -numbers[1];
            pdf_update_text_position(&parser->state, numbers[0], numbers[1]);
        }
    } else if (strcmp(op_name, "Tj") == 0) {
        op->type = PDF_OP_Tj;
        if (str_count >= 1) {
            op->operands.show_text.text = strings[0];
        }
    } else if (strcmp(op_name, "TJ") == 0) {
        op->type = PDF_OP_TJ;
        // Array parsing - simplified for now
    } else if (strcmp(op_name, "q") == 0) {
        op->type = PDF_OP_q;
        pdf_graphics_state_save(&parser->state);
    } else if (strcmp(op_name, "Q") == 0) {
        op->type = PDF_OP_Q;
        pdf_graphics_state_restore(&parser->state);
    } else if (strcmp(op_name, "rg") == 0) {
        op->type = PDF_OP_rg;
        if (num_count >= 3) {
            op->operands.rgb_color.r = numbers[0];
            op->operands.rgb_color.g = numbers[1];
            op->operands.rgb_color.b = numbers[2];
            parser->state.fill_color[0] = numbers[0];
            parser->state.fill_color[1] = numbers[1];
            parser->state.fill_color[2] = numbers[2];
        }
    } else if (strcmp(op_name, "RG") == 0) {
        op->type = PDF_OP_RG;
        if (num_count >= 3) {
            op->operands.rgb_color.r = numbers[0];
            op->operands.rgb_color.g = numbers[1];
            op->operands.rgb_color.b = numbers[2];
            parser->state.stroke_color[0] = numbers[0];
            parser->state.stroke_color[1] = numbers[1];
            parser->state.stroke_color[2] = numbers[2];
        }
    }
    // Path construction operators
    else if (strcmp(op_name, "m") == 0) {
        op->type = PDF_OP_m;
        if (num_count >= 2) {
            op->operands.text_position.tx = numbers[0];  // reuse for x,y
            op->operands.text_position.ty = numbers[1];
            parser->state.current_x = numbers[0];
            parser->state.current_y = numbers[1];
        }
    } else if (strcmp(op_name, "l") == 0) {
        op->type = PDF_OP_l;
        if (num_count >= 2) {
            op->operands.text_position.tx = numbers[0];
            op->operands.text_position.ty = numbers[1];
            parser->state.current_x = numbers[0];
            parser->state.current_y = numbers[1];
        }
    } else if (strcmp(op_name, "c") == 0) {
        op->type = PDF_OP_c;
        if (num_count >= 6) {
            op->operands.text_matrix.a = numbers[0];  // x1
            op->operands.text_matrix.b = numbers[1];  // y1
            op->operands.text_matrix.c = numbers[2];  // x2
            op->operands.text_matrix.d = numbers[3];  // y2
            op->operands.text_matrix.e = numbers[4];  // x3
            op->operands.text_matrix.f = numbers[5];  // y3
            parser->state.current_x = numbers[4];
            parser->state.current_y = numbers[5];
        }
    } else if (strcmp(op_name, "re") == 0) {
        op->type = PDF_OP_re;
        if (num_count >= 4) {
            op->operands.rect.x = numbers[0];
            op->operands.rect.y = numbers[1];
            op->operands.rect.width = numbers[2];
            op->operands.rect.height = numbers[3];
        }
    } else if (strcmp(op_name, "h") == 0) {
        op->type = PDF_OP_h;
    }
    // Path painting operators
    else if (strcmp(op_name, "S") == 0) {
        op->type = PDF_OP_S;
    } else if (strcmp(op_name, "s") == 0) {
        op->type = PDF_OP_s;
    } else if (strcmp(op_name, "f") == 0) {
        op->type = PDF_OP_f;
    } else if (strcmp(op_name, "F") == 0) {
        op->type = PDF_OP_F;
    } else if (strcmp(op_name, "f*") == 0) {
        op->type = PDF_OP_f_star;
    } else if (strcmp(op_name, "B") == 0) {
        op->type = PDF_OP_B;
    } else if (strcmp(op_name, "B*") == 0) {
        op->type = PDF_OP_B_star;
    } else if (strcmp(op_name, "b") == 0) {
        op->type = PDF_OP_b;
    } else if (strcmp(op_name, "b*") == 0) {
        op->type = PDF_OP_b_star;
    } else if (strcmp(op_name, "n") == 0) {
        op->type = PDF_OP_n;
    }

    return op;
}

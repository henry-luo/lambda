// tex_conditional.cpp - TeX Conditional Processing Implementation
//
// Implements conditionals following TeXBook Chapter 20.
//
// Reference: TeXBook Chapter 20

#include "tex_conditional.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace tex {

// ============================================================================
// ConditionalStack Implementation
// ============================================================================

void ConditionalStack::push(ConditionalState state) {
    if (count >= capacity) {
        // Grow capacity
        int new_capacity = capacity == 0 ? 16 : capacity * 2;
        ConditionalState* new_states = (ConditionalState*)malloc(new_capacity * sizeof(ConditionalState));
        if (states) {
            memcpy(new_states, states, count * sizeof(ConditionalState));
            free(states);
        }
        states = new_states;
        capacity = new_capacity;
    }
    states[count++] = state;
}

ConditionalState ConditionalStack::pop() {
    if (count == 0) {
        ConditionalState empty = {};
        return empty;
    }
    return states[--count];
}

ConditionalState* ConditionalStack::top() {
    if (count == 0) return nullptr;
    return &states[count - 1];
}

// ============================================================================
// ConditionalProcessor Implementation
// ============================================================================

ConditionalProcessor::ConditionalProcessor(Arena* arena, MacroProcessor* macros)
    : arena(arena)
    , macros(macros)
    , in_vmode(true)
    , in_hmode(false)
    , in_mmode(false)
    , in_inner(false)
{
    stack.states = nullptr;
    stack.count = 0;
    stack.capacity = 0;
}

ConditionalProcessor::~ConditionalProcessor() {
    if (stack.states) {
        free(stack.states);
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

size_t ConditionalProcessor::skip_whitespace(const char* input, size_t pos, size_t len) {
    while (pos < len && (input[pos] == ' ' || input[pos] == '\t' ||
                         input[pos] == '\n' || input[pos] == '\r')) {
        pos++;
    }
    return pos;
}

size_t ConditionalProcessor::parse_token(const char* input, size_t pos, size_t len,
                                         const char** token, size_t* token_len) {
    pos = skip_whitespace(input, pos, len);
    if (pos >= len) {
        *token = nullptr;
        *token_len = 0;
        return pos;
    }

    size_t start = pos;
    if (input[pos] == '\\') {
        pos++;
        while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                             (input[pos] >= 'A' && input[pos] <= 'Z'))) {
            pos++;
        }
        if (pos == start + 1 && pos < len) {
            pos++;  // single-char command like \{ or \}
        }
    } else {
        pos++;  // single character token
    }

    *token = input + start;
    *token_len = pos - start;
    return pos;
}

int ConditionalProcessor::parse_number(const char* input, size_t* pos, size_t len) {
    *pos = skip_whitespace(input, *pos, len);

    int sign = 1;
    if (*pos < len && input[*pos] == '-') {
        sign = -1;
        (*pos)++;
    } else if (*pos < len && input[*pos] == '+') {
        (*pos)++;
    }

    *pos = skip_whitespace(input, *pos, len);

    int num = 0;
    while (*pos < len && input[*pos] >= '0' && input[*pos] <= '9') {
        num = num * 10 + (input[*pos] - '0');
        (*pos)++;
    }

    return sign * num;
}

float ConditionalProcessor::parse_dimension(const char* input, size_t* pos, size_t len) {
    // Parse number part
    *pos = skip_whitespace(input, *pos, len);

    float sign = 1.0f;
    if (*pos < len && input[*pos] == '-') {
        sign = -1.0f;
        (*pos)++;
    } else if (*pos < len && input[*pos] == '+') {
        (*pos)++;
    }

    *pos = skip_whitespace(input, *pos, len);

    float num = 0.0f;
    while (*pos < len && input[*pos] >= '0' && input[*pos] <= '9') {
        num = num * 10.0f + (input[*pos] - '0');
        (*pos)++;
    }

    // Fractional part
    if (*pos < len && input[*pos] == '.') {
        (*pos)++;
        float frac = 0.1f;
        while (*pos < len && input[*pos] >= '0' && input[*pos] <= '9') {
            num += (input[*pos] - '0') * frac;
            frac *= 0.1f;
            (*pos)++;
        }
    }

    num *= sign;

    // Parse unit
    *pos = skip_whitespace(input, *pos, len);
    float unit_scale = 1.0f;  // Default: pt

    if (*pos + 1 < len) {
        if (strncmp(input + *pos, "pt", 2) == 0) {
            unit_scale = 1.0f;
            *pos += 2;
        } else if (strncmp(input + *pos, "mm", 2) == 0) {
            unit_scale = 2.845f;  // mm to pt
            *pos += 2;
        } else if (strncmp(input + *pos, "cm", 2) == 0) {
            unit_scale = 28.45f;  // cm to pt
            *pos += 2;
        } else if (strncmp(input + *pos, "in", 2) == 0) {
            unit_scale = 72.27f;  // in to pt
            *pos += 2;
        } else if (strncmp(input + *pos, "bp", 2) == 0) {
            unit_scale = 1.00375f;  // bp to pt
            *pos += 2;
        } else if (strncmp(input + *pos, "em", 2) == 0) {
            unit_scale = 10.0f;  // approximate
            *pos += 2;
        } else if (strncmp(input + *pos, "ex", 2) == 0) {
            unit_scale = 4.5f;   // approximate
            *pos += 2;
        } else if (strncmp(input + *pos, "sp", 2) == 0) {
            unit_scale = 1.0f / 65536.0f;  // scaled point
            *pos += 2;
        }
    }

    return num * unit_scale;
}

int ConditionalProcessor::parse_relation(const char* input, size_t* pos, size_t len) {
    *pos = skip_whitespace(input, *pos, len);

    if (*pos >= len) return 0;

    if (input[*pos] == '<') {
        (*pos)++;
        return -1;  // less than
    } else if (input[*pos] == '=') {
        (*pos)++;
        return 0;   // equal
    } else if (input[*pos] == '>') {
        (*pos)++;
        return 1;   // greater than
    }

    return 0;
}

size_t ConditionalProcessor::find_fi(const char* input, size_t pos, size_t len) {
    int depth = 1;
    while (pos < len && depth > 0) {
        if (input[pos] == '\\') {
            pos++;
            // Check for \if* (increase depth)
            if (pos + 1 < len && strncmp(input + pos, "if", 2) == 0) {
                depth++;
                pos += 2;
                // Skip rest of \if command name
                while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                                     (input[pos] >= 'A' && input[pos] <= 'Z'))) {
                    pos++;
                }
            }
            // Check for \fi (decrease depth)
            else if (pos + 1 < len && strncmp(input + pos, "fi", 2) == 0 &&
                     (pos + 2 >= len || !((input[pos + 2] >= 'a' && input[pos + 2] <= 'z') ||
                                          (input[pos + 2] >= 'A' && input[pos + 2] <= 'Z')))) {
                depth--;
                pos += 2;
            } else {
                // Skip other command
                while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                                     (input[pos] >= 'A' && input[pos] <= 'Z'))) {
                    pos++;
                }
            }
        } else {
            pos++;
        }
    }
    return pos;
}

size_t ConditionalProcessor::find_else_or_fi(const char* input, size_t pos, size_t len, bool* found_else) {
    int depth = 1;
    *found_else = false;

    while (pos < len && depth > 0) {
        if (input[pos] == '\\') {
            pos++;
            // Check for \if* (increase depth)
            if (pos + 1 < len && strncmp(input + pos, "if", 2) == 0) {
                depth++;
                pos += 2;
                while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                                     (input[pos] >= 'A' && input[pos] <= 'Z'))) {
                    pos++;
                }
            }
            // Check for \fi
            else if (pos + 1 < len && strncmp(input + pos, "fi", 2) == 0 &&
                     (pos + 2 >= len || !((input[pos + 2] >= 'a' && input[pos + 2] <= 'z') ||
                                          (input[pos + 2] >= 'A' && input[pos + 2] <= 'Z')))) {
                depth--;
                if (depth == 0) {
                    pos += 2;
                    return pos;
                }
                pos += 2;
            }
            // Check for \else at current depth
            else if (depth == 1 && pos + 3 < len && strncmp(input + pos, "else", 4) == 0 &&
                     (pos + 4 >= len || !((input[pos + 4] >= 'a' && input[pos + 4] <= 'z') ||
                                          (input[pos + 4] >= 'A' && input[pos + 4] <= 'Z')))) {
                *found_else = true;
                pos += 4;
                return pos;
            } else {
                while (pos < len && ((input[pos] >= 'a' && input[pos] <= 'z') ||
                                     (input[pos] >= 'A' && input[pos] <= 'Z'))) {
                    pos++;
                }
            }
        } else {
            pos++;
        }
    }
    return pos;
}

// ============================================================================
// Conditional Evaluation
// ============================================================================

bool ConditionalProcessor::eval_if(const char* input, size_t* pos, size_t len) {
    // \if compares character codes of next two tokens
    const char* tok1;
    size_t tok1_len;
    *pos = parse_token(input, *pos, len, &tok1, &tok1_len);

    const char* tok2;
    size_t tok2_len;
    *pos = parse_token(input, *pos, len, &tok2, &tok2_len);

    // Get character codes (first char of each token, or 0 for commands)
    int code1 = 0, code2 = 0;
    if (tok1 && tok1_len > 0) {
        code1 = (tok1[0] == '\\' && tok1_len > 1) ? tok1[1] : tok1[0];
    }
    if (tok2 && tok2_len > 0) {
        code2 = (tok2[0] == '\\' && tok2_len > 1) ? tok2[1] : tok2[0];
    }

    log_debug("conditional: \\if code1=%d code2=%d result=%d", code1, code2, code1 == code2);
    return code1 == code2;
}

bool ConditionalProcessor::eval_ifx(const char* input, size_t* pos, size_t len) {
    // \ifx compares meanings (macro definitions)
    const char* tok1;
    size_t tok1_len;
    *pos = parse_token(input, *pos, len, &tok1, &tok1_len);

    const char* tok2;
    size_t tok2_len;
    *pos = parse_token(input, *pos, len, &tok2, &tok2_len);

    // If both are commands, compare their definitions
    if (tok1 && tok1_len > 0 && tok1[0] == '\\' &&
        tok2 && tok2_len > 0 && tok2[0] == '\\') {

        const MacroDef* def1 = macros->get_macro(tok1 + 1, tok1_len - 1);
        const MacroDef* def2 = macros->get_macro(tok2 + 1, tok2_len - 1);

        // Both undefined
        if (!def1 && !def2) return true;

        // One defined, one not
        if (!def1 || !def2) return false;

        // Compare replacement texts
        if (def1->replacement_len != def2->replacement_len) return false;
        bool result = (strncmp(def1->replacement, def2->replacement, def1->replacement_len) == 0);

        log_debug("conditional: \\ifx comparing macros result=%d", result);
        return result;
    }

    // Otherwise compare character codes (same as \if)
    int code1 = (tok1 && tok1_len > 0) ? tok1[0] : 0;
    int code2 = (tok2 && tok2_len > 0) ? tok2[0] : 0;

    return code1 == code2;
}

bool ConditionalProcessor::eval_ifnum(const char* input, size_t* pos, size_t len) {
    // \ifnum <number1> <relation> <number2>
    int num1 = parse_number(input, pos, len);
    int rel = parse_relation(input, pos, len);
    int num2 = parse_number(input, pos, len);

    bool result = false;
    if (rel < 0) result = (num1 < num2);
    else if (rel > 0) result = (num1 > num2);
    else result = (num1 == num2);

    log_debug("conditional: \\ifnum %d %c %d = %d", num1, rel < 0 ? '<' : (rel > 0 ? '>' : '='), num2, result);
    return result;
}

bool ConditionalProcessor::eval_ifdim(const char* input, size_t* pos, size_t len) {
    // \ifdim <dimen1> <relation> <dimen2>
    float dim1 = parse_dimension(input, pos, len);
    int rel = parse_relation(input, pos, len);
    float dim2 = parse_dimension(input, pos, len);

    bool result = false;
    if (rel < 0) result = (dim1 < dim2);
    else if (rel > 0) result = (dim1 > dim2);
    else result = (fabs(dim1 - dim2) < 0.001f);

    log_debug("conditional: \\ifdim %.2fpt %c %.2fpt = %d", dim1, rel < 0 ? '<' : (rel > 0 ? '>' : '='), dim2, result);
    return result;
}

bool ConditionalProcessor::eval_ifodd(const char* input, size_t* pos, size_t len) {
    int num = parse_number(input, pos, len);
    bool result = (num % 2) != 0;
    log_debug("conditional: \\ifodd %d = %d", num, result);
    return result;
}

bool ConditionalProcessor::eval_ifdefined(const char* input, size_t* pos, size_t len) {
    const char* tok;
    size_t tok_len;
    *pos = parse_token(input, *pos, len, &tok, &tok_len);

    if (tok && tok_len > 0 && tok[0] == '\\') {
        bool result = macros->is_defined(tok + 1, tok_len - 1);
        log_debug("conditional: \\ifdefined \\%.*s = %d", (int)(tok_len - 1), tok + 1, result);
        return result;
    }

    return false;
}

size_t ConditionalProcessor::evaluate_conditional(const char* input, size_t pos, size_t len,
                                                  bool* result) {
    // pos should be at the backslash of \if*
    if (pos >= len || input[pos] != '\\') {
        *result = false;
        return pos;
    }

    size_t cmd_start = pos + 1;
    size_t cmd_end = cmd_start;
    while (cmd_end < len && ((input[cmd_end] >= 'a' && input[cmd_end] <= 'z') ||
                             (input[cmd_end] >= 'A' && input[cmd_end] <= 'Z'))) {
        cmd_end++;
    }

    const char* cmd = input + cmd_start;
    size_t cmd_len = cmd_end - cmd_start;
    size_t after_cmd = cmd_end;

    // Evaluate based on conditional type
    if (cmd_len == 2 && strncmp(cmd, "if", 2) == 0) {
        *result = eval_if(input, &after_cmd, len);
    } else if (cmd_len == 3 && strncmp(cmd, "ifx", 3) == 0) {
        *result = eval_ifx(input, &after_cmd, len);
    } else if (cmd_len == 5 && strncmp(cmd, "ifnum", 5) == 0) {
        *result = eval_ifnum(input, &after_cmd, len);
    } else if (cmd_len == 5 && strncmp(cmd, "ifdim", 5) == 0) {
        *result = eval_ifdim(input, &after_cmd, len);
    } else if (cmd_len == 5 && strncmp(cmd, "ifodd", 5) == 0) {
        *result = eval_ifodd(input, &after_cmd, len);
    } else if (cmd_len == 9 && strncmp(cmd, "ifdefined", 9) == 0) {
        *result = eval_ifdefined(input, &after_cmd, len);
    } else if (cmd_len == 6 && strncmp(cmd, "iftrue", 6) == 0) {
        *result = true;
    } else if (cmd_len == 7 && strncmp(cmd, "iffalse", 7) == 0) {
        *result = false;
    } else if (cmd_len == 7 && strncmp(cmd, "ifvmode", 7) == 0) {
        *result = in_vmode;
    } else if (cmd_len == 7 && strncmp(cmd, "ifhmode", 7) == 0) {
        *result = in_hmode;
    } else if (cmd_len == 7 && strncmp(cmd, "ifmmode", 7) == 0) {
        *result = in_mmode;
    } else if (cmd_len == 7 && strncmp(cmd, "ifinner", 7) == 0) {
        *result = in_inner;
    } else {
        // Unknown conditional
        log_error("conditional: unknown \\%.*s", (int)cmd_len, cmd);
        *result = false;
    }

    return after_cmd;
}

// ============================================================================
// Main Processing
// ============================================================================

char* ConditionalProcessor::process(const char* input, size_t len, size_t* out_len) {
    Strbuf result;
    strbuf_init(&result);

    size_t pos = 0;
    while (pos < len) {
        if (input[pos] == '\\' && pos + 2 < len &&
            strncmp(input + pos + 1, "if", 2) == 0) {

            // Evaluate the conditional
            bool cond_result;
            pos = evaluate_conditional(input, pos, len, &cond_result);

            // Find \else and \fi
            bool found_else;
            size_t else_or_fi_pos = find_else_or_fi(input, pos, len, &found_else);

            if (cond_result) {
                // Take true branch (from pos to else_or_fi_pos)
                size_t true_end = else_or_fi_pos;
                if (found_else) {
                    // Need to subtract \else length
                    true_end -= 5;  // \else is 5 chars including backslash
                }

                // Recursively process true branch
                size_t branch_len;
                char* branch = process(input + pos, true_end - pos, &branch_len);
                strbuf_append_n(&result, branch, branch_len);

                // Skip to after \fi
                if (found_else) {
                    pos = find_fi(input, else_or_fi_pos, len);
                } else {
                    pos = else_or_fi_pos;
                }
            } else {
                // Take false branch (after \else if present)
                if (found_else) {
                    size_t fi_pos = find_fi(input, else_or_fi_pos, len);

                    // Recursively process false branch
                    size_t false_end = fi_pos - 3;  // subtract \fi length
                    size_t branch_len;
                    char* branch = process(input + else_or_fi_pos, false_end - else_or_fi_pos, &branch_len);
                    strbuf_append_n(&result, branch, branch_len);

                    pos = fi_pos;
                } else {
                    // No \else, skip to \fi
                    pos = else_or_fi_pos;
                }
            }
        } else {
            strbuf_append_char(&result, input[pos]);
            pos++;
        }
    }

    // Copy result to arena
    size_t result_len = strbuf_len(&result);
    char* output = (char*)arena_alloc(arena, result_len + 1);
    memcpy(output, result.chars, result_len);
    output[result_len] = '\0';

    strbuf_free(&result);

    *out_len = result_len;
    return output;
}

// ============================================================================
// Utility Functions
// ============================================================================

bool is_conditional_command(const char* str, size_t len) {
    if (len < 3 || str[0] != '\\') return false;
    return (str[1] == 'i' && str[2] == 'f');
}

ConditionalType get_conditional_type(const char* cmd, size_t len) {
    if (len == 2 && strncmp(cmd, "if", 2) == 0) return ConditionalType::If;
    if (len == 3 && strncmp(cmd, "ifx", 3) == 0) return ConditionalType::Ifx;
    if (len == 5 && strncmp(cmd, "ifcat", 5) == 0) return ConditionalType::Ifcat;
    if (len == 5 && strncmp(cmd, "ifnum", 5) == 0) return ConditionalType::Ifnum;
    if (len == 5 && strncmp(cmd, "ifdim", 5) == 0) return ConditionalType::Ifdim;
    if (len == 5 && strncmp(cmd, "ifodd", 5) == 0) return ConditionalType::Ifodd;
    if (len == 7 && strncmp(cmd, "ifvmode", 7) == 0) return ConditionalType::Ifvmode;
    if (len == 7 && strncmp(cmd, "ifhmode", 7) == 0) return ConditionalType::Ifhmode;
    if (len == 7 && strncmp(cmd, "ifmmode", 7) == 0) return ConditionalType::Ifmmode;
    if (len == 7 && strncmp(cmd, "ifinner", 7) == 0) return ConditionalType::Ifinner;
    if (len == 6 && strncmp(cmd, "ifvoid", 6) == 0) return ConditionalType::Ifvoid;
    if (len == 6 && strncmp(cmd, "ifhbox", 6) == 0) return ConditionalType::Ifhbox;
    if (len == 6 && strncmp(cmd, "ifvbox", 6) == 0) return ConditionalType::Ifvbox;
    if (len == 5 && strncmp(cmd, "ifeof", 5) == 0) return ConditionalType::Ifeof;
    if (len == 6 && strncmp(cmd, "iftrue", 6) == 0) return ConditionalType::Iftrue;
    if (len == 7 && strncmp(cmd, "iffalse", 7) == 0) return ConditionalType::Iffalse;
    if (len == 6 && strncmp(cmd, "ifcase", 6) == 0) return ConditionalType::Ifcase;
    if (len == 9 && strncmp(cmd, "ifdefined", 9) == 0) return ConditionalType::Ifdefined;
    return ConditionalType::If;  // default
}

} // namespace tex

/**
 * Lambda Unified Font Module — CSS family-list parsing
 */

#include "font.h"
#include "../str.h"

bool font_family_list_next(const char** cursor, char* family, size_t family_capacity) {
    if (!cursor || !*cursor || !family || family_capacity == 0) return false;

    const char* input = *cursor;
    while (*input) {
        while (*input == ',' || str_char_is_ascii_space(*input)) input++;
        if (!*input) {
            *cursor = input;
            return false;
        }

        char quote = (*input == '\'' || *input == '"') ? *input++ : '\0';
        bool quoted = quote != '\0';
        size_t length = 0;
        while (*input && ((quoted && *input != quote) || (!quoted && *input != ','))) {
            if (*input == '\\' && input[1] && quoted) input++;
            if (length + 1 < family_capacity) family[length++] = *input;
            input++;
        }
        if (quoted && *input == quote) input++;
        while (*input && *input != ',') input++;
        if (*input == ',') input++;
        if (!quoted) {
            while (length > 0 && str_char_is_ascii_space(family[length - 1])) length--;
        }
        family[length] = '\0';
        *cursor = input;
        if (length > 0) return true;
    }

    *cursor = input;
    return false;
}

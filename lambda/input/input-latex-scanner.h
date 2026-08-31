#ifndef LAMBDA_INPUT_LATEX_SCANNER_H
#define LAMBDA_INPUT_LATEX_SCANNER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return the end position after a balanced group and its content span.
// A zero return means the opening delimiter was not balanced.
size_t latex_scan_group_end(const char* source, size_t length, size_t start,
                            char open, char close, size_t* content_start,
                            size_t* content_end);

// Scan one control word/symbol into caller-provided buffers and return its end.
// `full` includes the leading backslash; `name` does not.
size_t latex_scan_command(const char* source, size_t length, size_t start,
                          char* name, size_t name_capacity, char* full,
                          size_t full_capacity);

bool latex_scan_starts_with(const char* source, size_t length, size_t start,
                            const char* text);

#ifdef __cplusplus
}
#endif

#endif

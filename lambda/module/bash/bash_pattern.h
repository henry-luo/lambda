#pragma once

// bash_pattern.h — Unified pattern matching engine for Bash transpiler
//
// Handles:
//   - Basic globs: *, ?, [abc], [a-z], [!abc]
//   - POSIX bracket expressions: [[:alpha:]], [[:digit:]], etc.
//   - Extended globs: ?(p|q), *(p|q), +(p|q), @(p|q), !(p|q)
//   - Case-insensitive matching (nocasematch)
//   - Backslash escaping
//
// All functions are pure C with no Lambda runtime dependency.

#ifdef __cplusplus
extern "C" {
#endif

// Pattern matching flags
#define BASH_PAT_EXTGLOB    0x01   // enable ?(pat), *(pat), +(pat), @(pat), !(pat)
#define BASH_PAT_NOCASE     0x02   // case-insensitive (nocasematch)
#define BASH_PAT_DOTGLOB    0x04   // leading dot matched by * and ?
#define BASH_PAT_GLOBSTAR   0x08   // ** matches directory separators recursively
#define BASH_PAT_PERIOD     0x10   // leading period requires explicit match (FNM_PERIOD)
#define BASH_PAT_PATHNAME   0x20   // / only matched by literal / (FNM_PATHNAME)

// Match a string against a bash/glob pattern.
// Returns 1 on match, 0 on no match, -1 on error (e.g., invalid pattern).
int bash_pattern_match(const char* string, const char* pattern, int flags);

// Check if a single character matches a POSIX bracket expression.
// bracket_expr is the content between '[' and ']' (excluding the brackets).
// e.g., for [[:alpha:]0-9], bracket_expr = "[:alpha:]0-9"
// Returns 1 if the character matches, 0 if not.
int bash_bracket_match(char c, const char* bracket_expr, int flags);

#ifdef __cplusplus
}
#endif

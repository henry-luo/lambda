// bash_expand.h — Word Expansion Engine (Phase A — Module 1)
//
// Provides the unified word expansion pipeline:
//   IFS word splitting, quote removal, and a field-split helper.
//
// The transpiler handles most expansion stages at compile time
// (tilde, parameter, command sub, arithmetic, brace, glob).
// This module fills the remaining runtime gaps:
//   - Standalone IFS word splitting with explicit IFS parameter
//   - Runtime quote removal for dynamic contexts
//   - Field splitting for the read builtin and eval contexts

#ifndef BASH_EXPAND_H
#define BASH_EXPAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ========================================================================
// Expansion flags (for bash_expand_word)
// ========================================================================
#define BASH_EXPAND_FULL       0xFF   // all stages
#define BASH_EXPAND_NO_SPLIT   0x01   // suppress IFS word splitting (double-quoted)
#define BASH_EXPAND_NO_GLOB    0x02   // suppress pathname expansion (set -f)
#define BASH_EXPAND_ASSIGNMENT 0x04   // assignment context (tilde after :)
#define BASH_EXPAND_PATTERN    0x08   // pattern context (preserve glob chars)

// ========================================================================
// IFS Word Splitting
// ========================================================================

// Split a string by the given IFS value.
// Returns a new array (List) of words.
// If ifs is NULL or empty, returns the whole string unsplit in a 1-element array.
// Implements full POSIX IFS algorithm:
//   - IFS whitespace chars (space/tab/newline) act as collapsible separators
//   - IFS non-whitespace chars are strict delimiters (preserve empty fields)
//   - Leading/trailing IFS whitespace is trimmed
Item bash_word_split(Item str, Item ifs);

// Split a string by IFS and populate a pre-existing array (append mode).
// Like bash_word_split but appends to arr instead of creating a new one.
// This is a convenience alias for bashing existing bash_ifs_split_into semantics.
Item bash_word_split_into(Item arr, Item str, Item ifs);

// ========================================================================
// Quote Removal
// ========================================================================

// Remove syntactic quotes from a string at runtime:
//   - Single quotes: 'text' → text (no interpretation inside)
//   - Double quotes: "text" → text (preserves content, just strips the quotes)
//   - Backslash escaping: \c → c (outside quotes, removes the backslash)
//   - ANSI-C quotes: $'...' → processed escapes (\n, \t, \xHH, \uHHHH, etc.)
// Returns a new string with quotes removed.
Item bash_quote_remove(Item word);

// Process ANSI-C escape sequences in a $'...' string.
// Handles: \a \b \e \E \f \n \r \t \v \\ \' \" \? \0NNN \xHH \uHHHH \UHHHHHHHH \cX
// Returns a new string with escapes resolved.
Item bash_process_ansi_escapes(Item str);

// ========================================================================
// Unified Expansion Pipeline
// ========================================================================

// Perform full word expansion on a string:
//   brace → tilde → parameter → command sub → arithmetic → IFS split → glob → quote remove
// flags control which stages to skip (BASH_EXPAND_NO_SPLIT, etc.)
// NOTE: Most stages are handled at transpile time. This function is for
// dynamic contexts (eval, ${!var}, indirect expansion) where the expansion
// must happen at runtime.
Item bash_expand_word(Item word, int flags);

#ifdef __cplusplus
}
#endif

#endif // BASH_EXPAND_H

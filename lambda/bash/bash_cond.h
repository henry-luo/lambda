// bash_cond.h — Conditional Engine (Phase G — Module 10)
//
// Provides:
// - Regex match with BASH_REMATCH population ([[ str =~ re ]])
// - Shopt-aware pattern matching ([[ str == pattern ]])
// - File comparison operators (-nt, -ot, -ef)
// - BASH_REMATCH array access

#ifndef BASH_COND_H
#define BASH_COND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ============================================================================
// Regex match with BASH_REMATCH
// ============================================================================

// [[ string =~ pattern ]]
// Returns: Item boolean (true=match, false=no match)
// Side effect: populates BASH_REMATCH[0..N] with match groups, sets exit code
Item bash_cond_regex(Item string, Item pattern);

// ============================================================================
// BASH_REMATCH access
// ============================================================================

Item bash_get_rematch(Item index);        // ${BASH_REMATCH[n]}
Item bash_get_rematch_count(void);        // ${#BASH_REMATCH[@]}
Item bash_get_rematch_all(void);          // ${BASH_REMATCH[@]}
void bash_clear_rematch(void);            // clear before new =~ test

// ============================================================================
// Shopt-aware pattern matching
// ============================================================================

// [[ string == pattern ]]
// Reads nocasematch and extglob from current shopt state automatically.
// Returns boolean Item (true=match, false=no match).
Item bash_cond_pattern(Item string, Item pattern);

// ============================================================================
// File comparison operators
// ============================================================================

Item bash_test_nt(Item file1, Item file2);  // file1 -nt file2 (newer than)
Item bash_test_ot(Item file1, Item file2);  // file1 -ot file2 (older than)
Item bash_test_ef(Item file1, Item file2);  // file1 -ef file2 (same inode)

#ifdef __cplusplus
}
#endif

#endif // BASH_COND_H

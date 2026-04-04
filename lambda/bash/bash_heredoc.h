// bash_heredoc.h — Here-Document Engine (Phase H — Module 11)
//
// Provides:
// - Here-doc body expansion (variables, command sub, arithmetic)
// - Here-string processing
// - Tab stripping for <<-
// - Stdin content passing for heredoc/herestring to commands

#ifndef BASH_HEREDOC_H
#define BASH_HEREDOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ============================================================================
// Here-document expansion
// ============================================================================

// Expand a here-document body in expansion mode (<<EOF, not <<'EOF').
// Performs: $var, ${var...}, $(cmd), $((expr)), \-escaping of $, `, \, newline.
// Returns the expanded string.
Item bash_heredoc_expand(Item body);

// ============================================================================
// Here-string
// ============================================================================

// Process a here-string: expand word, append newline.
// <<<word → expand(word) + "\n" → stdin of command
Item bash_herestring_expand(Item word);

// ============================================================================
// Tab stripping
// ============================================================================

// Strip leading tabs from each line of here-doc body (<<-EOF mode).
Item bash_heredoc_strip_tabs(Item body);

// ============================================================================
// Heredoc stdin passing
// ============================================================================

// Feed expanded content as stdin to the next command.
// Integrates with bash_set_stdin_item() / bash_get_stdin_item().
void bash_set_heredoc_stdin(Item content);
Item bash_get_heredoc_stdin(void);
void bash_clear_heredoc_stdin(void);

#ifdef __cplusplus
}
#endif

#endif // BASH_HEREDOC_H

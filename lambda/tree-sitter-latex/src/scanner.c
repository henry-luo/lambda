#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/parser.h>

// External token types matching grammar.js externals
enum TokenType {
  VERBATIM_CONTENT,
  COMMENT_ENV_CONTENT,
  BEGIN_DOCUMENT,
  END_DOCUMENT,
  VERB_COMMAND,
  CHAR_COMMAND,
  CARET_CHAR,   // ^^XX (2 hex) or ^^^^XXXX (4 hex) or ^^c (char +/- 64)
};

// Check if character is a hex digit
static bool is_hex_digit(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

// Skip whitespace (space, tab, newline)
static void skip_ws(TSLexer *lexer) {
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
         lexer->lookahead == '\n' || lexer->lookahead == '\r') {
    lexer->advance(lexer, true);
  }
}

// Check if next chars match a string (case sensitive)
// Returns true if matched, lexer position updated past the string
static bool match_string(TSLexer *lexer, const char *str) {
  for (size_t i = 0; str[i]; i++) {
    if (lexer->eof(lexer) || lexer->lookahead != str[i]) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  return true;
}

// Scan until \end{<env_name>} is found
// Returns true if any content was scanned before the end tag
static bool scan_until_end(TSLexer *lexer, const char *env_name) {
  bool has_content = false;

  while (!lexer->eof(lexer)) {
    // Look for backslash
    if (lexer->lookahead == '\\') {
      // Mark end before consuming \end{...}
      lexer->mark_end(lexer);

      lexer->advance(lexer, false);
      if (lexer->eof(lexer)) break;

      // Check for "end{"
      if (lexer->lookahead == 'e') {
        lexer->advance(lexer, false);
        if (!lexer->eof(lexer) && lexer->lookahead == 'n') {
          lexer->advance(lexer, false);
          if (!lexer->eof(lexer) && lexer->lookahead == 'd') {
            lexer->advance(lexer, false);
            if (!lexer->eof(lexer) && lexer->lookahead == '{') {
              lexer->advance(lexer, false);

              // Check for environment name
              bool matches = true;
              for (size_t i = 0; env_name[i] && matches; i++) {
                if (lexer->eof(lexer) || lexer->lookahead != env_name[i]) {
                  matches = false;
                } else {
                  lexer->advance(lexer, false);
                }
              }

              if (matches && !lexer->eof(lexer) && lexer->lookahead == '}') {
                // Found \end{env_name} - don't consume the closing brace
                // The mark_end before \end captures the content
                return has_content;
              }
            }
          }
        }
      }
      has_content = true;
    } else {
      lexer->advance(lexer, false);
      has_content = true;
    }
    lexer->mark_end(lexer);
  }

  return has_content;
}

// Scan verbatim content (for verbatim, lstlisting, minted environments)
static bool scan_verbatim(TSLexer *lexer) {
  bool has_content = false;

  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == '\\') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);

      // Check for \end{verbatim} or \end{lstlisting} or \end{minted}
      if (lexer->lookahead == 'e') {
        // Save position and try to match \end{...}
        lexer->advance(lexer, false);
        if (!lexer->eof(lexer) && lexer->lookahead == 'n') {
          lexer->advance(lexer, false);
          if (!lexer->eof(lexer) && lexer->lookahead == 'd') {
            lexer->advance(lexer, false);
            if (!lexer->eof(lexer) && lexer->lookahead == '{') {
              lexer->advance(lexer, false);

              // Check for verbatim, lstlisting, or minted
              if (lexer->lookahead == 'v') {
                if (match_string(lexer, "verbatim}")) {
                  return has_content;
                }
              } else if (lexer->lookahead == 'l') {
                if (match_string(lexer, "lstlisting}")) {
                  return has_content;
                }
              } else if (lexer->lookahead == 'm') {
                if (match_string(lexer, "minted}")) {
                  return has_content;
                }
              }
            }
          }
        }
      }
      has_content = true;
      lexer->mark_end(lexer);
    } else {
      lexer->advance(lexer, false);
      has_content = true;
      lexer->mark_end(lexer);
    }
  }

  return has_content;
}

// Scan comment environment content
static bool scan_comment(TSLexer *lexer) {
  return scan_until_end(lexer, "comment");
}

// Try to match a string, advancing lexer for each matching character.
// Returns true if ALL characters match (lexer advanced past string).
// Returns false on first mismatch (lexer advanced only to mismatch point).
// WARNING: This function DOES advance the lexer even on partial match!
static bool try_match_string(TSLexer *lexer, const char *str) {
  for (size_t i = 0; str[i]; i++) {
    if (lexer->eof(lexer) || lexer->lookahead != (unsigned char)str[i]) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  return true;
}

// Scan \begin{document} - returns true if matched
static bool scan_begin_document(TSLexer *lexer) {
  // Must start with backslash
  if (lexer->lookahead != '\\') {
    return false;
  }
  lexer->advance(lexer, false);

  // Match "begin{document}"
  if (try_match_string(lexer, "begin{document}")) {
    lexer->mark_end(lexer);
    return true;
  }

  return false;
}

// Scan \end{document} - returns true if matched
static bool scan_end_document(TSLexer *lexer) {
  // Must start with backslash
  if (lexer->lookahead != '\\') {
    return false;
  }
  lexer->advance(lexer, false);

  // Match "end{document}"
  if (try_match_string(lexer, "end{document}")) {
    lexer->mark_end(lexer);
    return true;
  }

  return false;
}

// Scan \verb<delim>content<delim> - returns true if matched
// Handles arbitrary delimiter characters (e.g., \verb|text|, \verb+text+)
static bool scan_verb_command(TSLexer *lexer) {
  // Must start with backslash
  if (lexer->lookahead != '\\') {
    return false;
  }

  // Advance past backslash
  lexer->advance(lexer, false);

  // Match "verb"
  if (!try_match_string(lexer, "verb")) {
    return false;
  }

  // Optional asterisk for \verb* (visible spaces)
  if (lexer->lookahead == '*') {
    lexer->advance(lexer, false);
  }

  // Next character is the delimiter (must not be whitespace or newline)
  if (lexer->eof(lexer) || lexer->lookahead == ' ' ||
      lexer->lookahead == '\t' || lexer->lookahead == '\n' ||
      lexer->lookahead == '\r') {
    return false;
  }

  // Capture the delimiter
  unsigned char delimiter = lexer->lookahead;
  lexer->advance(lexer, false);

  // Scan until we find the matching delimiter or newline
  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == delimiter) {
      // Found closing delimiter - include it
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      return true;
    } else if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      // \verb cannot span lines
      return false;
    } else {
      lexer->advance(lexer, false);
    }
  }

  // EOF without closing delimiter
  return false;
}

// Note: scan_char_command is deprecated - use scan_backslash_command instead
// The old function had lexer position corruption issues when failing to match

// Tree-sitter external scanner interface
void *tree_sitter_latex_external_scanner_create(void) {
  return NULL;
}

void tree_sitter_latex_external_scanner_destroy(void *payload) {
  // Nothing to destroy
}

unsigned tree_sitter_latex_external_scanner_serialize(void *payload, char *buffer) {
  return 0;
}

void tree_sitter_latex_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  // Nothing to deserialize
}

// Scan for \verb or \char after already having advanced past \
// Assumes lexer is positioned at character after backslash
// Returns token type if matched, -1 otherwise
static int scan_backslash_command(TSLexer *lexer) {
  // Check first character to determine which command this might be
  if (lexer->lookahead == 'c') {
    // Could be \char
    lexer->advance(lexer, false);
    if (lexer->lookahead == 'h') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == 'a') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == 'r') {
          lexer->advance(lexer, false);
          // We have \char - now check for number format
          if (lexer->eof(lexer)) return -1;

          if (lexer->lookahead == '"') {
            // Hex format: \char"<hex>
            lexer->advance(lexer, false);
            int hex_count = 0;
            while (!lexer->eof(lexer) &&
                   ((lexer->lookahead >= '0' && lexer->lookahead <= '9') ||
                    (lexer->lookahead >= 'A' && lexer->lookahead <= 'F') ||
                    (lexer->lookahead >= 'a' && lexer->lookahead <= 'f'))) {
              lexer->advance(lexer, false);
              hex_count++;
            }
            if (hex_count > 0) {
              lexer->mark_end(lexer);
              return CHAR_COMMAND;
            }
          } else if (lexer->lookahead == '\'') {
            // Octal format: \char'<octal>
            lexer->advance(lexer, false);
            int octal_count = 0;
            while (!lexer->eof(lexer) &&
                   (lexer->lookahead >= '0' && lexer->lookahead <= '7')) {
              lexer->advance(lexer, false);
              octal_count++;
            }
            if (octal_count > 0) {
              lexer->mark_end(lexer);
              return CHAR_COMMAND;
            }
          } else if (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
            // Decimal format: \char<decimal>
            while (!lexer->eof(lexer) &&
                   (lexer->lookahead >= '0' && lexer->lookahead <= '9')) {
              lexer->advance(lexer, false);
            }
            lexer->mark_end(lexer);
            return CHAR_COMMAND;
          }
          // \char without valid number format - not a char_command
          return -1;
        }
      }
    }
    // Not \char, might be other command - return -1
    return -1;

  } else if (lexer->lookahead == 'v') {
    // Could be \verb
    lexer->advance(lexer, false);
    if (lexer->lookahead == 'e') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == 'r') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == 'b') {
          lexer->advance(lexer, false);

          // Optional asterisk for \verb*
          if (lexer->lookahead == '*') {
            lexer->advance(lexer, false);
          }

          // Next character is the delimiter (must not be whitespace or newline)
          if (lexer->eof(lexer) || lexer->lookahead == ' ' ||
              lexer->lookahead == '\t' || lexer->lookahead == '\n' ||
              lexer->lookahead == '\r') {
            return -1;
          }

          // Capture the delimiter
          unsigned char delimiter = lexer->lookahead;
          lexer->advance(lexer, false);

          // Scan until we find the matching delimiter or newline
          while (!lexer->eof(lexer)) {
            if (lexer->lookahead == delimiter) {
              // Found closing delimiter - include it
              lexer->advance(lexer, false);
              lexer->mark_end(lexer);
              return VERB_COMMAND;
            } else if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
              // \verb cannot span lines
              return -1;
            } else {
              lexer->advance(lexer, false);
            }
          }
          // EOF without closing delimiter
          return -1;
        }
      }
    }
    // Not \verb
    return -1;

  } else if (lexer->lookahead == 'b') {
    // Could be \begin{document}
    if (try_match_string(lexer, "begin{document}")) {
      lexer->mark_end(lexer);
      return BEGIN_DOCUMENT;
    }
    return -1;

  } else if (lexer->lookahead == 'e') {
    // Could be \end{document}
    if (try_match_string(lexer, "end{document}")) {
      lexer->mark_end(lexer);
      return END_DOCUMENT;
    }
    return -1;
  }

  return -1;
}

bool tree_sitter_latex_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  // Debug: uncomment to trace scanner calls
  // fprintf(stderr, "SCANNER: lookahead='%c' (%d), valid_symbols=[%d,%d,%d,%d,%d,%d,%d]\n",
  //         lexer->lookahead, lexer->lookahead,
  //         valid_symbols[0], valid_symbols[1], valid_symbols[2],
  //         valid_symbols[3], valid_symbols[4], valid_symbols[5], valid_symbols[6]);

  // PROACTIVE SCANNING: Check for special commands at backslash
  // We handle \char, \verb, \begin{document}, \end{document} in a single pass
  // IMPORTANT: Only advance and return true if we FULLY match a special command.
  // If we don't match, we must NOT advance the lexer - let the normal lexer handle it.

  if (lexer->lookahead == '\\') {
    // DON'T advance yet - peek at next char first
    // We need to check if this could be a special command before advancing
    
    // Use mark_end to save position BEFORE any advances
    lexer->mark_end(lexer);
    lexer->advance(lexer, false);

    // Try to match special commands
    int result = scan_backslash_command(lexer);
    if (result >= 0) {
      // fprintf(stderr, "SCANNER: matched token %d\n", result);
      lexer->result_symbol = result;
      return true;
    }
    
    // Failed to match any special command.
    // Return false WITHOUT advancing - tree-sitter will fall back to normal lexer.
    // The advance we did is undone because we return false and tree-sitter
    // resets to the position we marked with mark_end at the start.
    return false;
  }

  // Check for TeX caret notation: ^^XX (hex) or ^^^^XXXX (hex) or ^^c (char)
  // This is a low-level TeX feature for character input
  if (lexer->lookahead == '^') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '^') {
      lexer->advance(lexer, false);

      // Check for ^^^^ (4 hex digits)
      if (lexer->lookahead == '^') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '^') {
          lexer->advance(lexer, false);
          // Expect 4 hex digits
          int hex_count = 0;
          while (!lexer->eof(lexer) && is_hex_digit(lexer->lookahead) && hex_count < 4) {
            lexer->advance(lexer, false);
            hex_count++;
          }
          if (hex_count == 4) {
            lexer->mark_end(lexer);
            lexer->result_symbol = CARET_CHAR;
            return true;
          }
        }
        // Not valid ^^^^ - fall through
        return false;
      }

      // Check for ^^ followed by 2 hex digits or a character
      if (!lexer->eof(lexer)) {
        // First check for 2 hex digits
        if (is_hex_digit(lexer->lookahead)) {
          lexer->advance(lexer, false);
          if (!lexer->eof(lexer) && is_hex_digit(lexer->lookahead)) {
            // Two hex digits: ^^XX
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            lexer->result_symbol = CARET_CHAR;
            return true;
          }
          // Only one hex digit - treat as ^^c (single char)
          // We already advanced past the char, so just mark and return
          lexer->mark_end(lexer);
          lexer->result_symbol = CARET_CHAR;
          return true;
        }

        // Otherwise, ^^c where c is any character
        // The charcode is c+64 if c<64, or c-64 otherwise
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        lexer->result_symbol = CARET_CHAR;
        return true;
      }
    }
    // Single ^ - not a caret char notation
    return false;
  }

  return false;
}

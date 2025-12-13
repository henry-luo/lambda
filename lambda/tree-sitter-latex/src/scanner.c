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
};

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

// Try to match a string without advancing if it doesn't match
// Returns true if matched (and advances past string), false otherwise (no advance)
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

bool tree_sitter_latex_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  // Check for \begin{document} and \end{document} FIRST - they take precedence
  // Don't skip whitespace - we want to match from current position
  if (valid_symbols[BEGIN_DOCUMENT] && lexer->lookahead == '\\') {
    if (scan_begin_document(lexer)) {
      lexer->result_symbol = BEGIN_DOCUMENT;
      return true;
    }
  }
  
  if (valid_symbols[END_DOCUMENT] && lexer->lookahead == '\\') {
    if (scan_end_document(lexer)) {
      lexer->result_symbol = END_DOCUMENT;
      return true;
    }
  }
  
  // DISABLED: verbatim/comment scanning causes issues with GLR conflicts
  // The scanner can't know if we're actually inside a verbatim environment
  // TODO: Implement proper state tracking to fix this
  /*
  // Skip any leading whitespace for verbatim scanning
  skip_ws(lexer);
  
  if (valid_symbols[VERBATIM_CONTENT]) {
    lexer->result_symbol = VERBATIM_CONTENT;
    return scan_verbatim(lexer);
  }
  
  if (valid_symbols[COMMENT_ENV_CONTENT]) {
    lexer->result_symbol = COMMENT_ENV_CONTENT;
    return scan_comment(lexer);
  }
  */
  
  return false;
}

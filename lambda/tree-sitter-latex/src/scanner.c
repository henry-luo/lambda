#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/parser.h>

enum TokenType {
  TRIVIA_RAW_FI,
  TRIVIA_RAW_ENV_COMMENT,
  TRIVIA_RAW_ENV_VERBATIM,
  TRIVIA_RAW_ENV_LISTING,
  TRIVIA_RAW_ENV_MINTED,
  TRIVIA_RAW_ENV_ASY,         // Used for asy, asydef
  TRIVIA_RAW_ENV_PYCODE,      // Used for pycode, luacode, luacode*
  TRIVIA_RAW_ENV_SAGESILENT,  // Used for sagesilent, sageblock
};

// Special handler for code environments (pycode, luacode, luacode*)
// Scans until \end{pycode}, \end{luacode}, or \end{luacode*}
static bool find_code_env_end(TSLexer *lexer) {
  bool has_marked = false;
  while (true) {
    if (lexer->eof(lexer)) {
      break;
    }

    // Look for \end{
    if (lexer->lookahead != '\\') {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }

    // Found \, check for end{
    lexer->advance(lexer, false);
    if (lexer->eof(lexer) || lexer->lookahead != 'e') {
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }
    lexer->advance(lexer, false);
    if (lexer->eof(lexer) || lexer->lookahead != 'n') {
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }
    lexer->advance(lexer, false);
    if (lexer->eof(lexer) || lexer->lookahead != 'd') {
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }
    lexer->advance(lexer, false);
    if (lexer->eof(lexer) || lexer->lookahead != '{') {
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }
    lexer->advance(lexer, false);

    // Now check for pycode, luacode, or luacode*
    const char *pycode = "pycode}";
    const char *luacode = "luacode";
    bool is_pycode = true;
    bool is_luacode = true;

    // Check for pycode}
    for (size_t i = 0; pycode[i] && is_pycode; i++) {
      if (lexer->eof(lexer) || lexer->lookahead != pycode[i]) {
        is_pycode = false;
      } else {
        lexer->advance(lexer, false);
      }
    }
    if (is_pycode) {
      return has_marked;
    }

    // Check for luacode} or luacode*}
    for (size_t i = 0; luacode[i] && is_luacode; i++) {
      if (lexer->eof(lexer) || lexer->lookahead != luacode[i]) {
        is_luacode = false;
      } else {
        lexer->advance(lexer, false);
      }
    }
    if (is_luacode) {
      // Check for optional * before }
      if (lexer->lookahead == '*') {
        lexer->advance(lexer, false);
      }
      if (lexer->lookahead == '}') {
        return has_marked;
      }
    }

    // Not a valid end, continue searching
    lexer->mark_end(lexer);
    has_marked = true;
  }

  return has_marked;
}

// Special handler for asy environments (asy, asydef)
// Scans until \end{asy} or \end{asydef}
static bool find_asy_env_end(TSLexer *lexer) {
  bool has_marked = false;
  while (true) {
    if (lexer->eof(lexer)) {
      break;
    }

    // Look for \end{
    if (lexer->lookahead != '\\') {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }

    // Found \, check for end{asy
    const char *prefix = "end{asy";
    lexer->advance(lexer, false);
    bool match = true;
    for (size_t i = 0; prefix[i] && match; i++) {
      if (lexer->eof(lexer) || lexer->lookahead != prefix[i]) {
        match = false;
      } else {
        lexer->advance(lexer, false);
      }
    }
    
    if (!match) {
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }

    // Check for "}" or "def}"
    if (lexer->lookahead == '}') {
      return has_marked;
    }
    
    const char *def = "def}";
    bool is_def = true;
    for (size_t i = 0; def[i] && is_def; i++) {
      if (lexer->eof(lexer) || lexer->lookahead != def[i]) {
        is_def = false;
      } else {
        lexer->advance(lexer, false);
      }
    }
    if (is_def) {
      return has_marked;
    }

    // Not a valid end, continue searching
    lexer->mark_end(lexer);
    has_marked = true;
  }

  return has_marked;
}

// Special handler for sage environments (sagesilent, sageblock)
// Scans until \end{sagesilent} or \end{sageblock}
static bool find_sage_env_end(TSLexer *lexer) {
  bool has_marked = false;
  while (true) {
    if (lexer->eof(lexer)) {
      break;
    }

    // Look for \end{
    if (lexer->lookahead != '\\') {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }

    // Found \, check for end{sage
    const char *prefix = "end{sage";
    lexer->advance(lexer, false);
    bool match = true;
    for (size_t i = 0; prefix[i] && match; i++) {
      if (lexer->eof(lexer) || lexer->lookahead != prefix[i]) {
        match = false;
      } else {
        lexer->advance(lexer, false);
      }
    }
    
    if (!match) {
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }

    // Now check for "silent}" or "block}"
    const char *silent = "silent}";
    const char *block = "block}";
    bool is_silent = true;
    bool is_block = true;

    // Try silent}
    for (size_t i = 0; silent[i] && is_silent; i++) {
      if (lexer->eof(lexer) || lexer->lookahead != silent[i]) {
        is_silent = false;
      } else {
        lexer->advance(lexer, false);
      }
    }
    if (is_silent) {
      return has_marked;
    }

    // Try block}
    for (size_t i = 0; block[i] && is_block; i++) {
      if (lexer->eof(lexer) || lexer->lookahead != block[i]) {
        is_block = false;
      } else {
        lexer->advance(lexer, false);
      }
    }
    if (is_block) {
      return has_marked;
    }

    // Not a valid end, continue searching
    lexer->mark_end(lexer);
    has_marked = true;
  }

  return has_marked;
}

static bool find_verbatim(TSLexer *lexer, const char *keyword,
                          bool is_command_name) {
  bool has_marked = false;
  while (true) {
    if (lexer->eof(lexer)) {
      break;
    }

    bool advanced = false;
    bool failed = false;
    for (size_t i = 0; keyword[i]; i++) {
      if (lexer->eof(lexer)) {
        return has_marked;
      }

      if (lexer->lookahead != keyword[i]) {
        failed = true;
        break;
      }

      lexer->advance(lexer, false);
      advanced = true;
    }

    if (failed && !advanced) {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      has_marked = true;
      continue;
    }

    if (!failed) {
      if (is_command_name) {
        if (lexer->eof(lexer)) {
          return has_marked;
        }

        char c = lexer->lookahead;
        switch (c) {
        case ':':
        case '_':
        case '@':
          failed = true;
          break;
        default:
          failed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
          break;
        }

        if (failed) {
          lexer->mark_end(lexer);
          has_marked = true;
          continue;
        }
      }

      return has_marked;
    }
  }

  return has_marked;
}

void *tree_sitter_latex_external_scanner_create() { return NULL; }

void tree_sitter_latex_external_scanner_destroy(void *payload) {}

unsigned tree_sitter_latex_external_scanner_serialize(void *payload,
                                                      char *buffer) {
  return 0;
}

void tree_sitter_latex_external_scanner_deserialize(void *payload,
                                                    const char *buffer,
                                                    unsigned length) {}

bool tree_sitter_latex_external_scanner_scan(void *payload, TSLexer *lexer,
                                             const bool *valid_symbols) {
  bool found = false;
  TSSymbol type = 0xFFFF;
  for (int i = 0; i <= TRIVIA_RAW_ENV_SAGESILENT; i++) {
    if (valid_symbols[i]) {
      if (found) {
        return false;
      } else {
        found = true;
        type = i;
      }
    }
  }

  lexer->result_symbol = type;
  switch (type) {
  case TRIVIA_RAW_FI:
    return find_verbatim(lexer, "\\fi", true);
  case TRIVIA_RAW_ENV_COMMENT:
    return find_verbatim(lexer, "\\end{comment}", false);
  case TRIVIA_RAW_ENV_VERBATIM:
    return find_verbatim(lexer, "\\end{verbatim}", false);
  case TRIVIA_RAW_ENV_LISTING:
    return find_verbatim(lexer, "\\end{lstlisting}", false);
  case TRIVIA_RAW_ENV_MINTED:
    return find_verbatim(lexer, "\\end{minted}", false);
  case TRIVIA_RAW_ENV_ASY:
    // Handles asy and asydef
    return find_asy_env_end(lexer);
  case TRIVIA_RAW_ENV_PYCODE:
    // Handles pycode, luacode, luacode*
    return find_code_env_end(lexer);
  case TRIVIA_RAW_ENV_SAGESILENT:
    // Handles sagesilent and sageblock
    return find_sage_env_end(lexer);
  }

  return false;
}

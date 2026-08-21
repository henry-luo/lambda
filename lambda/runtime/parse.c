#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <tree_sitter/api.h>

// Keep the first-party parser entry points in the same archive member as the
// existing Lambda parser factory. Static-link extraction is member-based: the
// AST builder and the RD/Pratt implementation are otherwise invisible to the
// linker when both symbols live only inside lambda-rt-cpp.a.
#include "parser/lambda_lexer.c"
#include "parser/lambda_parser.c"

#ifndef LAMBDA_NO_TREE_SITTER_LAMBDA
// the `tree_sitter_lambda` function, implemented by the `tree-sitter-lambda` library.
const TSLanguage *tree_sitter_lambda(void);
#endif

TSParser * lambda_parser(void) {
#ifdef LAMBDA_NO_TREE_SITTER_LAMBDA
  // release builds use the RD/Pratt frontend; no Lambda grammar archive is linked.
  return NULL;
#else
  TSParser *parser = ts_parser_new();
  // set the parser's language
  ts_parser_set_language(parser, tree_sitter_lambda());
  return parser;  
#endif
}

TSTree* lambda_parse_source(TSParser* parser, const char* source_code) {
#ifdef LAMBDA_NO_TREE_SITTER_LAMBDA
  (void)parser;
  (void)source_code;
  return NULL;
#else
  if (!parser || !source_code) return NULL;
  TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
  return tree;
#endif
}


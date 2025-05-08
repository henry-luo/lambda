#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <tree_sitter/api.h>

// the `tree_sitter_lambda` function, implemented by the `tree-sitter-lambda` library.
const TSLanguage *tree_sitter_lambda(void);

TSParser * lambda_parser(void) {
  TSParser *parser = ts_parser_new();
  // set the parser's language
  ts_parser_set_language(parser, tree_sitter_lambda());
  return parser;  
}

TSTree* lambda_parse_source(TSParser* parser, const char* source_code) {
  TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
  return tree;
}




#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <tree_sitter/api.h>

// Declare the `tree_sitter_lambda` function, which is
// implemented by the `tree-sitter-lambda` library.
const TSLanguage *tree_sitter_lambda(void);

TSParser * lambda_parser(void) {
  // Create a parser.
  TSParser *parser = ts_parser_new();
  // Set the parser's language
  ts_parser_set_language(parser, tree_sitter_lambda());
  return parser;  
}

TSTree* lambda_parse_source(TSParser* parser, const char* source_code) {
  // Build a syntax tree based on source code stored in a string.
  // const char *source_code = "[1, true, null]";
  TSTree* tree = ts_parser_parse_string(
    parser, NULL, source_code, strlen(source_code)
  );

  // ts_tree_delete(tree);
  // ts_parser_delete(parser);
  return tree;
}

char* lambda_print_tree(TSTree* tree) {
  TSNode root_node = ts_tree_root_node(tree);
  char *string = ts_node_string(root_node);
  return string;
}

/* compile:
 clang                                   \
   -I tree-sitter/lib/include            \
   test-lambda-parser.c                  \
   tree-sitter-lambda/src/parser.c       \
   tree-sitter/libtree-sitter.a          \
   -o test-lambda-parser
 ./test-lambda-parser
*/

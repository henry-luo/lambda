#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
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

// print the syntax tree as an s-expr
void print_ts_node(const char *source, TSNode node, uint32_t indent) {
  for (uint32_t i = 0; i < indent; i++) {
      printf("  ");  // 2 spaces per indent level
  }
  const char *type = ts_node_type(node);
  if (isalpha(*type)) {
      printf("(%s", type);
  } else if (*type == '\'') {
      printf("(\"%s\"", type);
  } else { // special char
      printf("('%s'", type);
  }

  uint32_t child_count = ts_node_child_count(node);
  if (child_count > 0) {
      printf("\n");
      for (uint32_t i = 0; i < child_count; i++) {
          TSNode child = ts_node_child(node, i);
          print_ts_node(source, child, indent + 1);
      }
      for (uint32_t i = 0; i < indent; i++) {
          printf("  ");
      }
  }
  else {
    int start_byte = ts_node_start_byte(node);
    int end_byte = ts_node_end_byte(node);
    const char* start = source + start_byte;
    printf(" '%.*s'", end_byte - start_byte, start);
  }
  printf(")\n");
}

// char* lambda_print_tree(TSTree* tree) {
//   TSNode root_node = ts_tree_root_node(tree);
//   char *string = ts_node_string(root_node);
//   return string;
// }


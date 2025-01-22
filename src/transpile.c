#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
#include "../lib/string_buffer/string_buffer.h"

TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
char* lambda_print_tree(TSTree* tree);

int main(void) {
    printf("Starting transpiler...\n");

    // Create a parser.
    TSParser* parser = lambda_parser();

    const char* source_code = "[1, true, null]";
    TSTree* tree = lambda_parse_source(parser, source_code);

    // Print the syntax tree as an S-expression.
    char *string = lambda_print_tree(tree);
    printf("Syntax tree: %s\n", string);

    // transpile the AST 
    StrBuf* myBuff = strbuf_new(100);
    strbuf_append_str(myBuff, "Hello, ");
    strbuf_sprintf(myBuff, "%s %i", "world", 42);
    printf("%s\n", myBuff->b);

    // Free all of the heap-allocated memory.
    free(string);
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return 0;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tree_sitter/api.h>
#include "../lib/string_buffer/string_buffer.h"

// Function to read and display the content of a text file
 StrBuf* readTextFile(const char *filename) {
    FILE *file = fopen(filename, "r"); // open the file in read mode
    if (file == NULL) { // handle error when file cannot be opened
        perror("Error opening file"); 
        return NULL;
    }

    fseek(file, 0, SEEK_END);  // move the file pointer to the end to determine file size
    long fileSize = ftell(file);
    rewind(file); // reset file pointer to the beginning

    StrBuf* buf = strbuf_new(fileSize + 1);
    if (buf == NULL) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // read the file content into the buffer
    size_t bytesRead = fread(buf->b, 1, fileSize, file);
    buf->b[bytesRead] = '\0'; // Null-terminate the buffer
    printf("%s\n", buf->b); // Print the file content

    // clean up
    fclose(file);
    return buf;
}

TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
char* lambda_print_tree(TSTree* tree);

// https://www.gnu.org/software/emacs/manual/html_node/elisp/Tree_002dsitter-C-API.html

void transpile_fn(StrBuf* codeBuf, TSNode fn_node, const char* source) {
    // get the function name
    TSNode fn_name_node = ts_node_child_by_field_name(fn_node, "name", 4);
    strbuf_append_str(codeBuf, "void ");
    int start_byte = ts_node_start_byte(fn_name_node);
    strbuf_append_strn(codeBuf, source + start_byte, ts_node_end_byte(fn_name_node) - start_byte);
    strbuf_append_str(codeBuf, " (){\n");
   
    // get the function body
    TSNode fn_body_node = ts_node_named_child(fn_node, 1);
    printf("body %s\n", ts_node_type(fn_body_node));
    
    strbuf_append_str(codeBuf, "}\n");
}

int main(void) {
    printf("Starting transpiler...\n");

    // Create a parser.
    TSParser* parser = lambda_parser();

    StrBuf* buf = readTextFile("hello-world.ls");
    const char* source_code = buf->b;
    TSTree* tree = lambda_parse_source(parser, source_code);

    // Print the syntax tree as an S-expression.
    char *string = lambda_print_tree(tree);
    printf("Syntax tree: %s\n", string);
    free(string);

    // transpile the AST 
    StrBuf* codeBuf = strbuf_new(1024);
    TSNode root_node = ts_tree_root_node(tree);
    assert(strcmp(ts_node_type(root_node), "document") == 0);
    TSNode main_node = ts_node_named_child(root_node, 0);
    char* main_node_type = ts_node_type(main_node);
    printf("main node: %s\n", main_node_type);

    if (strcmp(main_node_type, "fn")) {
        transpile_fn(codeBuf, main_node, source_code);
    }

    printf("transpiled code: %s\n", codeBuf->b);

    // clean up
    strbuf_free(codeBuf);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 0;
}

/*
todo:
1. from AST construct const table;
2. gen function fn_name(const_table) -> code;
3. able to compile and run the generated code;

- to add unit test for Lambda grammar;
*/
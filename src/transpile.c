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

    StrBuf* buf = strbuf_new_cap(fileSize + 1);
    if (buf == NULL) {
        perror("Memory allocation failed");
        fclose(file);
        return NULL;
    }

    // read the file content into the buffer
    size_t bytesRead = fread(buf->b, 1, fileSize, file);
    buf->b[bytesRead] = '\0'; // Null-terminate the buffer

    // clean up
    fclose(file);
    return buf;
}

void writeTextFile(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w"); // Open the file in write mode
    if (file == NULL) {
        perror("Error opening file"); // Handle error if file cannot be opened
        return;
    }
    // Write the string to the file
    if (fprintf(file, "%s", content) < 0) {
        perror("Error writing to file");
    }
    fclose(file); // Close the file
}

TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
char* lambda_print_tree(TSTree* tree);

typedef struct {
    StrBuf* code_buf;
    const char* source;

    TSSymbol SYM_IF_EXPR;
    TSSymbol SYM_LET_EXPR;
    TSSymbol SYM_ASSIGNMENT_EXPR;
    TSSymbol SYM_BINARY_EXPR;
    TSSymbol SYM_PRIMARY_EXPR;
    TSFieldId ID_COND;
    TSFieldId ID_THEN;
    TSFieldId ID_ELSE;
    TSFieldId ID_LEFT;
    TSFieldId ID_RIGHT;

    enum TP_PHASE {
        DECLARE,
        EVALUATE,
    } phase;
} Transpiler;

void transpile_expr(Transpiler* tp, TSNode expr_node);

void writeNodeSource(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    char* start = tp->source + start_byte;
    strbuf_append_strn(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

void transpile_primary_expr(Transpiler* tp, TSNode pri_node) {
    printf("transpile primary expr\n");
    writeNodeSource(tp, pri_node);
}

void transpile_binary_expr(Transpiler* tp, TSNode bi_node) {
    printf("transpile binary expr\n");
    strbuf_append_char(tp->code_buf, '(');
    TSNode left_node = ts_node_child_by_field_id(bi_node, tp->ID_LEFT);
    transpile_expr(tp, left_node);

    TSNode op_node = ts_node_child_by_field_name(bi_node, "operator", 8);
    writeNodeSource(tp, op_node);

    TSNode right_node = ts_node_child_by_field_id(bi_node, tp->ID_RIGHT);
    transpile_expr(tp, right_node);
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_if_expr(Transpiler* tp, TSNode if_node) {
    // transpile as C conditional expr
    TSNode cond_node = ts_node_child_by_field_id(if_node, tp->ID_COND);
    strbuf_append_str(tp->code_buf, "(");
    transpile_expr(tp, cond_node);
    strbuf_append_str(tp->code_buf, ")?(");
    TSNode then_node = ts_node_child_by_field_id(if_node, tp->ID_THEN);
    transpile_expr(tp, then_node);
    strbuf_append_str(tp->code_buf, "):(");
    TSNode else_node = ts_node_child_by_field_id(if_node, tp->ID_ELSE);
    transpile_expr(tp, else_node);
    strbuf_append_str(tp->code_buf, ")");
}

void transpile_assignment_expr(Transpiler* tp, TSNode asn_node) {
    printf("transpile assignment expr\n");
    // declare the variable
    strbuf_append_str(tp->code_buf, "int ");
    TSNode id_node = ts_node_named_child(asn_node, 0);
    if (!ts_node_is_null(id_node)) writeNodeSource(tp, id_node);
    else { printf("no identifier found\n"); return; }

    strbuf_append_char(tp->code_buf, '=');
    TSNode val_node = ts_node_named_child(asn_node, 1);
    if (!ts_node_is_null(val_node)) transpile_expr(tp, val_node);
    else { printf("no value found\n"); return; }
    strbuf_append_char(tp->code_buf, ';');
}

void transpile_let_expr(Transpiler* tp, TSNode let_node) {
    printf("transpile let expr\n");
    if (tp->phase == DECLARE) {
        // declare the variable
        TSNode cond_node = ts_node_child_by_field_id(let_node, tp->ID_COND);
        transpile_expr(tp, cond_node);
    }
    else if (tp->phase == EVALUATE) {
        // evaluate the expression
        TSNode then_node = ts_node_child_by_field_id(let_node, tp->ID_THEN);
        transpile_expr(tp, then_node);
    }
    else {
        printf("unknown phase\n");
    }
}

void transpile_expr(Transpiler* tp, TSNode expr_node) {
    // get the function name
    TSSymbol symbol = ts_node_symbol(expr_node);
    if (symbol == tp->SYM_IF_EXPR) {
        transpile_if_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_BINARY_EXPR) {
        transpile_binary_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_PRIMARY_EXPR) {
        transpile_primary_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_LET_EXPR) {
        transpile_let_expr(tp, expr_node);
    }
    else if (symbol == tp->SYM_ASSIGNMENT_EXPR) {
        transpile_assignment_expr(tp, expr_node);
    }
    else {
        printf("unknown expr %s\n", ts_node_type(expr_node));
        strbuf_append_str(tp->code_buf, ts_node_type(expr_node));
    }
}

void transpile_fn(Transpiler* tp, TSNode fn_node) {
    // get the function name
    
    TSNode fn_name_node = ts_node_child_by_field_name(fn_node, "name", 4);
    strbuf_append_str(tp->code_buf, "int ");
    writeNodeSource(tp, fn_name_node);
    strbuf_append_str(tp->code_buf, " (){");
   
    // get the function body
    TSNode fn_body_node = ts_node_named_child(fn_node, 1);
    tp->phase = DECLARE;
    transpile_expr(tp, fn_body_node);
    
    tp->phase = EVALUATE;
    strbuf_append_str(tp->code_buf, "char* ret=");
    transpile_expr(tp, fn_body_node);
    strbuf_append_str(tp->code_buf, "; printf(\"%s\\n\",ret); return 0;}\n");
}

int main(void) {
    Transpiler tp;

    printf("Starting transpiler...\n");

    // Create a parser.
    const TSParser* parser = lambda_parser();
    if (parser == NULL) {
        return 1;
    }
    StrBuf* buf = readTextFile("hello-world.ls");
    printf("%s\n", buf->b); // Print the file content
    tp.source = buf->b;
    TSTree* tree = lambda_parse_source(parser, tp.source);
    tp.SYM_IF_EXPR = ts_language_symbol_for_name(ts_tree_language(tree), "if_expr", 7, true);
    tp.SYM_LET_EXPR = ts_language_symbol_for_name(ts_tree_language(tree), "let_expr", 8, true);
    tp.SYM_ASSIGNMENT_EXPR = ts_language_symbol_for_name(ts_tree_language(tree), "assignment_expr", 15, true);
    tp.SYM_PRIMARY_EXPR = ts_language_symbol_for_name(ts_tree_language(tree), "primary_expr", 12, true);
    tp.SYM_BINARY_EXPR = ts_language_symbol_for_name(ts_tree_language(tree), "binary_expr", 11, true);
    tp.ID_COND = ts_language_field_id_for_name(ts_tree_language(tree), "cond", 4);
    tp.ID_THEN = ts_language_field_id_for_name(ts_tree_language(tree), "then", 4);
    tp.ID_ELSE = ts_language_field_id_for_name(ts_tree_language(tree), "else", 4);
    tp.ID_LEFT = ts_language_field_id_for_name(ts_tree_language(tree), "left", 4);
    tp.ID_RIGHT = ts_language_field_id_for_name(ts_tree_language(tree), "right", 5);

    // Print the syntax tree as an S-expression.
    char *string = lambda_print_tree(tree);
    printf("Syntax tree: %s\n", string);
    free(string);

    // transpile the AST 
    tp.code_buf = strbuf_new_cap(1024);
    TSNode root_node = ts_tree_root_node(tree);
    assert(strcmp(ts_node_type(root_node), "document") == 0);
    TSNode main_node = ts_node_named_child(root_node, 0);
    char* main_node_type = ts_node_type(main_node);
    printf("main node: %s\n", main_node_type);
    strbuf_append_str(tp.code_buf, "#include <stdio.h>\n");

    if (strcmp(main_node_type, "fn")) {
        transpile_fn(&tp, main_node);
    }

    printf("transpiled code: %s\n", tp.code_buf->b);
    writeTextFile("hello-world.c", tp.code_buf->b);

    // clean up
    strbuf_free(buf);
    strbuf_free(tp.code_buf);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 0;
}

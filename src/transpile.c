
#include "transpiler.h"

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
    size_t bytesRead = fread(buf->str, 1, fileSize, file);
    buf->str[bytesRead] = '\0'; // Null-terminate the buffer

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

void transpile_expr(Transpiler* tp, TSNode expr_node);

void writeNodeSource(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
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
    
    TSNode id_node = ts_node_child_by_field_id(asn_node, tp->ID_NAME);
    if (ts_node_is_null(id_node)) { printf("no identifier found\n"); return; }

    TSNode val_node = ts_node_child_by_field_id(asn_node, tp->ID_BODY);
    if (ts_node_is_null(val_node)) { printf("no value found\n"); return; }

    // declare the type
    LambdaTypeId type_id = infer_expr(tp, val_node).type;
    printf("assigned type id: %d\n", type_id);
    switch (type_id) {
    case LMD_TYPE_NULL:
        strbuf_append_str(tp->code_buf, "void*");
        break;
    case LMD_TYPE_INT:
        strbuf_append_str(tp->code_buf, "long");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(tp->code_buf, "double");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(tp->code_buf, "char*");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "bool");
        break;
    case LMD_TYPE_ARRAY:
        strbuf_append_str(tp->code_buf, "Item*");
        break;
    default:
        printf("unknown type %d\n", type_id);
    }
    strbuf_append_char(tp->code_buf, ' ');
    // declare the variable
    writeNodeSource(tp, id_node);
    strbuf_append_char(tp->code_buf, '=');

    transpile_expr(tp, val_node);
    strbuf_append_char(tp->code_buf, ';');
}

void transpile_let_expr(Transpiler* tp, TSNode let_node) {
    printf("transpile let expr\n");
    if (tp->phase == DECLARE) {
        // let can have multiple cond declarations
        TSTreeCursor cursor = ts_tree_cursor_new(let_node);
        bool has_node = ts_tree_cursor_goto_first_child(&cursor);
        while (has_node) {
            // Check if the current node's field ID matches the target field ID
            TSSymbol field_id = ts_tree_cursor_current_field_id(&cursor);
            if (field_id == tp->ID_COND) {
                TSNode child = ts_tree_cursor_current_node(&cursor);
                transpile_expr(tp, child);
            }
            has_node = ts_tree_cursor_goto_next_sibling(&cursor);
        }
        ts_tree_cursor_delete(&cursor);
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
        // strbuf_append_str(tp->code_buf, ts_node_type(expr_node));
    }
}

void transpile_fn(Transpiler* tp, TSNode fn_node) {
    // get the function name
    
    TSNode fn_name_node = ts_node_child_by_field_name(fn_node, "name", 4);
    strbuf_append_str(tp->code_buf, "int ");
    writeNodeSource(tp, fn_name_node);
    strbuf_append_str(tp->code_buf, " (){");
   
    // get the function body
    TSNode fn_body_node = ts_node_child_by_field_name(fn_node, "body", 4);
    tp->phase = DECLARE;
    transpile_expr(tp, fn_body_node);
    
    tp->phase = EVALUATE;
    strbuf_append_str(tp->code_buf, "char* ret=");
    transpile_expr(tp, fn_body_node);
    strbuf_append_str(tp->code_buf, "; printf(\"%s\\n\",ret); return 0;}\n");
}

int main(void) {
    Transpiler tp;
    memset(&tp, 0, sizeof(Transpiler));

    printf("Starting transpiler...\n");

    // Create a parser.
    const TSParser* parser = lambda_parser();
    if (parser == NULL) { return 1; }

    StrBuf* buf = readTextFile("hello-world.ls");
    printf("%s\n", buf->str); // Print the file content
    tp.source = buf->str;
    TSTree* tree = lambda_parse_source(parser, tp.source);
    if (tree == NULL) {
        printf("Error: Failed to parse the source code.\n");
        strbuf_free(buf);  ts_parser_delete(parser);
        return 1;
    }

    tp.SYM_NULL = ts_language_symbol_for_name(ts_tree_language(tree), "null", 4, true);
    tp.SYM_TRUE = ts_language_symbol_for_name(ts_tree_language(tree), "true", 4, true);
    tp.SYM_FALSE = ts_language_symbol_for_name(ts_tree_language(tree), "false", 5, true);
    tp.SYM_NUMBER = ts_language_symbol_for_name(ts_tree_language(tree), "number", 6, true);
    tp.SYM_STRING = ts_language_symbol_for_name(ts_tree_language(tree), "string", 6, true);
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
    tp.ID_NAME = ts_language_field_id_for_name(ts_tree_language(tree), "name", 4);
    tp.ID_BODY = ts_language_field_id_for_name(ts_tree_language(tree), "body", 4);

    // Print the syntax tree as an S-expression.
    char *string = lambda_print_tree(tree);
    printf("Syntax tree: %s\n", string);
    free(string);

    // transpile the AST
    TSNode root_node = ts_tree_root_node(tree);
    if (ts_node_is_null(root_node) || strcmp(ts_node_type(root_node), "document") != 0) {
        printf("Error: The tree has no valid root node.\n");
        strbuf_free(buf);  ts_parser_delete(parser);
        ts_tree_delete(tree);
        return 1;
    }

    tp.code_buf = strbuf_new_cap(1024);
    TSNode main_node = ts_node_named_child(root_node, 0);
    char* main_node_type = ts_node_type(main_node);
    printf("main node: %s\n", main_node_type);
    strbuf_append_str(tp.code_buf, "#include <stdio.h>\n#include <stdbool.h>\n#define null 0\n"
        "typedef void* Item;\n");

    if (strcmp(main_node_type, "fn_definition") == 0) {
        transpile_fn(&tp, main_node);
        printf("transpiled code: %s\n", tp.code_buf->str);
        writeTextFile("hello-world.c", tp.code_buf->str);        
    }
    else {
        printf("Error: main node is not a function.\n");
    }

    // clean up
    strbuf_free(buf);
    strbuf_free(tp.code_buf);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 0;
}
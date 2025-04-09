
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

void transpile_expr(Transpiler* tp, AstNode *expr_node);

void writeNodeSource(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

void writeType(Transpiler* tp, LambdaTypeId type_id) {
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
}

/*
void transpile_ts_node(Transpiler* tp, TSNode node) {
    TSSymbol symbol = ts_node_symbol(node);
    if (symbol == tp->SYM_TRUE) {
        strbuf_append_str(tp->code_buf, "true");
    }
    else if (symbol == tp->SYM_FALSE) {
        strbuf_append_str(tp->code_buf, "false");
    }
    else if (symbol == tp->SYM_NUMBER) {
        writeNodeSource(tp, node);
    }
    else if (symbol == tp->SYM_STRING) {
        writeNodeSource(tp, node);
    }
    else {
        printf("unknown node type %s\n", ts_node_type(node));
        // strbuf_append_str(tp->code_buf, ts_node_type(node));
    }
}
*/

void transpile_primary_expr(Transpiler* tp, AstNode *pri_node) {
    printf("transpile primary expr\n");
    writeNodeSource(tp, pri_node->node);
}

void transpile_binary_expr(Transpiler* tp, AstBinaryNode *bi_node) {
    printf("transpile binary expr\n");
    strbuf_append_char(tp->code_buf, '(');
    transpile_expr(tp, bi_node->left);

    TSNode op_node = ts_node_child_by_field_name(bi_node->node, "operator", 8);
    writeNodeSource(tp, op_node);

    transpile_expr(tp, bi_node->right);
    strbuf_append_char(tp->code_buf, ')');
}

void transpile_if_expr(Transpiler* tp, AstIfExprNode *if_node) {
    // transpile as C conditional expr
    strbuf_append_str(tp->code_buf, "(");
    transpile_expr(tp, if_node->cond);
    strbuf_append_str(tp->code_buf, ")?(");
    transpile_expr(tp, if_node->then);
    strbuf_append_str(tp->code_buf, "):(");
    transpile_expr(tp, if_node->otherwise);
    strbuf_append_str(tp->code_buf, ")");
}

void transpile_assign_expr(Transpiler* tp, AstAssignNode *asn_node) {
    // declare the type
    LambdaTypeId type_id = asn_node->expr->type.type;
    printf("assigned type id: %d\n", type_id);
    writeType(tp, type_id);
    strbuf_append_char(tp->code_buf, ' ');
    // declare the variable
    strbuf_append_str_n(tp->code_buf, asn_node->name.str, asn_node->name.length);
    strbuf_append_char(tp->code_buf, '=');

    transpile_expr(tp, asn_node->expr);
    strbuf_append_str(tp->code_buf, ";\n");
}

void transpile_let_expr(Transpiler* tp, AstLetNode *let_node) {
    printf("transpile let expr\n");
    if (tp->phase == TP_DECLARE) {
        AstNode *declare = let_node->declare;
        while (declare) {
            printf("transpile let declare\n");
            transpile_expr(tp, declare);
            declare = declare->next;
        }
    }
    else if (tp->phase == TP_COMPOSE) {
        printf("transpile let then\n");
        transpile_expr(tp, let_node->then);
    }
}

void transpile_expr(Transpiler* tp, AstNode *expr_node) {
    if (!expr_node) {
        printf("missing expression node\n");  return;
    }
    // get the function name
    switch (expr_node->node_type) {
    case AST_NODE_IF_EXPR:
        transpile_if_expr(tp, (AstIfExprNode*)expr_node);
        break;
    case AST_NODE_BINARY:
        transpile_binary_expr(tp, (AstBinaryNode*)expr_node);
        break;
    case AST_NODE_PRIMARY:
        transpile_primary_expr(tp, expr_node);
        break;
    case AST_NODE_LET_EXPR:
        transpile_let_expr(tp, (AstLetNode*)expr_node);
        break;
    case AST_NODE_ASSIGN:
        transpile_assign_expr(tp, (AstAssignNode*)expr_node);
        break;
    default:
        printf("unknown expression type\n");
        break;
    }
}

void transpile_fn(Transpiler* tp, AstFuncNode *fn_node) {
    // use function body type as the return type for the time being
    LambdaTypeId ret_type = fn_node->body->type.type;
    writeType(tp, ret_type);
    // write the function name, with a prefix '_', so as to diff from built-in functions
    strbuf_append_str(tp->code_buf, " _");
    writeNodeSource(tp, fn_node->name);
    strbuf_append_str(tp->code_buf, " (){\n");
   
    // get the function body
    tp->phase = TP_DECLARE;
    transpile_expr(tp, fn_node->body);
    
    tp->phase = TP_COMPOSE;
    writeType(tp, ret_type);
    strbuf_append_str(tp->code_buf, " ret=");
    transpile_expr(tp, fn_node->body);
    strbuf_append_str(tp->code_buf, ";\nreturn ret;\n}\n");
}

void transpile_script(Transpiler* tp, AstScript *script) {
    strbuf_append_str(tp->code_buf, "#include <stdio.h>\n#include <stdbool.h>\n#define null 0\n"
        "typedef void* Item;\n");

    AstNode *node = script->child;
    tp->phase = TP_DECLARE;
    while (node) {
        if (node->node_type == AST_NODE_LET_STAM) {
            transpile_let_expr(tp, (AstLetNode*)node);
        }
        node = node->next;
    }
    tp->phase = TP_COMPOSE;
    node = script->child;
    while (node) {
        if (node->node_type == AST_NODE_FUNC) {
            transpile_fn(tp, (AstFuncNode*)node);
        }
        node = node->next;
    }    
    strbuf_append_str(tp->code_buf, "int main() {void* ret=_main(); printf(\"%s\\n\", (char*)ret); return 0;}\n");

    printf("transpiled code:\n----------------\n%s\n", tp->code_buf->str);
    writeTextFile("hello-world.c", tp->code_buf->str);  
}

int main(void) {
    Transpiler tp;
    memset(&tp, 0, sizeof(Transpiler));

    printf("Starting transpiler...\n");

    // create a parser.
    const TSParser* parser = lambda_parser();
    if (parser == NULL) { return 1; }
    // read the source and parse it
    StrBuf* source_buf = readTextFile("hello-world.ls");
    // printf("%s\n", buf->str); // print the file content
    tp.source = source_buf->str;
    TSTree* tree = lambda_parse_source(parser, tp.source);
    if (tree == NULL) {
        printf("Error: Failed to parse the source code.\n");
        strbuf_free(source_buf);  ts_parser_delete(parser);
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
    tp.SYM_FUNC = ts_language_symbol_for_name(ts_tree_language(tree), "fn_definition", 13, true);
    tp.SYM_LET_STAM = ts_language_symbol_for_name(ts_tree_language(tree), "let_stam", 8, true);
    tp.SYM_IDENTIFIER = ts_language_symbol_for_name(ts_tree_language(tree), "identifier", 10, true);

    tp.ID_COND = ts_language_field_id_for_name(ts_tree_language(tree), "cond", 4);
    tp.ID_THEN = ts_language_field_id_for_name(ts_tree_language(tree), "then", 4);
    tp.ID_ELSE = ts_language_field_id_for_name(ts_tree_language(tree), "else", 4);
    tp.ID_LEFT = ts_language_field_id_for_name(ts_tree_language(tree), "left", 4);
    tp.ID_RIGHT = ts_language_field_id_for_name(ts_tree_language(tree), "right", 5);
    tp.ID_NAME = ts_language_field_id_for_name(ts_tree_language(tree), "name", 4);
    tp.ID_BODY = ts_language_field_id_for_name(ts_tree_language(tree), "body", 4);
    tp.ID_DECLARE = ts_language_field_id_for_name(ts_tree_language(tree), "declare", 7);

    // print the syntax tree as an S-expression.
    char *string = lambda_print_tree(tree);
    printf("Syntax tree: %s\n", string);
    free(string);
    // todo: verify the source tree, report errors if any
    // we'll transpile functions without error, and ignore the rest

    // build the AST from the syntax tree
    size_t grow_size = 4096;  // 4k
    size_t tolerance_percent = 20;
    printf("init view pool\n");
    if (MEM_POOL_ERR_OK != pool_variable_init(&tp.ast_node_pool, grow_size, tolerance_percent)) {
        printf("Failed to initialize AST node pool\n");  return 1;
    }

    TSNode root_node = ts_tree_root_node(tree);
    if (ts_node_is_null(root_node) || strcmp(ts_node_type(root_node), "document") != 0) {
        printf("Error: The tree has no valid root node.\n");
        strbuf_free(source_buf);  ts_parser_delete(parser);
        ts_tree_delete(tree);
        return 1;
    }
    // build the AST
    tp.ast_root = build_script(&tp, root_node);
    // print the AST for debugging
    print_ast_node(tp.ast_root, 0);

    // transpile the AST to C code
    printf("transpiling...\n");
    tp.code_buf = strbuf_new_cap(1024);
    transpile_script(&tp, (AstScript*)tp.ast_root);

    // clean up
    strbuf_free(source_buf);
    strbuf_free(tp.code_buf);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 0;
}
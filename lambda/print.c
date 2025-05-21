
#include "transpiler.h"

extern TypeInfo type_info[];

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
    else if (isalpha(*type)) {
      int start_byte = ts_node_start_byte(node);
      int end_byte = ts_node_end_byte(node);
      const char* start = source + start_byte;
      printf(" '%.*s'", end_byte - start_byte, start);
    }
    printf(")\n");
}

void writeNodeSource(Transpiler* tp, TSNode node) {
    int start_byte = ts_node_start_byte(node);
    const char* start = tp->source + start_byte;
    strbuf_append_str_n(tp->code_buf, start, ts_node_end_byte(node) - start_byte);
}

// write the native C type for the lambda type
void writeType(Transpiler* tp, LambdaType *type) {
    TypeId type_id = type->type_id;
    switch (type_id) {
    case LMD_TYPE_NULL:
        strbuf_append_str(tp->code_buf, "void*");
        break;
    case LMD_TYPE_ANY:
        strbuf_append_str(tp->code_buf, "Item");
        break;
    case LMD_TYPE_ERROR:
        strbuf_append_str(tp->code_buf, "Item");
        break;        
    case LMD_TYPE_BOOL:
        strbuf_append_str(tp->code_buf, "bool");
        break;
    case LMD_TYPE_IMP_INT:  case LMD_TYPE_INT:
        strbuf_append_str(tp->code_buf, "long");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(tp->code_buf, "double");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(tp->code_buf, "char*");
        break;
    case LMD_TYPE_BINARY:
        strbuf_append_str(tp->code_buf, "uint8_t*");
        break;
    case LMD_TYPE_ARRAY:
        LambdaTypeArray *array_type = (LambdaTypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_IMP_INT) {
            strbuf_append_str(tp->code_buf, "ArrayLong*");
        } else {
            strbuf_append_str(tp->code_buf, "Array*");
        }
        break;
    case LMD_TYPE_MAP:
        strbuf_append_str(tp->code_buf, "Map*");
        break;
    case LMD_TYPE_FUNC:
        strbuf_append_str(tp->code_buf, "Function*");
        break;
    case LMD_TYPE_TYPE:
        strbuf_append_str(tp->code_buf, "LambdaType*");
        break;
    default:
        printf("unknown type %d\n", type_id);
    }
}

void print_item(StrBuf *strbuf, Item item) {
    LambdaItem ld_item = {.item = item};
    if (ld_item.type_id) { // packed value
        TypeId type_id = ld_item.type_id;
        if (type_id == LMD_TYPE_NULL) {
            strbuf_append_str(strbuf, "null");
        } 
        else if (type_id == LMD_TYPE_BOOL) {
            strbuf_append_str(strbuf, ld_item.bool_val ? "true" : "false");
        }
        else if (type_id == LMD_TYPE_IMP_INT) {
            int int_val = (int32_t)ld_item.long_val;
            strbuf_append_format(strbuf, "%d", int_val);
        }
        else if (type_id == LMD_TYPE_INT) {
            long long_val = *(long*)ld_item.pointer;
            strbuf_append_format(strbuf, "%ld", long_val);
        }
        else if (type_id == LMD_TYPE_FLOAT) {
            double num = *(double*)ld_item.pointer;
            int exponent;
            double mantissa = frexp(num, &exponent);
            if (-20 < exponent && exponent < 30) {
                printf("num f: %.10f, g: %g, exponent: %d\n", num, num, exponent);
                strbuf_append_format(strbuf, "%.10f", num);
                // trim trailing zeros
                char *end = strbuf->str + strbuf->length - 1;
                while (*end == '0' && end > strbuf->str) { *end-- = '\0'; }
                // if it ends with a dot, remove that too
                if (*end == '.') { *end-- = '\0'; }
                strbuf->length = end - strbuf->str + 1;
            }
            else if (-30 < exponent && exponent <= -20) {
                printf("num g: %g, exp: %d\n", num, exponent);
                strbuf_append_format(strbuf, "%.g", num);
                // remove the zero in exponent, like 'e07'
                char *end = strbuf->str + strbuf->length - 1;
                if (*(end-1) == '0' && *(end-2) == '-' && *(end-3) == 'e') { 
                    *(end-1) = *end;  *end = '\0';
                    strbuf->length = end - strbuf->str; 
                }
            }
            else {
                printf("num g: %g, exponent: %d\n", num, exponent);
                strbuf_append_format(strbuf, "%g", num);
            }
        }
        else if (type_id == LMD_TYPE_STRING) {
            String *string = (String*)ld_item.pointer;
            // todo: escape the string
            strbuf_append_format(strbuf, "\"%s\"", string->str);
        }
        else if (type_id == LMD_TYPE_SYMBOL) {
            String *string = (String*)ld_item.pointer;
            // todo: escape the symbol chars
            strbuf_append_format(strbuf, "'%s'", string->str);
        } 
        else if (type_id == LMD_TYPE_DTIME) {
            String *string = (String*)ld_item.pointer;
            strbuf_append_format(strbuf, "t'%s'", string->str);
        }
        else if (type_id == LMD_TYPE_BINARY) {
            String *string = (String*)ld_item.pointer;
            strbuf_append_format(strbuf, "b'%s'", string->str);
        }
        else if (type_id == LMD_TYPE_ERROR) {
            strbuf_append_str(strbuf, "ERROR");
        }
        else {
            strbuf_append_format(strbuf, "unknown type:: %d", type_id);
        }        
    }
    else { // pointer types
        TypeId type_id = *((uint8_t*)item);
        if (type_id == LMD_TYPE_LIST) {
            List *list = (List*)item;
            printf("print list: %p, length: %ld\n", list, list->length);
            strbuf_append_char(strbuf, '(');
            for (int i = 0; i < list->length; i++) {
                if (i) strbuf_append_char(strbuf, ',');
                print_item(strbuf, list->items[i]);
            }
            strbuf_append_char(strbuf, ')');
        }
        else if (type_id == LMD_TYPE_ARRAY) {
            Array *array = (Array*)item;
            printf("print array: %p, length: %ld\n", array, array->length);
            strbuf_append_char(strbuf, '[');
            for (int i = 0; i < array->length; i++) {
                if (i) strbuf_append_char(strbuf, ',');
                print_item(strbuf, array->items[i]);
            }
            strbuf_append_char(strbuf, ']');
        }        
        else if (type_id == LMD_TYPE_ARRAY_INT) {
            strbuf_append_char(strbuf, '[');
            ArrayLong *array = (ArrayLong*)item;
            printf("print array int: %p, length: %ld\n", array, array->length);
            for (int i = 0; i < array->length; i++) {
                if (i) strbuf_append_char(strbuf, ',');
                strbuf_append_format(strbuf, "%ld", array->items[i]);
            }
            strbuf_append_char(strbuf, ']');            
        }
        else if (type_id == LMD_TYPE_MAP) {
            Map *map = (Map*)item;
            LambdaTypeMap *map_type = (LambdaTypeMap*)map->type;
            printf("print map: %p, length: %d\n", map, map_type->length);
            strbuf_append_char(strbuf, '{');
            ShapeEntry *field = map_type->shape;
            for (int i = 0; i < map_type->length; i++) {
                if (i) strbuf_append_char(strbuf, ',');
                strbuf_append_format(strbuf, "%.*s:", (int)field->name.length, field->name.str);
                printf("field %.*s:%d\n", (int)field->name.length, field->name.str, field->type->type_id);
                void* data = ((char*)map->data) + field->byte_offset;
                switch (field->type->type_id) {
                case LMD_TYPE_NULL:
                    strbuf_append_str(strbuf, "null");
                    break;
                case LMD_TYPE_BOOL:
                    strbuf_append_format(strbuf, "%s", *(bool*)data ? "true" : "false");
                    break;                    
                case LMD_TYPE_IMP_INT:  case LMD_TYPE_INT:
                    strbuf_append_format(strbuf, "%ld", *(long*)data);
                    break;
                case LMD_TYPE_FLOAT:
                    strbuf_append_format(strbuf, "%g", *(double*)data);
                    break;
                case LMD_TYPE_STRING:
                    String *string = *(String**)data;
                    strbuf_append_format(strbuf, "\"%s\"", string->str);
                    break;
                case LMD_TYPE_SYMBOL:
                    String *symbol = *(String**)data;
                    strbuf_append_format(strbuf, "'%s'", symbol->str);
                    break;
                case LMD_TYPE_DTIME:
                    String *dt = *(String**)data;
                    strbuf_append_format(strbuf, "t'%s'", dt->str);
                    break;
                case LMD_TYPE_BINARY:
                    String *bin = *(String**)data;
                    strbuf_append_format(strbuf, "b'%s'", bin->str);
                    break;
                case LMD_TYPE_LIST:  case LMD_TYPE_MAP:
                case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  
                    print_item(strbuf, *(Item*)data);
                    break;
                default:
                    strbuf_append_format(strbuf, "unknown");
                }
                field = field->next;
            }
            strbuf_append_char(strbuf, '}');
        }
        else if (type_id == LMD_TYPE_FUNC) {
            Function *func = (Function*)item;
            strbuf_append_format(strbuf, "fn %p", func);
        }
        else if (type_id == LMD_TYPE_TYPE) {
            LambdaTypeType *type = (LambdaTypeType*)item;
            strbuf_append_format(strbuf, "type %s", type_info[type->type->type_id].name);
        }
        else {
            strbuf_append_format(strbuf, "unknown type! %d", type_id);
        }
    }
}

// print the type of the AST node
char* formatType(LambdaType *type) {
    if (!type) { return "null*"; }
    TypeId type_id = type->type_id;
    switch (type_id) {
    case LMD_TYPE_NULL:
        return "void*";
    case LMD_TYPE_ANY:
        return "any";
    case LMD_TYPE_ERROR:
        return "ERROR";        
    case LMD_TYPE_BOOL:
        return "bool";
    case LMD_TYPE_IMP_INT:
        return "int^";
    case LMD_TYPE_INT:
        return "int";
    case LMD_TYPE_FLOAT:
        return "float";
    case LMD_TYPE_NUMBER:
        return "number";
    case LMD_TYPE_STRING:
        return "char*";

    case LMD_TYPE_ARRAY:
        LambdaTypeArray *array_type = (LambdaTypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_IMP_INT) {
            return "ArrayLong*";
        } else {
            return "Array*";
        }
    case LMD_TYPE_LIST:
        return "List*";
    case LMD_TYPE_MAP:
        return "Map*";
    case LMD_TYPE_ELEMENT:
        return "Elmt*";
    case LMD_TYPE_FUNC:
        return "Func*";
    case LMD_TYPE_TYPE:
        return "Type*";
    default:
        return "UNKNOWN";
    }
}

void print_label(int indent, const char *label) {
    for (int i = 0; i < indent; i++) { printf("  "); }
    printf("%s\n", label);
}

void print_ast_node(AstNode *node, int indent) {
    for (int i = 0; i < indent; i++) { printf("  "); }
    // get the function name
    switch(node->node_type) {
    case AST_NODE_IDENT:
        printf("[ident:%.*s:%s]\n", (int)((AstNamedNode*)node)->name.length, 
            ((AstNamedNode*)node)->name.str, formatType(node->type));
        break;
    case AST_NODE_PRIMARY:
        printf("[primary expr:%s]\n", formatType(node->type));
        if (((AstPrimaryNode*)node)->expr) {
            print_ast_node(((AstPrimaryNode*)node)->expr, indent + 1);
        }
        break;
    case AST_NODE_UNARY:
        printf("[unary expr %.*s:%s]\n", (int)((AstUnaryNode*)node)->operator.length, 
            ((AstUnaryNode*)node)->operator.str, formatType(node->type));
        print_ast_node(((AstUnaryNode*)node)->operand, indent + 1);
        break;
    case AST_NODE_BINARY:
        printf("[binary expr:%s]\n", formatType(node->type));
        print_ast_node(((AstBinaryNode*)node)->left, indent + 1);
        print_ast_node(((AstBinaryNode*)node)->right, indent + 1);
        break;
    case AST_NODE_IF_EXPR:
        printf("[if expr:%s]\n", formatType(node->type));
        AstIfExprNode* if_node = (AstIfExprNode*)node;
        print_ast_node(if_node->cond, indent + 1);
        print_label(indent + 1, "then:");
        print_ast_node(if_node->then, indent + 1);
        if (if_node->otherwise) {
            print_label(indent + 1, "else:");            
            print_ast_node(if_node->otherwise, indent + 1);
        }
        break;
    case AST_NODE_LET_STAM:
        printf("[let stam:%s]\n", formatType(node->type));
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            print_label(indent + 1, "declare:");
            print_ast_node(declare, indent + 1);
            declare = declare->next;
        }
        break;
    case AST_NODE_FOR_EXPR:
        printf("[for %s:%s]\n", node->node_type == AST_NODE_FOR_EXPR ? "expr" : "stam", formatType(node->type));
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            print_label(indent + 1, "loop:");
            print_ast_node(loop, indent + 1);
            loop = loop->next;
        }
        if (node->node_type == AST_NODE_FOR_EXPR) {
            print_label(indent + 1, "then:");
            print_ast_node(((AstForNode*)node)->then, indent + 1);
        }
        break;
    case AST_NODE_ASSIGN:
        AstNamedNode* assign = (AstNamedNode*)node;
        printf("[assign expr:%.*s:%s]\n", (int)assign->name.length, assign->name.str, formatType(node->type));
        print_ast_node(assign->as, indent + 1);
        break;
    case AST_NODE_KEY_EXPR:
        AstNamedNode* key = (AstNamedNode*)node;
        printf("[key expr:%.*s:%s]\n", (int)key->name.length, key->name.str, formatType(node->type));
        print_ast_node(key->as, indent + 1);
        break;
    case AST_NODE_LOOP:
        printf("[loop expr:%s]\n", formatType(node->type));
        print_ast_node(((AstNamedNode*)node)->as, indent + 1);
        break;
    case AST_NODE_ARRAY:
        printf("[array expr:%s]\n", formatType(node->type));
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            print_label(indent + 1, "item:");
            print_ast_node(item, indent + 1);
            item = item->next;
        }        
        break;
    case AST_NODE_LIST:  case AST_NODE_CONTENT:
        printf("[%s:%s[%d]]\n", node->node_type == AST_NODE_CONTENT ? "content" : "list", 
            formatType(node->type), ((LambdaTypeList*)node->type)->length);
        AstNode *ld = ((AstListNode*)node)->declare;
        if (!ld) {
            print_label(indent + 1, "no declare");
        }
        while (ld) {
            print_label(indent + 1, "declare:");
            print_ast_node(ld, indent + 1);
            ld = ld->next;
        }        
        AstNode *li = ((AstListNode*)node)->item;
        while (li) {
            print_label(indent + 1, "item:");
            print_ast_node(li, indent + 1);
            li = li->next;
        }        
        break; 
    case AST_NODE_MAP:
        printf("[map expr:%s]\n", formatType(node->type));
        AstNamedNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            print_label(indent + 1, "item:");
            print_ast_node((AstNode*)nm_item, indent + 1);
            nm_item = (AstNamedNode*)nm_item->next;
        }
        break;
    case AST_NODE_PARAM:
        AstNamedNode* param = (AstNamedNode*)node;
        printf("[param: %.*s:%s]\n", (int)param->name.length, param->name.str, formatType(node->type));
        break;
    case AST_NODE_FIELD_EXPR:
        printf("[field expr:%s]\n", formatType(node->type));
        print_label(indent + 1, "object:");
        print_ast_node(((AstFieldNode*)node)->object, indent + 1);
        print_label(indent + 1, "field:");     
        print_ast_node(((AstFieldNode*)node)->field, indent + 1);
        break;
    case AST_NODE_CALL_EXPR:
        printf("[call expr:%s]\n", formatType(node->type));
        print_ast_node(((AstCallNode*)node)->function, indent + 1);
        print_label(indent + 1, "args:"); 
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            print_ast_node(arg, indent + 1);
            arg = arg->next;
        }
        break;
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:
        AstFuncNode* func = (AstFuncNode*)node;
        if (node->node_type == AST_NODE_FUNC_EXPR) {
            printf("[function expr:%s]\n", formatType(node->type));
        } else {
            printf("[function: %.*s:%s]\n", (int)func->name.length, func->name.str, formatType(node->type));
        }
        print_label(indent + 1, "params:"); 
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            print_ast_node(fn_param, indent + 1);
            fn_param = fn_param->next;
        }
        print_ast_node(func->body, indent + 1);
        break;
    case AST_NODE_TYPE:
        printf("[type:%s]\n", formatType(node->type));
        break;
    case AST_SCRIPT:
        printf("[script:%s]\n", formatType(node->type));
        AstNode* child = ((AstScript*)node)->child;
        while (child) {
            print_ast_node(child, indent + 1);
            child = child->next;
        }
        break;
    default:
        printf("unknown expression type\n");
        break;
    }
}



#include "transpiler.h"

#define MAX_DEPTH 500

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
void writeType(Transpiler* tp, Type *type) {
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
    case LMD_TYPE_INT:  case LMD_TYPE_INT64:
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
    case LMD_TYPE_LIST:
        strbuf_append_str(tp->code_buf, "List*");
        break;
    case LMD_TYPE_ARRAY:
        TypeArray *array_type = (TypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_INT) {
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
        strbuf_append_str(tp->code_buf, "Type*");
        break;
    default:
        printf("unknown type %d\n", type_id);
    }
}

void print_named_items_with_depth(StrBuf *strbuf, TypeMap *map_type, void* map_data, int depth);
void print_item_with_depth(StrBuf *strbuf, Item item, int depth);

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data) {
    print_named_items_with_depth(strbuf, map_type, map_data, 0);
}

void print_named_items_with_depth(StrBuf *strbuf, TypeMap *map_type, void* map_data, int depth) {
    // Prevent infinite recursion
    if (depth > MAX_DEPTH) { strbuf_append_str(strbuf, "[MAX_DEPTH_REACHED]");  return; }

    ShapeEntry *field = map_type->shape;
    for (int i = 0; i < map_type->length; i++) {
        if (i) strbuf_append_char(strbuf, ',');
        if (!field) {
            printf("ERROR: missing map field for index %d\n", i);
            strbuf_append_str(strbuf, "[null field shape]");
            continue;
        }
        void* data = ((char*)map_data) + field->byte_offset;
        if (!field->name) { // nested map
            Map *nest_map = *(Map**)data;
            TypeMap *nest_map_type = (TypeMap*)nest_map->type;
            print_named_items_with_depth(strbuf, nest_map_type, nest_map->data, depth + 1);
        }
        else {
            // printf("print_named_item: name %.*s, type %d\n", 
            //     (int)field->name->length, field->name->str, field->type ? field->type->type_id : 0);
            strbuf_append_format(strbuf, "%.*s:", (int)field->name->length, field->name->str);
            switch (field->type->type_id) {
            case LMD_TYPE_NULL:
                strbuf_append_str(strbuf, "null");
                break;
            case LMD_TYPE_BOOL:
                strbuf_append_format(strbuf, "%s", *(bool*)data ? "true" : "false");
                break;                    
            case LMD_TYPE_INT:  case LMD_TYPE_INT64:
                strbuf_append_format(strbuf, "%ld", *(long*)data);
                break;
            case LMD_TYPE_FLOAT:
                strbuf_append_format(strbuf, "%g", *(double*)data);
                break;
            case LMD_TYPE_STRING:
                String *string = *(String**)data;
                if (string && string->chars) {
                    strbuf_append_format(strbuf, "\"%s\"", string->chars);
                } else {
                    strbuf_append_str(strbuf, "\"\"");
                }
                break;
            case LMD_TYPE_SYMBOL:
                String *symbol = *(String**)data;
                if (symbol && symbol->chars) {
                    strbuf_append_format(strbuf, "'%s'", symbol->chars);
                } else {
                    strbuf_append_str(strbuf, "''");
                }
                break;
            case LMD_TYPE_DTIME:
                String *dt = *(String**)data;
                if (dt && dt->chars) {
                    strbuf_append_format(strbuf, "t'%s'", dt->chars);
                } else {
                    strbuf_append_str(strbuf, "t''");
                }
                break;
            case LMD_TYPE_BINARY:
                String *bin = *(String**)data;
                if (bin && bin->chars) {
                    strbuf_append_format(strbuf, "b'%s'", bin->chars);
                } else {
                    strbuf_append_str(strbuf, "b''");
                }
                break;
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_LIST:  
            case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_ANY:
                print_item_with_depth(strbuf, *(Item*)data, depth + 1);
                break;
            default:
                strbuf_append_format(strbuf, "unknown");
            }
        }
        field = field->next;
    }
}

void print_item_with_depth(StrBuf *strbuf, Item item, int depth);

void print_item(StrBuf *strbuf, Item item) {
    print_item_with_depth(strbuf, item, 0);
}

void print_item_with_depth(StrBuf *strbuf, Item item, int depth) {
    // limit depth to prevent infinite recursion
    if (depth > MAX_DEPTH) { strbuf_append_str(strbuf, "[MAX_DEPTH_REACHED]");  return; }
    if (!item) { strbuf_append_str(strbuf, "null"); return; }

    LambdaItem ld_item = {.item = item};
    // printf("print_item: type %d\n", !ld_item.type_id ? *((uint8_t*)item) : ld_item.type_id);  
    if (ld_item.type_id) { // packed value
        TypeId type_id = ld_item.type_id;
        if (type_id == LMD_TYPE_NULL) {
            strbuf_append_str(strbuf, "null");
        } 
        else if (type_id == LMD_TYPE_BOOL) {
            strbuf_append_str(strbuf, ld_item.bool_val ? "true" : "false");
        }
        else if (type_id == LMD_TYPE_INT) {
            int int_val = (int32_t)ld_item.long_val;
            strbuf_append_format(strbuf, "%d", int_val);
        }
        else if (type_id == LMD_TYPE_INT64) {
            long long_val = *(long*)ld_item.pointer;
            strbuf_append_format(strbuf, "%ld", long_val);
        }
        else if (type_id == LMD_TYPE_FLOAT) {
            double num = *(double*)ld_item.pointer;
            int exponent;
            double mantissa = frexp(num, &exponent);
            if (-20 < exponent && exponent < 30) {
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
        else if (type_id == LMD_TYPE_DECIMAL) {
            mpf_t *num = (mpf_t*)ld_item.pointer;
            char buf[128];
            gmp_sprintf(buf, "%.Ff", *num);
            strbuf_append_str(strbuf, buf);
        }
        else if (type_id == LMD_TYPE_STRING) {
            String *string = (String*)ld_item.pointer;
            // todo: escape the string
            if (string && string->chars) strbuf_append_format(strbuf, "\"%s\"", string->chars);
            else strbuf_append_str(strbuf, "\"\"");
        }
        else if (type_id == LMD_TYPE_SYMBOL) {
            String *string = (String*)ld_item.pointer;
            // todo: escape the symbol chars
            if (string && string->chars) strbuf_append_format(strbuf, "'%s'", string->chars);
            else strbuf_append_str(strbuf, "''");
        } 
        else if (type_id == LMD_TYPE_DTIME) {
            String *string = (String*)ld_item.pointer;
            if (string && string->chars) strbuf_append_format(strbuf, "t'%s'", string->chars);
            else strbuf_append_str(strbuf, "t''");
        }
        else if (type_id == LMD_TYPE_BINARY) {
            String *string = (String*)ld_item.pointer;
            if (string && string->chars) strbuf_append_format(strbuf, "b'%s'", string->chars);
            else strbuf_append_str(strbuf, "b''");
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
            // printf("print list: %p, length: %ld\n", list, list->length);
            strbuf_append_char(strbuf, '(');
            for (int i = 0; i < list->length; i++) {
                if (i) strbuf_append_char(strbuf, ',');
                print_item_with_depth(strbuf, list->items[i], depth + 1);
            }
            strbuf_append_char(strbuf, ')');
        }
        else if (type_id == LMD_TYPE_RANGE) {
            Range *range = (Range*)item;
            // printf("print range: %p, start: %ld, end: %ld\n", range, range->start, range->end);
            strbuf_append_format(strbuf, "%ld to %ld", range->start, range->end);
        }
        else if (type_id == LMD_TYPE_ARRAY) {
            Array *array = (Array*)item;
            // printf("print array: %p, length: %ld\n", array, array->length);
            strbuf_append_char(strbuf, '[');
            for (int i = 0; i < array->length; i++) {
                if (i) strbuf_append_char(strbuf, ',');
                print_item_with_depth(strbuf, array->items[i], depth + 1);
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
            TypeMap *map_type = (TypeMap*)map->type;
            strbuf_append_char(strbuf, '{');
            print_named_items_with_depth(strbuf, map_type, map->data, depth + 1);
            strbuf_append_char(strbuf, '}');
        }
        else if (type_id == LMD_TYPE_ELEMENT) {
            Element *element = (Element*)item;
            TypeElmt *elmt_type = (TypeElmt*)element->type;
            printf("print element, attr len: %ld, content len: %ld, actual content len: %ld\n", 
                elmt_type->length, elmt_type->content_length, element->length);
            strbuf_append_format(strbuf, "<%.*s ", (int)elmt_type->name.length, elmt_type->name.str);
            print_named_items(strbuf, (TypeMap*)elmt_type, element->data);
            // print content
            if (elmt_type->length) { strbuf_append_char(strbuf, ';'); }
            for (long i = 0; i < element->length; i++) {
                if (i) strbuf_append_char(strbuf, ';');
                print_item(strbuf, element->items[i]);
            }            
            strbuf_append_char(strbuf, '>');
        }
        else if (type_id == LMD_TYPE_FUNC) {
            strbuf_append_str(strbuf, "function");
        }
        else if (type_id == LMD_TYPE_FUNC) {
            Function *func = (Function*)item;
            strbuf_append_format(strbuf, "fn %p", func);
        }
        else if (type_id == LMD_TYPE_TYPE) {
            TypeType *type = (TypeType*)item;
            strbuf_append_format(strbuf, "type %s", type_info[type->type->type_id].name);
        }
        else {
            strbuf_append_format(strbuf, "unknown type! %d", type_id);
        }
    }
}

// print the type of the AST node
char* format_type(Type *type) {
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
    case LMD_TYPE_INT:
        return "int^";
    case LMD_TYPE_INT64:
        return "int";
    case LMD_TYPE_FLOAT:
        return "float";
    case LMD_TYPE_DECIMAL:
        return "decimal";
    case LMD_TYPE_NUMBER:
        return "number";
    case LMD_TYPE_STRING:
        return "char*";
    case LMD_TYPE_SYMBOL:
        return "char*";
    case LMD_TYPE_DTIME:
        return "char*";
    case LMD_TYPE_BINARY:
        return "uint8_t*";

    case LMD_TYPE_LIST:
        return "List*";
    case LMD_TYPE_RANGE:
        return "Range*";
    case LMD_TYPE_ARRAY:
        TypeArray *array_type = (TypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_INT) {
            return "ArrayLong*";
        } else {
            return "Array*";
        }

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
        printf("[ident:%.*s:%s,const:%d]\n", (int)((AstIdentNode*)node)->name.length, 
            ((AstIdentNode*)node)->name.str, format_type(node->type), node->type->is_const);
        break;
    case AST_NODE_PRIMARY:
        printf("[primary expr:%s,const:%d]\n", format_type(node->type), node->type->is_const);
        if (((AstPrimaryNode*)node)->expr) {
            print_ast_node(((AstPrimaryNode*)node)->expr, indent + 1);
        }
        break;
    case AST_NODE_UNARY:
        printf("[unary expr %.*s:%s]\n", (int)((AstUnaryNode*)node)->op_str.length, 
            ((AstUnaryNode*)node)->op_str.str, format_type(node->type));
        print_ast_node(((AstUnaryNode*)node)->operand, indent + 1);
        break;
    case AST_NODE_BINARY:
        AstBinaryNode* bnode = (AstBinaryNode*)node;
        printf("[binary expr %.*s.%d:%s]\n", (int)bnode->op_str.length, bnode->op_str.str, 
            bnode->op, format_type(node->type));
        print_ast_node(bnode->left, indent + 1);
        print_ast_node(bnode->right, indent + 1);
        break;
    case AST_NODE_IF_EXPR:
        printf("[if expr:%s]\n", format_type(node->type));
        AstIfExprNode* if_node = (AstIfExprNode*)node;
        print_ast_node(if_node->cond, indent + 1);
        print_label(indent + 1, "then:");
        print_ast_node(if_node->then, indent + 1);
        if (if_node->otherwise) {
            print_label(indent + 1, "else:");            
            print_ast_node(if_node->otherwise, indent + 1);
        }
        break;
    case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM:
        printf("[%s stam:%s]\n", node->node_type == AST_NODE_PUB_STAM ? "pub" : "let", format_type(node->type));
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            print_label(indent + 1, "declare:");
            print_ast_node(declare, indent + 1);
            declare = declare->next;
        }
        break;
    case AST_NODE_FOR_EXPR:
        printf("[for %s:%s]\n", node->node_type == AST_NODE_FOR_EXPR ? "expr" : "stam", format_type(node->type));
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
        printf("[assign expr:%.*s:%s]\n", (int)assign->name.length, assign->name.str, format_type(node->type));
        print_ast_node(assign->as, indent + 1);
        break;
    case AST_NODE_KEY_EXPR:
        AstNamedNode* key = (AstNamedNode*)node;
        printf("[key expr:%.*s:%s]\n", (int)key->name.length, key->name.str, format_type(node->type));
        print_ast_node(key->as, indent + 1);
        break;
    case AST_NODE_LOOP:
        printf("[loop expr:%s]\n", format_type(node->type));
        print_ast_node(((AstNamedNode*)node)->as, indent + 1);
        break;
    case AST_NODE_ARRAY:
        printf("[array expr:%s]\n", format_type(node->type));
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            print_label(indent + 1, "item:");
            print_ast_node(item, indent + 1);
            item = item->next;
        }        
        break;
    case AST_NODE_LIST:  case AST_NODE_CONTENT:  case AST_NODE_CONTENT_TYPE:
        printf("[%s:%s[%ld]]\n", node->node_type == 
            AST_NODE_CONTENT_TYPE ? "content_type" : AST_NODE_CONTENT ? "content" : "list", 
            format_type(node->type), ((TypeList*)node->type)->length);
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
        printf("[map expr:%s]\n", format_type(node->type));
        AstNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            print_label(indent + 1, "map item:");
            print_ast_node(nm_item, indent + 1);
            nm_item = nm_item->next;
        }
        break;
    case AST_NODE_ELEMENT:
        printf("[elmt expr:%s]\n", format_type(node->type));
        AstElementNode* elmt_node = (AstElementNode*)node;
        AstNode *elmt_item = elmt_node->item;
        while (elmt_item) {
            print_label(indent + 1, "attr:");
            print_ast_node(elmt_item, indent + 1);
            elmt_item = elmt_item->next;
        }
        if (elmt_node->content) print_ast_node(elmt_node->content, indent + 1);
        break;
    case AST_NODE_PARAM:
        AstNamedNode* param = (AstNamedNode*)node;
        printf("[param: %.*s:%s]\n", (int)param->name.length, param->name.str, format_type(node->type));
        break;
    case AST_NODE_FIELD_EXPR:
        printf("[field expr:%s]\n", format_type(node->type));
        print_label(indent + 1, "object:");
        print_ast_node(((AstFieldNode*)node)->object, indent + 1);
        print_label(indent + 1, "field:");     
        print_ast_node(((AstFieldNode*)node)->field, indent + 1);
        break;
    case AST_NODE_CALL_EXPR:
        printf("[call expr:%s]\n", format_type(node->type));
        print_ast_node(((AstCallNode*)node)->function, indent + 1);
        print_label(indent + 1, "args:"); 
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            print_ast_node(arg, indent + 1);
            arg = arg->next;
        }
        break;
    case AST_NODE_SYS_FUNC:
        printf("[sys func:%d:%s]\n", ((AstSysFuncNode*)node)->fn, format_type(node->type));
        break;
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR:
        AstFuncNode* func = (AstFuncNode*)node;
        if (node->node_type == AST_NODE_FUNC_EXPR) {
            printf("[function expr:%s]\n", format_type(node->type));
        } else {
            printf("[function: %.*s:%s]\n", (int)func->name.length, func->name.str, format_type(node->type));
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
        assert(node->type->type_id == LMD_TYPE_TYPE && 
            ((TypeType*)node->type)->type);
        printf("[type:%s, %s]\n", format_type(node->type), format_type(((TypeType*)node->type)->type));
        break;
    case AST_NODE_LIST_TYPE:
        printf("[list type:%s]\n", format_type(node->type));
        AstNode *ls_item = ((AstListNode*)node)->item;
        while (ls_item) {
            print_label(indent + 1, "item:");
            print_ast_node(ls_item, indent + 1);
            ls_item = ls_item->next;
        }        
        break;           
    case AST_NODE_ARRAY_TYPE:
        printf("[array type:%s]\n", format_type(node->type));
        AstNode *arr_item = ((AstArrayNode*)node)->item;
        while (arr_item) {
            print_label(indent + 1, "item:");
            print_ast_node(arr_item, indent + 1);
            arr_item = arr_item->next;
        }        
        break;    
    case AST_NODE_MAP_TYPE:
        printf("[map type:%s]\n", format_type(node->type));
        AstNode *mt_item = ((AstMapNode*)node)->item;
        while (mt_item) {
            print_label(indent + 1, "map item:");
            print_ast_node(mt_item, indent + 1);
            mt_item = mt_item->next;
        }
        break;
    case AST_NODE_ELMT_TYPE:
        printf("[elmt type:%s]\n", format_type(node->type));
        AstElementNode* et_node = (AstElementNode*)node;
        AstNode *et_item = et_node->item;
        while (et_item) {
            print_label(indent + 1, "attr:");
            print_ast_node(et_item, indent + 1);
            et_item = et_item->next;
        }
        if (et_node->content) print_ast_node(et_node->content, indent + 1);
        break;
    case AST_NODE_FUNC_TYPE:
        printf("[func type:%s]\n", format_type(node->type));
        AstFuncNode* ft = (AstFuncNode*)node;
        print_label(indent + 1, "params:"); 
        AstNode* ft_param = (AstNode*)ft->param;
        while (ft_param) {
            print_ast_node(ft_param, indent + 1);
            ft_param = ft_param->next;
        }    
        break;
    case AST_NODE_BINARY_TYPE:
        AstBinaryNode* bt_node = (AstBinaryNode*)node;
        printf("[binary type %.*s.%d:%s]\n", (int)bt_node->op_str.length, bt_node->op_str.str, 
            bt_node->op, format_type(node->type));
        print_ast_node(bt_node->left, indent + 1);
        print_ast_node(bt_node->right, indent + 1);        
        break;
    case AST_NODE_IMPORT:
        printf("[import %.*s:%.*s]\n", 
            (int)((AstImportNode*)node)->alias.length, ((AstImportNode*)node)->alias.str, 
            (int)((AstImportNode*)node)->module.length, ((AstImportNode*)node)->module.str);
        break;
    case AST_SCRIPT:
        printf("[script:%s]\n", format_type(node->type));
        AstNode* child = ((AstScript*)node)->child;
        while (child) {
            print_ast_node(child, indent + 1);
            child = child->next;
        }
        break;
    default:
        printf("unknown expression type!\n");
        break;
    }
}


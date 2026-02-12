#include "lambda-data.hpp"
#include "lambda-decimal.hpp"
#include "../lib/log.h"
#include <math.h>
#include <inttypes.h>  // for PRId64
#include "ast.hpp"

#define MAX_DEPTH 2000
#define MAX_FIELD_COUNT 10000

void print_typeditem(StrBuf *strbuf, TypedItem* citem, int depth, char* indent);

// Static memory pool for DateTime formatting
static Pool* datetime_format_pool = NULL;

// Initialize datetime formatting pool if needed
static void init_datetime_format_pool() {
    if (!datetime_format_pool) {
        datetime_format_pool = pool_create(); // Small pool for formatting strings
    }
}

// print the syntax tree as an s-expr
void print_ts_node(const char *source, TSNode node, uint32_t indent) {
    if (indent > 0) log_enter();
    const char *type = ts_node_type(node);
    if (isalpha(*type)) {
        log_debug("(%s", type);
    } else if (*type == '\'') {
        log_debug("(\"%s\"", type);
    } else { // special char
        log_debug("('%s'", type);
    }
    // print children if any
    uint32_t child_count = ts_node_child_count(node);
    if (child_count > 0) {
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            print_ts_node(source, child, indent + 1);
        }
    }
    else if (isalpha(*type)) {
        int start_byte = ts_node_start_byte(node);
        int end_byte = ts_node_end_byte(node);
        const char* start = source + start_byte;
        log_debug(" '%.*s'", end_byte - start_byte, start);
    }
    log_debug(")");
    if (indent > 0) log_leave();
}

void print_ts_root(const char *source, TSTree* syntax_tree) {
    log_debug("Syntax tree: ---------");
    TSNode root_node = ts_tree_root_node(syntax_tree);
    print_ts_node(source, root_node, 0);
}

// write the native C type for the lambda type
void write_type(StrBuf* code_buf, Type *type) {
    if (!type) {
        strbuf_append_str(code_buf, "Item");
        return;
    }
    TypeId type_id = type->type_id;
    switch (type_id) {
    case LMD_TYPE_NULL:
        // NULL type means variable can hold any value (e.g., var x = null; x = something)
        strbuf_append_str(code_buf, "Item");
        break;
    case LMD_TYPE_ANY:
        strbuf_append_str(code_buf, "Item");
        break;
    case LMD_TYPE_ERROR:
        strbuf_append_str(code_buf, "Item");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(code_buf, "bool");
        break;
    case LMD_TYPE_INT:
        strbuf_append_str(code_buf, "int32_t");
        break;
    case LMD_TYPE_INT64:
        strbuf_append_str(code_buf, "int64_t");
        break;
    case LMD_TYPE_FLOAT:
        strbuf_append_str(code_buf, "double");
        break;
    case LMD_TYPE_DTIME:
        strbuf_append_str(code_buf, "DateTime");
        break;
    case LMD_TYPE_DECIMAL:
        strbuf_append_str(code_buf, "Decimal*");
        break;
    case LMD_TYPE_STRING:
        strbuf_append_str(code_buf, "String*");
        break;
    case LMD_TYPE_BINARY:
        strbuf_append_str(code_buf, "String*");
        break;
    case LMD_TYPE_SYMBOL:
        strbuf_append_str(code_buf, "Symbol*");
        break;

    case LMD_TYPE_RANGE:
        strbuf_append_str(code_buf, "Range*");
        break;
    case LMD_TYPE_LIST:
        strbuf_append_str(code_buf, "List*");
        break;
    case LMD_TYPE_ARRAY: {
        TypeArray *array_type = (TypeArray*)type;
        if (array_type->nested) {
            if (array_type->nested->type_id == LMD_TYPE_INT)
                strbuf_append_str(code_buf, "ArrayInt*");
            else if (array_type->nested->type_id == LMD_TYPE_INT64)
                strbuf_append_str(code_buf, "ArrayInt64*");
            else if (array_type->nested->type_id == LMD_TYPE_FLOAT)
                strbuf_append_str(code_buf, "ArrayFloat*");
            else
                strbuf_append_str(code_buf, "Array*");
        } else {
            strbuf_append_str(code_buf, "Array*");
        }
        break;
    }
    case LMD_TYPE_MAP:
        strbuf_append_str(code_buf, "Map*");
        break;
    case LMD_TYPE_ELEMENT:
        strbuf_append_str(code_buf, "Element*");
        break;
    case LMD_TYPE_PATH:
        strbuf_append_str(code_buf, "Path*");
        break;
    case LMD_TYPE_FUNC:
        strbuf_append_str(code_buf, "Function*");
        break;
    case LMD_TYPE_TYPE:
        strbuf_append_str(code_buf, "Type*");
        break;
    default:
        log_error("unknown type to write %d", type_id);
    }
}

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data, int depth = 0, char* indent = NULL, bool is_attrs = false);

void print_double(StrBuf *strbuf, double num) {
    // Handle special cases: NaN should always be printed as "nan" (never "-nan")
    if (isnan(num)) {
        strbuf_append_str(strbuf, "nan");
        return;
    }
    int exponent;
    double mantissa = frexp(num, &exponent);
    if (-20 < exponent && exponent < 30) {
        // log_debug("printing fancy double: %.10f", num);
        strbuf_append_format(strbuf, "%.10f", num);
        // trim trailing zeros
        char *end = strbuf->str + strbuf->length - 1;
        while (*end == '0' && end > strbuf->str) { *end-- = '\0'; }
        // if it ends with a dot, remove that too
        if (*end == '.') { *end-- = '\0'; }
        strbuf->length = end - strbuf->str + 1;
    }
    else if (-30 < exponent && exponent <= -20) {
        // log_debug("printing small double: %.10f", num);
        strbuf_append_format(strbuf, "%.g", num);
        // remove the zero in exponent, like 'e07'
        char *end = strbuf->str + strbuf->length - 1;
        if (*(end-1) == '0' && *(end-2) == '-' && *(end-3) == 'e') {
            *(end-1) = *end;  *end = '\0';
            strbuf->length = end - strbuf->str;
        }
    }
    else {
        // log_debug("printing normal double: %.10f", num);
        strbuf_append_format(strbuf, "%g", num);
    }
}

void print_decimal(StrBuf *strbuf, Decimal *decimal) {
    if (!decimal || !decimal->dec_val) { strbuf_append_str(strbuf, "error");  return; }
    // Use centralized decimal_to_string function
    char *decimal_str = decimal_to_string(decimal);
    if (!decimal_str) { strbuf_append_str(strbuf, "error");  return; }
    // log_debug("printed decimal: %s", decimal_str);
    strbuf_append_str(strbuf, decimal_str);
    decimal_free_string(decimal_str);
}

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data, int depth, char* indent, bool is_attrs) {
    // Prevent infinite recursion
    if (depth > MAX_DEPTH) { strbuf_append_str(strbuf, "[MAX_DEPTH_REACHED]");  return; }
    if (!map_type) { strbuf_append_str(strbuf, "[null map_type]");  return; }
    // Safety check for map_type length
    if (map_type->length < 0 || map_type->length > MAX_FIELD_COUNT) {
        strbuf_append_str(strbuf, "[invalid map_type length]");
        return;
    }

    ShapeEntry *field = map_type->shape;
    // log_debug("printing named items: %p, type: %d, length: %ld", map_data, map_type->type_id, map_type->length);
    for (int i = 0; i < map_type->length; i++) {
        // Safety check for valid field pointer
        if (!field || (uintptr_t)field < 0x1000) {
        log_error("invalid field pointer: %p", field);
            strbuf_append_str(strbuf, "[invalid field pointer]");
            break;
        }
        if (i) strbuf_append_char(strbuf, ',');
        void* data = ((char*)map_data) + field->byte_offset;
        if (!field->name) { // nested map
            log_debug("nested map at field %d: %p", i, data);
            Map *nest_map = *(Map**)data;
            if (!nest_map) {
                log_error("expected a map, got null pointer at field %d", i);
                strbuf_append_str(strbuf, "[null nested map]");
            } else {
                TypeMap *nest_map_type = (TypeMap*)nest_map->type;
                print_named_items(strbuf, nest_map_type, nest_map->data, depth, indent, is_attrs);
            }
        }
        else {
            // Safety check for field name and type
            if (!field->name || (uintptr_t)field->name < 0x1000) {
                log_error("invalid field name: %p", field->name);
                strbuf_append_str(strbuf, "[invalid field name]");
                goto advance_field;
            }
            if (!field->type || (uintptr_t)field->type < 0x1000) {
                log_error("invalid field type: %p", field->type);
                strbuf_append_str(strbuf, "[invalid field type]");
                goto advance_field;
            }
            // Safety check for type_id range
            TypeId field_type_id = field->type->type_id;
            if (field_type_id > 50) {
                log_error("invalid type_id: %d", field_type_id);
                strbuf_append_str(strbuf, "[invalid type_id]");
                goto advance_field;
            }
            // add indentation if needed
            if (indent && !is_attrs) {
                strbuf_append_str(strbuf, "\n");
                for (int j = 0; j < depth; j++) strbuf_append_str(strbuf, indent);
            } else {
                strbuf_append_str(strbuf, " ");
            }
            strbuf_append_format(strbuf, "%.*s: ", (int)field->name->length, field->name->str);
            switch (field->type->type_id) {
            case LMD_TYPE_NULL:
                strbuf_append_str(strbuf, "null");
                break;
            case LMD_TYPE_BOOL:
                strbuf_append_format(strbuf, "%s", *(bool*)data ? "true" : "false");
                break;
            case LMD_TYPE_INT:
                strbuf_append_format(strbuf, "%" PRId64, *(int64_t*)data);  // read full int64 to preserve 56-bit value
                break;
            case LMD_TYPE_INT64:
                strbuf_append_format(strbuf, "%" PRId64, *(int64_t*)data);
                break;
            case LMD_TYPE_FLOAT:
                print_double(strbuf, *(double*)data);
                break;
            case LMD_TYPE_DTIME: {
                DateTime dt = *(DateTime*)data;
                strbuf_append_str(strbuf, "t'");
                datetime_format_lambda(strbuf, &dt);
                strbuf_append_char(strbuf, '\'');
                break;
            }
            case LMD_TYPE_DECIMAL: {
                Decimal *decimal = *(Decimal**)data;
                print_decimal(strbuf, decimal);
                break;
            }
            case LMD_TYPE_STRING: {
                String *string = *(String**)data;
                if (string && string->chars) {
                    strbuf_append_format(strbuf, "\"%s\"", string->chars);
                } else {
                    strbuf_append_str(strbuf, "\"\"");
                }
                break;
            }
            case LMD_TYPE_SYMBOL: {
                Symbol *symbol = *(Symbol**)data;
                if (symbol && symbol->chars) {
                    strbuf_append_format(strbuf, "'%s'", symbol->chars);
                } else {
                    strbuf_append_str(strbuf, "''");
                }
                break;
            }
            case LMD_TYPE_BINARY: {
                String *bin = *(String**)data;
                if (bin && bin->chars) {
                    strbuf_append_format(strbuf, "b'%s'", bin->chars);
                } else {
                    strbuf_append_str(strbuf, "b''");
                }
                break;
            }
            case LMD_TYPE_PATH: {
                Path *path = *(Path**)data;
                path_to_string(path, strbuf);
                break;
            }
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:
            case LMD_TYPE_FUNC:  case LMD_TYPE_TYPE:
                print_item(strbuf, *(Item*)data, depth, indent);
                break;
            case LMD_TYPE_ANY:
                print_typeditem(strbuf, (TypedItem*)data, depth, indent);
                break;
            default:
                strbuf_append_str(strbuf, "[unknown]");
            }
        }

        advance_field:
        ShapeEntry *next_field = field->next;
        field = next_field;
    }
}

void print_typeditem(StrBuf *strbuf, TypedItem *titem, int depth, char* indent) {
    if (depth > MAX_DEPTH) {
        strbuf_append_str(strbuf, "[MAX_DEPTH_REACHED]");
        return;
    }
    if (!titem) {
        strbuf_append_str(strbuf, "null");
        return;
    }

    switch (titem->type_id) {
    case LMD_TYPE_NULL:
        strbuf_append_str(strbuf, "null");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(strbuf, titem->bool_val ? "true" : "false");
        break;
    case LMD_TYPE_INT:
        strbuf_append_format(strbuf, "%d", titem->int_val);
        break;
    case LMD_TYPE_INT64:
        strbuf_append_format(strbuf, "%" PRId64, titem->long_val);
        break;
    case LMD_TYPE_FLOAT:
        print_double(strbuf, titem->double_val);
        break;
    case LMD_TYPE_DTIME: {
        DateTime dt = titem->datetime_val;
        strbuf_append_str(strbuf, "t'");
        datetime_format_lambda(strbuf, &dt);
        strbuf_append_char(strbuf, '\'');
        break;
    }
    case LMD_TYPE_DECIMAL:
        print_decimal(strbuf, titem->decimal);
        break;
    case LMD_TYPE_STRING:
        if (titem->string && titem->string->chars) {
            strbuf_append_format(strbuf, "\"%s\"", titem->string->chars);
        } else {
            strbuf_append_str(strbuf, "\"\"");
        }
        break;
    case LMD_TYPE_SYMBOL:
        if (titem->symbol && titem->symbol->chars) {
            strbuf_append_str(strbuf, titem->symbol->chars);
        } else {
            strbuf_append_str(strbuf, "");
        }
        break;
    case LMD_TYPE_BINARY:
        if (titem->string && titem->string->chars) {
            strbuf_append_format(strbuf, "0x%s", titem->string->chars);
        } else {
            strbuf_append_str(strbuf, "0x");
        }
        break;
    case LMD_TYPE_PATH:
        path_to_string(titem->path, strbuf);
        break;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_RANGE:  case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
        // For complex types, create a temporary Item and use existing print_item logic
        Item temp_item = {.item = titem->item};
        print_item(strbuf, temp_item, depth + 1, indent);
        break;
    }
    case LMD_TYPE_ERROR:
        strbuf_append_str(strbuf, "error");
        break;
    default:
        strbuf_append_format(strbuf, "unknown_type_%d", titem->type_id);
        break;
    }
}

void print_item(StrBuf *strbuf, Item item, int depth, char* indent) {
    // limit depth to prevent infinite recursion
    if (depth > MAX_DEPTH) { strbuf_append_str(strbuf, "[MAX_DEPTH_REACHED]");  return; }
    if (!item.item) {
        log_debug("TRACE: print_item - item is NULL, appending null");
        strbuf_append_str(strbuf, "null");
        return;
    }

    TypeId type_id = get_type_id(item);
    switch (type_id) { // packed value
    case LMD_TYPE_NULL:
        strbuf_append_str(strbuf, "null");
        break;
    case LMD_TYPE_BOOL:
        strbuf_append_str(strbuf, item.bool_val ? "true" : "false");
        break;
    case LMD_TYPE_INT: {
        int64_t int_val = item.get_int56();
        strbuf_append_format(strbuf, "%" PRId64, int_val);
        break;
    }
    case LMD_TYPE_INT64: {
        int64_t long_val = item.get_int64();
        log_debug("print int64: %" PRId64, long_val);
        strbuf_append_format(strbuf, "%" PRId64, long_val);
        break;
    }
    case LMD_TYPE_FLOAT: {
        double num = item.get_double();
        print_double(strbuf, num);
        break;
    }
    case LMD_TYPE_DECIMAL: {
        Decimal *decimal = item.get_decimal();
        print_decimal(strbuf, decimal);
        break;
    }
    case LMD_TYPE_STRING: {
        String *string = item.get_string();
        if (string && string->chars) {
            // Safety check: validate string length before assertion
            size_t actual_len = strlen(string->chars);
            if (actual_len != string->len) {
                log_warn("WARNING: String length mismatch. Expected: %u, Actual: %zu\n", string->len, actual_len);
                // Use the actual length to prevent crashes
                strbuf_append_format(strbuf, "\"%.*s\"", (int)actual_len, string->chars);
            } else {
                assert(strlen(string->chars) == string->len && "asserting tring length");
                strbuf_append_format(strbuf, "\"%s\"", string->chars);
            }
        } else {
            strbuf_append_str(strbuf, "\"\"");
        }
        break;
    }
    case LMD_TYPE_SYMBOL: {
        Symbol *symbol = item.get_symbol();
        if (symbol && symbol->chars) {
            // Safety check: validate string length before assertion
            size_t actual_len = strlen(symbol->chars);
            if (actual_len != symbol->len) {
                log_warn("WARNING: Symbol length mismatch. Expected: %u, Actual: %zu\n", symbol->len, actual_len);
                // Use the actual length to prevent crashes
                strbuf_append_format(strbuf, "'%.*s'", (int)actual_len, symbol->chars);
            } else {
                assert(strlen(symbol->chars) == symbol->len && "asserting symbol length");
                strbuf_append_format(strbuf, "'%s'", symbol->chars);
            }
        } else {
            strbuf_append_str(strbuf, "''");
        }
        break;
    }
    case LMD_TYPE_DTIME: {
        DateTime *dt = (DateTime*)item.datetime_ptr;
        if (dt) {
            strbuf_append_str(strbuf, "t'");
            datetime_format_lambda(strbuf, dt);
            strbuf_append_char(strbuf, '\'');
        }
        else {
            strbuf_append_str(strbuf, "[null datetime]");
        }
        break;
    }
    case LMD_TYPE_BINARY: {
        String *string = item.get_string();
        if (string && string->chars) strbuf_append_format(strbuf, "b'%s'", string->chars);
        else strbuf_append_str(strbuf, "b''");
        break;
    }
    case LMD_TYPE_RANGE: {
        Range *range = item.range;
        log_debug("print range: %p, start: %ld, end: %ld", range, range->start, range->end);
        strbuf_append_char(strbuf, '[');
        for (int i = range->start; i <= range->end; i++) {
            if (i > range->start) strbuf_append_str(strbuf, ", ");
            strbuf_append_int(strbuf, i);
        }
        strbuf_append_char(strbuf, ']');
        break;
    }
    case LMD_TYPE_LIST: {
        List *list = item.list;
        if (depth) strbuf_append_char(strbuf, '(');
        for (int i = 0; i < list->length; i++) {
            if (i) strbuf_append_str(strbuf, depth ? ", " : "\n");
            print_item(strbuf, list->items[i], depth, indent);
        }
        if (depth) strbuf_append_char(strbuf, ')');
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array *array = item.array;
        log_debug("print array: %p, length: %ld", array, array->length);
        strbuf_append_char(strbuf, '[');
        for (int i = 0; i < array->length; i++) {
            if (i) strbuf_append_str(strbuf, ", ");
            print_item(strbuf, array->items[i], depth + 1, indent);
        }
        strbuf_append_char(strbuf, ']');
        break;
    }
    case LMD_TYPE_ARRAY_INT: {
        strbuf_append_char(strbuf, '[');
        ArrayInt *array = item.array_int;
        for (int i = 0; i < array->length; i++) {
            if (i) strbuf_append_str(strbuf, ", ");
            strbuf_append_format(strbuf, "%d", array->items[i]);
        }
        strbuf_append_char(strbuf, ']');
        break;
    }
    case LMD_TYPE_ARRAY_INT64: {
        strbuf_append_str(strbuf, "[");
        ArrayInt64 *array = item.array_int64;
        for (int i = 0; i < array->length; i++) {
            if (i) strbuf_append_str(strbuf, ", ");
            strbuf_append_format(strbuf, "%lld", array->items[i]);
        }
        strbuf_append_str(strbuf, "]");
        break;
    }
    case LMD_TYPE_ARRAY_FLOAT: {
        strbuf_append_str(strbuf, "[");
        ArrayFloat *array = item.array_float;
        for (int i = 0; i < array->length; i++) {
            if (i) strbuf_append_str(strbuf, ", ");
            print_double(strbuf, array->items[i]);
        }
        strbuf_append_str(strbuf, "]");
        break;
    }
    case LMD_TYPE_MAP: {
        Map *map = item.map;
        TypeMap *map_type = (TypeMap*)map->type;
        strbuf_append_char(strbuf, '{');
        print_named_items(strbuf, map_type, map->data, depth + 1, indent);
        // add closing indentation if we have nested structures
        if (indent && map_type->length > 0) {
            strbuf_append_char(strbuf, '\n');
            for (int i = 0; i < depth; i++) strbuf_append_str(strbuf, indent);
        }
        strbuf_append_char(strbuf, '}');
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element *element = item.element;
        TypeElmt *elmt_type = (TypeElmt*)element->type;
        strbuf_append_format(strbuf, "<%.*s", (int)elmt_type->name.length, elmt_type->name.str);

        // print attributes
        if (elmt_type->length) {
            print_named_items(strbuf, (TypeMap*)elmt_type, element->data, depth + 1, indent, true);
        }
        // print content
        if (element->length) {
            strbuf_append_str(strbuf, indent ? "\n": (elmt_type->length ? "; ":" "));
            for (long i = 0; i < element->length; i++) {
                if (i) strbuf_append_str(strbuf, indent ? "\n" : "; ");
                if (indent) { for (int i=0; i<depth+1; i++) strbuf_append_str(strbuf, indent); }
                print_item(strbuf, element->items[i], depth + 1, indent);
            }
        }
        // no indentation for closing '>'
        strbuf_append_char(strbuf, '>');
        break;
    }
    case LMD_TYPE_FUNC: {
        Function *func = item.function;
        // TypeFunc *func_type = (TypeFunc*)func->fn;
        strbuf_append_format(strbuf, "[fn %p]", func);  // (int)func->name.length, func->name.str);
        break;
    }
    case LMD_TYPE_TYPE: {
        TypeType *type = (TypeType*)item.type;
        // Check if inner type is a TypeBinary (union/intersection)
        if (type->type->kind == TYPE_KIND_BINARY) {
            strbuf_append_str(strbuf, "type");  // union types print as "type"
            break;
        }
        char* type_name = type_info[type->type->type_id].name;
        if (type->type->type_id == LMD_TYPE_NULL) {
            // print as "type.null"
            strbuf_append_format(strbuf, "type.%s", type_name);
        } else {
            strbuf_append_str(strbuf, type_name);
        }
        break;
    }
    case LMD_TYPE_PATH: {
        Path* path = (Path*)item.item;
        // For sys:// paths, print the resolved content instead of the path
        if (path_get_scheme(path) == PATH_SCHEME_SYS) {
            if (path->result != 0) {
                // Already resolved - print the resolved content
                print_item(strbuf, {.item = path->result}, depth, indent);
                break;
            }
            // Not resolved - just print path (resolution happens during execution)
        }
        // Fall through for non-sys paths or unresolved sys paths
        path_to_string(path, strbuf);
        break;
    }
    case LMD_TYPE_ERROR: {
        strbuf_append_str(strbuf, "error");
        break;
    }
    case LMD_TYPE_ANY:
        strbuf_append_str(strbuf, "any");
        break;
    default:
        strbuf_append_format(strbuf, "[unknown type %s!!]", get_type_name(type_id));
    }
}

void print_root_item(StrBuf *strbuf, Item item, char* indent) {
    print_item(strbuf, item, 0, indent);
    // append last '\n'
    strbuf_append_char(strbuf, '\n');
}

void log_root_item(Item item, char* indent) {
    StrBuf *output = strbuf_new_cap(256);
    print_root_item(output, item, indent);
    log_debug("%s", output->str);
    strbuf_free(output);
}

extern "C" void format_item(StrBuf *strbuf, Item item, int depth, char* indent) {
    print_item(strbuf, item, depth, indent);
}

// Convenience wrapper for testing - prints to stdout
void print_item(Item item, int depth) {
    StrBuf *strbuf = strbuf_new_cap(1024);
    print_item(strbuf, item, depth, nullptr);
    printf("%s", strbuf->str);
    strbuf_free(strbuf);
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
        return "int";
    case LMD_TYPE_INT64:
        return "int64";
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
        return "DateTime*";
    case LMD_TYPE_BINARY:
        return "uint8_t*";

    case LMD_TYPE_LIST:
        return "List*";
    case LMD_TYPE_RANGE:
        return "Range*";
    case LMD_TYPE_ARRAY: {
        TypeArray *array_type = (TypeArray*)type;
        if (array_type->nested && array_type->nested->type_id == LMD_TYPE_INT) {
            return "ArrayInt*";
        } else {
            return "Array*";
        }
    }
    case LMD_TYPE_ARRAY_INT:
        return "ArrayInt*";
    case LMD_TYPE_ARRAY_INT64:
        return "ArrayInt64*";
    case LMD_TYPE_ARRAY_FLOAT:
        return "ArrayFloat*";
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

void log_item(Item item, const char* msg) {
    StrBuf *strbuf = strbuf_new();
    print_item(strbuf, item, 0, NULL);
    log_debug("%s: %s", msg, strbuf->str);
    strbuf_free(strbuf);
}

void print_label(int indent, const char *label) {
    log_debug("  %s", label);
}

void print_const(Script *script, Type* type) {
    char* type_name = type_info[type->type_id].name;
    if (type->type_id == LMD_TYPE_NULL || type->type_id == LMD_TYPE_BOOL || type->type_id == LMD_TYPE_INT) {
        log_debug("[const: %s]", type_name);  return;
    }
    TypeConst *const_type = (TypeConst*)type;
    void* data = script->const_list->data[const_type->const_index];
    switch (type->type_id) {
    case LMD_TYPE_FLOAT: {
        double num = *(double*)data;
        log_debug("[const@%d, %s, %g]", const_type->const_index, type_name, num);
        break;
    }
    case LMD_TYPE_INT64: {
        int64_t num = *(int64_t*)data;
        log_debug("[const@%d, %s, %" PRId64 "]", const_type->const_index, type_name, num);
        break;
    }
    case LMD_TYPE_DTIME: {
        DateTime datetime = *(DateTime*)data;
        StrBuf *strbuf = strbuf_new();
        datetime_format_lambda(strbuf, &datetime);
        log_debug("[const@%d, %s, '%s']", const_type->const_index, type_name, strbuf->str);
        strbuf_free(strbuf);
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_BINARY: {
        String* string = (String*)data;
        log_debug("[const@%d, %s, %p, '%.*s']", const_type->const_index,
            type_name, string, (int)string->len, string->chars);
        break;
    }
    case LMD_TYPE_SYMBOL: {
        Symbol* symbol = (Symbol*)data;
        log_debug("[const@%d, %s, %p, '%.*s']", const_type->const_index,
            type_name, symbol, (int)symbol->len, symbol->chars);
        break;
    }
    case LMD_TYPE_DECIMAL: {
        Decimal *decimal = (Decimal*)data;
        StrBuf *strbuf = strbuf_new();
        print_decimal(strbuf, decimal);
        log_debug("[const@%d, %s, %s]", const_type->const_index, type_name, strbuf->str);
        strbuf_free(strbuf);
        break;
    }
    default:  // LMD_TYPE_BOOL, LMD_TYPE_INT should not be in const pool
        log_debug("[const: %s, unexpected!!]", type_name);
    }
}

void print_ast_node(Script *script, AstNode *node, int indent) {
    if (!script) {
        log_debug("[null script]");  return;
    }
    if (!node) {
        log_debug("[null node]");  return;
    }
    if (indent > 0) log_enter();
    const char* type_name = node->type ? type_info[node->type->type_id].name : "unknown";
    // log_debug("print_ast_node: node_type=%d, name=%s", node->node_type, type_name);
    switch(node->node_type) {
    case AST_NODE_IDENT:
        log_debug("[ident:%.*s:%s,const:%d]", (int)((AstIdentNode*)node)->name->len,
            ((AstIdentNode*)node)->name->chars, type_name, node->type ? node->type->is_const : -1);
        break;
    case AST_NODE_PRIMARY:
        log_debug("[primary expr:%s,const:%d]", type_name, node->type ? node->type->is_const : -1);
        if (((AstPrimaryNode*)node)->expr) {
            print_ast_node(script, ((AstPrimaryNode*)node)->expr, indent + 1);
        } else {
            // for (int i = 0; i < indent+1; i++) { log_debug("  "); }
            log_enter();
            if (node->type && node->type->is_const) {
                print_const(script, node->type);
            }
            else { log_debug("(%s)", ts_node_type(node->node)); }
            log_leave();
        }
        break;
    case AST_NODE_UNARY:
    case AST_NODE_SPREAD:
        log_debug("[unary expr %.*s:%s]", (int)((AstUnaryNode*)node)->op_str.length,
            ((AstUnaryNode*)node)->op_str.str, type_name);
        print_ast_node(script, ((AstUnaryNode*)node)->operand, indent + 1);
        break;
    case AST_NODE_BINARY: {
        AstBinaryNode* bnode = (AstBinaryNode*)node;
        log_debug("[binary expr %.*s.%d:%s]", (int)bnode->op_str.length, bnode->op_str.str,
            bnode->op, type_name);
        print_ast_node(script, bnode->left, indent + 1);
        print_ast_node(script, bnode->right, indent + 1);
        break;
    }
    case AST_NODE_IF_EXPR: {
        log_debug("[if expr:%s]", type_name);
        AstIfNode* if_node = (AstIfNode*)node;
        print_ast_node(script, if_node->cond, indent + 1);
        print_label(indent + 1, "then:");
        print_ast_node(script, if_node->then, indent + 1);
        if (if_node->otherwise) {
            print_label(indent + 1, "else:");
            print_ast_node(script, if_node->otherwise, indent + 1);
        }
        break;
    }
    case AST_NODE_IF_STAM: {
        log_debug("[if stam:%s]", type_name);
        AstIfNode* if_node = (AstIfNode*)node;
        print_ast_node(script, if_node->cond, indent + 1);
        print_label(indent + 1, "then:");
        print_ast_node(script, if_node->then, indent + 1);
        if (if_node->otherwise) {
            print_label(indent + 1, "else:");
            print_ast_node(script, if_node->otherwise, indent + 1);
        }
        break;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        log_debug("[match expr:%s] arms=%d", type_name, match->arm_count);
        print_label(indent + 1, "scrutinee:");
        print_ast_node(script, match->scrutinee, indent + 1);
        AstMatchArm* arm = match->first_arm;
        while (arm) {
            if (arm->pattern) {
                print_label(indent + 1, "pattern:");
                print_ast_node(script, arm->pattern, indent + 2);
            } else {
                print_label(indent + 1, "default:");
            }
            print_label(indent + 1, "body:");
            print_ast_node(script, arm->body, indent + 2);
            arm = (AstMatchArm*)arm->next;
        }
        break;
    }
    case AST_NODE_TYPE_STAM: {
        log_debug("[type def:%s]", type_name);
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            print_label(indent + 1, "declare:");
            print_ast_node(script, declare, indent + 1);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_LET_STAM:  case AST_NODE_PUB_STAM: {
        log_debug("[%s stam:%s]", node->node_type == AST_NODE_PUB_STAM ? "pub" : "let", type_name);
        AstNode *declare = ((AstLetNode*)node)->declare;
        while (declare) {
            print_label(indent + 1, "declare:");
            print_ast_node(script, declare, indent + 1);
            declare = declare->next;
        }
        break;
    }
    case AST_NODE_FOR_EXPR: {
        log_debug("[for expr:%s]", type_name);
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            print_label(indent + 1, "loop:");
            print_ast_node(script, loop, indent + 1);
            loop = loop->next;
        }
        print_label(indent + 1, "then:");
        print_ast_node(script, ((AstForNode*)node)->then, indent + 1);
        break;
    }
    case AST_NODE_FOR_STAM: {
        log_debug("[for stam:%s]", type_name);
        AstNode *loop = ((AstForNode*)node)->loop;
        while (loop) {
            print_label(indent + 1, "loop:");
            print_ast_node(script, loop, indent + 1);
            loop = loop->next;
        }
        print_label(indent + 1, "then:");
        print_ast_node(script, ((AstForNode*)node)->then, indent + 1);
        break;
    }
    case AST_NODE_ASSIGN: {
        AstNamedNode* assign = (AstNamedNode*)node;
        log_debug("[assign expr:%.*s:%s]", (int)assign->name->len, assign->name->chars, type_name);
        print_ast_node(script, assign->as, indent + 1);
        break;
    }
    case AST_NODE_KEY_EXPR: {
        AstNamedNode* key = (AstNamedNode*)node;
        log_debug("[key expr:%.*s:%s]", (int)key->name->len, key->name->chars, type_name);
        print_ast_node(script, key->as, indent + 1);
        break;
    }
    case AST_NODE_LOOP:
        log_debug("[loop expr:%s]", type_name);
        print_ast_node(script, ((AstNamedNode*)node)->as, indent + 1);
        break;
    case AST_NODE_ARRAY: {
        log_debug("[array expr:%s]", type_name);
        AstNode *item = ((AstArrayNode*)node)->item;
        while (item) {
            print_label(indent + 1, "item:");
            print_ast_node(script, item, indent + 1);
            item = item->next;
        }
        break;
    }
    case AST_NODE_LIST:  case AST_NODE_CONTENT:  case AST_NODE_CONTENT_TYPE: {
        AstListNode* list_node = (AstListNode*)node;
        log_debug("[%s:%s[%ld]]",
            node->node_type == AST_NODE_CONTENT_TYPE ? "content_type" :
            node->node_type == AST_NODE_CONTENT ? "content" : "list",
            type_name, list_node->list_type->length);
        AstNode *ld = list_node->declare;
        if (!ld) {
            print_label(indent + 1, "no declare");
        }
        while (ld) {
            print_label(indent + 1, "declare:");
            print_ast_node(script, ld, indent + 1);
            ld = ld->next;
        }
        AstNode *li = list_node->item;
        while (li) {
            print_label(indent + 1, "item:");
            print_ast_node(script, li, indent + 1);
            li = li->next;
        }
        break;
    }
    case AST_NODE_MAP: {
        log_debug("[map expr:%s]", type_name);
        AstNode *nm_item = ((AstMapNode*)node)->item;
        while (nm_item) {
            print_label(indent + 1, "map item:");
            print_ast_node(script, nm_item, indent + 1);
            nm_item = nm_item->next;
        }
        break;
    }
    case AST_NODE_ELEMENT: {
        log_debug("[elmt expr:%s]", type_name);
        AstElementNode* elmt_node = (AstElementNode*)node;
        AstNode *elmt_item = elmt_node->item;
        while (elmt_item) {
            print_label(indent + 1, "attr:");
            print_ast_node(script, elmt_item, indent + 1);
            elmt_item = elmt_item->next;
        }
        if (elmt_node->content) print_ast_node(script, elmt_node->content, indent + 1);
        break;
    }
    case AST_NODE_PARAM: {
        AstNamedNode* param = (AstNamedNode*)node;
        log_debug("[param: %.*s:%s]", (int)param->name->len, param->name->chars, type_name);
        break;
    }
    case AST_NODE_MEMBER_EXPR:  case AST_NODE_INDEX_EXPR:
        log_debug("[%s expr:%s]", node->node_type == AST_NODE_MEMBER_EXPR ? "member" : "index", type_name);
        print_label(indent + 1, "object:");
        print_ast_node(script, ((AstFieldNode*)node)->object, indent + 1);
        print_label(indent + 1, "field:");
        print_ast_node(script, ((AstFieldNode*)node)->field, indent + 1);
        break;
    case AST_NODE_CALL_EXPR: {
        Type *type = node->type;
        log_debug("[call expr:%s,const:%d]", type_name, type->is_const);
        print_ast_node(script, ((AstCallNode*)node)->function, indent + 1);
        print_label(indent + 1, "args:");
        AstNode* arg = ((AstCallNode*)node)->argument;
        while (arg) {
            log_debug("  (arg:%s)", arg->type ? type_info[arg->type->type_id].name : "unknown");
            print_ast_node(script, arg, indent + 1);
            arg = arg->next;
        }
        break;
    }
    case AST_NODE_SYS_FUNC: {
        AstSysFuncNode* sys_node = (AstSysFuncNode*)node;
        log_debug("[sys %s_%s:%s]", sys_node->fn_info->is_proc ? "pn" : "fn", sys_node->fn_info->name, type_name);
        break;
    }
    case AST_NODE_FUNC:  case AST_NODE_FUNC_EXPR: case AST_NODE_PROC: {
        // function definition
        AstFuncNode* func = (AstFuncNode*)node;
        if (node->node_type == AST_NODE_FUNC_EXPR) {
            log_debug("[fn expr:%s]", type_name);
        }
        else if (node->node_type == AST_NODE_FUNC) {
            log_debug("[fn: %.*s:%s]", (int)func->name->len, func->name->chars, type_name);
        }
        else {
            log_debug("[pn: %.*s:%s]", (int)func->name->len, func->name->chars, type_name);
        }
        print_label(indent + 1, "params:");
        AstNode* fn_param = (AstNode*)func->param;
        while (fn_param) {
            print_ast_node(script, fn_param, indent + 1);
            fn_param = fn_param->next;
        }
        print_ast_node(script, func->body, indent + 1);
        break;
    }
    case AST_NODE_TYPE: {
        TypeType* actual_type = (TypeType*)node->type;
        assert(node->type->type_id == LMD_TYPE_TYPE && actual_type->type);
        char* actual_type_name = type_info[actual_type->type->type_id].name;
        log_debug("[%s: %s]", type_name, actual_type_name);
        break;
    }
    case AST_NODE_LIST_TYPE: {
        log_debug("[list type:%s]", type_name);
        AstNode *ls_item = ((AstListNode*)node)->item;
        while (ls_item) {
            print_label(indent + 1, "item:");
            print_ast_node(script, ls_item, indent + 1);
            ls_item = ls_item->next;
        }
        break;
    }
    case AST_NODE_ARRAY_TYPE: {
        log_debug("[array type:%s]", type_name);
        AstNode *arr_item = ((AstArrayNode*)node)->item;
        while (arr_item) {
            print_label(indent + 1, "item:");
            print_ast_node(script, arr_item, indent + 1);
            arr_item = arr_item->next;
        }
        break;
    }
    case AST_NODE_MAP_TYPE: {
        log_debug("[map type:%s]", type_name);
        AstNode *mt_item = ((AstMapNode*)node)->item;
        while (mt_item) {
            print_label(indent + 1, "map item:");
            print_ast_node(script, mt_item, indent + 1);
            mt_item = mt_item->next;
        }
        break;
    }
    case AST_NODE_ELMT_TYPE: {
        log_debug("[elmt type:%s]", type_name);
        AstElementNode* et_node = (AstElementNode*)node;
        AstNode *et_item = et_node->item;
        while (et_item) {
            print_label(indent + 1, "attr:");
            print_ast_node(script, et_item, indent + 1);
            et_item = et_item->next;
        }
        if (et_node->content) print_ast_node(script, et_node->content, indent + 1);
        break;
    }
    case AST_NODE_FUNC_TYPE: {
        log_debug("[func type:%s]", type_name);
        AstFuncNode* ft = (AstFuncNode*)node;
        print_label(indent + 1, "params:");
        AstNode* ft_param = (AstNode*)ft->param;
        while (ft_param) {
            print_ast_node(script, ft_param, indent + 1);
            ft_param = ft_param->next;
        }
        break;
    }
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* bt_node = (AstBinaryNode*)node;
        log_debug("[binary type %.*s.%d:%s]", (int)bt_node->op_str.length, bt_node->op_str.str,
            bt_node->op, type_name);
        print_ast_node(script, bt_node->left, indent + 1);
        print_ast_node(script, bt_node->right, indent + 1);
        break;
    }
    case AST_NODE_UNARY_TYPE: {
        AstUnaryNode* ut_node = (AstUnaryNode*)node;
        log_debug("[unary type %.*s.%d:%s]", (int)ut_node->op_str.length, ut_node->op_str.str,
            ut_node->op, type_name);
        print_ast_node(script, ut_node->operand, indent + 1);
        break;
    }
    case AST_NODE_IMPORT: {
        AstImportNode* import_node = (AstImportNode*)node;
        if (!import_node->module.str) {
            log_debug("[import: missing module!!]");
        } else {
            log_debug("[import %.*s%s%.*s]",
                (int)import_node->module.length, import_node->module.str,
                (import_node->alias ? ":" : ""),
                (int)(import_node->alias ? import_node->alias->len:0), (import_node->alias ? import_node->alias->chars : ""));
        }
        break;
    }
    case AST_SCRIPT: {
        log_debug("[script:%s]", type_name);
        AstNode* child = ((AstScript*)node)->child;
        while (child) {
            print_ast_node(script, child, indent + 1);
            child = child->next;
        }
        break;
    }
    default:
        log_debug("[unknown expression type: %d!]", node->node_type);
        break;
    }
    if (indent > 0) log_leave();
}

void print_ast_root(Script *script) {
    AstNode *node = script->ast_root;
    print_ast_node(script, node, 0);
}

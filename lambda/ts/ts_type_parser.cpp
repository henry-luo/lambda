// ts_type_parser.cpp — direct-parser admitted TypeScript type reductions

#include "ts_type_parser.hpp"
#include "../js/js_c_ast_helpers.hpp"
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/log.h"

#include <cctype>
#include <cstring>

// The production C parser owns syntax admission. This reducer translates its
// accepted spans directly to Lambda Type* facts, without a second type AST.
struct TsDirectTypeParser {
    JsTranspiler* tp;
    const char* text;
    int len;
    int pos;

    String* make_string(const char* source, int source_len) {
        String* result = (String*)pool_alloc(tp->pool,
            sizeof(String) + source_len + 1);
        if (!result) return NULL;
        result->len = source_len;
        result->flags = 0;
        result->is_ascii = 1;
        memcpy(result->chars, source, source_len);
        result->chars[source_len] = '\0';
        return result;
    }

    Type* make_base(TypeId type_id) {
        return (Type*)alloc_type(tp->pool, type_id, sizeof(Type));
    }

    Type* make_any() { return make_base(LMD_TYPE_ANY); }

    void skip_space() {
        while (pos < len) {
            if (isspace((unsigned char)text[pos])) {
                pos++;
            } else if (pos + 1 < len && text[pos] == '/' && text[pos + 1] == '/') {
                pos += 2;
                while (pos < len && text[pos] != '\n') pos++;
            } else if (pos + 1 < len && text[pos] == '/' && text[pos + 1] == '*') {
                pos += 2;
                while (pos + 1 < len &&
                        !(text[pos] == '*' && text[pos + 1] == '/')) pos++;
                if (pos + 1 < len) pos += 2;
            } else {
                break;
            }
        }
    }

    bool match_char(char expected) {
        skip_space();
        if (pos >= len || text[pos] != expected) return false;
        pos++;
        return true;
    }

    bool peek_char(char expected) {
        skip_space();
        return pos < len && text[pos] == expected;
    }

    bool read_identifier(const char** start_out, int* length_out) {
        skip_space();
        if (pos >= len || !(isalpha((unsigned char)text[pos]) ||
                text[pos] == '_' || text[pos] == '$')) return false;
        int start = pos++;
        while (pos < len && (isalnum((unsigned char)text[pos]) ||
                text[pos] == '_' || text[pos] == '$')) pos++;
        *start_out = text + start;
        *length_out = pos - start;
        return true;
    }

    bool match_word(const char* expected) {
        int saved = pos;
        const char* start = NULL;
        int word_len = 0;
        if (!read_identifier(&start, &word_len)) return false;
        int expected_len = (int)strlen(expected);
        if (word_len == expected_len &&
                memcmp(start, expected, expected_len) == 0) return true;
        pos = saved;
        return false;
    }

    Type* parse_type() { return parse_union(); }

    Type* parse_union() {
        Type* result = parse_intersection();
        if (!result) return make_any();
        while (peek_char('|')) {
            pos++;
            Type* right = parse_intersection();
            if (!right) return make_any();
            TypeBinary* binary = (TypeBinary*)alloc_type(tp->pool,
                LMD_TYPE_TYPE, sizeof(TypeBinary));
            if (!binary) return NULL;
            binary->kind = TYPE_KIND_BINARY;
            binary->op = OPERATOR_UNION;
            binary->left = result;
            binary->right = right;
            result = (Type*)binary;
        }
        return result;
    }

    Type* parse_intersection() {
        Type* result = parse_postfix();
        if (!result) return make_any();
        while (peek_char('&')) {
            pos++;
            Type* right = parse_postfix();
            if (!right) return make_any();
            TypeBinary* binary = (TypeBinary*)alloc_type(tp->pool,
                LMD_TYPE_TYPE, sizeof(TypeBinary));
            if (!binary) return NULL;
            binary->kind = TYPE_KIND_BINARY;
            binary->op = OPERATOR_INTERSECT;
            binary->left = result;
            binary->right = right;
            result = (Type*)binary;
        }
        return result;
    }

    Type* parse_postfix() {
        Type* result = parse_primary();
        while (result && match_char('[')) {
            // The C parser only admits `T[]`; indexed-access syntax has
            // already been rejected before this reduction is entered.
            if (!match_char(']')) return make_any();
            TypeArray* array = (TypeArray*)alloc_type(tp->pool,
                LMD_TYPE_ARRAY, sizeof(TypeArray));
            if (!array) return NULL;
            array->nested = result;
            result = (Type*)array;
        }
        return result;
    }

    Type* parse_primary() {
        skip_space();
        if (pos >= len) return make_any();
        if (text[pos] == '(') return parse_parenthesized_or_function();
        if (text[pos] == '[') return parse_tuple();
        if (text[pos] == '{') return parse_object();
        if (text[pos] == '\'' || text[pos] == '"') return parse_string_literal();
        if (isdigit((unsigned char)text[pos])) return parse_number_literal();

        if (match_word("readonly")) return parse_primary();
        if (match_word("keyof") || match_word("typeof")) return parse_primary();

        const char* name = NULL;
        int name_len = 0;
        if (!read_identifier(&name, &name_len)) return make_any();
        TypeId type_id = ts_predefined_name_to_type_id(name, name_len);
        if (type_id != LMD_TYPE_ANY ||
                (name_len == 3 && memcmp(name, "any", 3) == 0)) {
            return make_base(type_id);
        }
        return parse_reference(name, name_len);
    }

    Type* parse_reference(const char* name, int name_len) {
        String* type_name = make_string(name, name_len);
        if (!type_name) return NULL;
        Type* args[32];
        int count = 0;
        if (match_char('<')) {
            if (!peek_char('>')) {
                do {
                    Type* argument = parse_type();
                    if (count < 32) args[count++] = argument;
                } while (match_char(','));
            }
            if (!match_char('>')) return make_any();
        }

        Type* found = ts_type_registry_lookup(tp, type_name->chars);
        if (found) return found;
        if (type_name->len == 5 && memcmp(type_name->chars, "Array", 5) == 0) {
            TypeArray* array = (TypeArray*)alloc_type(tp->pool,
                LMD_TYPE_ARRAY, sizeof(TypeArray));
            if (!array) return NULL;
            array->nested = count > 0 ? args[0] : NULL;
            return (Type*)array;
        }
        if (type_name->len == 6 && memcmp(type_name->chars, "Record", 6) == 0) {
            return make_base(LMD_TYPE_MAP);
        }
        if (type_name->len == 7 && memcmp(type_name->chars, "Promise", 7) == 0) {
            return count > 0 ? args[0] : make_any();
        }
        log_error("ts type unresolved: %.*s", type_name->len, type_name->chars);
        return make_any();
    }

    Type* parse_parenthesized_or_function() {
        (void)match_char('(');
        Type* params[32];
        int count = 0;
        if (!peek_char(')')) {
            do {
                if (count == 32) return make_any();
                int saved = pos;
                const char* name = NULL;
                int name_len = 0;
                bool named = read_identifier(&name, &name_len);
                if (named) {
                    (void)match_char('?');
                    named = match_char(':');
                }
                if (!named) pos = saved;
                params[count++] = parse_type();
            } while (match_char(','));
        }
        if (!match_char(')')) return make_any();
        if (!match_char('=')) return count == 1 ? params[0] : make_any();
        if (!match_char('>')) return make_any();

        TypeFunc* function = (TypeFunc*)alloc_type(tp->pool, LMD_TYPE_FUNC,
            sizeof(TypeFunc));
        if (!function) return NULL;
        function->returned = parse_type();
        function->inferred_return = function->returned;
        function->return_contract = function->returned;
        function->has_explicit_return_contract = function->returned != NULL;
        function->param_count = count;
        function->required_param_count = count;
        TypeParam* previous = NULL;
        for (int i = 0; i < count; i++) {
            TypeParam* parameter = (TypeParam*)pool_calloc(tp->pool,
                sizeof(TypeParam));
            if (!parameter) return NULL;
            parameter->type_id = LMD_TYPE_TYPE;
            parameter->kind = TYPE_KIND_PARAM;
            parameter->full_type = params[i];
            parameter->contract_type = params[i];
            parameter->has_explicit_contract = true;
            if (previous) previous->next = parameter;
            else function->param = parameter;
            previous = parameter;
        }
        return (Type*)function;
    }

    Type* parse_tuple() {
        (void)match_char('[');
        Type* first = NULL;
        int count = 0;
        if (!peek_char(']')) {
            do {
                Type* element = parse_type();
                if (count++ == 0) first = element;
            } while (match_char(','));
        }
        if (!match_char(']')) return make_any();
        TypeArray* tuple = (TypeArray*)alloc_type(tp->pool, LMD_TYPE_ARRAY,
            sizeof(TypeArray));
        if (!tuple) return NULL;
        tuple->length = count;
        tuple->nested = first;
        return (Type*)tuple;
    }

    Type* parse_object() {
        (void)match_char('{');
        TypeMap* object = (TypeMap*)alloc_type(tp->pool, LMD_TYPE_MAP,
            sizeof(TypeMap));
        if (!object) return NULL;
        ShapeEntry* previous = NULL;
        while (!peek_char('}') && pos < len) {
            if (match_char(';') || match_char(',')) continue;
            String* name = NULL;
            bool optional = false;
            if (peek_char('(')) {
                (void)match_char('(');
                if (!match_char(')') || !match_char(':')) return make_any();
                name = make_string("()", 2);
            } else {
                const char* source = NULL;
                int source_len = 0;
                if (!read_identifier(&source, &source_len)) return make_any();
                name = make_string(source, source_len);
                optional = match_char('?');
                if (!match_char(':')) return make_any();
            }
            if (!name) return NULL;
            Type* member_type = parse_type();
            if (!member_type) return NULL;
            ShapeEntry* entry = (ShapeEntry*)pool_calloc(tp->pool,
                sizeof(ShapeEntry));
            if (!entry) return NULL;
            entry->name = (StrView*)pool_calloc(tp->pool, sizeof(StrView));
            if (!entry->name) return NULL;
            entry->name->str = name->chars;
            entry->name->length = name->len;
            if (optional) {
                TypeUnary* unary = (TypeUnary*)alloc_type(tp->pool,
                    LMD_TYPE_TYPE, sizeof(TypeUnary));
                if (!unary) return NULL;
                unary->kind = TYPE_KIND_UNARY;
                unary->op = OPERATOR_OPTIONAL;
                unary->operand = member_type;
                entry->type = (Type*)unary;
            } else {
                entry->type = member_type;
            }
            if (previous) previous->next = entry;
            else object->shape = entry;
            previous = entry;
            object->length++;
        }
        if (!match_char('}')) return make_any();
        object->last = previous;
        typemap_hash_build(object, tp->pool);
        return (Type*)object;
    }

    Type* parse_string_literal() {
        char quote = text[pos++];
        while (pos < len && text[pos] != quote) {
            if (text[pos] == '\\' && pos + 1 < len) pos++;
            pos++;
        }
        if (pos < len) pos++;
        return make_base(LMD_TYPE_STRING);
    }

    Type* parse_number_literal() {
        while (pos < len && (isdigit((unsigned char)text[pos]) ||
                text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E' ||
                text[pos] == '+' || text[pos] == '-')) pos++;
        return make_base(LMD_TYPE_FLOAT);
    }
};

TypeId ts_predefined_name_to_type_id(const char* name, int len) {
    if (len == 6 && memcmp(name, "number", 6) == 0) return LMD_TYPE_FLOAT;
    if (len == 6 && memcmp(name, "string", 6) == 0) return LMD_TYPE_STRING;
    if (len == 7 && memcmp(name, "boolean", 7) == 0) return LMD_TYPE_BOOL;
    if (len == 4 && memcmp(name, "null", 4) == 0) return LMD_TYPE_NULL;
    if (len == 9 && memcmp(name, "undefined", 9) == 0) return LMD_TYPE_NULL;
    if (len == 4 && memcmp(name, "void", 4) == 0) return LMD_TYPE_NULL;
    if (len == 3 && memcmp(name, "any", 3) == 0) return LMD_TYPE_ANY;
    if (len == 7 && memcmp(name, "unknown", 7) == 0) return LMD_TYPE_ANY;
    if (len == 5 && memcmp(name, "never", 5) == 0) return LMD_TYPE_ERROR;
    if (len == 6 && memcmp(name, "object", 6) == 0) return LMD_TYPE_MAP;
    if (len == 6 && memcmp(name, "symbol", 6) == 0) return LMD_TYPE_SYMBOL;
    if (len == 6 && memcmp(name, "bigint", 6) == 0) return LMD_TYPE_INT64;
    return LMD_TYPE_ANY;
}

TsTypeFactNode* ts_parse_type_text(JsTranspiler* tp, const char* text, int len) {
    if (!tp) return NULL;
    TsDirectTypeParser parser = {tp, text, len, 0};
    Type* type = text && len > 0 ? parser.parse_type() : parser.make_any();
    if (!type) type = parser.make_any();
    TsTypeFactNode* fact = (TsTypeFactNode*)alloc_js_ast_node_span(tp,
        (JsAstNodeType)TS_AST_NODE_TYPE_FACT, (SourceSpan){0, 0},
        sizeof(TsTypeFactNode));
    if (!fact) return NULL;
    fact->resolved_type = type;
    return fact;
}

HASHMAP_DEFINE_STRKEY(ts_type_reg, TsTypeRegistryEntry, name)

void ts_type_registry_init(JsTranspiler* tp) {
    tp->type_registry = hashmap_new(sizeof(TsTypeRegistryEntry), 32, 0, 0,
        ts_type_reg_hash, ts_type_reg_cmp, NULL, NULL);
}

void ts_type_registry_add(JsTranspiler* tp, const char* name, Type* type) {
    TsTypeRegistryEntry entry;
    memset(&entry, 0, sizeof(entry));
    size_t name_len = strlen(name);
    if (name_len >= sizeof(entry.name)) name_len = sizeof(entry.name) - 1;
    memcpy(entry.name, name, name_len);
    entry.name[name_len] = '\0';
    entry.type = type;
    hashmap_set(tp->type_registry, &entry);
}

Type* ts_type_registry_lookup(JsTranspiler* tp, const char* name) {
    TsTypeRegistryEntry query;
    memset(&query, 0, sizeof(query));
    size_t name_len = strlen(name);
    if (name_len >= sizeof(query.name)) name_len = sizeof(query.name) - 1;
    memcpy(query.name, name, name_len);
    query.name[name_len] = '\0';
    const TsTypeRegistryEntry* found =
        (const TsTypeRegistryEntry*)hashmap_get(tp->type_registry, &query);
    return found ? found->type : NULL;
}

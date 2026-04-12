// ts_type_parser.cpp — Recursive descent parser for opaque TS type text
//
// Parses raw type text (e.g. "string | number", "(a: Foo) => Bar",
// "{ x: number; y?: string }") into TsTypeNode* AST trees.
//
// Grammar (simplified):
//   type            = conditional_type
//   conditional_type = union_type ('extends' union_type '?' type ':' type)?
//   union_type      = intersection_type ('|' intersection_type)*
//   intersection_type = unary_type ('&' unary_type)*
//   unary_type      = 'keyof' unary_type | 'typeof' typeof_expr | 'infer' IDENT
//                   | 'readonly' unary_type | 'unique' 'symbol' | postfix_type
//   postfix_type    = primary_type ('[' ']' | '[' type ']')*
//   primary_type    = predefined_type | IDENT generic_args? ('.' IDENT generic_args?)*
//                   | '(' param_list ')' '=>' type      (function type)
//                   | '(' type ')'                      (parenthesized)
//                   | '[' type_list ']'                  (tuple)
//                   | '{' member_list '}'                (object / mapped)
//                   | literal_type | template_literal_type

#include "ts_type_parser.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include <cstring>
#include <cctype>
#include "../../lib/mem.h"

// ============================================================================
// Parser state
// ============================================================================

struct TsTypeParser {
    JsTranspiler* tp;
    const char* text;
    int len;
    int pos;
    TSNode null_node; // zeroed TSNode for allocation

    // allocation helpers
    JsAstNode* alloc_node(TsAstNodeType type, size_t size) {
        return alloc_js_ast_node(tp, (JsAstNodeType)type, null_node, size);
    }

    String* make_string(const char* src, int slen) {
        String* s = (String*)pool_alloc(tp->ast_pool, sizeof(String) + slen + 1);
        s->len = slen;
        s->is_ascii = 1;
        memcpy(s->chars, src, slen);
        s->chars[slen] = '\0';
        return s;
    }

    // lexer helpers
    int ch() { return pos < len ? (unsigned char)text[pos] : 0; }
    int peek(int offset) { return (pos + offset) < len ? (unsigned char)text[pos + offset] : 0; }
    void advance_one() { if (pos < len) pos++; }

    void skip_ws() {
        while (pos < len) {
            if (isspace((unsigned char)text[pos])) {
                pos++;
            } else if (pos + 1 < len && text[pos] == '/' && text[pos + 1] == '/') {
                pos += 2;
                while (pos < len && text[pos] != '\n') pos++;
            } else if (pos + 1 < len && text[pos] == '/' && text[pos + 1] == '*') {
                pos += 2;
                while (pos + 1 < len && !(text[pos] == '*' && text[pos + 1] == '/')) pos++;
                if (pos + 1 < len) pos += 2;
            } else {
                break;
            }
        }
    }

    bool at_end() { skip_ws(); return pos >= len; }

    bool match_char(char c) {
        skip_ws();
        if (pos < len && text[pos] == c) { pos++; return true; }
        return false;
    }

    bool match_str(const char* s) {
        skip_ws();
        int slen = (int)strlen(s);
        if (pos + slen > len) return false;
        if (memcmp(text + pos, s, slen) != 0) return false;
        // for keyword matching, ensure next char is not alphanumeric/underscore
        if (slen > 0 && isalpha((unsigned char)s[0])) {
            if (pos + slen < len && (isalnum((unsigned char)text[pos + slen]) || text[pos + slen] == '_'))
                return false;
        }
        pos += slen;
        return true;
    }

    bool check_char(char c) {
        skip_ws();
        return pos < len && text[pos] == c;
    }

    bool check_str(const char* s) {
        skip_ws();
        int slen = (int)strlen(s);
        if (pos + slen > len) return false;
        if (memcmp(text + pos, s, slen) != 0) return false;
        if (slen > 0 && isalpha((unsigned char)s[0])) {
            if (pos + slen < len && (isalnum((unsigned char)text[pos + slen]) || text[pos + slen] == '_'))
                return false;
        }
        return true;
    }

    bool is_ident_start(int c) { return isalpha(c) || c == '_' || c == '$'; }
    bool is_ident_char(int c) { return isalnum(c) || c == '_' || c == '$'; }

    // read an identifier, return pointer and length (does NOT allocate)
    bool read_ident(const char** out, int* out_len) {
        skip_ws();
        if (pos >= len || !is_ident_start((unsigned char)text[pos])) return false;
        int start = pos;
        while (pos < len && is_ident_char((unsigned char)text[pos])) pos++;
        *out = text + start;
        *out_len = pos - start;
        return true;
    }

    // check if an identifier matches a keyword without consuming it
    bool is_keyword(const char* kw) {
        skip_ws();
        int klen = (int)strlen(kw);
        if (pos + klen > len) return false;
        if (memcmp(text + pos, kw, klen) != 0) return false;
        if (pos + klen < len && is_ident_char((unsigned char)text[pos + klen])) return false;
        return true;
    }

    // predefined type check
    bool is_predefined(const char* name, int nlen) {
        return ts_predefined_name_to_type_id(name, nlen) != LMD_TYPE_ANY
            || (nlen == 3 && memcmp(name, "any", 3) == 0);
    }

    // ================================================================
    // fallback: return `any` type
    // ================================================================
    TsTypeNode* make_any() {
        TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_node(
            TS_AST_NODE_PREDEFINED_TYPE, sizeof(TsPredefinedTypeNode));
        pn->predefined_id = LMD_TYPE_ANY;
        return (TsTypeNode*)pn;
    }

    // ================================================================
    // type = conditional_type
    // ================================================================
    TsTypeNode* parse_type() {
        return parse_conditional_type();
    }

    // ================================================================
    // conditional_type = union_type ('extends' union_type '?' type ':' type)?
    // ================================================================
    TsTypeNode* parse_conditional_type() {
        TsTypeNode* left = parse_union_type();
        if (!left) return make_any();

        if (check_str("extends")) {
            pos += 7; // consume "extends"
            TsTypeNode* extends_type = parse_union_type();
            if (!match_char('?')) return left; // malformed, return what we have
            TsTypeNode* true_type = parse_type();
            if (!match_char(':')) return left;
            TsTypeNode* false_type = parse_type();

            TsConditionalTypeNode* cn = (TsConditionalTypeNode*)alloc_node(
                TS_AST_NODE_CONDITIONAL_TYPE, sizeof(TsConditionalTypeNode));
            cn->check_type = left;
            cn->extends_type = extends_type;
            cn->true_type = true_type;
            cn->false_type = false_type;
            return (TsTypeNode*)cn;
        }
        return left;
    }

    // ================================================================
    // union_type = intersection_type ('|' intersection_type)*
    // ================================================================
    TsTypeNode* parse_union_type() {
        // handle leading |
        if (check_char('|')) { advance_one(); skip_ws(); }

        TsTypeNode* first = parse_intersection_type();
        if (!first) return nullptr;

        if (!check_char('|')) return first;

        // collect all union members
        TsTypeNode* members[64];
        int count = 0;
        members[count++] = first;

        while (check_char('|') && count < 64) {
            advance_one(); // consume '|'
            TsTypeNode* next = parse_intersection_type();
            if (next) members[count++] = next;
        }

        if (count == 1) return first;

        TsUnionTypeNode* un = (TsUnionTypeNode*)alloc_node(
            TS_AST_NODE_UNION_TYPE, sizeof(TsUnionTypeNode));
        un->types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * count);
        un->type_count = count;
        memcpy(un->types, members, sizeof(TsTypeNode*) * count);
        return (TsTypeNode*)un;
    }

    // ================================================================
    // intersection_type = unary_type ('&' unary_type)*
    // ================================================================
    TsTypeNode* parse_intersection_type() {
        // handle leading &
        if (check_char('&')) { advance_one(); skip_ws(); }

        TsTypeNode* first = parse_unary_type();
        if (!first) return nullptr;

        if (!check_char('&')) return first;

        TsTypeNode* members[64];
        int count = 0;
        members[count++] = first;

        while (check_char('&') && count < 64) {
            advance_one(); // consume '&'
            TsTypeNode* next = parse_unary_type();
            if (next) members[count++] = next;
        }

        if (count == 1) return first;

        TsIntersectionTypeNode* in_node = (TsIntersectionTypeNode*)alloc_node(
            TS_AST_NODE_INTERSECTION_TYPE, sizeof(TsIntersectionTypeNode));
        in_node->types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * count);
        in_node->type_count = count;
        memcpy(in_node->types, members, sizeof(TsTypeNode*) * count);
        return (TsTypeNode*)in_node;
    }

    // ================================================================
    // unary_type = 'keyof' unary_type | 'typeof' typeof_expr
    //            | 'infer' IDENT ('extends' type)?
    //            | 'readonly' unary_type | 'unique' 'symbol'
    //            | postfix_type
    // ================================================================
    TsTypeNode* parse_unary_type() {
        if (match_str("keyof")) {
            TsTypeNode* inner = parse_unary_type();
            // wrap as keyof — using predefined as fallback for now
            // TS_AST_NODE_KEYOF_TYPE exists in enum but no struct defined yet
            // store as parenthesized wrapping the inner type for now
            TsParenthesizedTypeNode* kn = (TsParenthesizedTypeNode*)alloc_node(
                TS_AST_NODE_KEYOF_TYPE, sizeof(TsParenthesizedTypeNode));
            kn->inner = inner;
            return (TsTypeNode*)kn;
        }
        if (match_str("typeof")) {
            // typeof expr — scan the expression as text and store as type reference
            skip_ws();
            const char* start = text + pos;
            // consume dotted identifiers: typeof X.Y.Z
            while (pos < len && (is_ident_char((unsigned char)text[pos]) || text[pos] == '.')) pos++;
            int elen = (int)((text + pos) - start);
            TsTypeReferenceNode* tn = (TsTypeReferenceNode*)alloc_node(
                TS_AST_NODE_TYPEOF_TYPE, sizeof(TsTypeReferenceNode));
            tn->name = make_string(start, elen);
            return (TsTypeNode*)tn;
        }
        if (match_str("infer")) {
            skip_ws();
            const char* name; int nlen;
            if (read_ident(&name, &nlen)) {
                // infer T — store using TsTypeParamNode-like struct with the name
                TsTypeReferenceNode* in_node = (TsTypeReferenceNode*)alloc_node(
                    TS_AST_NODE_INFER_TYPE, sizeof(TsTypeReferenceNode));
                in_node->name = make_string(name, nlen);
                // optional: 'extends' constraint
                if (check_str("extends")) {
                    pos += 7;
                    // skip constraint for now — parser will consume it
                    parse_type();
                }
                return (TsTypeNode*)in_node;
            }
            return make_any();
        }
        if (match_str("readonly")) {
            TsTypeNode* inner = parse_unary_type();
            return inner; // readonly is a modifier, pass through
        }
        if (match_str("unique")) {
            if (match_str("symbol")) {
                TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_node(
                    TS_AST_NODE_PREDEFINED_TYPE, sizeof(TsPredefinedTypeNode));
                pn->predefined_id = LMD_TYPE_SYMBOL;
                return (TsTypeNode*)pn;
            }
        }
        return parse_postfix_type();
    }

    // ================================================================
    // postfix_type = primary_type ('[' ']' | '[' type ']')*
    // ================================================================
    TsTypeNode* parse_postfix_type() {
        TsTypeNode* base = parse_primary_type();
        if (!base) return nullptr;

        while (check_char('[')) {
            advance_one(); // consume '['
            skip_ws();
            if (check_char(']')) {
                advance_one(); // consume ']'
                // T[] — array type
                TsArrayTypeNode* an = (TsArrayTypeNode*)alloc_node(
                    TS_AST_NODE_ARRAY_TYPE, sizeof(TsArrayTypeNode));
                an->element_type = base;
                base = (TsTypeNode*)an;
            } else {
                // T[K] — indexed access type
                TsTypeNode* index = parse_type();
                match_char(']');
                // store as indexed access using conditional node layout (reuse check+extends)
                TsTypeReferenceNode* ia = (TsTypeReferenceNode*)alloc_node(
                    TS_AST_NODE_INDEXED_ACCESS_TYPE, sizeof(TsTypeReferenceNode));
                ia->type_args = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * 2);
                ia->type_args[0] = base;
                ia->type_args[1] = index;
                ia->type_arg_count = 2;
                base = (TsTypeNode*)ia;
            }
        }
        return base;
    }

    // ================================================================
    // primary_type — the core type atoms
    // ================================================================
    TsTypeNode* parse_primary_type() {
        skip_ws();
        if (at_end()) return nullptr;

        int c = ch();

        // parenthesized type or function type: '(' ...
        if (c == '(') {
            return parse_paren_or_function_type();
        }

        // tuple type: '[' ...
        if (c == '[') {
            return parse_tuple_type();
        }

        // object type or mapped type: '{' ...
        if (c == '{') {
            return parse_object_or_mapped_type();
        }

        // template literal type: `...`
        if (c == '`') {
            return parse_template_literal_type();
        }

        // string literal: '...' or "..."
        if (c == '\'' || c == '"') {
            return parse_string_literal_type();
        }

        // numeric literal: digit or negative number
        if (isdigit(c) || (c == '-' && pos + 1 < len && isdigit((unsigned char)text[pos + 1]))) {
            return parse_number_literal_type();
        }

        // 'new' — constructor type: new (...) => T
        if (is_keyword("new")) {
            return parse_constructor_type();
        }

        // identifier-based types: predefined, type reference, or generic
        if (is_ident_start(c)) {
            return parse_identifier_type();
        }

        // unrecognized — skip one char and return any
        log_debug("ts type parser: unexpected char '%c' at pos %d", (char)c, pos);
        advance_one();
        return make_any();
    }

    // ================================================================
    // '(' ... — disambiguate parenthesized vs function type
    // ================================================================
    TsTypeNode* parse_paren_or_function_type() {
        // save position for backtracking
        int save = pos;
        advance_one(); // consume '('
        skip_ws();

        // empty parens: () => T  (function type with no params)
        if (check_char(')')) {
            advance_one(); // consume ')'
            skip_ws();
            if (match_str("=>")) {
                TsTypeNode* ret = parse_type();
                TsFunctionTypeNode* fn = (TsFunctionTypeNode*)alloc_node(
                    TS_AST_NODE_FUNCTION_TYPE, sizeof(TsFunctionTypeNode));
                fn->return_type = ret;
                return (TsTypeNode*)fn;
            }
            // empty parens without => — treat as void/any
            return make_any();
        }

        // try to parse as function parameters
        // heuristic: if first item has ":" after ident, or "..." rest param, it's a function type
        if (check_char('.') && peek(1) == '.' && peek(2) == '.') {
            // rest parameter — definitely function type
            pos = save;
            return parse_function_type();
        }

        // check for "ident :" or "ident ," or "ident ?" patterns (function params)
        int probe = pos;
        if (is_ident_start((unsigned char)text[pos])) {
            while (probe < len && is_ident_char((unsigned char)text[probe])) probe++;
            while (probe < len && isspace((unsigned char)text[probe])) probe++;
            if (probe < len && (text[probe] == ':' || text[probe] == ',' || text[probe] == '?')) {
                // looks like function parameter — parse as function type
                pos = save;
                return parse_function_type();
            }
        }

        // not a function type — parse as parenthesized type
        pos = save + 1; // after '('
        TsTypeNode* inner = parse_type();
        match_char(')');

        // check for => after closing paren (could be function type with single unnamed param)
        skip_ws();
        if (match_str("=>")) {
            TsTypeNode* ret = parse_type();
            TsFunctionTypeNode* fn = (TsFunctionTypeNode*)alloc_node(
                TS_AST_NODE_FUNCTION_TYPE, sizeof(TsFunctionTypeNode));
            fn->param_count = 1;
            fn->param_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*));
            fn->param_types[0] = inner;
            fn->return_type = ret;
            return (TsTypeNode*)fn;
        }

        TsParenthesizedTypeNode* pn = (TsParenthesizedTypeNode*)alloc_node(
            TS_AST_NODE_PARENTHESIZED_TYPE, sizeof(TsParenthesizedTypeNode));
        pn->inner = inner;
        return (TsTypeNode*)pn;
    }

    // ================================================================
    // function type: (param: T, ...) => ReturnType
    // ================================================================
    TsTypeNode* parse_function_type() {
        match_char('(');

        String* names[32];
        TsTypeNode* types[32];
        int count = 0;

        while (!check_char(')') && !at_end() && count < 32) {
            if (count > 0) match_char(',');
            skip_ws();

            // handle rest parameter: ...name: T
            bool is_rest = false;
            if (check_char('.') && peek(1) == '.' && peek(2) == '.') {
                pos += 3;
                is_rest = true;
            }
            (void)is_rest; // rest param indicator for future use

            // parameter name
            const char* pname; int pname_len;
            if (read_ident(&pname, &pname_len)) {
                names[count] = make_string(pname, pname_len);
            } else {
                names[count] = nullptr;
            }

            // optional '?'
            match_char('?');

            // ':' type
            if (match_char(':')) {
                types[count] = parse_type();
            } else {
                types[count] = make_any();
            }
            count++;
        }
        match_char(')');

        // '=>' return type
        TsTypeNode* ret = nullptr;
        skip_ws();
        if (match_str("=>")) {
            ret = parse_type();
        }

        TsFunctionTypeNode* fn = (TsFunctionTypeNode*)alloc_node(
            TS_AST_NODE_FUNCTION_TYPE, sizeof(TsFunctionTypeNode));
        fn->param_count = count;
        if (count > 0) {
            fn->param_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * count);
            fn->param_names = (String**)pool_alloc(tp->ast_pool, sizeof(String*) * count);
            memcpy(fn->param_types, types, sizeof(TsTypeNode*) * count);
            memcpy(fn->param_names, names, sizeof(String*) * count);
        }
        fn->return_type = ret;
        return (TsTypeNode*)fn;
    }

    // ================================================================
    // new (...) => T — constructor type
    // ================================================================
    TsTypeNode* parse_constructor_type() {
        pos += 3; // consume "new"
        skip_ws();
        // parse as function type
        return parse_function_type();
    }

    // ================================================================
    // tuple type: [T, U, V]
    // ================================================================
    TsTypeNode* parse_tuple_type() {
        advance_one(); // consume '['

        TsTypeNode* elements[64];
        int count = 0;

        while (!check_char(']') && !at_end() && count < 64) {
            if (count > 0) match_char(',');
            skip_ws();

            // handle rest element: ...T
            if (check_char('.') && peek(1) == '.' && peek(2) == '.') {
                pos += 3;
            }

            // handle labeled tuple: name: T or name?: T
            int save = pos;
            const char* name; int nlen;
            bool is_labeled = false;
            if (read_ident(&name, &nlen)) {
                skip_ws();
                if (check_char(':') || check_char('?')) {
                    // labeled
                    match_char('?');
                    match_char(':');
                    is_labeled = true;
                } else {
                    pos = save; // not labeled, reparse as type
                }
            }

            if (!is_labeled) {
                // might have consumed an ident — restore
            }

            elements[count] = parse_type();
            count++;
        }
        match_char(']');

        TsTupleTypeNode* tn = (TsTupleTypeNode*)alloc_node(
            TS_AST_NODE_TUPLE_TYPE, sizeof(TsTupleTypeNode));
        tn->element_count = count;
        if (count > 0) {
            tn->element_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * count);
            memcpy(tn->element_types, elements, sizeof(TsTypeNode*) * count);
        }
        return (TsTypeNode*)tn;
    }

    // ================================================================
    // object type: { ... } or mapped type: { [K in T]: V }
    // ================================================================
    TsTypeNode* parse_object_or_mapped_type() {
        advance_one(); // consume '{'
        skip_ws();

        // check for mapped type: { [K in T]: V } or { [K in T as U]: V }
        // also: {| ... |} (exact object type)
        bool exact = false;
        if (check_char('|')) { advance_one(); exact = true; }
        (void)exact;

        if (check_char('[')) {
            // could be mapped type or index signature
            int save = pos;
            advance_one(); // consume '['
            skip_ws();
            const char* name; int nlen;
            if (read_ident(&name, &nlen)) {
                skip_ws();
                if (is_keyword("in")) {
                    // mapped type: [K in T]: V
                    pos += 2; // consume "in"
                    return parse_mapped_type_rest(name, nlen);
                }
            }
            pos = save; // not a mapped type, parse as regular object
        }

        return parse_object_type_body();
    }

    // ================================================================
    // mapped type rest: already consumed { [K in — parse T]: V }
    // ================================================================
    TsTypeNode* parse_mapped_type_rest(const char* key_name, int key_len) {
        // [K in T as U]?: V }
        TsTypeNode* constraint = parse_type();
        TsTypeNode* name_type = nullptr;
        if (check_str("as")) {
            pos += 2;
            name_type = parse_type();
        }
        (void)name_type; // stored for future use if needed
        match_char(']');

        // optional +/- modifiers and ?
        if (check_char('+') || check_char('-')) advance_one();
        match_char('?');
        match_char(':');

        TsTypeNode* value_type = parse_type();

        // optional ; or ,
        match_char(';');
        match_char(',');

        // closing }
        match_char('}');

        // build as mapped type — reuse TsObjectTypeNode with single member
        TsObjectTypeNode* mn = (TsObjectTypeNode*)alloc_node(
            TS_AST_NODE_MAPPED_TYPE, sizeof(TsObjectTypeNode));
        mn->member_count = 1;
        mn->member_names = (String**)pool_alloc(tp->ast_pool, sizeof(String*));
        mn->member_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*));
        mn->member_optional = (bool*)pool_calloc(tp->ast_pool, sizeof(bool));
        mn->member_readonly = (bool*)pool_calloc(tp->ast_pool, sizeof(bool));
        mn->member_names[0] = make_string(key_name, key_len);
        mn->member_types[0] = value_type;
        return (TsTypeNode*)mn;
    }

    // ================================================================
    // object type body: { member; member; ... }
    // ================================================================
    TsTypeNode* parse_object_type_body() {
        String* names[64];
        TsTypeNode* types[64];
        bool optional[64];
        bool readonly_flags[64];
        int count = 0;

        while (!check_char('}') && !at_end() && count < 64) {
            // skip separators
            while (match_char(';') || match_char(',')) {}
            if (check_char('}')) break;

            // check for closing |}
            if (check_char('|') && peek(1) == '}') { advance_one(); break; }

            bool is_readonly = false;
            if (match_str("readonly")) {
                is_readonly = true;
            }

            // check for index signature: [key: type]: type
            if (check_char('[')) {
                advance_one(); // consume '['
                skip_ws();
                // skip key name and type
                const char* dummy; int dlen;
                read_ident(&dummy, &dlen);
                if (match_char(':')) {
                    parse_type(); // index type
                }
                match_char(']');

                // optional ?
                bool opt = match_char('?');

                match_char(':');
                types[count] = parse_type();
                names[count] = make_string("[index]", 7);
                optional[count] = opt;
                readonly_flags[count] = is_readonly;
                count++;
                continue;
            }

            // call signature: (params): returnType
            if (check_char('(')) {
                types[count] = parse_function_type();
                names[count] = make_string("()", 2);
                optional[count] = false;
                readonly_flags[count] = false;
                count++;
                // check for '=>' return type if not already handled
                continue;
            }

            // construct signature: new (params): returnType
            if (is_keyword("new")) {
                pos += 3;
                skip_ws();
                types[count] = parse_function_type();
                names[count] = make_string("new", 3);
                optional[count] = false;
                readonly_flags[count] = false;
                count++;
                continue;
            }

            // property: name?: type
            const char* mname; int mname_len;
            if (!read_ident(&mname, &mname_len)) {
                // might be a string key
                if (ch() == '\'' || ch() == '"') {
                    int quote = ch();
                    advance_one();
                    const char* sstart = text + pos;
                    while (pos < len && text[pos] != quote) {
                        if (text[pos] == '\\') pos++;
                        pos++;
                    }
                    mname = sstart;
                    mname_len = (int)((text + pos) - sstart);
                    if (pos < len) advance_one(); // closing quote
                } else {
                    // skip unknown content
                    advance_one();
                    continue;
                }
            }

            bool opt = match_char('?');

            // method signature: name(...): T or name<T>(...): U
            if (check_char('(') || check_char('<')) {
                // skip type parameters
                if (check_char('<')) skip_angle_brackets();
                types[count] = parse_function_type();
                names[count] = make_string(mname, mname_len);
                optional[count] = opt;
                readonly_flags[count] = is_readonly;
                count++;
                continue;
            }

            match_char(':');
            types[count] = parse_type();
            names[count] = make_string(mname, mname_len);
            optional[count] = opt;
            readonly_flags[count] = is_readonly;
            count++;
        }

        match_char('|'); // for |} closing
        match_char('}');

        TsObjectTypeNode* on = (TsObjectTypeNode*)alloc_node(
            TS_AST_NODE_OBJECT_TYPE, sizeof(TsObjectTypeNode));
        on->member_count = count;
        if (count > 0) {
            on->member_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * count);
            on->member_names = (String**)pool_alloc(tp->ast_pool, sizeof(String*) * count);
            on->member_optional = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * count);
            on->member_readonly = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * count);
            memcpy(on->member_types, types, sizeof(TsTypeNode*) * count);
            memcpy(on->member_names, names, sizeof(String*) * count);
            memcpy(on->member_optional, optional, sizeof(bool) * count);
            memcpy(on->member_readonly, readonly_flags, sizeof(bool) * count);
        }
        return (TsTypeNode*)on;
    }

    // ================================================================
    // identifier-based types: predefined | type_reference | generic
    // ================================================================
    TsTypeNode* parse_identifier_type() {
        const char* name; int nlen;
        if (!read_ident(&name, &nlen)) return make_any();

        // check for true/false/null literals
        if (nlen == 4 && memcmp(name, "true", 4) == 0) {
            TsLiteralTypeNode* ln = (TsLiteralTypeNode*)alloc_node(
                TS_AST_NODE_LITERAL_TYPE, sizeof(TsLiteralTypeNode));
            ln->literal_type = JS_LITERAL_BOOLEAN;
            ln->value.boolean_value = true;
            return (TsTypeNode*)ln;
        }
        if (nlen == 5 && memcmp(name, "false", 5) == 0) {
            TsLiteralTypeNode* ln = (TsLiteralTypeNode*)alloc_node(
                TS_AST_NODE_LITERAL_TYPE, sizeof(TsLiteralTypeNode));
            ln->literal_type = JS_LITERAL_BOOLEAN;
            ln->value.boolean_value = false;
            return (TsTypeNode*)ln;
        }
        if (nlen == 4 && memcmp(name, "null", 4) == 0) {
            TsLiteralTypeNode* ln = (TsLiteralTypeNode*)alloc_node(
                TS_AST_NODE_LITERAL_TYPE, sizeof(TsLiteralTypeNode));
            ln->literal_type = JS_LITERAL_NULL;
            return (TsTypeNode*)ln;
        }

        // predefined types: number, string, boolean, any, void, never, etc.
        if (is_predefined(name, nlen)) {
            TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_node(
                TS_AST_NODE_PREDEFINED_TYPE, sizeof(TsPredefinedTypeNode));
            pn->predefined_id = ts_predefined_name_to_type_id(name, nlen);
            return (TsTypeNode*)pn;
        }

        // type reference: Name<T, U> or Name.SubName<T>
        return parse_type_reference(name, nlen);
    }

    // ================================================================
    // type reference with optional generics and dotted names
    // ================================================================
    TsTypeNode* parse_type_reference(const char* name, int nlen) {
        // collect dotted name: A.B.C
        char full_name[256];
        int full_len = 0;
        if (nlen < 200) {
            memcpy(full_name, name, nlen);
            full_len = nlen;
        }

        while (check_char('.')) {
            advance_one(); // consume '.'
            const char* part; int plen;
            if (read_ident(&part, &plen) && full_len + 1 + plen < 256) {
                full_name[full_len++] = '.';
                memcpy(full_name + full_len, part, plen);
                full_len += plen;
            }
        }

        TsTypeReferenceNode* rn = (TsTypeReferenceNode*)alloc_node(
            TS_AST_NODE_TYPE_REFERENCE, sizeof(TsTypeReferenceNode));
        rn->name = make_string(full_name, full_len);

        // generic arguments: <T, U>
        if (check_char('<')) {
            TsTypeNode* args[32];
            int arg_count = 0;
            advance_one(); // consume '<'

            while (!check_char('>') && !at_end() && arg_count < 32) {
                if (arg_count > 0) match_char(',');
                args[arg_count] = parse_type();
                arg_count++;
            }
            match_char('>');

            if (arg_count > 0) {
                rn->type_args = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * arg_count);
                rn->type_arg_count = arg_count;
                memcpy(rn->type_args, args, sizeof(TsTypeNode*) * arg_count);
            }
        }

        return (TsTypeNode*)rn;
    }

    // ================================================================
    // string literal type: 'hello' or "world"
    // ================================================================
    TsTypeNode* parse_string_literal_type() {
        int quote = ch();
        advance_one(); // consume opening quote
        const char* start = text + pos;
        while (pos < len && text[pos] != quote) {
            if (text[pos] == '\\') pos++; // skip escaped char
            pos++;
        }
        int slen = (int)((text + pos) - start);
        if (pos < len) advance_one(); // consume closing quote

        TsLiteralTypeNode* ln = (TsLiteralTypeNode*)alloc_node(
            TS_AST_NODE_LITERAL_TYPE, sizeof(TsLiteralTypeNode));
        ln->literal_type = JS_LITERAL_STRING;
        ln->value.string_value = make_string(start, slen);
        return (TsTypeNode*)ln;
    }

    // ================================================================
    // number literal type: 42, -1, 3.14
    // ================================================================
    TsTypeNode* parse_number_literal_type() {
        const char* start = text + pos;
        if (ch() == '-') advance_one();
        while (pos < len && isdigit((unsigned char)text[pos])) pos++;
        if (pos < len && text[pos] == '.') {
            advance_one();
            while (pos < len && isdigit((unsigned char)text[pos])) pos++;
        }
        // handle exponent
        if (pos < len && (text[pos] == 'e' || text[pos] == 'E')) {
            advance_one();
            if (pos < len && (text[pos] == '+' || text[pos] == '-')) advance_one();
            while (pos < len && isdigit((unsigned char)text[pos])) pos++;
        }
        int nlen = (int)((text + pos) - start);

        TsLiteralTypeNode* ln = (TsLiteralTypeNode*)alloc_node(
            TS_AST_NODE_LITERAL_TYPE, sizeof(TsLiteralTypeNode));
        ln->literal_type = JS_LITERAL_NUMBER;
        ln->value.number_value = strtod(start, nullptr);
        return (TsTypeNode*)ln;
    }

    // ================================================================
    // template literal type: `prefix${T}suffix`
    // ================================================================
    TsTypeNode* parse_template_literal_type() {
        advance_one(); // consume '`'
        // scan to closing backtick, handling ${...} expressions
        int tmpl_depth = 0;
        while (pos < len) {
            if (text[pos] == '\\') { pos += 2; continue; }
            if (text[pos] == '$' && pos + 1 < len && text[pos + 1] == '{') {
                pos += 2;
                tmpl_depth++;
            } else if (text[pos] == '}' && tmpl_depth > 0) {
                pos++;
                tmpl_depth--;
            } else if (text[pos] == '`' && tmpl_depth == 0) {
                pos++; // consume closing backtick
                break;
            } else {
                pos++;
            }
        }
        // return as predefined any for now — template literal type resolution is complex
        TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_node(
            TS_AST_NODE_TEMPLATE_LITERAL_TYPE, sizeof(TsPredefinedTypeNode));
        pn->predefined_id = LMD_TYPE_STRING;
        return (TsTypeNode*)pn;
    }

    // ================================================================
    // skip balanced angle brackets <...>
    // ================================================================
    void skip_angle_brackets() {
        if (!check_char('<')) return;
        advance_one();
        int depth = 1;
        while (pos < len && depth > 0) {
            if (text[pos] == '<') depth++;
            else if (text[pos] == '>') depth--;
            else if (text[pos] == '\'' || text[pos] == '"') {
                int q = text[pos]; pos++;
                while (pos < len && text[pos] != q) {
                    if (text[pos] == '\\') pos++;
                    pos++;
                }
            }
            pos++;
        }
    }

    // ================================================================
    // interface body members: { member; member; ... }
    // Enhanced version of parse_object_type_body with interface modifiers
    // (accessibility, static, override, abstract, async, get/set, *)
    // ================================================================
    TsTypeNode* parse_interface_body_members() {
        String* names[64];
        TsTypeNode* types[64];
        bool optional_arr[64];
        bool readonly_arr[64];
        int count = 0;

        while (!check_char('}') && !at_end() && count < 64) {
            // skip separators
            while (match_char(';') || match_char(',')) {}
            if (check_char('}')) break;

            // check for closing |}
            if (check_char('|') && peek(1) == '}') { advance_one(); break; }

            // skip interface-specific modifiers
            while (match_str("export") || match_str("public") || match_str("private") ||
                   match_str("protected") || match_str("static") || match_str("override") ||
                   match_str("abstract") || match_str("declare")) {}

            bool is_readonly = false;
            if (match_str("readonly")) {
                is_readonly = true;
            }

            // skip async
            match_str("async");

            // index signature: [key: type]: type
            if (check_char('[')) {
                advance_one(); // consume '['
                skip_ws();
                const char* dummy; int dlen;
                // check for mapped type: [K in T]: V
                if (read_ident(&dummy, &dlen)) {
                    skip_ws();
                    if (is_keyword("in")) {
                        // mapped type inside interface body (unusual but valid)
                        pos += 2;
                        TsTypeNode* mapped = parse_mapped_type_rest(dummy, dlen);
                        // mapped_type_rest consumes through }, so we're done
                        // but actually we need to add this as a member... treat as special case
                        // for now, just return the mapped type directly
                        return mapped;
                    }
                    if (match_char(':')) {
                        parse_type(); // index type
                    }
                }
                match_char(']');

                // optional modifiers: +?, -?, ?
                if (check_char('+') || check_char('-')) advance_one();
                bool opt = match_char('?');

                match_char(':');
                types[count] = parse_type();
                names[count] = make_string("[index]", 7);
                optional_arr[count] = opt;
                readonly_arr[count] = is_readonly;
                count++;
                continue;
            }

            // call signature: (params): returnType
            if (check_char('(')) {
                types[count] = parse_function_type();
                names[count] = make_string("()", 2);
                optional_arr[count] = false;
                readonly_arr[count] = false;
                count++;
                continue;
            }

            // construct signature: new (params): returnType
            if (is_keyword("new")) {
                pos += 3;
                skip_ws();
                // skip optional type parameters
                if (check_char('<')) skip_angle_brackets();
                types[count] = parse_function_type();
                names[count] = make_string("new", 3);
                optional_arr[count] = false;
                readonly_arr[count] = false;
                count++;
                continue;
            }

            // handle get/set as modifiers (not property names)
            // if 'get'/'set' is followed by an identifier, '(' or '[' => modifier
            // otherwise => property name
            int save = pos;
            bool consumed_accessor = false;
            if (is_keyword("get") || is_keyword("set")) {
                int klen = is_keyword("get") ? 3 : 3;
                pos += klen;
                skip_ws();
                if (is_ident_start(ch()) || check_char('[') || check_char('(')) {
                    consumed_accessor = true; // it's a modifier, continue to name
                } else {
                    pos = save; // it's actually the property name "get"/"set"
                }
            }

            // handle generator *
            match_char('*');

            // property or method name
            const char* mname; int mname_len;
            if (!read_ident(&mname, &mname_len)) {
                // might be a string key
                if (ch() == '\'' || ch() == '"') {
                    int quote = ch();
                    advance_one();
                    const char* sstart = text + pos;
                    while (pos < len && text[pos] != quote) {
                        if (text[pos] == '\\') pos++;
                        pos++;
                    }
                    mname = sstart;
                    mname_len = (int)((text + pos) - sstart);
                    if (pos < len) advance_one(); // closing quote
                } else if (check_char('#')) {
                    // private field: #name
                    advance_one();
                    if (!read_ident(&mname, &mname_len)) {
                        advance_one();
                        continue;
                    }
                } else {
                    // skip unknown content
                    advance_one();
                    continue;
                }
            }

            bool opt = match_char('?');

            // method signature: name(...): T or name<T>(...): U
            if (check_char('(') || check_char('<')) {
                if (check_char('<')) skip_angle_brackets();
                types[count] = parse_function_type();
                names[count] = make_string(mname, mname_len);
                optional_arr[count] = opt;
                readonly_arr[count] = is_readonly;
                count++;
                continue;
            }

            // property: name?: type
            match_char(':');
            types[count] = parse_type();
            names[count] = make_string(mname, mname_len);
            optional_arr[count] = opt;
            readonly_arr[count] = is_readonly;
            count++;
        }

        match_char('|'); // for |} closing
        match_char('}');

        TsObjectTypeNode* on = (TsObjectTypeNode*)alloc_node(
            TS_AST_NODE_OBJECT_TYPE, sizeof(TsObjectTypeNode));
        on->member_count = count;
        if (count > 0) {
            on->member_types = (TsTypeNode**)pool_alloc(tp->ast_pool, sizeof(TsTypeNode*) * count);
            on->member_names = (String**)pool_alloc(tp->ast_pool, sizeof(String*) * count);
            on->member_optional = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * count);
            on->member_readonly = (bool*)pool_calloc(tp->ast_pool, sizeof(bool) * count);
            memcpy(on->member_types, types, sizeof(TsTypeNode*) * count);
            memcpy(on->member_names, names, sizeof(String*) * count);
            memcpy(on->member_optional, optional_arr, sizeof(bool) * count);
            memcpy(on->member_readonly, readonly_arr, sizeof(bool) * count);
        }
        return (TsTypeNode*)on;
    }
};

// ============================================================================
// public entry point
// ============================================================================

TsTypeNode* ts_parse_type_text(JsTranspiler* tp, const char* text, int len) {
    if (!text || len <= 0) {
        TsPredefinedTypeNode* pn = (TsPredefinedTypeNode*)alloc_js_ast_node(tp,
            (JsAstNodeType)TS_AST_NODE_PREDEFINED_TYPE, (TSNode){{0},0,0}, sizeof(TsPredefinedTypeNode));
        pn->predefined_id = LMD_TYPE_ANY;
        return (TsTypeNode*)pn;
    }

    TsTypeParser parser;
    parser.tp = tp;
    parser.text = text;
    parser.len = len;
    parser.pos = 0;
    memset(&parser.null_node, 0, sizeof(TSNode));

    TsTypeNode* result = parser.parse_type();
    return result ? result : parser.make_any();
}

// ============================================================================
// parse opaque interface body text: "{ member; member; ... }"
// ============================================================================

TsTypeNode* ts_parse_interface_body_text(JsTranspiler* tp, const char* text, int len) {
    if (!text || len <= 0) {
        TsObjectTypeNode* on = (TsObjectTypeNode*)alloc_js_ast_node(tp,
            (JsAstNodeType)TS_AST_NODE_OBJECT_TYPE, (TSNode){{0},0,0}, sizeof(TsObjectTypeNode));
        on->member_count = 0;
        return (TsTypeNode*)on;
    }

    TsTypeParser parser;
    parser.tp = tp;
    parser.text = text;
    parser.len = len;
    parser.pos = 0;
    memset(&parser.null_node, 0, sizeof(TSNode));

    // consume opening { (or {|)
    parser.skip_ws();
    if (!parser.match_char('{')) {
        return parser.make_any();
    }
    parser.match_char('|'); // exact object: {|

    TsTypeNode* result = parser.parse_interface_body_members();
    return result ? result : parser.make_any();
}

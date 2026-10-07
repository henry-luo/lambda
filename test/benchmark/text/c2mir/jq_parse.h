/* jq-core parser: jq source text -> AST (see jq_core.h).
 *
 * Recursive descent over jq's precedence levels (src/parser.y), lowest first:
 * '|', ',', '//', assignment ops, 'or', 'and', comparisons, '+' '-',
 * '*' '/' '%', unary '-', postfix terms. `Term as $x | body`, `reduce`,
 * `foreach`, `if`, `try`, `label`, `def` and string interpolation are parsed
 * as jq does; anything outside the jq-core subset is a parse error.
 */

enum {
    N_IDENTITY = 1, N_RECURSE_DEFAULT, N_LITERAL, N_INDEX, N_ITER, N_SLICE,
    N_TRY, N_ARRAY, N_OBJECT, N_NEG, N_BINOP, N_AND, N_OR, N_ALT,
    N_ASSIGN, N_UPDATE, N_ARITH_UPDATE, N_ALT_UPDATE,
    N_PIPE, N_COMMA, N_BIND, N_REDUCE, N_FOREACH, N_IF,
    N_FUNCDEF, N_CALL, N_VAR, N_LABEL, N_BREAK
};

/* binary operators (also the native ids of arithmetic in the VM) */
enum { OPB_ADD = 1, OPB_SUB, OPB_MUL, OPB_DIV, OPB_MOD, OPB_EQ, OPB_NE, OPB_LT, OPB_LE, OPB_GT, OPB_GE };

typedef struct Node Node;
struct Node {
    int kind;
    int op;              /* binop / arith-update operator */
    Node *a, *b, *c, *d;
    Node **list;         /* call args, object keys/values (pairs), def params */
    int nlist;
    const char *name;    /* call / var / label / def name */
    V value;             /* literal */
    int *param_is_var;   /* def: 1 for a $param */
};

typedef struct {
    const char *src;
    int pos, len;
    int failed;
    char message[200];
} Parser;

static Node *node_new(int kind) {
    Node *n = (Node *) malloc(sizeof(Node));
    n->kind = kind;
    n->op = 0;
    n->a = n->b = n->c = n->d = 0;
    n->list = 0;
    n->nlist = 0;
    n->name = 0;
    n->value = V_NULL;
    n->param_is_var = 0;
    return n;
}

static Node *node2(int kind, Node *a, Node *b) {
    Node *n = node_new(kind);
    n->a = a;
    n->b = b;
    return n;
}

static void list_add(Node *n, Node *item) {
    n->list = (Node **) realloc(n->list, sizeof(Node *) * (unsigned long) (n->nlist + 1));
    n->list[n->nlist++] = item;
}

static void parse_fail(Parser *p, const char *what) {
    if (!p->failed) {
        p->failed = 1;
        snprintf(p->message, sizeof p->message, "jq parse error: %s at offset %d", what, p->pos);
    }
}

static void skip_ws(Parser *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { p->pos++; continue; }
        if (c == '#') {
            while (p->pos < p->len && p->src[p->pos] != '\n') p->pos++;
            continue;
        }
        break;
    }
}

static int is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

/* match a punctuation token exactly (callers order longer tokens first) */
static int accept(Parser *p, const char *tok) {
    skip_ws(p);
    int n = (int) strlen(tok);
    if (p->pos + n <= p->len && memcmp(p->src + p->pos, tok, (unsigned long) n) == 0) {
        p->pos += n;
        return 1;
    }
    return 0;
}

static int peek(Parser *p, const char *tok) {
    skip_ws(p);
    int n = (int) strlen(tok);
    return p->pos + n <= p->len && memcmp(p->src + p->pos, tok, (unsigned long) n) == 0;
}

static void expect(Parser *p, const char *tok) {
    if (!accept(p, tok)) {
        char what[64];
        snprintf(what, sizeof what, "expected '%s'", tok);
        parse_fail(p, what);
    }
}

/* read an identifier (no leading whitespace skip beyond the usual) */
static const char *read_ident(Parser *p) {
    skip_ws(p);
    int start = p->pos;
    if (p->pos >= p->len || !is_ident_start(p->src[p->pos])) return 0;
    while (p->pos < p->len && (is_ident_char(p->src[p->pos]) ||
            (p->src[p->pos] == ':' && p->pos + 1 < p->len && p->src[p->pos + 1] == ':'))) {
        if (p->src[p->pos] == ':') p->pos++;
        p->pos++;
    }
    int n = p->pos - start;
    char *s = (char *) malloc((unsigned long) n + 1);
    memcpy(s, p->src + start, (unsigned long) n);
    s[n] = 0;
    return s;
}

static int peek_keyword(Parser *p, const char *kw) {
    skip_ws(p);
    int n = (int) strlen(kw);
    return p->pos + n <= p->len && memcmp(p->src + p->pos, kw, (unsigned long) n) == 0 &&
        (p->pos + n == p->len || !is_ident_char(p->src[p->pos + n]));
}

static int accept_keyword(Parser *p, const char *kw) {
    if (!peek_keyword(p, kw)) return 0;
    p->pos += (int) strlen(kw);
    return 1;
}

static const char *read_var(Parser *p) {
    if (!accept(p, "$")) { parse_fail(p, "expected $variable"); return "?"; }
    const char *name = read_ident(p);
    if (!name) { parse_fail(p, "expected variable name"); return "?"; }
    return name;
}

static Node *parse_pipe(Parser *p);
static Node *parse_postfix(Parser *p, int allow_as);
static Node *parse_alt(Parser *p);

static Node *call_node(const char *name, Node *arg0, Node *arg1) {
    Node *n = node_new(N_CALL);
    n->name = name;
    if (arg0) list_add(n, arg0);
    if (arg1) list_add(n, arg1);
    return n;
}

static Node *literal(V v) {
    Node *n = node_new(N_LITERAL);
    n->value = v;
    return n;
}

/* string literal with \(...) interpolation, built as "a" + (e|tostring) + ... */
static Node *parse_string(Parser *p) {
    Buf b = {0, 0, 0};
    Node *result = 0;
    p->pos++;   /* opening quote */
    for (;;) {
        if (p->pos >= p->len) { parse_fail(p, "unterminated string"); break; }
        char c = p->src[p->pos++];
        if (c == '"') break;
        if (c != '\\') { buf_addc(&b, c); continue; }
        if (p->pos >= p->len) { parse_fail(p, "bad escape"); break; }
        c = p->src[p->pos++];
        if (c == '(') {
            Node *piece = literal(buf_to_str(&b));
            Node *expr = parse_pipe(p);
            expect(p, ")");
            Node *text = node2(N_PIPE, expr, call_node("tostring", 0, 0));
            Node *joined = node2(N_BINOP, piece, text);
            joined->op = OPB_ADD;
            if (result) {
                Node *sum = node2(N_BINOP, result, joined);
                sum->op = OPB_ADD;
                result = sum;
            } else {
                result = joined;
            }
            continue;
        }
        switch (c) {
        case 'n': buf_addc(&b, '\n'); break;
        case 't': buf_addc(&b, '\t'); break;
        case 'r': buf_addc(&b, '\r'); break;
        case 'b': buf_addc(&b, '\b'); break;
        case 'f': buf_addc(&b, '\f'); break;
        case 'u': {
            int cp = 0;
            for (int i = 0; i < 4 && p->pos < p->len; i++) cp = cp * 16 + hex_digit(p->src[p->pos++]);
            char enc[4];
            buf_add(&b, enc, utf8_encode(cp, enc));
            break;
        }
        default: buf_addc(&b, c);
        }
    }
    Node *tail = literal(buf_to_str(&b));
    if (!result) return tail;
    Node *sum = node2(N_BINOP, result, tail);
    sum->op = OPB_ADD;
    return sum;
}

static Node *parse_number(Parser *p) {
    int start = p->pos;
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if ((c >= '0' && c <= '9') || c == '.') { p->pos++; continue; }
        if ((c == 'e' || c == 'E') && p->pos + 1 < p->len) {
            p->pos++;
            if (p->src[p->pos] == '+' || p->src[p->pos] == '-') p->pos++;
            continue;
        }
        break;
    }
    char tmp[64];
    int n = p->pos - start < 63 ? p->pos - start : 63;
    memcpy(tmp, p->src + start, (unsigned long) n);
    tmp[n] = 0;
    return literal(v_num(strtod(tmp, 0)));
}

/* object value: ExpD := ExpD '|' ExpD | '-' ExpD | Term */
static Node *parse_object_value(Parser *p) {
    Node *v;
    if (accept(p, "-")) {
        v = node_new(N_NEG);
        v->a = parse_object_value(p);
        return v;
    }
    v = parse_postfix(p, 0);
    if (!peek(p, "||") && !peek(p, "|=") && accept(p, "|")) v = node2(N_PIPE, v, parse_object_value(p));
    return v;
}

static Node *parse_object(Parser *p) {
    Node *obj = node_new(N_OBJECT);
    if (accept(p, "}")) return obj;
    for (;;) {
        Node *key, *val;
        skip_ws(p);
        if (peek(p, "$")) {
            const char *var = read_var(p);
            key = literal(str_cstr(var));
            val = node_new(N_VAR);
            val->name = var;
        } else {
            if (peek(p, "\"")) {
                key = parse_string(p);
            } else if (accept(p, "(")) {
                key = parse_pipe(p);
                expect(p, ")");
            } else {
                const char *id = read_ident(p);
                if (!id) { parse_fail(p, "bad object key"); return obj; }
                key = literal(str_cstr(id));
            }
            if (accept(p, ":")) {
                val = parse_object_value(p);
            } else {
                /* {a} is {a: .a} */
                val = node2(N_INDEX, node_new(N_IDENTITY), key);
            }
        }
        list_add(obj, key);
        list_add(obj, val);
        if (accept(p, ",")) continue;
        expect(p, "}");
        return obj;
    }
}

/* suffixes: .foo  ."str"  [e]  []  [e:e]  ?  and `as $x | body` */
static Node *parse_suffixes(Parser *p, Node *term, int allow_as) {
    for (;;) {
        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == '.' && p->pos + 1 < p->len &&
                (is_ident_start(p->src[p->pos + 1]) || p->src[p->pos + 1] == '"' || p->src[p->pos + 1] == '[')) {
            p->pos++;
            if (p->src[p->pos] == '"') {
                term = node2(N_INDEX, term, parse_string(p));
            } else if (p->src[p->pos] == '[') {
                continue;   /* .[ handled as [ below */
            } else {
                term = node2(N_INDEX, term, literal(str_cstr(read_ident(p))));
            }
            continue;
        }
        if (accept(p, "[")) {
            if (accept(p, "]")) { term = node2(N_ITER, term, 0); continue; }
            if (accept(p, ":")) {
                Node *s = node_new(N_SLICE);
                s->a = term;
                s->c = parse_pipe(p);
                expect(p, "]");
                term = s;
                continue;
            }
            Node *idx = parse_pipe(p);
            if (accept(p, ":")) {
                Node *s = node_new(N_SLICE);
                s->a = term;
                s->b = idx;
                if (!peek(p, "]")) s->c = parse_pipe(p);
                expect(p, "]");
                term = s;
                continue;
            }
            expect(p, "]");
            term = node2(N_INDEX, term, idx);
            continue;
        }
        if (!peek(p, "?//") && accept(p, "?")) {
            term = node2(N_TRY, term, 0);
            continue;
        }
        break;
    }
    if (allow_as && peek_keyword(p, "as")) {
        accept_keyword(p, "as");
        const char *var = read_var(p);
        expect(p, "|");
        Node *bind = node_new(N_BIND);
        bind->a = term;
        bind->name = var;
        bind->b = parse_pipe(p);
        return bind;
    }
    return term;
}

static Node *parse_primary(Parser *p) {
    skip_ws(p);
    if (p->pos >= p->len) { parse_fail(p, "unexpected end"); return node_new(N_IDENTITY); }
    char c = p->src[p->pos];
    if (c == '.') {
        if (accept(p, "..")) return node_new(N_RECURSE_DEFAULT);
        p->pos++;
        if (p->pos < p->len && is_ident_start(p->src[p->pos]))
            return node2(N_INDEX, node_new(N_IDENTITY), literal(str_cstr(read_ident(p))));
        if (p->pos < p->len && p->src[p->pos] == '"')
            return node2(N_INDEX, node_new(N_IDENTITY), parse_string(p));
        return node_new(N_IDENTITY);
    }
    if (c >= '0' && c <= '9') return parse_number(p);
    if (c == '"') return parse_string(p);
    if (accept(p, "(")) {
        Node *e = parse_pipe(p);
        expect(p, ")");
        return e;
    }
    if (accept(p, "[")) {
        Node *arr = node_new(N_ARRAY);
        if (!accept(p, "]")) {
            arr->a = parse_pipe(p);
            expect(p, "]");
        }
        return arr;
    }
    if (accept(p, "{")) return parse_object(p);
    if (c == '$') {
        Node *v = node_new(N_VAR);
        v->name = read_var(p);
        return v;
    }
    if (accept_keyword(p, "if")) {
        Node *top = node_new(N_IF), *cur = top;
        cur->a = parse_pipe(p);
        expect(p, "then");
        cur->b = parse_pipe(p);
        for (;;) {
            if (accept_keyword(p, "elif")) {
                Node *next = node_new(N_IF);
                next->a = parse_pipe(p);
                if (!accept_keyword(p, "then")) parse_fail(p, "expected then");
                next->b = parse_pipe(p);
                cur->c = next;
                cur = next;
                continue;
            }
            if (accept_keyword(p, "else")) cur->c = parse_pipe(p);
            if (!accept_keyword(p, "end")) parse_fail(p, "expected end");
            return top;
        }
    }
    if (accept_keyword(p, "try")) {
        Node *t = node_new(N_TRY);
        t->a = parse_postfix(p, 0);
        if (accept_keyword(p, "catch")) t->b = parse_postfix(p, 0);
        return t;
    }
    if (accept_keyword(p, "reduce") || peek_keyword(p, "foreach")) {
        int is_foreach = accept_keyword(p, "foreach");
        Node *r = node_new(is_foreach ? N_FOREACH : N_REDUCE);
        r->a = parse_postfix(p, 0);
        if (!accept_keyword(p, "as")) parse_fail(p, "expected as");
        r->name = read_var(p);
        expect(p, "(");
        r->b = parse_pipe(p);
        expect(p, ";");
        r->c = parse_pipe(p);
        if (is_foreach && accept(p, ";")) r->d = parse_pipe(p);
        expect(p, ")");
        return r;
    }
    if (accept_keyword(p, "label")) {
        Node *l = node_new(N_LABEL);
        l->name = read_var(p);
        expect(p, "|");
        l->a = parse_pipe(p);
        return l;
    }
    if (accept_keyword(p, "break")) {
        Node *b = node_new(N_BREAK);
        b->name = read_var(p);
        return b;
    }
    if (is_ident_start(c)) {
        Node *call = node_new(N_CALL);
        call->name = read_ident(p);
        if (accept(p, "(")) {
            for (;;) {
                list_add(call, parse_pipe(p));
                if (accept(p, ";")) continue;
                expect(p, ")");
                break;
            }
        }
        return call;
    }
    parse_fail(p, "unexpected character");
    p->pos++;
    return node_new(N_IDENTITY);
}

static Node *parse_postfix(Parser *p, int allow_as) {
    return parse_suffixes(p, parse_primary(p), allow_as);
}

static Node *parse_unary(Parser *p) {
    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == '-' && !peek(p, "-=")) {
        p->pos++;
        Node *n = node_new(N_NEG);
        n->a = parse_postfix(p, 1);
        return n;
    }
    return parse_postfix(p, 1);
}

static Node *binop(int op, Node *a, Node *b) {
    Node *n = node2(N_BINOP, a, b);
    n->op = op;
    return n;
}

static Node *parse_mul(Parser *p) {
    Node *l = parse_unary(p);
    for (;;) {
        if (peek(p, "*=") || peek(p, "/=") || peek(p, "%=")) return l;
        if (accept(p, "*")) l = binop(OPB_MUL, l, parse_unary(p));
        else if (!peek(p, "//") && accept(p, "/")) l = binop(OPB_DIV, l, parse_unary(p));
        else if (accept(p, "%")) l = binop(OPB_MOD, l, parse_unary(p));
        else return l;
    }
}

static Node *parse_add(Parser *p) {
    Node *l = parse_mul(p);
    for (;;) {
        if (peek(p, "+=") || peek(p, "-=")) return l;
        if (accept(p, "+")) l = binop(OPB_ADD, l, parse_mul(p));
        else if (accept(p, "-")) l = binop(OPB_SUB, l, parse_mul(p));
        else return l;
    }
}

static Node *parse_cmp(Parser *p) {
    Node *l = parse_add(p);
    if (accept(p, "==")) return binop(OPB_EQ, l, parse_add(p));
    if (accept(p, "!=")) return binop(OPB_NE, l, parse_add(p));
    if (accept(p, "<=")) return binop(OPB_LE, l, parse_add(p));
    if (accept(p, ">=")) return binop(OPB_GE, l, parse_add(p));
    if (accept(p, "<")) return binop(OPB_LT, l, parse_add(p));
    if (accept(p, ">")) return binop(OPB_GT, l, parse_add(p));
    return l;
}

static Node *parse_and(Parser *p) {
    Node *l = parse_cmp(p);
    while (accept_keyword(p, "and")) l = node2(N_AND, l, parse_cmp(p));
    return l;
}

static Node *parse_or(Parser *p) {
    Node *l = parse_and(p);
    while (accept_keyword(p, "or")) l = node2(N_OR, l, parse_and(p));
    return l;
}

static Node *parse_assign(Parser *p) {
    Node *l = parse_or(p);
    int op = 0, kind = 0;
    if (accept(p, "|=")) kind = N_UPDATE;
    else if (accept(p, "+=")) { kind = N_ARITH_UPDATE; op = OPB_ADD; }
    else if (accept(p, "-=")) { kind = N_ARITH_UPDATE; op = OPB_SUB; }
    else if (accept(p, "*=")) { kind = N_ARITH_UPDATE; op = OPB_MUL; }
    else if (accept(p, "/=")) { kind = N_ARITH_UPDATE; op = OPB_DIV; }
    else if (accept(p, "%=")) { kind = N_ARITH_UPDATE; op = OPB_MOD; }
    else if (accept(p, "//=")) kind = N_ALT_UPDATE;
    else if (!peek(p, "==") && accept(p, "=")) kind = N_ASSIGN;
    if (!kind) return l;
    Node *n = node2(kind, l, parse_alt(p));
    n->op = op;
    return n;
}

static Node *parse_alt(Parser *p) {
    Node *l = parse_assign(p);
    if (!peek(p, "//=") && accept(p, "//")) return node2(N_ALT, l, parse_alt(p));
    return l;
}

static Node *parse_comma(Parser *p) {
    Node *l = parse_alt(p);
    while (accept(p, ",")) l = node2(N_COMMA, l, parse_alt(p));
    return l;
}

static Node *parse_funcdef(Parser *p) {
    Node *def = node_new(N_FUNCDEF);
    def->name = read_ident(p);
    if (!def->name) parse_fail(p, "expected function name");
    int nparams = 0;
    if (accept(p, "(")) {
        for (;;) {
            Node *param = node_new(N_VAR);
            int is_var = peek(p, "$");
            param->name = is_var ? read_var(p) : read_ident(p);
            if (!param->name) { parse_fail(p, "expected parameter"); break; }
            list_add(def, param);
            def->param_is_var = (int *) realloc(def->param_is_var, sizeof(int) * (unsigned long) (nparams + 1));
            def->param_is_var[nparams++] = is_var;
            if (accept(p, ";")) continue;
            expect(p, ")");
            break;
        }
    }
    expect(p, ":");
    def->a = parse_pipe(p);
    expect(p, ";");
    def->b = parse_pipe(p);   /* the scope the definition is visible in */
    return def;
}

static Node *parse_pipe(Parser *p) {
    if (accept_keyword(p, "def")) return parse_funcdef(p);
    Node *l = parse_comma(p);
    if (!peek(p, "||") && !peek(p, "|=") && accept(p, "|")) return node2(N_PIPE, l, parse_pipe(p));
    return l;
}

static Node *parse_program(const char *src, int len, char *error, int error_cap) {
    Parser p;
    p.src = src;
    p.pos = 0;
    p.len = len;
    p.failed = 0;
    p.message[0] = 0;
    Node *n = parse_pipe(&p);
    skip_ws(&p);
    if (!p.failed && p.pos != p.len) parse_fail(&p, "trailing input");
    if (p.failed) {
        snprintf(error, (unsigned long) error_cap, "%s", p.message);
        return 0;
    }
    return n;
}

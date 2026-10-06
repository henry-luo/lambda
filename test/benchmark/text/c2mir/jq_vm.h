/* jq-core compiler and backtracking VM (see jq_core.h).
 *
 * The design follows jq's src/compile.c + src/execute.c:
 * - an expression's code takes its input on top of the data stack and leaves
 *   one output there; a generator yields further outputs on backtracking;
 * - the data stack is jq's forkable stack: cells link to the cell below, and a
 *   fork point protects every cell at or below it (`limit`), so a pop after a
 *   fork never destroys what backtracking will restore;
 * - fork records carry the resume pc, stack position, frame and path state;
 *   EACH and RANGE records also carry the iteration state;
 * - try/label records catch errors; TRY_END deactivates a try when its body
 *   yields and pushes a TRY_EXIT record that reactivates it on re-entry, so
 *   errors raised downstream of a try are not caught (jq 1.7 semantics);
 * - path(f) tracking follows jq: PATH_BEGIN/PATH_END, INDEX/EACH/getpath
 *   append to the current path while no SUBEXP is open;
 * - calls make heap frames: closures (filter arguments) capture the frame of
 *   the call site, `$x` variables live in frame slots resolved statically,
 *   and a call in tail position reuses the frame when no fork point refers
 *   into it (so `until` runs in constant frame depth).
 */

enum {
    OP_DUP = 1, OP_POP, OP_LOADK, OP_NEWARR, OP_OBJ_START, OP_SUBEXP_BEGIN, OP_SUBEXP_END,
    OP_INDEX, OP_INDEXK, OP_EACH, OP_SLICE, OP_FORK, OP_JUMP, OP_JUMP_F, OP_JUMP_F_SUB,
    OP_JUMP_F_KEEP, OP_BACKTRACK, OP_STOREV, OP_STOREV_UNDER, OP_LOADV, OP_LOADVN, OP_APPEND,
    OP_INSERT, OP_RANGE, OP_PATH_BEGIN, OP_PATH_END, OP_CALL_NATIVE, OP_CALL_JQ,
    OP_TAIL_CALL_JQ, OP_CALL_PARAM, OP_TAIL_CALL_PARAM, OP_RET, OP_TRY_BEGIN, OP_TRY_END,
    OP_LABEL_BEGIN, OP_BREAK, OP_BINOP, OP_NEG, OP_TOBOOL
};

/* ---------- natives ---------- */

enum {
    NAT_LENGTH = 1, NAT_NOT, NAT_TYPE, NAT_KEYS, NAT_KEYS_UNSORTED, NAT_HAS, NAT_CONTAINS,
    NAT_TOSTRING, NAT_TOJSON, NAT_FROMJSON, NAT_TONUMBER, NAT_ASCII_UPCASE, NAT_ASCII_DOWNCASE,
    NAT_EXPLODE, NAT_IMPLODE, NAT_SPLIT, NAT_JOIN, NAT_ADD, NAT_FLATTEN, NAT_FLOOR, NAT_CEIL,
    NAT_MIN, NAT_MAX, NAT_UNIQUE, NAT_SORT, NAT_REVERSE, NAT_GETPATH, NAT_SETPATH, NAT_DELPATHS,
    NAT_FROM_ENTRIES, NAT_SORT_BY_IMPL, NAT_GROUP_BY_IMPL, NAT_ERROR0, NAT_ERROR1
};

typedef struct {
    const char *name;
    int arity;
    int id;
} NativeDef;

static const NativeDef natives[] = {
    {"length", 0, NAT_LENGTH}, {"not", 0, NAT_NOT}, {"type", 0, NAT_TYPE}, {"keys", 0, NAT_KEYS},
    {"keys_unsorted", 0, NAT_KEYS_UNSORTED}, {"has", 1, NAT_HAS}, {"contains", 1, NAT_CONTAINS},
    {"tostring", 0, NAT_TOSTRING}, {"tojson", 0, NAT_TOJSON}, {"fromjson", 0, NAT_FROMJSON},
    {"tonumber", 0, NAT_TONUMBER}, {"ascii_upcase", 0, NAT_ASCII_UPCASE},
    {"ascii_downcase", 0, NAT_ASCII_DOWNCASE}, {"explode", 0, NAT_EXPLODE},
    {"implode", 0, NAT_IMPLODE}, {"split", 1, NAT_SPLIT}, {"join", 1, NAT_JOIN}, {"add", 0, NAT_ADD},
    {"flatten", 0, NAT_FLATTEN}, {"floor", 0, NAT_FLOOR}, {"ceil", 0, NAT_CEIL}, {"min", 0, NAT_MIN},
    {"max", 0, NAT_MAX}, {"unique", 0, NAT_UNIQUE}, {"sort", 0, NAT_SORT}, {"reverse", 0, NAT_REVERSE},
    {"getpath", 1, NAT_GETPATH}, {"setpath", 2, NAT_SETPATH}, {"delpaths", 1, NAT_DELPATHS},
    {"from_entries", 0, NAT_FROM_ENTRIES}, {"_sort_by_impl", 1, NAT_SORT_BY_IMPL},
    {"_group_by_impl", 1, NAT_GROUP_BY_IMPL}, {"error", 0, NAT_ERROR0}, {"error", 1, NAT_ERROR1},
    {0, 0, 0}
};

/* jq 1.7.1 src/builtin.jq definitions used by jq-core. add, join, flatten and
 * from_entries are native instead: their jq definitions are linear only
 * because jq mutates refcount-1 values in place, which immutable values
 * cannot do. */
static const char *jq_prelude =
    "def select(f): if f then . else empty end;\n"
    "def recurse(f): def r: ., (f | r); r;\n"
    "def recurse: recurse(.[]?);\n"
    "def map(f): [.[] | f];\n"
    "def to_entries: [keys_unsorted[] as $k | {key: $k, value: .[$k]}];\n"
    "def with_entries(f): to_entries | map(f) | from_entries;\n"
    "def paths: path(..) | select(length > 0);\n"
    "def paths(node_filter): . as $dot | paths | select(. as $p | $dot | getpath($p) | node_filter);\n"
    "def del(f): delpaths([path(f)]);\n"
    "def _assign(paths; $value): reduce path(paths) as $p (.; setpath($p; $value));\n"
    "def _modify(paths; update): reduce path(paths) as $p (.; . as $x | label $out"
    " | (setpath($p; $x | getpath($p) | update) | ., break $out), delpaths([$p]));\n"
    "def first(f): label $out | f | ., break $out;\n"
    "def last(f): reduce f as $x (null; $x);\n"
    "def limit($n; f): if $n > 0 then label $out | foreach f as $item (0; . + 1; $item,"
    " if . >= $n then break $out else empty end) elif $n == 0 then empty else f end;\n"
    "def nth($n; f): if $n < 0 then error(\"Out of bounds negative array index\")"
    " else last(limit($n + 1; f)) end;\n"
    "def repeat(f): def _repeat: f, _repeat; _repeat;\n"
    "def until(cond; update): def _until: if cond then . else (update | _until) end; _until;\n"
    "def first: .[0];\n"
    "def last: .[-1];\n"
    "def sort_by(f): _sort_by_impl(map([f]));\n"
    "def group_by(f): _group_by_impl(map([f]));\n"
    "def unique_by(f): [group_by(f)[] | .[0]];\n"
    "def scalars: select(type | . != \"array\" and . != \"object\");\n"
    "def objects: select(type == \"object\");\n"
    "def arrays: select(type == \"array\");\n"
    "def numbers: select(type == \"number\");\n"
    "def strings: select(type == \"string\");\n"
    "def range($x): range(0; $x);\n"
    "def isempty(g): first((g | false), true);\n"
    "def any: reduce .[] as $x (false; . or $x);\n"
    "def all: reduce .[] as $x (true; . and $x);\n";

/* ---------- program ---------- */

typedef struct {
    int entry;
    int nparams;
    int nlocals;
} Fn;

typedef struct {
    int *code;
    int ncode, capcode;
    V *consts;
    int nconsts, capconsts;
    Fn *fns;
    int nfns, capfns;
} Program;

static Program prog;

static int emit(int x) {
    if (prog.ncode == prog.capcode) {
        prog.capcode = prog.capcode ? prog.capcode * 2 : 1024;
        prog.code = (int *) realloc(prog.code, sizeof(int) * (unsigned long) prog.capcode);
    }
    prog.code[prog.ncode] = x;
    return prog.ncode++;
}

static int add_const(V v) {
    if (prog.nconsts == prog.capconsts) {
        prog.capconsts = prog.capconsts ? prog.capconsts * 2 : 64;
        prog.consts = (V *) realloc(prog.consts, sizeof(V) * (unsigned long) prog.capconsts);
    }
    prog.consts[prog.nconsts] = v;
    return prog.nconsts++;
}

static int new_fn(void) {
    if (prog.nfns == prog.capfns) {
        prog.capfns = prog.capfns ? prog.capfns * 2 : 64;
        prog.fns = (Fn *) realloc(prog.fns, sizeof(Fn) * (unsigned long) prog.capfns);
    }
    prog.fns[prog.nfns].entry = 0;
    prog.fns[prog.nfns].nparams = 0;
    prog.fns[prog.nfns].nlocals = 0;
    return prog.nfns++;
}

/* ---------- compiler ---------- */

enum { SC_VAR = 1, SC_DEF, SC_PARAM };

typedef struct Scope {
    int kind;
    const char *name;
    int arity;
    int index;           /* slot, fn index, or param index */
    struct Scope *prev;
} Scope;

typedef struct Ctx {
    struct Ctx *parent;
    int level;
    int fn;
    Scope *scope;
    int nlocals;
} Ctx;

static char compile_error[256];
static int compile_failed = 0;
static int synthetic_var_counter = 0;

static void compile_fail(const char *what, const char *name) {
    if (!compile_failed) {
        compile_failed = 1;
        snprintf(compile_error, sizeof compile_error, "jq compile error: %s %s", what, name ? name : "");
    }
}

static int name_eq(const char *a, const char *b) {
    unsigned long n = strlen(a);
    return n == strlen(b) && memcmp(a, b, n) == 0;
}

static Scope *scope_push(Ctx *c, int kind, const char *name, int arity, int index) {
    Scope *s = (Scope *) malloc(sizeof(Scope));
    s->kind = kind;
    s->name = name;
    s->arity = arity;
    s->index = index;
    s->prev = c->scope;
    c->scope = s;
    return s;
}

static int new_local(Ctx *c) { return c->nlocals++; }

static void emit_jump_target(int at) { prog.code[at] = prog.ncode; }

static void compile(Ctx *c, Node *n, int tail);

/* `code` with SUBEXP brackets: leaves [result, input] on the stack */
static void compile_subexp(Ctx *c, Node *n) {
    emit(OP_SUBEXP_BEGIN);
    compile(c, n, 0);
    emit(OP_SUBEXP_END);
}

static void emit_var_op(int op, int hops, int slot) {
    emit(op);
    emit(hops);
    emit(slot);
}

static int lookup_var(Ctx *c, const char *name, int *hops) {
    for (Ctx *x = c; x; x = x->parent) {
        for (Scope *s = x->scope; s; s = s->prev) {
            if (s->kind == SC_VAR && name_eq(s->name, name)) {
                *hops = c->level - x->level;
                return s->index;
            }
        }
    }
    compile_fail("undefined variable $", name);
    return 0;
}

/* compile each argument as a closure: an inline function of the call site */
static int compile_closure(Ctx *c, Node *body) {
    int jump = emit(OP_JUMP);
    int target = emit(0);
    Ctx inner;
    inner.parent = c;
    inner.level = c->level + 1;
    inner.fn = new_fn();
    inner.scope = 0;
    inner.nlocals = 0;
    prog.fns[inner.fn].entry = prog.ncode;
    compile(&inner, body, 1);
    emit(OP_RET);
    prog.fns[inner.fn].nlocals = inner.nlocals;
    (void) jump;
    emit_jump_target(target);
    return inner.fn;
}

static int find_native(const char *name, int arity) {
    for (int i = 0; natives[i].name; i++)
        if (natives[i].arity == arity && name_eq(natives[i].name, name)) return natives[i].id;
    return 0;
}

static void compile_call(Ctx *c, Node *n, int tail) {
    const char *name = n->name;
    int arity = n->nlist;
    /* user/prelude definitions and parameters, innermost first */
    for (Ctx *x = c; x; x = x->parent) {
        for (Scope *s = x->scope; s; s = s->prev) {
            if (!name_eq(s->name, name)) continue;
            if (s->kind == SC_PARAM && arity == 0) {
                emit(tail ? OP_TAIL_CALL_PARAM : OP_CALL_PARAM);
                emit(c->level - x->level);
                emit(s->index);
                return;
            }
            if (s->kind == SC_DEF && s->arity == arity) {
                int closures[16];
                if (arity > 16) { compile_fail("too many arguments for", name); return; }
                for (int i = 0; i < arity; i++) closures[i] = compile_closure(c, n->list[i]);
                emit(tail ? OP_TAIL_CALL_JQ : OP_CALL_JQ);
                emit(s->index);
                emit(c->level - x->level);
                emit(arity);
                for (int i = 0; i < arity; i++) emit(closures[i]);
                return;
            }
        }
    }
    if (arity == 0 && name_eq(name, "empty")) { emit(OP_BACKTRACK); return; }
    if (arity == 0 && name_eq(name, "true")) { emit(OP_LOADK); emit(add_const(V_TRUE)); return; }
    if (arity == 0 && name_eq(name, "false")) { emit(OP_LOADK); emit(add_const(V_FALSE)); return; }
    if (arity == 0 && name_eq(name, "null")) { emit(OP_LOADK); emit(add_const(V_NULL)); return; }
    if (arity == 1 && name_eq(name, "path")) {
        emit(OP_PATH_BEGIN);
        compile(c, n->list[0], 0);
        emit(OP_PATH_END);
        return;
    }
    if ((arity == 2 || arity == 3) && name_eq(name, "range")) {
        for (int i = 0; i < arity; i++) compile_subexp(c, n->list[i]);
        emit(OP_RANGE);
        emit(arity);
        return;
    }
    int id = find_native(name, arity);
    if (!id) { compile_fail("undefined function", name); return; }
    for (int i = 0; i < arity; i++) compile_subexp(c, n->list[i]);
    emit(OP_CALL_NATIVE);
    emit(id);
    emit(arity);
}

static void compile_funcdef(Ctx *c, Node *n, int tail) {
    int nparams = n->nlist;
    int fn = new_fn();
    prog.fns[fn].nparams = nparams;
    /* visible in its own body (recursion) and in the rest of the scope */
    scope_push(c, SC_DEF, n->name, nparams, fn);
    emit(OP_JUMP);
    int target = emit(0);
    Ctx inner;
    inner.parent = c;
    inner.level = c->level + 1;
    inner.fn = fn;
    inner.scope = 0;
    inner.nlocals = 0;
    prog.fns[fn].entry = prog.ncode;
    for (int i = 0; i < nparams; i++) scope_push(&inner, SC_PARAM, n->list[i]->name, 0, i);
    /* def f($a): body  ==  def f(a): a as $a | body */
    Node *body = n->a;
    for (int i = nparams - 1; i >= 0; i--) {
        if (!n->param_is_var[i]) continue;
        Node *bind = node_new(N_BIND);
        bind->a = call_node(n->list[i]->name, 0, 0);
        bind->name = n->list[i]->name;
        bind->b = body;
        body = bind;
    }
    compile(&inner, body, 1);
    emit(OP_RET);
    prog.fns[fn].nlocals = inner.nlocals;
    emit_jump_target(target);
    compile(c, n->b, tail);
}

static Node *synthetic_var(void) {
    char name[32];
    snprintf(name, sizeof name, "__rhs%d", synthetic_var_counter++);
    Node *v = node_new(N_VAR);
    unsigned long len = strlen(name);
    char *copy = (char *) malloc(len + 1);
    memcpy(copy, name, len + 1);
    v->name = copy;
    return v;
}

static void compile(Ctx *c, Node *n, int tail) {
    if (compile_failed) return;
    switch (n->kind) {
    case N_IDENTITY:
        return;
    case N_RECURSE_DEFAULT: {
        Node *call = call_node("recurse", 0, 0);
        compile_call(c, call, tail);
        return;
    }
    case N_LITERAL:
        emit(OP_LOADK);
        emit(add_const(n->value));
        return;
    case N_VAR: {
        int hops = 0, slot = lookup_var(c, n->name, &hops);
        emit_var_op(OP_LOADV, hops, slot);
        return;
    }
    case N_INDEX:
        if (n->b->kind == N_LITERAL) {
            compile(c, n->a, 0);
            emit(OP_INDEXK);
            emit(add_const(n->b->value));
            return;
        }
        /* the key is evaluated against the term's own input */
        compile_subexp(c, n->b);
        compile(c, n->a, 0);
        emit(OP_INDEX);
        return;
    case N_ITER:
        compile(c, n->a, 0);
        emit(OP_EACH);
        return;
    case N_SLICE:
        compile_subexp(c, n->b ? n->b : literal(V_NULL));
        compile_subexp(c, n->c ? n->c : literal(V_NULL));
        compile(c, n->a, 0);
        emit(OP_SLICE);
        return;
    case N_TRY: {
        emit(OP_TRY_BEGIN);
        int handler = emit(0);
        compile(c, n->a, 0);
        emit(OP_TRY_END);
        emit(OP_JUMP);
        int end = emit(0);
        emit_jump_target(handler);
        if (n->b) compile(c, n->b, tail);
        else emit(OP_BACKTRACK);
        emit_jump_target(end);
        return;
    }
    case N_ARRAY: {
        if (!n->a) { emit(OP_NEWARR); return; }
        int slot = new_local(c);
        emit(OP_DUP);
        emit(OP_NEWARR);
        emit_var_op(OP_STOREV, 0, slot);
        emit(OP_FORK);
        int done = emit(0);
        compile(c, n->a, 0);
        emit_var_op(OP_APPEND, 0, slot);
        emit(OP_BACKTRACK);
        emit_jump_target(done);
        emit_var_op(OP_LOADVN, 0, slot);
        return;
    }
    case N_OBJECT:
        emit(OP_OBJ_START);
        for (int i = 0; i < n->nlist; i += 2) {
            compile_subexp(c, n->list[i]);
            compile_subexp(c, n->list[i + 1]);
            emit(OP_INSERT);
        }
        emit(OP_POP);
        return;
    case N_NEG:
        compile(c, n->a, 0);
        emit(OP_NEG);
        return;
    case N_BINOP:
        /* jq evaluates the right operand in the outer loop */
        compile_subexp(c, n->b);
        compile_subexp(c, n->a);
        emit(OP_BINOP);
        emit(n->op);
        return;
    case N_AND: {
        compile_subexp(c, n->a);
        emit(OP_JUMP_F_SUB);
        int no = emit(0);
        compile(c, n->b, 0);
        emit(OP_TOBOOL);
        emit(OP_JUMP);
        int end = emit(0);
        emit_jump_target(no);
        emit(OP_LOADK);
        emit(add_const(V_FALSE));
        emit_jump_target(end);
        return;
    }
    case N_OR: {
        compile_subexp(c, n->a);
        emit(OP_JUMP_F_SUB);
        int other = emit(0);
        emit(OP_LOADK);
        emit(add_const(V_TRUE));
        emit(OP_JUMP);
        int end = emit(0);
        emit_jump_target(other);
        compile(c, n->b, 0);
        emit(OP_TOBOOL);
        emit_jump_target(end);
        return;
    }
    case N_ALT: {
        /* a // b: every truthy output of a (errors suppressed); b when none */
        int found = new_local(c);
        emit(OP_DUP);
        emit(OP_LOADK);
        emit(add_const(V_FALSE));
        emit_var_op(OP_STOREV, 0, found);
        emit(OP_FORK);
        int other = emit(0);
        emit(OP_TRY_BEGIN);
        int skip = emit(0);
        compile(c, n->a, 0);
        emit(OP_TRY_END);
        emit(OP_JUMP_F_KEEP);
        int skip2 = emit(0);
        emit(OP_DUP);
        emit(OP_LOADK);
        emit(add_const(V_TRUE));
        emit_var_op(OP_STOREV, 0, found);
        emit(OP_JUMP);
        int end = emit(0);
        emit_jump_target(skip);
        emit_jump_target(skip2);
        emit(OP_BACKTRACK);
        emit_jump_target(other);
        emit(OP_DUP);
        emit_var_op(OP_LOADV, 0, found);
        emit(OP_JUMP_F);
        int run = emit(0);
        emit(OP_BACKTRACK);
        emit_jump_target(run);
        compile(c, n->b, tail);
        emit_jump_target(end);
        return;
    }
    case N_UPDATE: {
        Node *call = call_node("_modify", n->a, n->b);
        compile_call(c, call, tail);
        return;
    }
    case N_ASSIGN: {
        Node *call = call_node("_assign", n->a, n->b);
        compile_call(c, call, tail);
        return;
    }
    case N_ARITH_UPDATE:
    case N_ALT_UPDATE: {
        /* a op= b  ==  b as $v | _modify(a; . op $v), b evaluated against . */
        Node *var = synthetic_var();
        Node *update;
        if (n->kind == N_ARITH_UPDATE) update = binop(n->op, node_new(N_IDENTITY), var);
        else update = node2(N_ALT, node_new(N_IDENTITY), var);
        Node *bind = node_new(N_BIND);
        bind->a = n->b;
        bind->name = var->name;
        bind->b = call_node("_modify", n->a, update);
        compile(c, bind, tail);
        return;
    }
    case N_PIPE:
        compile(c, n->a, 0);
        compile(c, n->b, tail);
        return;
    case N_COMMA: {
        emit(OP_FORK);
        int second = emit(0);
        compile(c, n->a, tail);
        emit(OP_JUMP);
        int end = emit(0);
        emit_jump_target(second);
        compile(c, n->b, tail);
        emit_jump_target(end);
        return;
    }
    case N_BIND: {
        int slot = new_local(c);
        compile_subexp(c, n->a);
        emit_var_op(OP_STOREV_UNDER, 0, slot);
        Scope *saved = c->scope;
        scope_push(c, SC_VAR, n->name, 0, slot);
        compile(c, n->b, tail);
        c->scope = saved;
        return;
    }
    case N_REDUCE: {
        int acc = new_local(c), x = new_local(c);
        emit(OP_DUP);
        compile(c, n->b, 0);
        emit_var_op(OP_STOREV, 0, acc);
        emit(OP_FORK);
        int end = emit(0);
        emit(OP_DUP);
        compile(c, n->a, 0);
        emit_var_op(OP_STOREV, 0, x);
        emit_var_op(OP_LOADVN, 0, acc);
        Scope *saved = c->scope;
        scope_push(c, SC_VAR, n->name, 0, x);
        compile(c, n->c, 0);
        c->scope = saved;
        emit_var_op(OP_STOREV, 0, acc);
        emit(OP_BACKTRACK);
        emit_jump_target(end);
        emit_var_op(OP_LOADVN, 0, acc);
        return;
    }
    case N_FOREACH: {
        int acc = new_local(c), x = new_local(c);
        emit(OP_DUP);
        compile(c, n->b, 0);
        emit_var_op(OP_STOREV, 0, acc);
        emit(OP_DUP);
        compile(c, n->a, 0);
        emit_var_op(OP_STOREV, 0, x);
        emit_var_op(OP_LOADV, 0, acc);
        Scope *saved = c->scope;
        scope_push(c, SC_VAR, n->name, 0, x);
        compile(c, n->c, 0);
        emit(OP_DUP);
        emit_var_op(OP_STOREV, 0, acc);
        if (n->d) compile(c, n->d, tail);
        c->scope = saved;
        return;
    }
    case N_IF: {
        compile_subexp(c, n->a);
        emit(OP_JUMP_F_SUB);
        int other = emit(0);
        compile(c, n->b, tail);
        emit(OP_JUMP);
        int end = emit(0);
        emit_jump_target(other);
        if (n->c) compile(c, n->c, tail);
        emit_jump_target(end);
        return;
    }
    case N_FUNCDEF: {
        Scope *saved = c->scope;
        compile_funcdef(c, n, tail);
        c->scope = saved;
        return;
    }
    case N_CALL:
        compile_call(c, n, tail);
        return;
    case N_LABEL: {
        int slot = new_local(c);
        emit_var_op(OP_LABEL_BEGIN, 0, slot);
        Scope *saved = c->scope;
        scope_push(c, SC_VAR, n->name, 0, slot);
        compile(c, n->a, 0);
        c->scope = saved;
        emit(OP_TRY_END);
        return;
    }
    case N_BREAK: {
        int hops = 0, slot = lookup_var(c, n->name, &hops);
        emit_var_op(OP_BREAK, hops, slot);
        return;
    }
    default:
        compile_fail("unsupported construct", 0);
    }
}

/* compile prelude + user program as one top-level function (fn 0) */
static int compile_program(const char *user_src, int user_len, char *error, int error_cap) {
    int prelude_len = (int) strlen(jq_prelude);
    char *src = (char *) malloc((unsigned long) (prelude_len + user_len + 2));
    memcpy(src, jq_prelude, (unsigned long) prelude_len);
    src[prelude_len] = '\n';
    memcpy(src + prelude_len + 1, user_src, (unsigned long) user_len);
    src[prelude_len + 1 + user_len] = 0;
    Node *ast = parse_program(src, prelude_len + 1 + user_len, error, error_cap);
    if (!ast) return 0;
    Ctx top;
    top.parent = 0;
    top.level = 0;
    top.fn = new_fn();
    top.scope = 0;
    top.nlocals = 0;
    prog.fns[top.fn].entry = prog.ncode;
    compile(&top, ast, 1);
    emit(OP_RET);
    prog.fns[top.fn].nlocals = top.nlocals;
    if (compile_failed) {
        snprintf(error, (unsigned long) error_cap, "%s", compile_error);
        return 0;
    }
    return 1;
}

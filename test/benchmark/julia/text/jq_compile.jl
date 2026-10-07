# Native Julia port of ../../jq_vm.ls and ../../text/c2mir/jq_*.h.
# Lexically scoped bytecode compiler and jq prelude.
const SC_VAR = 1
const SC_DEF = 2
const SC_PARAM = 3
# jq 1.7.1 builtin.jq definitions used by the bounded benchmark subset.
const jq_prelude = raw"""
def select(f): if f then . else empty end;
def recurse(f): def r: ., (f | r); r;
def recurse: recurse(.[]?);
def map(f): [.[] | f];
def to_entries: [keys_unsorted[] as $k | {key: $k, value: .[$k]}];
def with_entries(f): to_entries | map(f) | from_entries;
def paths: path(..) | select(length > 0);
def paths(node_filter): . as $dot | paths | select(. as $p | $dot | getpath($p) | node_filter);
def del(f): delpaths([path(f)]);
def _assign(paths; $value): reduce path(paths) as $p (.; setpath($p; $value));
def _modify(paths; update): reduce path(paths) as $p (.; . as $x | label $out | (setpath($p; $x | getpath($p) | update) | ., break $out), delpaths([$p]));
def first(f): label $out | f | ., break $out;
def last(f): reduce f as $x (null; $x);
def limit($n; f): if $n > 0 then label $out | foreach f as $item (0; . + 1; $item, if . >= $n then break $out else empty end) elif $n == 0 then empty else f end;
def nth($n; f): if $n < 0 then error("Out of bounds negative array index") else last(limit($n + 1; f)) end;
def repeat(f): def _repeat: f, _repeat; _repeat;
def until(cond; update): def _until: if cond then . else (update | _until) end; _until;
def first: .[0];
def last: .[-1];
def sort_by(f): _sort_by_impl(map([f]));
def group_by(f): _group_by_impl(map([f]));
def unique_by(f): [group_by(f)[] | .[0]];
def scalars: select(type | . != "array" and . != "object");
def objects: select(type == "object");
def arrays: select(type == "array");
def numbers: select(type == "number");
def strings: select(type == "string");
def range($x): range(0; $x);
def isempty(g): first((g | false), true);
def any: reduce .[] as $x (false; . or $x);
def all: reduce .[] as $x (true; . and $x);
"""
Base.@kwdef mutable struct Comp
    code::Vector{Int}
    consts::Vector{Any}
    fn_entry::Vector{Int}
    fn_nparams::Vector{Int}
    fn_nlocals::Vector{Int}
    ctx_parent::Vector{Int}
    ctx_level::Vector{Int}
    ctx_fn::Vector{Int}
    ctx_scope::Vector{Int}
    ctx_nlocals::Vector{Int}
    sc_kind::Vector{Int}
    sc_name::Vector{String}
    sc_arity::Vector{Int}
    sc_index::Vector{Int}
    sc_prev::Vector{Int}
    failed::Bool
    msg::String
    synth::Int
end

function compile_fail(cs::Comp, what, name)
    if (!cs.failed)
        cs.failed = true
        cs.msg = jcat("jq compile error: ", what, " ", name)
    end
end

function emit_op(cs::Comp, x)
    push!(cs.code, x)
    (length(cs.code) - 1)
end

function add_const(cs::Comp, v)
    push!(cs.consts, v)
    (length(cs.consts) - 1)
end

function patch_here(cs::Comp, at)
    jset!(cs.code, at, length(cs.code))
end

function new_fn(cs::Comp, nparams)
    push!(cs.fn_entry, 0)
    push!(cs.fn_nparams, nparams)
    push!(cs.fn_nlocals, 0)
    (length(cs.fn_entry) - 1)
end

function new_ctx(cs::Comp, parent, fn_index)
    push!(cs.ctx_parent, parent)
    push!(cs.ctx_level, ((parent < 0) ? 0 : (jget(cs.ctx_level, parent) + 1)))
    push!(cs.ctx_fn, fn_index)
    push!(cs.ctx_scope, (-1))
    push!(cs.ctx_nlocals, 0)
    (length(cs.ctx_parent) - 1)
end

function scope_push(cs::Comp, c, kind, name, arity, index)
    push!(cs.sc_kind, kind)
    push!(cs.sc_name, name)
    push!(cs.sc_arity, arity)
    push!(cs.sc_index, index)
    push!(cs.sc_prev, jget(cs.ctx_scope, c))
    jset!(cs.ctx_scope, c, (length(cs.sc_kind) - 1))
end

function new_local(cs::Comp, c)
    slot = jget(cs.ctx_nlocals, c)
    jset!(cs.ctx_nlocals, c, (slot + 1))
    slot
end

function emit_var_op(cs::Comp, op, hops, slot)
    emit_op(cs, op)
    emit_op(cs, hops)
    emit_op(cs, slot)
end

# SUBEXP brackets retain the original input beside each generated result.
function compile_subexp(cs::Comp, c, n)
    emit_op(cs, OP_SUBEXP_BEGIN)
    compile(cs, c, n, false)
    emit_op(cs, OP_SUBEXP_END)
end

function lookup_var(cs::Comp, c, name)
    x = c
    while (x >= 0)
        s = jget(cs.ctx_scope, x)
        while (s >= 0)
            if ((jget(cs.sc_kind, s) == SC_VAR) && (jget(cs.sc_name, s) == name))
                return Any[jget(cs.sc_index, s), (jget(cs.ctx_level, c) - jget(cs.ctx_level, x))]
            end
            s = jget(cs.sc_prev, s)
        end
        x = jget(cs.ctx_parent, x)
    end
    compile_fail(cs, "undefined variable \$", name)
    Any[0, 0]
end

# inline argument closures capture their lexical call-site frame.
function compile_closure(cs::Comp, c, body)
    emit_op(cs, OP_JUMP)
    target = emit_op(cs, 0)
    fn_index = new_fn(cs, 0)
    inner = new_ctx(cs, c, fn_index)
    jset!(cs.fn_entry, fn_index, length(cs.code))
    compile(cs, inner, body, true)
    emit_op(cs, OP_RET)
    jset!(cs.fn_nlocals, fn_index, jget(cs.ctx_nlocals, inner))
    patch_here(cs, target)
    fn_index
end

function find_native(name, arity)
    hits = Any[(i + 1) for i in (0 : (length(native_names) - 1)) if ((jget(native_names, i) == name) && (jget(native_arity, i) == arity))]
    ((length(hits) > 0) ? jget(hits, 0) : 0)
end

function compile_call(cs::Comp, c, n, tail)
    name = n.name
    args = n.list
    arity = length(args)
    x = c
    while (x >= 0)
        s = jget(cs.ctx_scope, x)
        while (s >= 0)
            if (jget(cs.sc_name, s) == name)
                if ((jget(cs.sc_kind, s) == SC_PARAM) && (arity == 0))
                    emit_op(cs, (tail ? OP_TAIL_CALL_PARAM : OP_CALL_PARAM))
                    emit_op(cs, (jget(cs.ctx_level, c) - jget(cs.ctx_level, x)))
                    emit_op(cs, jget(cs.sc_index, s))
                    return nothing
                end
                if ((jget(cs.sc_kind, s) == SC_DEF) && (jget(cs.sc_arity, s) == arity))
                    closures = Int[]
                    i = 0
                    while (i < arity)
                        push!(closures, compile_closure(cs, c, jget(args, i)))
                        i = (i + 1)
                    end
                    emit_op(cs, (tail ? OP_TAIL_CALL_JQ : OP_CALL_JQ))
                    emit_op(cs, jget(cs.sc_index, s))
                    emit_op(cs, (jget(cs.ctx_level, c) - jget(cs.ctx_level, x)))
                    emit_op(cs, arity)
                    j = 0
                    while (j < arity)
                        emit_op(cs, jget(closures, j))
                        j = (j + 1)
                    end
                    return nothing
                end
            end
            s = jget(cs.sc_prev, s)
        end
        x = jget(cs.ctx_parent, x)
    end
    if ((arity == 0) && (name == "empty"))
        emit_op(cs, OP_BACKTRACK)
        return nothing
    end
    if ((arity == 0) && (((name == "true") || (name == "false")) || (name == "null")))
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, ((name == "true") ? true : ((name == "false") ? false : nothing))))
        return nothing
    end
    if ((arity == 1) && (name == "path"))
        emit_op(cs, OP_PATH_BEGIN)
        compile(cs, c, jget(args, 0), false)
        emit_op(cs, OP_PATH_END)
        return nothing
    end
    if (((arity == 2) || (arity == 3)) && (name == "range"))
        i = 0
        while (i < arity)
            compile_subexp(cs, c, jget(args, i))
            i = (i + 1)
        end
        emit_op(cs, OP_RANGE)
        emit_op(cs, arity)
        return nothing
    end
    id = find_native(name, arity)
    if (id == 0)
        compile_fail(cs, "undefined function", name)
        return nothing
    end
    k = 0
    while (k < arity)
        compile_subexp(cs, c, jget(args, k))
        k = (k + 1)
    end
    emit_op(cs, OP_CALL_NATIVE)
    emit_op(cs, id)
    emit_op(cs, arity)
end

# bind the definition before compiling its body so recursion sees itself.
function compile_funcdef(cs::Comp, c, n, tail)
    params = n.list
    nparams = length(params)
    fn_index = new_fn(cs, nparams)
    scope_push(cs, c, SC_DEF, n.name, nparams, fn_index)
    emit_op(cs, OP_JUMP)
    target = emit_op(cs, 0)
    inner = new_ctx(cs, c, fn_index)
    jset!(cs.fn_entry, fn_index, length(cs.code))
    i = 0
    while (i < nparams)
        scope_push(cs, inner, SC_PARAM, jget(params, i).name, 0, i)
        i = (i + 1)
    end
    body = n.a
    j = (nparams - 1)
    while (j >= 0)
        if jget(n.pvar, j)
            body = mk_named(N_BIND, jget(params, j).name, mk_call(jget(params, j).name, Any[]), body)
        end
        j = (j - 1)
    end
    compile(cs, inner, body, true)
    emit_op(cs, OP_RET)
    jset!(cs.fn_nlocals, fn_index, jget(cs.ctx_nlocals, inner))
    patch_here(cs, target)
    compile(cs, c, n.b, tail)
end

function synthetic_var(cs::Comp)
    cs.synth = (cs.synth + 1)
    jcat("__rhs", jstring(cs.synth))
end

function compile(cs::Comp, c, n, tail)
    if cs.failed
        return nothing
    end
    kind = n.kind
    if (kind == N_IDENTITY)
        return nothing
    end
    if (kind == N_RECURSE_DEFAULT)
        compile_call(cs, c, mk_call("recurse", Any[]), tail)
        return nothing
    end
    if (kind == N_LITERAL)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, n.value))
        return nothing
    end
    if (kind == N_VAR)
        at = lookup_var(cs, c, n.name)
        emit_var_op(cs, OP_LOADV, jget(at, 1), jget(at, 0))
        return nothing
    end
    if (kind == N_INDEX)
        if (n.b.kind == N_LITERAL)
            compile(cs, c, n.a, false)
            emit_op(cs, OP_INDEXK)
            emit_op(cs, add_const(cs, n.b.value))
            return nothing
        end
        compile_subexp(cs, c, n.b)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_INDEX)
        return nothing
    end
    if (kind == N_ITER)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_EACH)
        return nothing
    end
    if (kind == N_SLICE)
        compile_subexp(cs, c, ((n.b == nothing) ? mk_lit(nothing) : n.b))
        compile_subexp(cs, c, ((n.c == nothing) ? mk_lit(nothing) : n.c))
        compile(cs, c, n.a, false)
        emit_op(cs, OP_SLICE)
        return nothing
    end
    if (kind == N_TRY)
        emit_op(cs, OP_TRY_BEGIN)
        handler = emit_op(cs, 0)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_TRY_END)
        emit_op(cs, OP_JUMP)
        end_at = emit_op(cs, 0)
        patch_here(cs, handler)
        if (n.b != nothing)
            compile(cs, c, n.b, tail)
        else
            emit_op(cs, OP_BACKTRACK)
        end
        patch_here(cs, end_at)
        return nothing
    end
    if (kind == N_ARRAY)
        if (n.a == nothing)
            emit_op(cs, OP_NEWARR)
            return nothing
        end
        slot = new_local(cs, c)
        emit_op(cs, OP_DUP)
        emit_op(cs, OP_NEWARR)
        emit_var_op(cs, OP_STOREV, 0, slot)
        emit_op(cs, OP_FORK)
        done = emit_op(cs, 0)
        compile(cs, c, n.a, false)
        emit_var_op(cs, OP_APPEND, 0, slot)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, done)
        emit_var_op(cs, OP_LOADVN, 0, slot)
        return nothing
    end
    if (kind == N_OBJECT)
        emit_op(cs, OP_OBJ_START)
        i = 0
        while (i < length(n.list))
            compile_subexp(cs, c, jget(n.list, i))
            compile_subexp(cs, c, jget(n.list, (i + 1)))
            emit_op(cs, OP_INSERT)
            i = (i + 2)
        end
        emit_op(cs, OP_POP)
        return nothing
    end
    if (kind == N_NEG)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_NEG)
        return nothing
    end
    if (kind == N_BINOP)
        compile_subexp(cs, c, n.b)
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_BINOP)
        emit_op(cs, n.op)
        return nothing
    end
    if (kind == N_AND)
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_JUMP_F_SUB)
        no = emit_op(cs, 0)
        compile(cs, c, n.b, false)
        emit_op(cs, OP_TOBOOL)
        emit_op(cs, OP_JUMP)
        end_at = emit_op(cs, 0)
        patch_here(cs, no)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, false))
        patch_here(cs, end_at)
        return nothing
    end
    if (kind == N_OR)
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_JUMP_F_SUB)
        other = emit_op(cs, 0)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, true))
        emit_op(cs, OP_JUMP)
        end_at = emit_op(cs, 0)
        patch_here(cs, other)
        compile(cs, c, n.b, false)
        emit_op(cs, OP_TOBOOL)
        patch_here(cs, end_at)
        return nothing
    end
    if (kind == N_ALT)
        found = new_local(cs, c)
        emit_op(cs, OP_DUP)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, false))
        emit_var_op(cs, OP_STOREV, 0, found)
        emit_op(cs, OP_FORK)
        other = emit_op(cs, 0)
        emit_op(cs, OP_TRY_BEGIN)
        skip = emit_op(cs, 0)
        compile(cs, c, n.a, false)
        emit_op(cs, OP_TRY_END)
        emit_op(cs, OP_JUMP_F_KEEP)
        skip2 = emit_op(cs, 0)
        emit_op(cs, OP_DUP)
        emit_op(cs, OP_LOADK)
        emit_op(cs, add_const(cs, true))
        emit_var_op(cs, OP_STOREV, 0, found)
        emit_op(cs, OP_JUMP)
        end_at = emit_op(cs, 0)
        patch_here(cs, skip)
        patch_here(cs, skip2)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, other)
        emit_op(cs, OP_DUP)
        emit_var_op(cs, OP_LOADV, 0, found)
        emit_op(cs, OP_JUMP_F)
        run = emit_op(cs, 0)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, run)
        compile(cs, c, n.b, tail)
        patch_here(cs, end_at)
        return nothing
    end
    if (kind == N_UPDATE)
        compile_call(cs, c, mk_call("_modify", Any[n.a, n.b]), tail)
        return nothing
    end
    if (kind == N_ASSIGN)
        compile_call(cs, c, mk_call("_assign", Any[n.a, n.b]), tail)
        return nothing
    end
    if ((kind == N_ARITH_UPDATE) || (kind == N_ALT_UPDATE))
        name = synthetic_var(cs)
        var_node = mk_named(N_VAR, name, nothing, nothing)
        update = ((kind == N_ARITH_UPDATE) ? mk_binop(n.op, mk(N_IDENTITY, nothing, nothing), var_node) : mk(N_ALT, mk(N_IDENTITY, nothing, nothing), var_node))
        compile(cs, c, mk_named(N_BIND, name, n.b, mk_call("_modify", Any[n.a, update])), tail)
        return nothing
    end
    if (kind == N_PIPE)
        compile(cs, c, n.a, false)
        compile(cs, c, n.b, tail)
        return nothing
    end
    if (kind == N_COMMA)
        emit_op(cs, OP_FORK)
        second = emit_op(cs, 0)
        compile(cs, c, n.a, tail)
        emit_op(cs, OP_JUMP)
        end_at = emit_op(cs, 0)
        patch_here(cs, second)
        compile(cs, c, n.b, tail)
        patch_here(cs, end_at)
        return nothing
    end
    if (kind == N_BIND)
        slot = new_local(cs, c)
        compile_subexp(cs, c, n.a)
        emit_var_op(cs, OP_STOREV_UNDER, 0, slot)
        saved = jget(cs.ctx_scope, c)
        scope_push(cs, c, SC_VAR, n.name, 0, slot)
        compile(cs, c, n.b, tail)
        jset!(cs.ctx_scope, c, saved)
        return nothing
    end
    if (kind == N_REDUCE)
        acc = new_local(cs, c)
        x = new_local(cs, c)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.b, false)
        emit_var_op(cs, OP_STOREV, 0, acc)
        emit_op(cs, OP_FORK)
        end_at = emit_op(cs, 0)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.a, false)
        emit_var_op(cs, OP_STOREV, 0, x)
        emit_var_op(cs, OP_LOADVN, 0, acc)
        saved = jget(cs.ctx_scope, c)
        scope_push(cs, c, SC_VAR, n.name, 0, x)
        compile(cs, c, n.c, false)
        jset!(cs.ctx_scope, c, saved)
        emit_var_op(cs, OP_STOREV, 0, acc)
        emit_op(cs, OP_BACKTRACK)
        patch_here(cs, end_at)
        emit_var_op(cs, OP_LOADVN, 0, acc)
        return nothing
    end
    if (kind == N_FOREACH)
        acc = new_local(cs, c)
        x = new_local(cs, c)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.b, false)
        emit_var_op(cs, OP_STOREV, 0, acc)
        emit_op(cs, OP_DUP)
        compile(cs, c, n.a, false)
        emit_var_op(cs, OP_STOREV, 0, x)
        emit_var_op(cs, OP_LOADV, 0, acc)
        saved = jget(cs.ctx_scope, c)
        scope_push(cs, c, SC_VAR, n.name, 0, x)
        compile(cs, c, n.c, false)
        emit_op(cs, OP_DUP)
        emit_var_op(cs, OP_STOREV, 0, acc)
        if (n.d != nothing)
            compile(cs, c, n.d, tail)
        end
        jset!(cs.ctx_scope, c, saved)
        return nothing
    end
    if (kind == N_IF)
        compile_subexp(cs, c, n.a)
        emit_op(cs, OP_JUMP_F_SUB)
        other = emit_op(cs, 0)
        compile(cs, c, n.b, tail)
        emit_op(cs, OP_JUMP)
        end_at = emit_op(cs, 0)
        patch_here(cs, other)
        if (n.c != nothing)
            compile(cs, c, n.c, tail)
        end
        patch_here(cs, end_at)
        return nothing
    end
    if (kind == N_FUNCDEF)
        saved = jget(cs.ctx_scope, c)
        compile_funcdef(cs, c, n, tail)
        jset!(cs.ctx_scope, c, saved)
        return nothing
    end
    if (kind == N_CALL)
        compile_call(cs, c, n, tail)
        return nothing
    end
    if (kind == N_LABEL)
        slot = new_local(cs, c)
        emit_var_op(cs, OP_LABEL_BEGIN, 0, slot)
        saved = jget(cs.ctx_scope, c)
        scope_push(cs, c, SC_VAR, n.name, 0, slot)
        compile(cs, c, n.a, false)
        jset!(cs.ctx_scope, c, saved)
        emit_op(cs, OP_TRY_END)
        return nothing
    end
    if (kind == N_BREAK)
        at = lookup_var(cs, c, n.name)
        emit_var_op(cs, OP_BREAK, jget(at, 1), jget(at, 0))
        return nothing
    end
    compile_fail(cs, "unsupported construct", jstring(kind))
end

# parse the shared prelude and filter, then resolve scopes into bytecode.
function compile_program(user_src)
    text = jcat(jq_prelude, "\n", user_src)
    p = Parser(text, Any[jord(jslice(text, i, (i + 1))) for i in (0 : (length(text) - 1))], 0, length(text), false, "")
    ast = parse_pipe(p)
    skip_ws(p)
    if ((!p.failed) && (p.pos != p.n))
        parse_fail(p, "trailing input")
    end
    cs = Comp(;
        code = Int[],
        consts = Any[],
        fn_entry = Int[],
        fn_nparams = Int[],
        fn_nlocals = Int[],
        ctx_parent = Int[],
        ctx_level = Int[],
        ctx_fn = Int[],
        ctx_scope = Int[],
        ctx_nlocals = Int[],
        sc_kind = Int[],
        sc_name = String[],
        sc_arity = Int[],
        sc_index = Int[],
        sc_prev = Int[],
        failed = p.failed,
        msg = p.msg,
        synth = 0
    )
    if cs.failed
        return cs
    end
    top_fn = new_fn(cs, 0)
    top = new_ctx(cs, (-1), top_fn)
    jset!(cs.fn_entry, top_fn, length(cs.code))
    compile(cs, top, ast, true)
    emit_op(cs, OP_RET)
    jset!(cs.fn_nlocals, top_fn, jget(cs.ctx_nlocals, top))
    cs
end


# Native Julia port of ../../jq_vm.ls and ../../text/c2mir/jq_*.h.
# Recursive-descent jq parser; AST nodes use fixed fields with dynamic children.
const N_IDENTITY = 1
const N_RECURSE_DEFAULT = 2
const N_LITERAL = 3
const N_INDEX = 4
const N_ITER = 5
const N_SLICE = 6
const N_TRY = 7
const N_ARRAY = 8
const N_OBJECT = 9
const N_NEG = 10
const N_BINOP = 11
const N_AND = 12
const N_OR = 13
const N_ALT = 14
const N_ASSIGN = 15
const N_UPDATE = 16
const N_ARITH_UPDATE = 17
const N_ALT_UPDATE = 18
const N_PIPE = 19
const N_COMMA = 20
const N_BIND = 21
const N_REDUCE = 22
const N_FOREACH = 23
const N_IF = 24
const N_FUNCDEF = 25
const N_CALL = 26
const N_VAR = 27
const N_LABEL = 28
const N_BREAK = 29
Base.@kwdef mutable struct Parser
    src::String
    codes::Vector{Int}
    pos::Int
    n::Int
    failed::Bool
    msg::String
end

function mk(kind, a, b)
    Node(; kind = kind, op = 0, a = a, b = b, c = nothing, d = nothing, list = Any[], name = "", value = nothing, pvar = Any[])
end

function mk_lit(v)
    Node(; kind = N_LITERAL, op = 0, a = nothing, b = nothing, c = nothing, d = nothing, list = Any[], name = "", value = v, pvar = Any[])
end

function mk_named(kind, name, a, b)
    Node(; kind = kind, op = 0, a = a, b = b, c = nothing, d = nothing, list = Any[], name = name, value = nothing, pvar = Any[])
end

function mk_binop(op, a, b)
    Node(; kind = N_BINOP, op = op, a = a, b = b, c = nothing, d = nothing, list = Any[], name = "", value = nothing, pvar = Any[])
end

function mk_call(name, args)
    Node(; kind = N_CALL, op = 0, a = nothing, b = nothing, c = nothing, d = nothing, list = args, name = name, value = nothing, pvar = Any[])
end

function parse_fail(p::Parser, what)
    if (!p.failed)
        p.failed = true
        p.msg = jcat("jq parse error: ", what, " at offset ", jstring(p.pos))
    end
end

function is_ident_start(c)
    ((((c >= 97) && (c <= 122)) || ((c >= 65) && (c <= 90))) || (c == 95))
end

function is_ident_char(c)
    (is_ident_start(c) || ((c >= 48) && (c <= 57)))
end

function ch(p::Parser)
    ((p.pos < p.n) ? jget(p.codes, p.pos) : (-1))
end

function skip_ws(p::Parser)
    while (p.pos < p.n)
        c = jget(p.codes, p.pos)
        if ((((c == 32) || (c == 9)) || (c == 10)) || (c == 13))
            p.pos = (p.pos + 1)
        elseif (c == 35)
            while ((p.pos < p.n) && (jget(p.codes, p.pos) != 10))
                p.pos = (p.pos + 1)
            end
        else
            break
        end
    end
end

function peek(p::Parser, tok)
    skip_ws(p)
    n = length(tok)
    (((p.pos + n) <= p.n) && (jslice(p.src, p.pos, (p.pos + n)) == tok))
end

function accept(p::Parser, tok)
    if peek(p, tok)
        p.pos = (p.pos + length(tok))
        return true
    end
    false
end

function expect(p::Parser, tok)
    if (!accept(p, tok))
        parse_fail(p, jcat("expected '", tok, "'"))
    end
end

function read_ident(p::Parser)
    skip_ws(p)
    start = p.pos
    if ((p.pos >= p.n) || (!is_ident_start(jget(p.codes, p.pos))))
        return ""
    end
    while ((p.pos < p.n) && is_ident_char(jget(p.codes, p.pos)))
        p.pos = (p.pos + 1)
    end
    jor(()->jslice(p.src, start, p.pos), ()->"")
end

function peek_keyword(p::Parser, kw)
    skip_ws(p)
    n = length(kw)
    ((((p.pos + n) <= p.n) && (jslice(p.src, p.pos, (p.pos + n)) == kw)) && (((p.pos + n) == p.n) || (!is_ident_char(jget(p.codes, (p.pos + n))))))
end

function accept_keyword(p::Parser, kw)
    if (!peek_keyword(p, kw))
        return false
    end
    p.pos = (p.pos + length(kw))
    true
end

function read_var(p::Parser)
    if (!accept(p, "\$"))
        parse_fail(p, "expected \$variable")
        return "?"
    end
    name = read_ident(p)
    if (name == "")
        parse_fail(p, "expected variable name")
    end
    name
end

function hex_value(c)
    (((c >= 48) && (c <= 57)) ? (c - 48) : (((c >= 97) && (c <= 102)) ? (c - 87) : (((c >= 65) && (c <= 70)) ? (c - 55) : 0)))
end

function codes_text(codes)
    join(Any[jchr(c) for c in codes], "")
end

function parse_string(p::Parser)
    codes = Int[]
    result = nothing
    p.pos = (p.pos + 1)
    while true
        if (p.pos >= p.n)
            parse_fail(p, "unterminated string")
            break
        end
        c = jget(p.codes, p.pos)
        p.pos = (p.pos + 1)
        if (c == 34)
            break
        end
        if (c != 92)
            push!(codes, c)
            continue
        end
        e = ch(p)
        p.pos = (p.pos + 1)
        if (e == 40)
            piece = mk_lit(codes_text(codes))
            codes = Any[]
            expr = parse_pipe(p)
            expect(p, ")")
            text = mk(N_PIPE, expr, mk_call("tostring", Any[]))
            joined = mk_binop(OPB_ADD, piece, text)
            result = ((result == nothing) ? joined : mk_binop(OPB_ADD, result, joined))
            continue
        end
        if (e == 117)
            cp = 0
            k = 0
            while ((k < 4) && (p.pos < p.n))
                cp = ((cp * 16) + hex_value(jget(p.codes, p.pos)))
                p.pos = (p.pos + 1)
                k = (k + 1)
            end
            push!(codes, cp)
            continue
        end
        push!(codes, ((e == 110) ? 10 : ((e == 116) ? 9 : ((e == 114) ? 13 : ((e == 98) ? 8 : ((e == 102) ? 12 : e))))))
    end
    tail = mk_lit(codes_text(codes))
    ((result == nothing) ? tail : mk_binop(OPB_ADD, result, tail))
end

function parse_number(p::Parser)
    start = p.pos
    while (p.pos < p.n)
        c = jget(p.codes, p.pos)
        if (((c >= 48) && (c <= 57)) || (c == 46))
            p.pos = (p.pos + 1)
        elseif (((c == 101) || (c == 69)) && ((p.pos + 1) < p.n))
            p.pos = (p.pos + 1)
            if ((jget(p.codes, p.pos) == 43) || (jget(p.codes, p.pos) == 45))
                p.pos = (p.pos + 1)
            end
        else
            break
        end
    end
    text = jor(()->jslice(p.src, start, p.pos), ()->"0")
    mk_lit(jrescue(()->jparse(text, Symbol("json")), ()->0))
end

function parse_object_value(p::Parser)
    if accept(p, "-")
        return mk(N_NEG, parse_object_value(p), nothing)
    end
    v = parse_postfix(p, false)
    if ((!peek(p, "|=")) && accept(p, "|"))
        return mk(N_PIPE, v, parse_object_value(p))
    end
    v
end

function parse_object(p::Parser)
    pairs = Any[]
    if accept(p, "}")
        return with_node(mk(N_OBJECT, nothing, nothing); list = pairs)
    end
    while true
        skip_ws(p)
        key = nothing
        val = nothing
        if peek(p, "\$")
            name = read_var(p)
            key = mk_lit(name)
            val = mk_named(N_VAR, name, nothing, nothing)
        else
            if peek(p, "\"")
                key = parse_string(p)
            elseif accept(p, "(")
                key = parse_pipe(p)
                expect(p, ")")
            else
                id = read_ident(p)
                if (id == "")
                    parse_fail(p, "bad object key")
                    return mk(N_OBJECT, nothing, nothing)
                end
                key = mk_lit(id)
            end
            if accept(p, ":")
                val = parse_object_value(p)
            else
                val = mk(N_INDEX, mk(N_IDENTITY, nothing, nothing), key)
            end
        end
        push!(pairs, key)
        push!(pairs, val)
        if (!accept(p, ","))
            expect(p, "}")
            break
        end
    end
    with_node(mk(N_OBJECT, nothing, nothing); list = pairs)
end

function parse_suffixes(p::Parser, start, allow_as)
    term = start
    while true
        skip_ws(p)
        c = ch(p)
        next = (((p.pos + 1) < p.n) ? jget(p.codes, (p.pos + 1)) : (-1))
        if ((c == 46) && ((is_ident_start(next) || (next == 34)) || (next == 91)))
            p.pos = (p.pos + 1)
            if (next == 34)
                term = mk(N_INDEX, term, parse_string(p))
            elseif (next != 91)
                term = mk(N_INDEX, term, mk_lit(read_ident(p)))
            end
        elseif accept(p, "[")
            if accept(p, "]")
                term = mk(N_ITER, term, nothing)
            elseif accept(p, ":")
                upper = parse_pipe(p)
                expect(p, "]")
                term = with_node(mk(N_SLICE, term, nothing); c = upper)
            else
                idx = parse_pipe(p)
                if accept(p, ":")
                    upper = (peek(p, "]") ? nothing : parse_pipe(p))
                    expect(p, "]")
                    term = with_node(mk(N_SLICE, term, idx); c = upper)
                else
                    expect(p, "]")
                    term = mk(N_INDEX, term, idx)
                end
            end
        elseif ((!peek(p, "?//")) && accept(p, "?"))
            term = mk(N_TRY, term, nothing)
        else
            break
        end
    end
    if (allow_as && peek_keyword(p, "as"))
        accept_keyword(p, "as")
        name = read_var(p)
        expect(p, "|")
        return mk_named(N_BIND, name, term, parse_pipe(p))
    end
    term
end

function parse_primary(p::Parser)
    skip_ws(p)
    if (p.pos >= p.n)
        parse_fail(p, "unexpected end")
        return mk(N_IDENTITY, nothing, nothing)
    end
    c = jget(p.codes, p.pos)
    if (c == 46)
        if accept(p, "..")
            return mk(N_RECURSE_DEFAULT, nothing, nothing)
        end
        p.pos = (p.pos + 1)
        if ((p.pos < p.n) && is_ident_start(jget(p.codes, p.pos)))
            return mk(N_INDEX, mk(N_IDENTITY, nothing, nothing), mk_lit(read_ident(p)))
        end
        if ((p.pos < p.n) && (jget(p.codes, p.pos) == 34))
            return mk(N_INDEX, mk(N_IDENTITY, nothing, nothing), parse_string(p))
        end
        return mk(N_IDENTITY, nothing, nothing)
    end
    if ((c >= 48) && (c <= 57))
        return parse_number(p)
    end
    if (c == 34)
        return parse_string(p)
    end
    if accept(p, "(")
        e = parse_pipe(p)
        expect(p, ")")
        return e
    end
    if accept(p, "[")
        if accept(p, "]")
            return mk(N_ARRAY, nothing, nothing)
        end
        body = parse_pipe(p)
        expect(p, "]")
        return mk(N_ARRAY, body, nothing)
    end
    if accept(p, "{")
        return parse_object(p)
    end
    if (c == 36)
        return mk_named(N_VAR, read_var(p), nothing, nothing)
    end
    if accept_keyword(p, "if")
        return parse_if(p)
    end
    if accept_keyword(p, "try")
        body = parse_postfix(p, false)
        handler = (accept_keyword(p, "catch") ? parse_postfix(p, false) : nothing)
        return mk(N_TRY, body, handler)
    end
    if (peek_keyword(p, "reduce") || peek_keyword(p, "foreach"))
        is_foreach = accept_keyword(p, "foreach")
        if (!is_foreach)
            accept_keyword(p, "reduce")
        end
        source = parse_postfix(p, false)
        if (!accept_keyword(p, "as"))
            parse_fail(p, "expected as")
        end
        name = read_var(p)
        expect(p, "(")
        init = parse_pipe(p)
        expect(p, ";")
        update = parse_pipe(p)
        extract = ((is_foreach && accept(p, ";")) ? parse_pipe(p) : nothing)
        expect(p, ")")
        return Node(; kind = (is_foreach ? N_FOREACH : N_REDUCE), op = 0, a = source, b = init, c = update, d = extract, list = Any[], name = name, value = nothing, pvar = Any[])
    end
    if accept_keyword(p, "label")
        name = read_var(p)
        expect(p, "|")
        return mk_named(N_LABEL, name, parse_pipe(p), nothing)
    end
    if accept_keyword(p, "break")
        return mk_named(N_BREAK, read_var(p), nothing, nothing)
    end
    if is_ident_start(c)
        name = read_ident(p)
        args = Any[]
        if accept(p, "(")
            while true
                push!(args, parse_pipe(p))
                if (!accept(p, ";"))
                    expect(p, ")")
                    break
                end
            end
        end
        return mk_call(name, args)
    end
    parse_fail(p, "unexpected character")
    p.pos = (p.pos + 1)
    mk(N_IDENTITY, nothing, nothing)
end

function parse_if(p::Parser)
    cond = parse_pipe(p)
    if (!accept_keyword(p, "then"))
        parse_fail(p, "expected then")
    end
    then_branch = parse_pipe(p)
    else_branch = nothing
    if accept_keyword(p, "elif")
        else_branch = parse_if(p)
        return with_node(mk(N_IF, cond, then_branch); c = else_branch)
    end
    if accept_keyword(p, "else")
        else_branch = parse_pipe(p)
    end
    if (!accept_keyword(p, "end"))
        parse_fail(p, "expected end")
    end
    with_node(mk(N_IF, cond, then_branch); c = else_branch)
end

function parse_postfix(p::Parser, allow_as)
    parse_suffixes(p, parse_primary(p), allow_as)
end

function parse_unary(p::Parser)
    skip_ws(p)
    if ((ch(p) == 45) && (!peek(p, "-=")))
        p.pos = (p.pos + 1)
        return mk(N_NEG, parse_postfix(p, true), nothing)
    end
    parse_postfix(p, true)
end

function parse_mul(p::Parser)
    l = parse_unary(p)
    while true
        if ((peek(p, "*=") || peek(p, "/=")) || peek(p, "%="))
            break
        end
        if accept(p, "*")
            l = mk_binop(OPB_MUL, l, parse_unary(p))
        elseif ((!peek(p, "//")) && accept(p, "/"))
            l = mk_binop(OPB_DIV, l, parse_unary(p))
        elseif accept(p, "%")
            l = mk_binop(OPB_MOD, l, parse_unary(p))
        else
            break
        end
    end
    l
end

function parse_add(p::Parser)
    l = parse_mul(p)
    while true
        if (peek(p, "+=") || peek(p, "-="))
            break
        end
        if accept(p, "+")
            l = mk_binop(OPB_ADD, l, parse_mul(p))
        elseif accept(p, "-")
            l = mk_binop(OPB_SUB, l, parse_mul(p))
        else
            break
        end
    end
    l
end

function parse_cmp(p::Parser)
    l = parse_add(p)
    if accept(p, "==")
        return mk_binop(OPB_EQ, l, parse_add(p))
    end
    if accept(p, "!=")
        return mk_binop(OPB_NE, l, parse_add(p))
    end
    if accept(p, "<=")
        return mk_binop(OPB_LE, l, parse_add(p))
    end
    if accept(p, ">=")
        return mk_binop(OPB_GE, l, parse_add(p))
    end
    if accept(p, "<")
        return mk_binop(OPB_LT, l, parse_add(p))
    end
    if accept(p, ">")
        return mk_binop(OPB_GT, l, parse_add(p))
    end
    l
end

function parse_and(p::Parser)
    l = parse_cmp(p)
    while accept_keyword(p, "and")
        l = mk(N_AND, l, parse_cmp(p))
    end
    l
end

function parse_or(p::Parser)
    l = parse_and(p)
    while accept_keyword(p, "or")
        l = mk(N_OR, l, parse_and(p))
    end
    l
end

function parse_assign(p::Parser)
    l = parse_or(p)
    kind = 0
    op = 0
    if accept(p, "|=")
        kind = N_UPDATE
    elseif accept(p, "+=")
        kind = N_ARITH_UPDATE
        op = OPB_ADD
    elseif accept(p, "-=")
        kind = N_ARITH_UPDATE
        op = OPB_SUB
    elseif accept(p, "*=")
        kind = N_ARITH_UPDATE
        op = OPB_MUL
    elseif accept(p, "/=")
        kind = N_ARITH_UPDATE
        op = OPB_DIV
    elseif accept(p, "%=")
        kind = N_ARITH_UPDATE
        op = OPB_MOD
    elseif accept(p, "//=")
        kind = N_ALT_UPDATE
    elseif ((!peek(p, "==")) && accept(p, "="))
        kind = N_ASSIGN
    end
    if (kind == 0)
        return l
    end
    with_node(mk(kind, l, parse_alt(p)); op = op)
end

function parse_alt(p::Parser)
    l = parse_assign(p)
    if ((!peek(p, "//=")) && accept(p, "//"))
        return mk(N_ALT, l, parse_alt(p))
    end
    l
end

function parse_comma(p::Parser)
    l = parse_alt(p)
    while accept(p, ",")
        l = mk(N_COMMA, l, parse_alt(p))
    end
    l
end

function parse_funcdef(p::Parser)
    name = read_ident(p)
    if (name == "")
        parse_fail(p, "expected function name")
    end
    params = Any[]
    pvar = Bool[]
    if accept(p, "(")
        while true
            is_var = peek(p, "\$")
            pname = (is_var ? read_var(p) : read_ident(p))
            if (pname == "")
                parse_fail(p, "expected parameter")
                break
            end
            push!(params, mk_named(N_VAR, pname, nothing, nothing))
            push!(pvar, is_var)
            if (!accept(p, ";"))
                expect(p, ")")
                break
            end
        end
    end
    expect(p, ":")
    body = parse_pipe(p)
    expect(p, ";")
    rest = parse_pipe(p)
    Node(; kind = N_FUNCDEF, op = 0, a = body, b = rest, c = nothing, d = nothing, list = params, name = name, value = nothing, pvar = pvar)
end

function parse_pipe(p::Parser)
    if accept_keyword(p, "def")
        return parse_funcdef(p)
    end
    l = parse_comma(p)
    if ((!peek(p, "|=")) && accept(p, "|"))
        return mk(N_PIPE, l, parse_pipe(p))
    end
    l
end


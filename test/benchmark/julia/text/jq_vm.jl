# Native Julia port of ../../jq_vm.ls and ../../text/c2mir/jq_*.h.
# Backtracking jq VM, with forkable stacks and a compacted frame arena.
module JqVM
include("jq_values.jl")
include("jq_native.jl")
include("jq_parse.jl")
include("jq_compile.jl")

const FK_FORK = 1
const FK_TRY = 2
const FK_TRY_EXIT = 3
const FK_EACH = 4
const FK_RANGE = 5
Base.@kwdef mutable struct Vm
    stv::Vector{Any}
    stp::Vector{Int}
    top::Int
    lim::Int
    high::Int
    fk_kind::Vector{Int}
    fk_pc::Vector{Int}
    fk_top::Vector{Int}
    fk_lim::Vector{Int}
    fk_fp::Vector{Int}
    fk_path::Vector{Any}
    fk_vat::Vector{Any}
    fk_subexp::Vector{Int}
    fk_aux::Vector{Any}
    fk_aux2::Vector{Any}
    fk_aux3::Vector{Any}
    fk_auxi::Vector{Int}
    fk_active::Vector{Bool}
    fk_label::Vector{Int}
    nforks::Int
    fr_env::Vector{Int}
    fr_caller::Vector{Int}
    fr_retpc::Vector{Int}
    fr_fn::Vector{Int}
    fr_base::Vector{Int}
    fr_locals::Vector{Int}
    fr_params::Vector{Int}
    nframes::Int
    locals::Vector{Any}
    pfn::Vector{Int}
    penv::Vector{Int}
    fp::Int
    path::Union{Nothing,Vector{Any}}
    vat::Union{Nothing,Any}
    subexp::Int
    labels::Int
    outputs::Vector{Any}
    err::Union{Nothing,Any}
    frame_limit::Int
end

# push above all protected cells so backtracking retains its stack snapshot.
function st_push(vm::Vm, v)
    idx = (((vm.top > vm.lim) ? vm.top : vm.lim) + 1)
    while (length(vm.stv) <= idx)
        push!(vm.stv, nothing)
        push!(vm.stp, (-1))
    end
    jset!(vm.stv, idx, v)
    jset!(vm.stp, idx, vm.top)
    vm.top = idx
    if (idx > vm.high)
        vm.high = idx
    end
end

function st_pop(vm::Vm)
    v = jget(vm.stv, vm.top)
    vm.top = jget(vm.stp, vm.top)
    v
end

# save stack, frame and path state; protect live cells from subsequent pushes.
function fork_push(vm::Vm, kind, pc)
    i = vm.nforks
    while (length(vm.fk_kind) <= i)
        push!(vm.fk_kind, 0)
        push!(vm.fk_pc, 0)
        push!(vm.fk_top, 0)
        push!(vm.fk_lim, 0)
        push!(vm.fk_fp, 0)
        push!(vm.fk_path, nothing)
        push!(vm.fk_vat, nothing)
        push!(vm.fk_subexp, 0)
        push!(vm.fk_aux, nothing)
        push!(vm.fk_aux2, nothing)
        push!(vm.fk_aux3, nothing)
        push!(vm.fk_auxi, 0)
        push!(vm.fk_active, true)
        push!(vm.fk_label, 0)
    end
    jset!(vm.fk_kind, i, kind)
    jset!(vm.fk_pc, i, pc)
    jset!(vm.fk_top, i, vm.top)
    jset!(vm.fk_lim, i, vm.lim)
    jset!(vm.fk_fp, i, vm.fp)
    jset!(vm.fk_path, i, vm.path)
    jset!(vm.fk_vat, i, vm.vat)
    jset!(vm.fk_subexp, i, vm.subexp)
    jset!(vm.fk_aux, i, nothing)
    jset!(vm.fk_aux2, i, nothing)
    jset!(vm.fk_aux3, i, nothing)
    jset!(vm.fk_auxi, i, 0)
    jset!(vm.fk_active, i, true)
    jset!(vm.fk_label, i, 0)
    vm.nforks = (i + 1)
    if (vm.top > vm.lim)
        vm.lim = vm.top
    end
    i
end

function fork_restore(vm::Vm, i)
    vm.top = jget(vm.fk_top, i)
    vm.lim = jget(vm.fk_lim, i)
    vm.high = ((vm.top > vm.lim) ? vm.top : vm.lim)
    vm.fp = jget(vm.fk_fp, i)
    vm.path = jget(vm.fk_path, i)
    vm.vat = jget(vm.fk_vat, i)
    vm.subexp = jget(vm.fk_subexp, i)
end

# frame ids index parallel arenas for locals and closure environments.
function frame_new(vm::Vm, cs::Comp, fn_index, env, caller, retpc)
    id = vm.nframes
    base = length(vm.locals)
    i = 0
    while (i < jget(cs.fn_nlocals, fn_index))
        push!(vm.locals, nothing)
        i = (i + 1)
    end
    pbase = length(vm.pfn)
    j = 0
    while (j < jget(cs.fn_nparams, fn_index))
        push!(vm.pfn, 0)
        push!(vm.penv, (-1))
        j = (j + 1)
    end
    while (length(vm.fr_env) <= id)
        push!(vm.fr_env, (-1))
        push!(vm.fr_caller, (-1))
        push!(vm.fr_retpc, 0)
        push!(vm.fr_fn, 0)
        push!(vm.fr_base, 0)
        push!(vm.fr_locals, 0)
        push!(vm.fr_params, 0)
    end
    jset!(vm.fr_env, id, env)
    jset!(vm.fr_caller, id, caller)
    jset!(vm.fr_retpc, id, retpc)
    jset!(vm.fr_fn, id, fn_index)
    jset!(vm.fr_base, id, vm.nforks)
    jset!(vm.fr_locals, id, base)
    jset!(vm.fr_params, id, pbase)
    vm.nframes = (id + 1)
    id
end

function frame_hop(vm::Vm, f, hops)
    x = f
    h = hops
    while (h > 0)
        x = jget(vm.fr_env, x)
        h = (h - 1)
    end
    x
end

# trace current and fork frames, then compact arenas and remap every live edge.
function vm_compact_frames(vm::Vm)
    n = vm.nframes
    marked = Bool[]
    k = 0
    while (k < n)
        push!(marked, false)
        k = (k + 1)
    end
    work = Int[vm.fp]
    f = 0
    while (f < vm.nforks)
        push!(work, jget(vm.fk_fp, f))
        f = (f + 1)
    end
    head = 0
    while (head < length(work))
        id = jget(work, head)
        head = (head + 1)
        if (((id >= 0) && (id < n)) && (!jget(marked, id)))
            jset!(marked, id, true)
            push!(work, jget(vm.fr_env, id))
            push!(work, jget(vm.fr_caller, id))
            pbase = jget(vm.fr_params, id)
            pend = (((id + 1) < n) ? jget(vm.fr_params, (id + 1)) : length(vm.pfn))
            q = pbase
            while (q < pend)
                push!(work, jget(vm.penv, q))
                q = (q + 1)
            end
        end
    end
    remap = Int[]
    live = 0
    r = 0
    while (r < n)
        push!(remap, (jget(marked, r) ? live : (-1)))
        if jget(marked, r)
            live = (live + 1)
        end
        r = (r + 1)
    end
    env2 = Int[]
    caller2 = Int[]
    retpc2 = Int[]
    fn2 = Int[]
    base2 = Int[]
    locals_at2 = Int[]
    params_at2 = Int[]
    locals2 = Any[]
    pfn2 = Int[]
    penv2 = Int[]
    id2 = 0
    while (id2 < n)
        if jget(marked, id2)
            push!(env2, ((jget(vm.fr_env, id2) >= 0) ? jget(remap, jget(vm.fr_env, id2)) : (-1)))
            push!(caller2, ((jget(vm.fr_caller, id2) >= 0) ? jget(remap, jget(vm.fr_caller, id2)) : (-1)))
            push!(retpc2, jget(vm.fr_retpc, id2))
            push!(fn2, jget(vm.fr_fn, id2))
            push!(base2, jget(vm.fr_base, id2))
            lbase = jget(vm.fr_locals, id2)
            lend = (((id2 + 1) < n) ? jget(vm.fr_locals, (id2 + 1)) : length(vm.locals))
            push!(locals_at2, length(locals2))
            a = lbase
            while (a < lend)
                push!(locals2, jget(vm.locals, a))
                a = (a + 1)
            end
            pbase = jget(vm.fr_params, id2)
            pend = (((id2 + 1) < n) ? jget(vm.fr_params, (id2 + 1)) : length(vm.pfn))
            push!(params_at2, length(pfn2))
            b = pbase
            while (b < pend)
                push!(pfn2, jget(vm.pfn, b))
                push!(penv2, ((jget(vm.penv, b) >= 0) ? jget(remap, jget(vm.penv, b)) : (-1)))
                b = (b + 1)
            end
        end
        id2 = (id2 + 1)
    end
    vm.fr_env = env2
    vm.fr_caller = caller2
    vm.fr_retpc = retpc2
    vm.fr_fn = fn2
    vm.fr_base = base2
    vm.fr_locals = locals_at2
    vm.fr_params = params_at2
    vm.locals = locals2
    vm.pfn = pfn2
    vm.penv = penv2
    vm.nframes = live
    vm.fp = ((vm.fp >= 0) ? jget(remap, vm.fp) : (-1))
    g = 0
    while (g < vm.nforks)
        if (jget(vm.fk_fp, g) >= 0)
            jset!(vm.fk_fp, g, jget(remap, jget(vm.fk_fp, g)))
        end
        g = (g + 1)
    end
    vm.frame_limit = (((live * 2) > 50000) ? (live * 2) : 50000)
end

function is_label(v)
    ((v isa Symbol) && startswith(jstring(v), "__jq_label_"))
end

function label_value(id)
    Symbol(jcat("__jq_label_", jstring(id)))
end

# path tracking retains both the value and its jq type.
function path_check(vm::Vm, t)
    if ((vm.path == nothing) || (vm.subexp != 0))
        return true
    end
    ((t == vm.vat) && (jtype(t) == jtype(vm.vat)))
end

function each_produce(vm::Vm, c, keys, i)
    key = nothing
    val = nothing
    if (c isa AbstractVector)
        key = i
        val = jget(c, i)
    else
        key = jget(keys, i)
        val = jget(c, jget(keys, i))
    end
    if ((vm.path != nothing) && (vm.subexp == 0))
        vm.path = jcat(vm.path, Any[key])
        vm.vat = val
    end
    st_push(vm, val)
end

function range_more(cur, upto, step)
    ((step > 0) ? (cur < upto) : ((step < 0) ? (cur > upto) : false))
end

# dispatch modes: execute (0), backtrack (1), raise a jq error (2).
function vm_run(cs::Comp, input)
    code = cs.code
    vm = Vm(;
        stv = Any[],
        stp = Int[],
        top = (-1),
        lim = (-1),
        high = (-1),
        fk_kind = Int[],
        fk_pc = Int[],
        fk_top = Int[],
        fk_lim = Int[],
        fk_fp = Int[],
        fk_path = Any[],
        fk_vat = Any[],
        fk_subexp = Int[],
        fk_aux = Any[],
        fk_aux2 = Any[],
        fk_aux3 = Any[],
        fk_auxi = Int[],
        fk_active = Bool[],
        fk_label = Int[],
        nforks = 0,
        fr_env = Int[],
        fr_caller = Int[],
        fr_retpc = Int[],
        fr_fn = Int[],
        fr_base = Int[],
        fr_locals = Int[],
        fr_params = Int[],
        nframes = 0,
        locals = Any[],
        pfn = Int[],
        penv = Int[],
        fp = (-1),
        path = nothing,
        vat = nothing,
        subexp = 0,
        labels = 0,
        outputs = Any[],
        err = nothing,
        frame_limit = 50000
    )
    vm.fp = frame_new(vm, cs, 0, (-1), (-1), (-1))
    st_push(vm, input)
    pc = jget(cs.fn_entry, 0)
    mode = 0
    while true
        if (mode == 1)
            if (vm.nforks == 0)
                return vm
            end
            i = (vm.nforks - 1)
            kind = jget(vm.fk_kind, i)
            if (kind == FK_FORK)
                fork_restore(vm, i)
                pc = jget(vm.fk_pc, i)
                vm.nforks = i
                mode = 0
                continue
            end
            if (kind == FK_EACH)
                fork_restore(vm, i)
                c = jget(vm.fk_aux, i)
                keys = jget(vm.fk_aux2, i)
                idx = jget(vm.fk_auxi, i)
                jset!(vm.fk_auxi, i, (idx + 1))
                pc = jget(vm.fk_pc, i)
                if ((idx + 1) >= length(c))
                    vm.nforks = i
                end
                if (((idx + 1) < length(c)) && (vm.top > vm.lim))
                    vm.lim = vm.top
                end
                each_produce(vm, c, keys, idx)
                mode = 0
                continue
            end
            if (kind == FK_RANGE)
                fork_restore(vm, i)
                cur = jget(vm.fk_aux, i)
                step = jget(vm.fk_aux3, i)
                next = (cur + step)
                pc = jget(vm.fk_pc, i)
                more = range_more(next, jget(vm.fk_aux2, i), step)
                if more
                    jset!(vm.fk_aux, i, next)
                end
                if (more && (vm.top > vm.lim))
                    vm.lim = vm.top
                end
                if (!more)
                    vm.nforks = i
                end
                st_push(vm, cur)
                mode = 0
                continue
            end
            if (kind == FK_TRY_EXIT)
                jset!(vm.fk_active, jget(vm.fk_auxi, i), true)
            end
            vm.nforks = i
            continue
        end
        if (mode == 2)
            if (vm.nforks == 0)
                error("uncaught jq error: " * json_text(vm.err))
            end
            i = (vm.nforks - 1)
            vm.nforks = i
            if ((jget(vm.fk_kind, i) != FK_TRY) || (!jget(vm.fk_active, i)))
                continue
            end
            if (jget(vm.fk_label, i) != 0)
                if (is_label(vm.err) && (vm.err == label_value(jget(vm.fk_label, i))))
                    fork_restore(vm, i)
                    mode = 1
                end
                continue
            end
            if is_label(vm.err)
                continue
            end
            fork_restore(vm, i)
            st_push(vm, vm.err)
            pc = jget(vm.fk_pc, i)
            mode = 0
            continue
        end
        op = jget(code, pc)
        pc = (pc + 1)
        if (op == OP_DUP)
            st_push(vm, jget(vm.stv, vm.top))
            continue
        end
        if (op == OP_LOADK)
            st_pop(vm)
            st_push(vm, jget(cs.consts, jget(code, pc)))
            pc = (pc + 1)
            continue
        end
        if (op == OP_SUBEXP_BEGIN)
            st_push(vm, jget(vm.stv, vm.top))
            vm.subexp = (vm.subexp + 1)
            continue
        end
        if (op == OP_SUBEXP_END)
            a = st_pop(vm)
            b = st_pop(vm)
            st_push(vm, a)
            st_push(vm, b)
            vm.subexp = (vm.subexp - 1)
            continue
        end
        if ((op == OP_INDEX) || (op == OP_INDEXK))
            t = st_pop(vm)
            k = nothing
            if (op == OP_INDEXK)
                k = jget(cs.consts, jget(code, pc))
            end
            if (op == OP_INDEXK)
                pc = (pc + 1)
            end
            if (op == OP_INDEX)
                k = st_pop(vm)
            end
            if (!path_check(vm, t))
                vm.err = jcat("Invalid path expression with result ", describe(t))
                mode = 2
                continue
            end
            r = index_value(t, k)
            if (!r.ok)
                vm.err = r.value
                mode = 2
                continue
            end
            if ((vm.path != nothing) && (vm.subexp == 0))
                vm.path = jcat(vm.path, Any[k])
                vm.vat = r.value
            end
            st_push(vm, r.value)
            continue
        end
        if (op == OP_JUMP)
            pc = jget(code, pc)
            continue
        end
        if (op == OP_JUMP_F)
            cnd = st_pop(vm)
            pc = (truthy(cnd) ? (pc + 1) : jget(code, pc))
            continue
        end
        if (op == OP_JUMP_F_SUB)
            inp = st_pop(vm)
            cnd = st_pop(vm)
            st_push(vm, inp)
            pc = (truthy(cnd) ? (pc + 1) : jget(code, pc))
            continue
        end
        if (op == OP_JUMP_F_KEEP)
            pc = (truthy(jget(vm.stv, vm.top)) ? (pc + 1) : jget(code, pc))
            continue
        end
        if (op == OP_FORK)
            fork_push(vm, FK_FORK, jget(code, pc))
            pc = (pc + 1)
            continue
        end
        if (op == OP_BACKTRACK)
            mode = 1
            continue
        end
        if ((((op >= OP_STOREV) && (op <= OP_APPEND)) || (op == OP_LABEL_BEGIN)) || (op == OP_BREAK))
            f = frame_hop(vm, vm.fp, jget(code, pc))
            slot = (jget(vm.fr_locals, f) + jget(code, (pc + 1)))
            pc = (pc + 2)
            if (op == OP_STOREV)
                jset!(vm.locals, slot, st_pop(vm))
                continue
            end
            if (op == OP_STOREV_UNDER)
                inp = st_pop(vm)
                jset!(vm.locals, slot, st_pop(vm))
                st_push(vm, inp)
                continue
            end
            if ((op == OP_LOADV) || (op == OP_LOADVN))
                st_pop(vm)
                st_push(vm, jget(vm.locals, slot))
                if (op == OP_LOADVN)
                    jset!(vm.locals, slot, nothing)
                end
                continue
            end
            if (op == OP_APPEND)
                push!(jget(vm.locals, slot), st_pop(vm))
                continue
            end
            if (op == OP_LABEL_BEGIN)
                vm.labels = (vm.labels + 1)
                jset!(vm.locals, slot, label_value(vm.labels))
                at = fork_push(vm, FK_TRY, (-1))
                jset!(vm.fk_label, at, vm.labels)
                continue
            end
            vm.err = jget(vm.locals, slot)
            mode = 2
            continue
        end
        if (op == OP_EACH)
            c = st_pop(vm)
            ct = jtype(c)
            if ((ct != 5) && (ct != 6))
                vm.err = jcat("Cannot iterate over ", describe(c))
                mode = 2
                continue
            end
            if (!path_check(vm, c))
                vm.err = jcat("Invalid path expression with result ", describe(c))
                mode = 2
                continue
            end
            if (length(c) == 0)
                mode = 1
                continue
            end
            keys = ((ct == 6) ? Any[jstring(k) for k in Base.keys(c)] : nothing)
            if (length(c) > 1)
                at = fork_push(vm, FK_EACH, pc)
                jset!(vm.fk_aux, at, c)
                jset!(vm.fk_aux2, at, keys)
                jset!(vm.fk_auxi, at, 1)
            end
            each_produce(vm, c, keys, 0)
            continue
        end
        if ((op == OP_CALL_JQ) || (op == OP_TAIL_CALL_JQ))
            if (vm.nframes >= vm.frame_limit)
                vm_compact_frames(vm)
            end
            fn_index = jget(code, pc)
            hops = jget(code, (pc + 1))
            nargs = jget(code, (pc + 2))
            caller = vm.fp
            retpc = ((pc + 3) + nargs)
            if (((op == OP_TAIL_CALL_JQ) && (vm.nforks == jget(vm.fr_base, vm.fp))) && (jget(vm.fr_caller, vm.fp) >= 0))
                caller = jget(vm.fr_caller, vm.fp)
                retpc = jget(vm.fr_retpc, vm.fp)
            end
            env = frame_hop(vm, vm.fp, hops)
            site = vm.fp
            f = frame_new(vm, cs, fn_index, env, caller, retpc)
            i = 0
            while (i < nargs)
                jset!(vm.pfn, (jget(vm.fr_params, f) + i), jget(code, ((pc + 3) + i)))
                jset!(vm.penv, (jget(vm.fr_params, f) + i), site)
                i = (i + 1)
            end
            vm.fp = f
            pc = jget(cs.fn_entry, fn_index)
            continue
        end
        if ((op == OP_CALL_PARAM) || (op == OP_TAIL_CALL_PARAM))
            if (vm.nframes >= vm.frame_limit)
                vm_compact_frames(vm)
            end
            owner = frame_hop(vm, vm.fp, jget(code, pc))
            slot = (jget(vm.fr_params, owner) + jget(code, (pc + 1)))
            cl_fn = jget(vm.pfn, slot)
            cl_env = jget(vm.penv, slot)
            caller = vm.fp
            retpc = (pc + 2)
            if (((op == OP_TAIL_CALL_PARAM) && (vm.nforks == jget(vm.fr_base, vm.fp))) && (jget(vm.fr_caller, vm.fp) >= 0))
                caller = jget(vm.fr_caller, vm.fp)
                retpc = jget(vm.fr_retpc, vm.fp)
            end
            vm.fp = frame_new(vm, cs, cl_fn, cl_env, caller, retpc)
            pc = jget(cs.fn_entry, cl_fn)
            continue
        end
        if (op == OP_RET)
            if (jget(vm.fr_caller, vm.fp) < 0)
                push!(vm.outputs, st_pop(vm))
                mode = 1
                continue
            end
            pc = jget(vm.fr_retpc, vm.fp)
            vm.fp = jget(vm.fr_caller, vm.fp)
            continue
        end
        if (op == OP_CALL_NATIVE)
            id = jget(code, pc)
            nargs = jget(code, (pc + 1))
            pc = (pc + 2)
            inp = st_pop(vm)
            args = Any[]
            i = 0
            while (i < nargs)
                push!(args, nothing)
                i = (i + 1)
            end
            j = (nargs - 1)
            while (j >= 0)
                jset!(args, j, st_pop(vm))
                j = (j - 1)
            end
            if ((id == NAT_GETPATH) && (!path_check(vm, inp)))
                vm.err = jcat("Invalid path expression with result ", describe(inp))
                mode = 2
                continue
            end
            r = call_native(id, inp, args)
            if (!r.ok)
                vm.err = r.value
                mode = 2
                continue
            end
            if (((id == NAT_GETPATH) && (vm.path != nothing)) && (vm.subexp == 0))
                vm.path = jcat(vm.path, jget(args, 0))
                vm.vat = r.value
            end
            st_push(vm, r.value)
            continue
        end
        if (op == OP_BINOP)
            bop = jget(code, pc)
            pc = (pc + 1)
            st_pop(vm)
            l = st_pop(vm)
            r0 = st_pop(vm)
            r = binop_value(bop, l, r0)
            if (!r.ok)
                vm.err = r.value
                mode = 2
                continue
            end
            st_push(vm, r.value)
            continue
        end
        if (op == OP_TRY_BEGIN)
            fork_push(vm, FK_TRY, jget(code, pc))
            pc = (pc + 1)
            continue
        end
        if (op == OP_TRY_END)
            i = (vm.nforks - 1)
            while ((i >= 0) && (!((jget(vm.fk_kind, i) == FK_TRY) && jget(vm.fk_active, i))))
                i = (i - 1)
            end
            if (i >= 0)
                jset!(vm.fk_active, i, false)
                at = fork_push(vm, FK_TRY_EXIT, (-1))
                jset!(vm.fk_auxi, at, i)
            end
            continue
        end
        if (op == OP_POP)
            st_pop(vm)
            continue
        end
        if (op == OP_NEWARR)
            st_pop(vm)
            st_push(vm, Any[])
            continue
        end
        if (op == OP_OBJ_START)
            inp = st_pop(vm)
            st_push(vm, Dict{String,Any}())
            st_push(vm, inp)
            continue
        end
        if (op == OP_INSERT)
            inp = st_pop(vm)
            val = st_pop(vm)
            key = st_pop(vm)
            obj = st_pop(vm)
            if (!(key isa AbstractString))
                vm.err = jcat("Object keys must be strings, not ", describe(key))
                mode = 2
                continue
            end
            st_push(vm, obj_with(obj, key, val))
            st_push(vm, inp)
            continue
        end
        if (op == OP_SLICE)
            t = st_pop(vm)
            upper = st_pop(vm)
            lower = st_pop(vm)
            if ((vm.path != nothing) && (vm.subexp == 0))
                vm.err = "jq-core does not support slice paths"
                mode = 2
                continue
            end
            r = slice_value(t, lower, upper)
            if (!r.ok)
                vm.err = r.value
                mode = 2
                continue
            end
            st_push(vm, r.value)
            continue
        end
        if (op == OP_RANGE)
            nargs = jget(code, pc)
            pc = (pc + 1)
            st_pop(vm)
            step = ((nargs == 3) ? st_pop(vm) : 1)
            upto = st_pop(vm)
            from = st_pop(vm)
            if (((jtype(from) != 3) || (jtype(upto) != 3)) || (jtype(step) != 3))
                vm.err = "Range bounds must be numeric"
                mode = 2
                continue
            end
            if (!range_more(from, upto, step))
                mode = 1
                continue
            end
            if range_more((from + step), upto, step)
                at = fork_push(vm, FK_RANGE, pc)
                jset!(vm.fk_aux, at, (from + step))
                jset!(vm.fk_aux2, at, upto)
                jset!(vm.fk_aux3, at, step)
            end
            st_push(vm, from)
            continue
        end
        if (op == OP_PATH_BEGIN)
            v = st_pop(vm)
            st_push(vm, vm.path)
            st_push(vm, vm.vat)
            st_push(vm, vm.subexp)
            vm.path = Any[]
            vm.vat = v
            vm.subexp = 0
            st_push(vm, v)
            continue
        end
        if (op == OP_PATH_END)
            r = st_pop(vm)
            if (!path_check(vm, r))
                vm.err = jcat("Invalid path expression with result ", describe(r))
                mode = 2
                continue
            end
            p = vm.path
            vm.subexp = st_pop(vm)
            vm.vat = st_pop(vm)
            vm.path = st_pop(vm)
            st_push(vm, p)
            continue
        end
        if (op == OP_NEG)
            v = st_pop(vm)
            if (jtype(v) != 3)
                vm.err = jcat(describe(v), " cannot be negated")
                mode = 2
                continue
            end
            st_push(vm, (0 - v))
            continue
        end
        if (op == OP_TOBOOL)
            st_push(vm, truthy(st_pop(vm)))
            continue
        end
        error("invalid jq opcode $(op) at $(pc - 1)")
    end
    vm
end

end # module JqVM

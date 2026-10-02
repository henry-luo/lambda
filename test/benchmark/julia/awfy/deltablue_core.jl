# port of test/benchmark/awfy/python/deltablue.py; algorithms retain their original control flow.
# This benchmark is derived from Mario Wolczko's Java and Smalltalk version of
# DeltaBlue.
#
# It is modified to use the SOM class library and Java 8 features.
# License details:
#   http://web.archive.org/web/20050825101121/
#      http://www.sunlabs.com/people/mario/java_benchmarking/index.html
abstract type A_DeltaBlue <: A_Benchmark end
mutable struct C_DeltaBlue <: A_DeltaBlue
    C_DeltaBlue(::Val{:raw}) = new()
end
function C_DeltaBlue(args...)
    self = C_DeltaBlue(Val(:raw))
    return self
end

function m_inner_benchmark_loop(self::A_DeltaBlue, inner_iterations)
    C__Planner__chain_test(inner_iterations)
    C__Planner__projection_test(inner_iterations)
    return true
end

function m_benchmark(self::A_DeltaBlue)
    throw(ErrorException("should never be reached"))
end

function m_verify_result(self::A_DeltaBlue, result)
    throw(ErrorException("should never be reached"))
end

abstract type A__Plan <: A_Vector end
mutable struct C__Plan <: A__Plan
    _storage::Union{Nothing,Vector{Any}}
    _first_idx::Int
    _last_idx::Int
    C__Plan(::Val{:raw}) = new(nothing, 0, 0)
end
function C__Plan(args...)
    self = C__Plan(Val(:raw))
    init__Plan(self, args...)
    return self
end

function init__Plan(self)
    init_Vector(self, 15)
    return nothing
end

function m_execute(self::A__Plan)
    m_for_each(self, (c)->m_execute(c))
    return nothing
end

abstract type A__Planner end
mutable struct C__Planner <: A__Planner
    _current_mark::Int
    C__Planner(::Val{:raw}) = new(0)
end
function C__Planner(args...)
    self = C__Planner(Val(:raw))
    init__Planner(self, args...)
    return self
end

function init__Planner(self)
    self._current_mark = 1
    return nothing
end

function m_incremental_add(self::A__Planner, c)
    local mark, overridden
    mark = m__new_mark(self)
    overridden = m_satisfy(c, mark, self)
    while truth0(((overridden !== nothing)))
        overridden = m_satisfy(overridden, mark, self)
    end
    return nothing
end

function m_incremental_remove(self::A__Planner, c)
    local out, unsatisfied
    out = m_get_output(c)
    m_mark_unsatisfied(c)
    m_remove_from_graph(c)
    unsatisfied = m__remove_propagate_from(self, out)
    m_for_each(unsatisfied, (args...)->m_incremental_add(self, args...))
    return nothing
end

function m_extract_plan_from_constraints(self::A__Planner, constraints)
    local sources
    sources = C_Vector()
    function each(c)
        if truth0((let _bool_value = m_is_input(c); truth0(_bool_value) ? m_is_satisfied(c) : _bool_value end))
            m_append(sources, c)
        end
        return nothing
    end

    m_for_each(constraints, each)
    return m__make_plan(self, sources)
end

function m__make_plan(self::A__Planner, sources)
    local c, mark, plan, todo
    mark = m__new_mark(self)
    plan = C__Plan()
    todo = sources
    while truth0(!truth0(m_is_empty(todo)))
        c = m_remove_first(todo)
        if truth0((let _bool_value = ((m_get_output(c).mark != mark)); truth0(_bool_value) ? m_inputs_known(c, mark) : _bool_value end))
            m_append(plan, c)
            m_get_output(c).mark = mark
            C__Planner___add_constraints_consuming_to(m_get_output(c), todo)
        end
    end
    return plan
end

function m_propagate_from(self::A__Planner, v)
    local c, todo
    todo = C_Vector()
    C__Planner___add_constraints_consuming_to(v, todo)
    while truth0(!truth0(m_is_empty(todo)))
        c = m_remove_first(todo)
        m_execute(c)
        C__Planner___add_constraints_consuming_to(m_get_output(c), todo)
    end
    return nothing
end

function C__Planner___add_constraints_consuming_to(v, coll)
    local determining_c
    determining_c = v.determined_by
    function each(c)
        if truth0((let _bool_value = ((c !== determining_c)); truth0(_bool_value) ? m_is_satisfied(c) : _bool_value end))
            m_append(coll, c)
        end
        return nothing
    end

    m_for_each(v.constraints, each)
    return nothing
end

function m_add_propagate(self::A__Planner, c, mark)
    local d, todo
    todo = vector_with(c)
    while truth0(!truth0(m_is_empty(todo)))
        d = m_remove_first(todo)
        if truth0(((m_get_output(d).mark == mark)))
            m_incremental_remove(self, c)
            return false
        end
        m_recalculate(d)
        C__Planner___add_constraints_consuming_to(m_get_output(d), todo)
    end
    return true
end

function m_change(self::A__Planner, var, new_value)
    local _, edit_c, edit_v, plan
    edit_c = C__EditConstraint(var, _PREFERRED, self)
    edit_v = vector_with(edit_c)
    plan = m_extract_plan_from_constraints(self, edit_v)
    for _ in range0(10)
        var.value = new_value
        m_execute(plan)
    end
    m_destroy_constraint(edit_c, self)
    return nothing
end

function C__Planner___constraints_consuming(v, fn)
    local determining_c
    determining_c = v.determined_by
    function each(c)
        if truth0((let _bool_value = ((c !== determining_c)); truth0(_bool_value) ? m_is_satisfied(c) : _bool_value end))
            fn(c)
        end
        return nothing
    end

    m_for_each(v.constraints, each)
    return nothing
end

function m__new_mark(self::A__Planner)
    self._current_mark = add0(self._current_mark, 1)
    return self._current_mark
end

function m__remove_propagate_from(self::A__Planner, out)
    local todo, unsatisfied, v
    unsatisfied = C_Vector()
    out.determined_by = nothing
    out.walk_strength = _absolute_weakest
    out.stay = true
    todo = vector_with(out)
    while truth0(!truth0(m_is_empty(todo)))
        v = m_remove_first(todo)
        function each(c)
            if truth0(!truth0(m_is_satisfied(c)))
                m_append(unsatisfied, c)
            end
            return nothing
        end

        m_for_each(v.constraints, each)
        function recalc(c)
            m_recalculate(c)
            m_append(todo, m_get_output(c))
            return nothing
        end

        C__Planner___constraints_consuming(v, recalc)
    end
    function comp(c1, c2)
        return (truth0(m_stronger(c1.strength, c2.strength)) ? -(1) : 1)
    end

    m_sort(unsatisfied, comp)
    return unsatisfied
end

function C__Planner__chain_test(n, destroy_edit=true)
    local edit_c, edit_v, i, plan, planner, v1, v2, variables
    planner = C__Planner()
    variables = mul0(Any[nothing], add0(n, 1))
    for i in range0(add0(n, 1))
        set0!(variables, i, C__Variable())
    end
    for i in range0(n)
        v1 = get0(variables, i)
        v2 = get0(variables, add0(i, 1))
        C__EqualityConstraint(v1, v2, _REQUIRED, planner)
    end
    C__StayConstraint(get0(variables, n), _STRONG_DEFAULT, planner)
    edit_c = C__EditConstraint(get0(variables, 0), _PREFERRED, planner)
    edit_v = vector_with(edit_c)
    plan = m_extract_plan_from_constraints(planner, edit_v)
    for i in range0(100)
        get0(variables, 0).value = i
        m_execute(plan)
        if truth0(((get0(variables, n).value != i)))
            throw(ErrorException("Chain test failed!"))
        end
    end
    destroy_edit && m_destroy_constraint(edit_c, planner)
    return nothing
end

function C__Planner__projection_test(n, first_value=1)
    local dests, dst, i, offset, planner, scale, src
    planner = C__Planner()
    dests = C_Vector()
    scale = C__Variable(10)
    offset = C__Variable(1000)
    src = nothing
    dst = nothing
    for i in range0(first_value, add0(n, first_value))
        src = C__Variable(i)
        dst = C__Variable(i)
        m_append(dests, dst)
        C__StayConstraint(src, _DEFAULT, planner)
        C__ScaleConstraint(src, scale, offset, dst, _REQUIRED, planner)
    end
    m_change(planner, src, 17)
    if truth0(((dst.value != 1170)))
        throw(ErrorException("Projection test 1 failed!"))
    end
    m_change(planner, dst, 1050)
    if truth0(((src.value != 5)))
        throw(ErrorException("Projection test 2 failed!"))
    end
    m_change(planner, scale, 5)
    for i in range0((n - 1))
        if truth0(((m_at(dests, i).value != add0(mul0(add0(i, first_value), 5), 1000))))
            throw(ErrorException("Projection test 3 failed!"))
        end
    end
    m_change(planner, offset, 2000)
    for i in range0((n - 1))
        if truth0(((m_at(dests, i).value != add0(mul0(add0(i, first_value), 5), 2000))))
            throw(ErrorException("Projection test 4 failed!"))
        end
    end
    return nothing
end

abstract type A__Sym end
mutable struct C__Sym <: A__Sym
    _hash
    C__Sym(::Val{:raw}) = new(nothing)
end
function C__Sym(args...)
    self = C__Sym(Val(:raw))
    init__Sym(self, args...)
    return self
end

function init__Sym(self, hash_)
    self._hash = hash_
    return nothing
end

function m_custom_hash(self::A__Sym)
    return self._hash
end

const _ABSOLUTE_STRONGEST = C__Sym(0)
const _REQUIRED = C__Sym(1)
const _STRONG_PREFERRED = C__Sym(2)
const _PREFERRED = C__Sym(3)
const _STRONG_DEFAULT = C__Sym(4)
const _DEFAULT = C__Sym(5)
const _WEAK_DEFAULT = C__Sym(6)
const _ABSOLUTE_WEAKEST = C__Sym(7)
abstract type A__Strength end
mutable struct C__Strength <: A__Strength
    _symbolic_value
    arithmetic_value
    C__Strength(::Val{:raw}) = new(nothing, nothing)
end
function C__Strength(args...)
    self = C__Strength(Val(:raw))
    init__Strength(self, args...)
    return self
end

function init__Strength(self, strength_sym)
    self._symbolic_value = strength_sym
    self.arithmetic_value = m_at(_strength_table, strength_sym)
    return nothing
end

function m_same_as(self::A__Strength, s)
    return ((self.arithmetic_value == s.arithmetic_value))
end

function m_stronger(self::A__Strength, s)
    return ((self.arithmetic_value < s.arithmetic_value))
end

function m_weaker(self::A__Strength, s)
    return ((self.arithmetic_value > s.arithmetic_value))
end

function m_strongest(self::A__Strength, s)
    return (truth0(m_stronger(s, self)) ? s : self)
end

function m_weakest(self::A__Strength, s)
    return (truth0(m_weaker(s, self)) ? s : self)
end

function C__Strength__of(strength)
    return m_at(_strength_constant, strength)
end

function _create_strength_table()
    local strength_table
    strength_table = C_IdentityDictionary()
    m_at_put(strength_table, _ABSOLUTE_STRONGEST, -(10000))
    m_at_put(strength_table, _REQUIRED, -(800))
    m_at_put(strength_table, _STRONG_PREFERRED, -(600))
    m_at_put(strength_table, _PREFERRED, -(400))
    m_at_put(strength_table, _STRONG_DEFAULT, -(200))
    m_at_put(strength_table, _DEFAULT, 0)
    m_at_put(strength_table, _WEAK_DEFAULT, 500)
    m_at_put(strength_table, _ABSOLUTE_WEAKEST, 10000)
    return strength_table
end

function _create_strength_constants()
    local strength_constant
    strength_constant = C_IdentityDictionary()
    m_for_each(m_get_keys(_strength_table), (key)->m_at_put(strength_constant, key, C__Strength(key)))
    return strength_constant
end

const _strength_table = _create_strength_table()
const _strength_constant = _create_strength_constants()
const _absolute_weakest = C__Strength__of(_ABSOLUTE_WEAKEST)
const _required = C__Strength__of(_REQUIRED)
const C__Direction__FORWARD = 1
const C__Direction__BACKWARD = 2

abstract type A__AbstractConstraint end
mutable struct C__AbstractConstraint <: A__AbstractConstraint
    strength
    C__AbstractConstraint(::Val{:raw}) = new(nothing)
end
function C__AbstractConstraint(args...)
    self = C__AbstractConstraint(Val(:raw))
    init__AbstractConstraint(self, args...)
    return self
end

function init__AbstractConstraint(self, strength)
    self.strength = C__Strength__of(strength)
    return nothing
end

function m_is_input(self::A__AbstractConstraint)
    return false
end

function m_is_satisfied(self::A__AbstractConstraint)
    nothing
    return nothing
end

function m_add_constraint(self::A__AbstractConstraint, planner)
    m_add_to_graph(self)
    m_incremental_add(planner, self)
    return nothing
end

function m_add_to_graph(self::A__AbstractConstraint)
    nothing
    return nothing
end

function m_destroy_constraint(self::A__AbstractConstraint, planner)
    if truth0(m_is_satisfied(self))
        m_incremental_remove(planner, self)
    end
    m_remove_from_graph(self)
    return nothing
end

function m_remove_from_graph(self::A__AbstractConstraint)
    nothing
    return nothing
end

function m_choose_method(self::A__AbstractConstraint, mark)
    nothing
    return nothing
end

function m_execute(self::A__AbstractConstraint)
    nothing
    return nothing
end

function m_inputs_do(self::A__AbstractConstraint, fn)
    nothing
    return nothing
end

function m_inputs_has_one(self::A__AbstractConstraint, fn)
    nothing
    return nothing
end

function m_inputs_known(self::A__AbstractConstraint, mark)
    return !truth0(m_inputs_has_one(self, (v)->!truth0((let _bool_value = ((v.mark == mark)); truth0(_bool_value) ? _bool_value : (let _bool_value = v.stay; truth0(_bool_value) ? _bool_value : ((v.determined_by === nothing)) end) end))))
end

function m_mark_unsatisfied(self::A__AbstractConstraint)
    nothing
    return nothing
end

function m_get_output(self::A__AbstractConstraint)
    nothing
    return nothing
end

function m_recalculate(self::A__AbstractConstraint)
    nothing
    return nothing
end

function m_satisfy(self::A__AbstractConstraint, mark, planner)
    local out, overridden
    m_choose_method(self, mark)
    if truth0(m_is_satisfied(self))
        function each(input_)
            input_.mark = mark
            return nothing
        end

        m_inputs_do(self, each)
        out = m_get_output(self)
        overridden = out.determined_by
        if truth0(((overridden !== nothing)))
            m_mark_unsatisfied(overridden)
        end
        out.determined_by = self
        if truth0(!truth0(m_add_propagate(planner, self, mark)))
            throw(ErrorException("Cycle encountered"))
        end
        out.mark = mark
    else
        overridden = nothing
        if truth0(m_same_as(self.strength, _required))
            throw(ErrorException("Could not satisfy a required constraint"))
        end
    end
    return overridden
end

abstract type A__BinaryConstraint <: A__AbstractConstraint end
mutable struct C__BinaryConstraint <: A__BinaryConstraint
    strength
    _v1
    _v2
    _direction
    C__BinaryConstraint(::Val{:raw}) = new(nothing, nothing, nothing, nothing)
end
function C__BinaryConstraint(args...)
    self = C__BinaryConstraint(Val(:raw))
    init__BinaryConstraint(self, args...)
    return self
end

function init__BinaryConstraint(self, var1, var2, strength, _planner)
    init__AbstractConstraint(self, strength)
    self._v1 = var1
    self._v2 = var2
    self._direction = nothing
    return nothing
end

function m_is_satisfied(self::A__BinaryConstraint)
    return ((self._direction !== nothing))
end

function m_add_to_graph(self::A__BinaryConstraint)
    m_add_constraint(self._v1, self)
    m_add_constraint(self._v2, self)
    self._direction = nothing
    return nothing
end

function m_remove_from_graph(self::A__BinaryConstraint)
    if truth0(((self._v1 !== nothing)))
        m_remove_constraint(self._v1, self)
    end
    if truth0(((self._v2 !== nothing)))
        m_remove_constraint(self._v2, self)
    end
    self._direction = nothing
    return nothing
end

function m_choose_method(self::A__BinaryConstraint, mark)
    if truth0(((self._v1.mark == mark)))
        if truth0((let _bool_value = ((self._v2.mark != mark)); truth0(_bool_value) ? m_stronger(self.strength, self._v2.walk_strength) : _bool_value end))
            self._direction = C__Direction__FORWARD
            return self._direction
        end
        self._direction = nothing
        return self._direction
    end
    if truth0(((self._v2.mark == mark)))
        if truth0((let _bool_value = ((self._v1.mark != mark)); truth0(_bool_value) ? m_stronger(self.strength, self._v1.walk_strength) : _bool_value end))
            self._direction = C__Direction__BACKWARD
            return self._direction
        end
        self._direction = nothing
        return self._direction
    end
    if truth0(m_weaker(self._v1.walk_strength, self._v2.walk_strength))
        if truth0(m_stronger(self.strength, self._v1.walk_strength))
            self._direction = C__Direction__BACKWARD
            return self._direction
        end
        self._direction = nothing
        return self._direction
    end
    if truth0(m_stronger(self.strength, self._v2.walk_strength))
        self._direction = C__Direction__FORWARD
        return self._direction
    end
    self._direction = nothing
    return self._direction
end

function m_inputs_do(self::A__BinaryConstraint, fn)
    if truth0(((self._direction === C__Direction__FORWARD)))
        fn(self._v1)
    else
        fn(self._v2)
    end
    return nothing
end

function m_inputs_has_one(self::A__BinaryConstraint, fn)
    if truth0(((self._direction === C__Direction__FORWARD)))
        return fn(self._v1)
    end
    return fn(self._v2)
end

function m_mark_unsatisfied(self::A__BinaryConstraint)
    self._direction = nothing
    return nothing
end

function m_get_output(self::A__BinaryConstraint)
    return (truth0(((self._direction === C__Direction__FORWARD))) ? self._v2 : self._v1)
end

function m_recalculate(self::A__BinaryConstraint)
    local input_, output
    if truth0(((self._direction === C__Direction__FORWARD)))
        input_ = self._v1
        output = self._v2
    else
        input_ = self._v2
        output = self._v1
    end
    output.walk_strength = m_weakest(self.strength, input_.walk_strength)
    output.stay = input_.stay
    if truth0(output.stay)
        m_execute(self)
    end
    return nothing
end

abstract type A__UnaryConstraint <: A__AbstractConstraint end
mutable struct C__UnaryConstraint <: A__UnaryConstraint
    strength
    _output
    _satisfied::Bool
    C__UnaryConstraint(::Val{:raw}) = new(nothing, nothing, false)
end
function C__UnaryConstraint(args...)
    self = C__UnaryConstraint(Val(:raw))
    init__UnaryConstraint(self, args...)
    return self
end

function init__UnaryConstraint(self, v, strength, planner)
    init__AbstractConstraint(self, strength)
    self._output = v
    self._satisfied = false
    m_add_constraint(self, planner)
    return nothing
end

function m_is_satisfied(self::A__UnaryConstraint)
    return self._satisfied
end

function m_add_to_graph(self::A__UnaryConstraint)
    m_add_constraint(self._output, self)
    self._satisfied = false
    return nothing
end

function m_remove_from_graph(self::A__UnaryConstraint)
    if truth0(((self._output !== nothing)))
        m_remove_constraint(self._output, self)
    end
    self._satisfied = false
    return nothing
end

function m_choose_method(self::A__UnaryConstraint, mark)
    self._satisfied = (let _bool_value = ((self._output.mark != mark)); truth0(_bool_value) ? m_stronger(self.strength, self._output.walk_strength) : _bool_value end)
    return nothing
end

function m_execute(self::A__UnaryConstraint)
    nothing
    return nothing
end

function m_inputs_do(self::A__UnaryConstraint, fn)
    nothing
    return nothing
end

function m_inputs_has_one(self::A__UnaryConstraint, fn)
    return false
end

function m_mark_unsatisfied(self::A__UnaryConstraint)
    self._satisfied = false
    return nothing
end

function m_get_output(self::A__UnaryConstraint)
    return self._output
end

function m_recalculate(self::A__UnaryConstraint)
    self._output.walk_strength = self.strength
    self._output.stay = !truth0(m_is_input(self))
    if truth0(self._output.stay)
        m_execute(self)
    end
    return nothing
end

abstract type A__EditConstraint <: A__UnaryConstraint end
mutable struct C__EditConstraint <: A__EditConstraint
    strength
    _output
    _satisfied::Bool
    C__EditConstraint(::Val{:raw}) = new(nothing, nothing, false)
end
function C__EditConstraint(args...)
    self = C__EditConstraint(Val(:raw))
    init__UnaryConstraint(self, args...)
    return self
end

function m_is_input(self::A__EditConstraint)
    return true
end

function m_execute(self::A__EditConstraint)
    nothing
    return nothing
end

abstract type A__EqualityConstraint <: A__BinaryConstraint end
mutable struct C__EqualityConstraint <: A__EqualityConstraint
    strength
    _v1
    _v2
    _direction
    C__EqualityConstraint(::Val{:raw}) = new(nothing, nothing, nothing, nothing)
end
function C__EqualityConstraint(args...)
    self = C__EqualityConstraint(Val(:raw))
    init__EqualityConstraint(self, args...)
    return self
end

function init__EqualityConstraint(self, var1, var2, strength, planner)
    init__BinaryConstraint(self, var1, var2, strength, planner)
    m_add_constraint(self, planner)
    return nothing
end

function m_execute(self::A__EqualityConstraint)
    if truth0(((self._direction === C__Direction__FORWARD)))
        self._v2.value = self._v1.value
    else
        self._v1.value = self._v2.value
    end
    return nothing
end

abstract type A__ScaleConstraint <: A__BinaryConstraint end
mutable struct C__ScaleConstraint <: A__ScaleConstraint
    strength
    _v1
    _v2
    _direction
    _scale
    _offset
    C__ScaleConstraint(::Val{:raw}) = new(nothing, nothing, nothing, nothing, nothing, nothing)
end
function C__ScaleConstraint(args...)
    self = C__ScaleConstraint(Val(:raw))
    init__ScaleConstraint(self, args...)
    return self
end

function init__ScaleConstraint(self, src, scale, offset, dest, strength, planner)
    init__BinaryConstraint(self, src, dest, strength, planner)
    self._scale = scale
    self._offset = offset
    m_add_constraint(self, planner)
    return nothing
end

function m_add_to_graph(self::A__ScaleConstraint)
    m_add_constraint(self._v1, self)
    m_add_constraint(self._v2, self)
    m_add_constraint(self._scale, self)
    m_add_constraint(self._offset, self)
    self._direction = nothing
    return nothing
end

function m_remove_from_graph(self::A__ScaleConstraint)
    if truth0(((self._v1 !== nothing)))
        m_remove_constraint(self._v1, self)
    end
    if truth0(((self._v2 !== nothing)))
        m_remove_constraint(self._v2, self)
    end
    if truth0(((self._scale !== nothing)))
        m_remove_constraint(self._scale, self)
    end
    if truth0(((self._offset !== nothing)))
        m_remove_constraint(self._offset, self)
    end
    self._direction = nothing
    return nothing
end

function m_execute(self::A__ScaleConstraint)
    if truth0(((self._direction === C__Direction__FORWARD)))
        self._v2.value = add0(mul0(self._v1.value, self._scale.value), self._offset.value)
    else
        self._v1.value = ((self._v2.value - self._offset.value) / self._scale.value)
    end
    return nothing
end

function m_inputs_do(self::A__ScaleConstraint, fn)
    if truth0(((self._direction === C__Direction__FORWARD)))
        fn(self._v1)
        fn(self._scale)
        fn(self._offset)
    else
        fn(self._v2)
        fn(self._scale)
        fn(self._offset)
    end
    return nothing
end

function m_recalculate(self::A__ScaleConstraint)
    local input_, output
    if truth0(((self._direction === C__Direction__FORWARD)))
        input_ = self._v1
        output = self._v2
    else
        output = self._v1
        input_ = self._v2
    end
    output.walk_strength = m_weakest(self.strength, input_.walk_strength)
    output.stay = (let _bool_value = input_.stay; truth0(_bool_value) ? (let _bool_value = self._scale.stay; truth0(_bool_value) ? self._offset.stay : _bool_value end) : _bool_value end)
    if truth0(output.stay)
        m_execute(self)
    end
    return nothing
end

abstract type A__StayConstraint <: A__UnaryConstraint end
mutable struct C__StayConstraint <: A__StayConstraint
    strength
    _output
    _satisfied::Bool
    C__StayConstraint(::Val{:raw}) = new(nothing, nothing, false)
end
function C__StayConstraint(args...)
    self = C__StayConstraint(Val(:raw))
    init__UnaryConstraint(self, args...)
    return self
end

function m_execute(self::A__StayConstraint)
    nothing
    return nothing
end

abstract type A__Variable end
mutable struct C__Variable <: A__Variable
    value::Union{Int,Float64}
    constraints
    determined_by
    mark::Int
    walk_strength
    stay::Bool
    C__Variable(::Val{:raw}) = new(0, nothing, nothing, 0, nothing, false)
end
function C__Variable(args...)
    self = C__Variable(Val(:raw))
    init__Variable(self, args...)
    return self
end

function init__Variable(self, value=0)
    self.value = value
    self.constraints = C_Vector(2)
    self.determined_by = nothing
    self.mark = 0
    self.walk_strength = _absolute_weakest
    self.stay = true
    return nothing
end

function m_add_constraint(self::A__Variable, c)
    m_append(self.constraints, c)
    return nothing
end

function m_remove_constraint(self::A__Variable, c)
    m_remove(self.constraints, c)
    if truth0(((self.determined_by === c)))
        self.determined_by = nothing
    end
    return nothing
end


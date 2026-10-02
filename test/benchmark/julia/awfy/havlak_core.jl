# port of test/benchmark/awfy/python/havlak.py; algorithms retain their original control flow.
# Adapted based on SOM benchmark.
# Copyright 2011 Google Inc.
#
#     Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
#     You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#         limitations under the License.
nothing
abstract type A_Havlak <: A_Benchmark end
mutable struct C_Havlak <: A_Havlak
    C_Havlak(::Val{:raw}) = new()
end
function C_Havlak(args...)
    self = C_Havlak(Val(:raw))
    return self
end

function m_inner_benchmark_loop(self::A_Havlak, inner_iterations)
    return C_Havlak___verify_result(m_main(C__LoopTesterApp(), inner_iterations, 50, 10, 10, 5), inner_iterations)
end

function C_Havlak___verify_result(result, inner_iterations)
    if truth0(((inner_iterations == 15000)))
        return (let _bool_value = ((get0(result, 0) == 46602)); truth0(_bool_value) ? ((get0(result, 1) == 5213)) : _bool_value end)
    end
    if truth0(((inner_iterations == 1500)))
        return (let _bool_value = ((get0(result, 0) == 6102)); truth0(_bool_value) ? ((get0(result, 1) == 5213)) : _bool_value end)
    end
    if truth0(((inner_iterations == 150)))
        return (let _bool_value = ((get0(result, 0) == 2052)); truth0(_bool_value) ? ((get0(result, 1) == 5213)) : _bool_value end)
    end
    if truth0(((inner_iterations == 15)))
        return (let _bool_value = ((get0(result, 0) == 1647)); truth0(_bool_value) ? ((get0(result, 1) == 5213)) : _bool_value end)
    end
    if truth0(((inner_iterations == 1)))
        return (let _bool_value = ((get0(result, 0) == 1605)); truth0(_bool_value) ? ((get0(result, 1) == 5213)) : _bool_value end)
    end
    println(add0(add0("No verification result for ", string(inner_iterations)), " found"))
    println(add0(add0(add0("Result is: ", string(get0(result, 0))), ", "), string(get0(result, 1))))
    return false
end

function m_benchmark(self::A_Havlak)
    throw(ErrorException("should not be reached"))
end

function m_verify_result(self::A_Havlak, result)
    throw(ErrorException("should not be reached"))
end

abstract type A__BasicBlock end
mutable struct C__BasicBlock <: A__BasicBlock
    _name
    in_edges
    out_edges
    C__BasicBlock(::Val{:raw}) = new(nothing, nothing, nothing)
end
function C__BasicBlock(args...)
    self = C__BasicBlock(Val(:raw))
    init__BasicBlock(self, args...)
    return self
end

function init__BasicBlock(self, name)
    self._name = name
    self.in_edges = C_Vector(2)
    self.out_edges = C_Vector(2)
    return nothing
end

function m_get_num_pred(self::A__BasicBlock)
    return m_size(self.in_edges)
end

function m_add_out_edge(self::A__BasicBlock, to)
    m_append(self.out_edges, to)
    return nothing
end

function m_add_in_edge(self::A__BasicBlock, from_)
    m_append(self.in_edges, from_)
    return nothing
end

function m_custom_hash(self::A__BasicBlock)
    return self._name
end

abstract type A__BasicBlockEdge end
mutable struct C__BasicBlockEdge <: A__BasicBlockEdge
    _from
    _to
    C__BasicBlockEdge(::Val{:raw}) = new(nothing, nothing)
end
function C__BasicBlockEdge(args...)
    self = C__BasicBlockEdge(Val(:raw))
    init__BasicBlockEdge(self, args...)
    return self
end

function init__BasicBlockEdge(self, cfg, from_name, to_name)
    self._from = m_create_node(cfg, from_name)
    self._to = m_create_node(cfg, to_name)
    m_add_out_edge(self._from, self._to)
    m_add_in_edge(self._to, self._from)
    m_add_edge(cfg, self)
    return nothing
end

abstract type A__ControlFlowGraph end
mutable struct C__ControlFlowGraph <: A__ControlFlowGraph
    start_basic_block
    basic_blocks
    _edge_list
    C__ControlFlowGraph(::Val{:raw}) = new(nothing, nothing, nothing)
end
function C__ControlFlowGraph(args...)
    self = C__ControlFlowGraph(Val(:raw))
    init__ControlFlowGraph(self, args...)
    return self
end

function init__ControlFlowGraph(self)
    self.start_basic_block = nothing
    self.basic_blocks = C_Vector()
    self._edge_list = C_Vector()
    return nothing
end

function m_create_node(self::A__ControlFlowGraph, name)
    local node
    if truth0(m_at(self.basic_blocks, name))
        node = m_at(self.basic_blocks, name)
    else
        node = C__BasicBlock(name)
        m_at_put(self.basic_blocks, name, node)
    end
    if truth0(((m_num_nodes(self) == 1)))
        self.start_basic_block = node
    end
    return node
end

function m_add_edge(self::A__ControlFlowGraph, edge)
    m_append(self._edge_list, edge)
    return nothing
end

function m_num_nodes(self::A__ControlFlowGraph)
    return m_size(self.basic_blocks)
end

abstract type A__LoopStructureGraph end
mutable struct C__LoopStructureGraph <: A__LoopStructureGraph
    _loop_counter::Int
    _loops
    _root
    C__LoopStructureGraph(::Val{:raw}) = new(0, nothing, nothing)
end
function C__LoopStructureGraph(args...)
    self = C__LoopStructureGraph(Val(:raw))
    init__LoopStructureGraph(self, args...)
    return self
end

function init__LoopStructureGraph(self)
    self._loop_counter = 0
    self._loops = C_Vector()
    self._root = C__SimpleLoop(nothing, true)
    m_set_nesting_level(self._root, 0)
    self._root.counter = self._loop_counter
    self._loop_counter = add0(self._loop_counter, 1)
    m_append(self._loops, self._root)
    return nothing
end

function m_create_new_loop(self::A__LoopStructureGraph, bb, is_reducible)
    local loop
    loop = C__SimpleLoop(bb, is_reducible)
    loop.counter = self._loop_counter
    self._loop_counter = add0(self._loop_counter, 1)
    m_append(self._loops, loop)
    return loop
end

function m_calculate_nesting_level(self::A__LoopStructureGraph)
    function each(liter)
        if truth0(!truth0(liter.is_root))
            if truth0(((liter.parent === nothing)))
                m_set_parent(liter, self._root)
            end
        end
        return nothing
    end

    m_for_each(self._loops, each)
    m__calculate_nesting_level_rec(self, self._root, 0)
    return nothing
end

function m__calculate_nesting_level_rec(self::A__LoopStructureGraph, loop, depth)
    loop.depth_level = depth
    function each(liter)
        m__calculate_nesting_level_rec(self, liter, add0(depth, 1))
        m_set_nesting_level(loop, max(loop.nesting_level, add0(1, liter.nesting_level)))
        return nothing
    end

    m_for_each(loop.children, each)
    return nothing
end

function m_num_loops(self::A__LoopStructureGraph)
    return m_size(self._loops)
end

abstract type A__SimpleLoop end
mutable struct C__SimpleLoop <: A__SimpleLoop
    _is_reducible
    parent
    is_root::Bool
    nesting_level::Int
    depth_level::Int
    counter::Int
    _basic_blocks
    children
    _header
    C__SimpleLoop(::Val{:raw}) = new(nothing, nothing, false, 0, 0, 0, nothing, nothing, nothing)
end
function C__SimpleLoop(args...)
    self = C__SimpleLoop(Val(:raw))
    init__SimpleLoop(self, args...)
    return self
end

function init__SimpleLoop(self, bb, is_reducible)
    self._is_reducible = is_reducible
    self.parent = nothing
    self.is_root = false
    self.nesting_level = 0
    self.depth_level = 0
    # loop numbering is assigned by the containing loop graph.
    self.counter = 0
    self._basic_blocks = C_IdentitySet()
    self.children = C_IdentitySet()
    if truth0(((bb !== nothing)))
        m_add(self._basic_blocks, bb)
    end
    self._header = bb
    return nothing
end

function m_add_node(self::A__SimpleLoop, bb)
    m_add(self._basic_blocks, bb)
    return nothing
end

function m_add_child_loop(self::A__SimpleLoop, loop)
    m_add(self.children, loop)
    return nothing
end

function m_set_parent(self::A__SimpleLoop, parent)
    self.parent = parent
    m_add_child_loop(self.parent, self)
    return nothing
end

function m_set_nesting_level(self::A__SimpleLoop, level)
    self.nesting_level = level
    if truth0(((level == 0)))
        self.is_root = true
    end
    return nothing
end

abstract type A__UnionFindNode end
mutable struct C__UnionFindNode <: A__UnionFindNode
    parent
    bb
    dfs_number::Int
    loop
    C__UnionFindNode(::Val{:raw}) = new(nothing, nothing, 0, nothing)
end
function C__UnionFindNode(args...)
    self = C__UnionFindNode(Val(:raw))
    init__UnionFindNode(self, args...)
    return self
end

function init__UnionFindNode(self)
    self.parent = nothing
    self.bb = nothing
    self.dfs_number = 0
    self.loop = nothing
    return nothing
end

function m_init_node(self::A__UnionFindNode, bb, dfs_number)
    self.parent = self
    self.bb = bb
    self.dfs_number = dfs_number
    self.loop = nothing
    return nothing
end

function m_find_set(self::A__UnionFindNode)
    local node, node_list
    node_list = C_Vector()
    node = self
    while truth0(((node !== node.parent)))
        if truth0(((node.parent !== node.parent.parent)))
            m_append(node_list, node)
        end
        node = node.parent
    end
    m_for_each(node_list, (i)->m_union(i, self.parent))
    return node
end

function m_union(self::A__UnionFindNode, basic_block)
    self.parent = basic_block
    return nothing
end

abstract type A__LoopTesterApp end
mutable struct C__LoopTesterApp <: A__LoopTesterApp
    _cfg
    _lsg
    C__LoopTesterApp(::Val{:raw}) = new(nothing, nothing)
end
function C__LoopTesterApp(args...)
    self = C__LoopTesterApp(Val(:raw))
    init__LoopTesterApp(self, args...)
    return self
end

function init__LoopTesterApp(self)
    self._cfg = C__ControlFlowGraph()
    self._lsg = C__LoopStructureGraph()
    m_create_node(self._cfg, 0)
    return nothing
end

function m__build_diamond(self::A__LoopTesterApp, start)
    local bb0
    bb0 = start
    C__BasicBlockEdge(self._cfg, bb0, add0(bb0, 1))
    C__BasicBlockEdge(self._cfg, bb0, add0(bb0, 2))
    C__BasicBlockEdge(self._cfg, add0(bb0, 1), add0(bb0, 3))
    C__BasicBlockEdge(self._cfg, add0(bb0, 2), add0(bb0, 3))
    return add0(bb0, 3)
end

function m__build_connect(self::A__LoopTesterApp, start, end_)
    C__BasicBlockEdge(self._cfg, start, end_)
    return nothing
end

function m__build_straight(self::A__LoopTesterApp, start, n)
    local i
    for i in range0(n)
        m__build_connect(self, add0(start, i), add0(add0(start, i), 1))
    end
    return add0(start, n)
end

function m__build_base_loop(self::A__LoopTesterApp, from_)
    local d11, diamond1, diamond2, footer, header
    header = m__build_straight(self, from_, 1)
    diamond1 = m__build_diamond(self, header)
    d11 = m__build_straight(self, diamond1, 1)
    diamond2 = m__build_diamond(self, d11)
    footer = m__build_straight(self, diamond2, 1)
    m__build_connect(self, diamond2, d11)
    m__build_connect(self, diamond1, header)
    m__build_connect(self, footer, from_)
    footer = m__build_straight(self, footer, 1)
    return footer
end

function m_main(self::A__LoopTesterApp, num_dummy_loops, find_loop_iterations, par_loops, ppar_loops, pppar_loops)
    local _
    m__construct_simple_cfg(self)
    m__add_dummy_loops(self, num_dummy_loops)
    m__construct_cfg(self, par_loops, ppar_loops, pppar_loops)
    m__find_loops(self, self._lsg)
    for _ in range0(find_loop_iterations)
        m__find_loops(self, C__LoopStructureGraph())
    end
    m_calculate_nesting_level(self._lsg)
    return Any[m_num_loops(self._lsg), m_num_nodes(self._cfg)]
end

function m__construct_cfg(self::A__LoopTesterApp, par_loops, ppar_loops, pppar_loops)
    local _, bottom, n, top
    n = 2
    for _ in range0(par_loops)
        m_create_node(self._cfg, add0(n, 1))
        m__build_connect(self, 2, add0(n, 1))
        n = add0(n, 1)
        for _ in range0(ppar_loops)
            top = n
            n = m__build_straight(self, n, 1)
            for _ in range0(pppar_loops)
                n = m__build_base_loop(self, n)
            end
            bottom = m__build_straight(self, n, 1)
            m__build_connect(self, n, top)
            n = bottom
        end
        m__build_connect(self, n, 1)
    end
    return nothing
end

function m__add_dummy_loops(self::A__LoopTesterApp, num_dummy_loops)
    local _
    for _ in range0(num_dummy_loops)
        m__find_loops(self, self._lsg)
    end
    return nothing
end

function m__find_loops(self::A__LoopTesterApp, loop_structure)
    local finder
    finder = C__HavlakLoopFinder(self._cfg, loop_structure)
    m_find_loops(finder)
    return nothing
end

function m__construct_simple_cfg(self::A__LoopTesterApp)
    m_create_node(self._cfg, 0)
    m__build_base_loop(self, 0)
    m_create_node(self._cfg, 1)
    C__BasicBlockEdge(self._cfg, 0, 2)
    return nothing
end

const _UNVISITED = 2147483647
const _MAXNONBACKPREDS = mul0(32, 1024)
const C__BasicBlockClass__BB_TOP = 0
const C__BasicBlockClass__BB_NONHEADER = 1
const C__BasicBlockClass__BB_REDUCIBLE = 2
const C__BasicBlockClass__BB_SELF = 3
const C__BasicBlockClass__BB_IRREDUCIBLE = 4
const C__BasicBlockClass__BB_DEAD = 5
const C__BasicBlockClass__BB_LAST = 6

abstract type A__HavlakLoopFinder end
mutable struct C__HavlakLoopFinder <: A__HavlakLoopFinder
    _cfg
    _lsg
    _non_back_preds
    _back_preds
    _number
    _max_size::Int
    _header
    _type
    _last
    _nodes
    C__HavlakLoopFinder(::Val{:raw}) = new(nothing, nothing, nothing, nothing, nothing, 0, nothing, nothing, nothing, nothing)
end
function C__HavlakLoopFinder(args...)
    self = C__HavlakLoopFinder(Val(:raw))
    init__HavlakLoopFinder(self, args...)
    return self
end

function init__HavlakLoopFinder(self, cfg, lsg)
    self._cfg = cfg
    self._lsg = lsg
    self._non_back_preds = C_Vector()
    self._back_preds = C_Vector()
    self._number = C_IdentityDictionary()
    self._max_size = 0
    self._header = nothing
    self._type = nothing
    self._last = nothing
    self._nodes = nothing
    return nothing
end

function m__is_ancestor(self::A__HavlakLoopFinder, w, v)
    return (let _bool_value = ((w <= v)); truth0(_bool_value) ? ((v <= get0(self._last, w))) : _bool_value end)
end

function m__do_dfs(self::A__HavlakLoopFinder, current_node, current)
    local last_id, outer_blocks
    m_init_node(get0(self._nodes, current), current_node, current)
    m_at_put(self._number, current_node, current)
    last_id = current
    outer_blocks = current_node.out_edges
    function each(target)
        if truth0(((m_at(self._number, target) == _UNVISITED)))
            last_id = m__do_dfs(self, target, add0(last_id, 1))
        end
        return nothing
    end

    m_for_each(outer_blocks, each)
    set0!(self._last, current, last_id)
    return last_id
end

function m__init_all_nodes(self::A__HavlakLoopFinder)
    m_for_each(self._cfg.basic_blocks, (bb)->m_at_put(self._number, bb, _UNVISITED))
    m__do_dfs(self, self._cfg.start_basic_block, 0)
    return nothing
end

function m__identify_edges(self::A__HavlakLoopFinder, size)
    local node_w, w
    for w in range0(size)
        set0!(self._header, w, 0)
        set0!(self._type, w, C__BasicBlockClass__BB_NONHEADER)
        node_w = get0(self._nodes, w).bb
        if truth0(((node_w === nothing)))
            set0!(self._type, w, C__BasicBlockClass__BB_DEAD)
        else
            m__process_edges(self, node_w, w)
        end
    end
    return nothing
end

function m__process_edges(self::A__HavlakLoopFinder, node_w, w)
    if truth0(((m_get_num_pred(node_w) > 0)))
        function each(node_v)
            local v
            v = m_at(self._number, node_v)
            if truth0(((v != _UNVISITED)))
                if truth0(m__is_ancestor(self, w, v))
                    m_append(m_at(self._back_preds, w), v)
                else
                    m_add(m_at(self._non_back_preds, w), v)
                end
            end
            return nothing
        end

        m_for_each(node_w.in_edges, each)
    end
    return nothing
end

function m_find_loops(self::A__HavlakLoopFinder)
    local i, loop, node_pool, node_w, non_back_size, size, w, work_list, x
    if truth0(((self._cfg.start_basic_block === nothing)))
        return nothing
    end
    size = m_num_nodes(self._cfg)
    m_remove_all(self._non_back_preds)
    m_remove_all(self._back_preds)
    m_remove_all(self._number)
    if truth0(((size > self._max_size)))
        self._header = mul0(Any[0], size)
        self._type = mul0(Any[nothing], size)
        self._last = mul0(Any[0], size)
        self._nodes = mul0(Any[nothing], size)
        self._max_size = size
    end
    for i in range0(size)
        m_append(self._non_back_preds, C_Set())
        m_append(self._back_preds, C_Vector())
        set0!(self._nodes, i, C__UnionFindNode())
    end
    m__init_all_nodes(self)
    m__identify_edges(self, size)
    set0!(self._header, 0, 0)
    for w in range0((size - 1), -(1), -(1))
        node_pool = C_Vector()
        node_w = get0(self._nodes, w).bb
        if truth0(((node_w !== nothing)))
            m__step_d(self, w, node_pool)
            work_list = C_Vector()
            m_for_each(node_pool, (args...)->m_append(work_list, args...))
            if truth0(((m_size(node_pool) != 0)))
                set0!(self._type, w, C__BasicBlockClass__BB_REDUCIBLE)
            end
            while truth0(!truth0(m_is_empty(work_list)))
                x = m_remove_first(work_list)
                non_back_size = m_size(m_at(self._non_back_preds, x.dfs_number))
                if truth0(((non_back_size > _MAXNONBACKPREDS)))
                    return nothing
                end
                m__step_e_process_non_back_preds(self, w, node_pool, work_list, x)
            end
        end
        if truth0((let _bool_value = ((m_size(node_pool) > 0)); truth0(_bool_value) ? _bool_value : ((get0(self._type, w) == C__BasicBlockClass__BB_SELF)) end))
            loop = m_create_new_loop(self._lsg, node_w, ((get0(self._type, w) != C__BasicBlockClass__BB_IRREDUCIBLE)))
            m__set_loop_attributes(self, w, node_pool, loop)
        end
    end
    return nothing
end

function m__step_e_process_non_back_preds(self::A__HavlakLoopFinder, w, node_pool, work_list, x)
    function each(i)
        local y, ydash
        y = get0(self._nodes, i)
        ydash = m_find_set(y)
        if truth0(!truth0(m__is_ancestor(self, w, ydash.dfs_number)))
            set0!(self._type, w, C__BasicBlockClass__BB_IRREDUCIBLE)
            m_add(m_at(self._non_back_preds, w), ydash.dfs_number)
        else
            if truth0(((ydash.dfs_number != w)))
                if truth0(!truth0(m_has_some(node_pool, (e)->((e == ydash)))))
                    m_append(work_list, ydash)
                    m_append(node_pool, ydash)
                end
            end
        end
        return nothing
    end

    m_for_each(m_at(self._non_back_preds, x.dfs_number), each)
    return nothing
end

function m__set_loop_attributes(self::A__HavlakLoopFinder, w, node_pool, loop)
    get0(self._nodes, w).loop = loop
    function each(node)
        set0!(self._header, node.dfs_number, w)
        m_union(node, get0(self._nodes, w))
        if truth0(((node.loop !== nothing)))
            m_set_parent(node.loop, loop)
        else
            m_add_node(loop, node.bb)
        end
        return nothing
    end

    m_for_each(node_pool, each)
    return nothing
end

function m__step_d(self::A__HavlakLoopFinder, w, node_pool)
    function each(v)
        if truth0(((v != w)))
            m_append(node_pool, m_find_set(get0(self._nodes, v)))
        else
            set0!(self._type, w, C__BasicBlockClass__BB_SELF)
        end
        return nothing
    end

    m_for_each(m_at(self._back_preds, w), each)
    return nothing
end

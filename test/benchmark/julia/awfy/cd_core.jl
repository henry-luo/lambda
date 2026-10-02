# port of test/benchmark/awfy/python/cd.py; algorithms retain their original control flow.
# Copyright (c) 2001-2021 Stefan Marr
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the 'Software'), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
const MIN_X = 0.0
const MIN_Y = 0.0
const MAX_X = 1000.0
const MAX_Y = 1000.0
const MIN_Z = 0.0
const MAX_Z = 10.0
const PROXIMITY_RADIUS = 1.0
const GOOD_VOXEL_SIZE = mul0(PROXIMITY_RADIUS, 2.0)
function _compare_numbers(a, b)
    if truth0(((a == b)))
        return 0
    end
    if truth0(((a < b)))
        return -(1)
    end
    if truth0(((a > b)))
        return 1
    end
    if truth0(((a == a)))
        return 1
    end
    return -(1)
end

abstract type A__Vector2D end
mutable struct C__Vector2D <: A__Vector2D
    x::Float64
    y::Float64
    C__Vector2D(::Val{:raw}) = new(0.0, 0.0)
end
function C__Vector2D(args...)
    self = C__Vector2D(Val(:raw))
    init__Vector2D(self, args...)
    return self
end

function init__Vector2D(self, x, y)
    self.x = x
    self.y = y
    return nothing
end

function m_plus(self::A__Vector2D, other)
    return C__Vector2D(add0(self.x, other.x), add0(self.y, other.y))
end

function m_minus(self::A__Vector2D, other)
    return C__Vector2D((self.x - other.x), (self.y - other.y))
end

function m_compare_to(self::A__Vector2D, other)
    local result
    result = _compare_numbers(self.x, other.x)
    if truth0(((result != 0)))
        return result
    end
    return _compare_numbers(self.y, other.y)
end

abstract type A__Vector3D end
mutable struct C__Vector3D <: A__Vector3D
    x::Float64
    y::Float64
    z::Float64
    C__Vector3D(::Val{:raw}) = new(0.0, 0.0, 0.0)
end
function C__Vector3D(args...)
    self = C__Vector3D(Val(:raw))
    init__Vector3D(self, args...)
    return self
end

function init__Vector3D(self, x, y, z)
    self.x = x
    self.y = y
    self.z = z
    return nothing
end

function m_plus(self::A__Vector3D, other)
    return C__Vector3D(add0(self.x, other.x), add0(self.y, other.y), add0(self.z, other.z))
end

function m_minus(self::A__Vector3D, other)
    return C__Vector3D((self.x - other.x), (self.y - other.y), (self.z - other.z))
end

function m_dot(self::A__Vector3D, other)
    return add0(add0(mul0(self.x, other.x), mul0(self.y, other.y)), mul0(self.z, other.z))
end

function m_squared_magnitude(self::A__Vector3D)
    return m_dot(self, self)
end

function m_magnitude(self::A__Vector3D)
    return sqrt(m_squared_magnitude(self))
end

function m_times(self::A__Vector3D, amount)
    return C__Vector3D(mul0(self.x, amount), mul0(self.y, amount), mul0(self.z, amount))
end

const _horizontal = C__Vector2D(GOOD_VOXEL_SIZE, 0.0)
const _vertical = C__Vector2D(0.0, GOOD_VOXEL_SIZE)
const C__Color__RED = 1
const C__Color__BLACK = 2

function _tree_minimum(x)
    local current
    current = x
    while truth0(((current.left !== nothing)))
        current = current.left
    end
    return current
end

abstract type A__Node end
mutable struct C__Node <: A__Node
    key
    value
    left
    right
    parent
    color
    C__Node(::Val{:raw}) = new(nothing, nothing, nothing, nothing, nothing, nothing)
end
function C__Node(args...)
    self = C__Node(Val(:raw))
    init__Node(self, args...)
    return self
end

function init__Node(self, key, value)
    self.key = key
    self.value = value
    self.left = nothing
    self.right = nothing
    self.parent = nothing
    self.color = C__Color__RED
    return nothing
end

function m_successor(self::A__Node)
    local x, y
    x = self
    if truth0(((x.right !== nothing)))
        return _tree_minimum(x.right)
    end
    y = x.parent
    while truth0((let _bool_value = ((y !== nothing)); truth0(_bool_value) ? ((x === y.right)) : _bool_value end))
        x = y
        y = y.parent
    end
    return y
end

abstract type A__Entry end
mutable struct C__Entry <: A__Entry
    key
    value
    C__Entry(::Val{:raw}) = new(nothing, nothing)
end
function C__Entry(args...)
    self = C__Entry(Val(:raw))
    init__Entry(self, args...)
    return self
end

function init__Entry(self, key, value)
    self.key = key
    self.value = value
    return nothing
end

abstract type A__InsertResult end
mutable struct C__InsertResult <: A__InsertResult
    is_new_entry
    new_node
    old_value
    C__InsertResult(::Val{:raw}) = new(nothing, nothing, nothing)
end
function C__InsertResult(args...)
    self = C__InsertResult(Val(:raw))
    init__InsertResult(self, args...)
    return self
end

function init__InsertResult(self, is_new_entry, new_node, old_value)
    self.is_new_entry = is_new_entry
    self.new_node = new_node
    self.old_value = old_value
    return nothing
end

abstract type A__RedBlackTree end
mutable struct C__RedBlackTree <: A__RedBlackTree
    _root
    C__RedBlackTree(::Val{:raw}) = new(nothing)
end
function C__RedBlackTree(args...)
    self = C__RedBlackTree(Val(:raw))
    init__RedBlackTree(self, args...)
    return self
end

function init__RedBlackTree(self)
    self._root = nothing
    return nothing
end

function m_put(self::A__RedBlackTree, key, value)
    local insertion_result, x, y
    insertion_result = m__tree_insert(self, key, value)
    if truth0(!truth0(insertion_result.is_new_entry))
        return insertion_result.old_value
    end
    x = insertion_result.new_node
    while truth0((let _bool_value = ((x !== self._root)); truth0(_bool_value) ? ((x.parent.color === C__Color__RED)) : _bool_value end))
        if truth0(((x.parent === x.parent.parent.left)))
            y = x.parent.parent.right
            if truth0((let _bool_value = ((y !== nothing)); truth0(_bool_value) ? ((y.color === C__Color__RED)) : _bool_value end))
                x.parent.color = C__Color__BLACK
                y.color = C__Color__BLACK
                x.parent.parent.color = C__Color__RED
                x = x.parent.parent
            else
                if truth0(((x === x.parent.right)))
                    x = x.parent
                    m__left_rotate(self, x)
                end
                x.parent.color = C__Color__BLACK
                x.parent.parent.color = C__Color__RED
                m__right_rotate(self, x.parent.parent)
            end
        else
            y = x.parent.parent.left
            if truth0((let _bool_value = ((y !== nothing)); truth0(_bool_value) ? ((y.color === C__Color__RED)) : _bool_value end))
                x.parent.color = C__Color__BLACK
                y.color = C__Color__BLACK
                x.parent.parent.color = C__Color__RED
                x = x.parent.parent
            else
                if truth0(((x === x.parent.left)))
                    x = x.parent
                    m__right_rotate(self, x)
                end
                x.parent.color = C__Color__BLACK
                x.parent.parent.color = C__Color__RED
                m__left_rotate(self, x.parent.parent)
            end
        end
    end
    self._root.color = C__Color__BLACK
    return nothing
end

function m_remove(self::A__RedBlackTree, key)
    local x, x_parent, y, z
    z = m__find_node(self, key)
    if truth0(((z === nothing)))
        return nothing
    end
    if truth0((let _bool_value = ((z.left === nothing)); truth0(_bool_value) ? _bool_value : ((z.right === nothing)) end))
        y = z
    else
        y = m_successor(z)
    end
    if truth0(((y.left !== nothing)))
        x = y.left
    else
        x = y.right
    end
    if truth0(((x !== nothing)))
        x.parent = y.parent
        x_parent = x.parent
    else
        x_parent = y.parent
    end
    if truth0(((y.parent === nothing)))
        self._root = x
    else
        if truth0(((y === y.parent.left)))
            y.parent.left = x
        else
            y.parent.right = x
        end
    end
    if truth0(((y !== z)))
        if truth0(((y.color === C__Color__BLACK)))
            m__remove_fixup(self, x, x_parent)
        end
        y.parent = z.parent
        y.color = z.color
        y.left = z.left
        y.right = z.right
        if truth0(((z.left !== nothing)))
            z.left.parent = y
        end
        if truth0(((z.right !== nothing)))
            z.right.parent = y
        end
        if truth0(((z.parent !== nothing)))
            if truth0(((z.parent.left == z)))
                z.parent.left = y
            else
                z.parent.right = y
            end
        else
            self._root = y
        end
    else
        if truth0(((y.color === C__Color__BLACK)))
            m__remove_fixup(self, x, x_parent)
        end
    end
    return z.value
end

function m_get(self::A__RedBlackTree, key)
    local node
    node = m__find_node(self, key)
    if truth0(((node === nothing)))
        return nothing
    end
    return node.value
end

function m_for_each(self::A__RedBlackTree, fn)
    local current
    if truth0(((self._root === nothing)))
        return nothing
    end
    current = _tree_minimum(self._root)
    while truth0(((current !== nothing)))
        fn(C__Entry(current.key, current.value))
        current = m_successor(current)
    end
    return nothing
end

function m__find_node(self::A__RedBlackTree, key)
    local comparison_result, current
    current = self._root
    while truth0(((current !== nothing)))
        comparison_result = m_compare_to(key, current.key)
        if truth0(((comparison_result == 0)))
            return current
        end
        if truth0(((comparison_result < 0)))
            current = current.left
        else
            current = current.right
        end
    end
    return nothing
end

function m__tree_insert(self::A__RedBlackTree, key, value)
    local comparison_result, old_value, x, y, z
    y = nothing
    x = self._root
    while truth0(((x !== nothing)))
        y = x
        comparison_result = m_compare_to(key, x.key)
        if truth0(((comparison_result < 0)))
            x = x.left
        else
            if truth0(((comparison_result > 0)))
                x = x.right
            else
                old_value = x.value
                x.value = value
                return C__InsertResult(false, nothing, old_value)
            end
        end
    end
    z = C__Node(key, value)
    z.parent = y
    if truth0(((y === nothing)))
        self._root = z
    else
        if truth0(((m_compare_to(key, y.key) < 0)))
            y.left = z
        else
            y.right = z
        end
    end
    return C__InsertResult(true, z, nothing)
end

function m__left_rotate(self::A__RedBlackTree, x)
    local y
    y = x.right
    x.right = y.left
    if truth0(((y.left !== nothing)))
        y.left.parent = x
    end
    y.parent = x.parent
    if truth0(((x.parent === nothing)))
        self._root = y
    else
        if truth0(((x === x.parent.left)))
            x.parent.left = y
        else
            x.parent.right = y
        end
    end
    y.left = x
    x.parent = y
    return y
end

function m__right_rotate(self::A__RedBlackTree, y)
    local x
    x = y.left
    y.left = x.right
    if truth0(((x.right !== nothing)))
        x.right.parent = y
    end
    x.parent = y.parent
    if truth0(((y.parent === nothing)))
        self._root = x
    else
        if truth0(((y === y.parent.left)))
            y.parent.left = x
        else
            y.parent.right = x
        end
    end
    x.right = y
    y.parent = x
    return x
end

function m__remove_fixup(self::A__RedBlackTree, x, x_parent)
    local w
    while truth0((let _bool_value = ((x !== self._root)); truth0(_bool_value) ? (let _bool_value = ((x === nothing)); truth0(_bool_value) ? _bool_value : ((x.color === C__Color__BLACK)) end) : _bool_value end))
        if truth0(((x === x_parent.left)))
            w = x_parent.right
            if truth0(((w.color === C__Color__RED)))
                w.color = C__Color__BLACK
                x_parent.color = C__Color__RED
                m__left_rotate(self, x_parent)
                w = x_parent.right
            end
            if truth0((let _bool_value = (let _bool_value = ((w.left === nothing)); truth0(_bool_value) ? _bool_value : ((w.left.color === C__Color__BLACK)) end); truth0(_bool_value) ? (let _bool_value = ((w.right === nothing)); truth0(_bool_value) ? _bool_value : ((w.right.color === C__Color__BLACK)) end) : _bool_value end))
                w.color = C__Color__RED
                x = x_parent
                x_parent = x.parent
            else
                if truth0((let _bool_value = ((w.right === nothing)); truth0(_bool_value) ? _bool_value : ((w.right.color === C__Color__BLACK)) end))
                    w.left.color = C__Color__BLACK
                    w.color = C__Color__RED
                    m__right_rotate(self, w)
                    w = x_parent.right
                end
                w.color = x_parent.color
                x_parent.color = C__Color__BLACK
                if truth0(((w.right !== nothing)))
                    w.right.color = C__Color__BLACK
                end
                m__left_rotate(self, x_parent)
                x = self._root
                x_parent = x.parent
            end
        else
            w = x_parent.left
            if truth0(((w.color === C__Color__RED)))
                w.color = C__Color__BLACK
                x_parent.color = C__Color__RED
                m__right_rotate(self, x_parent)
                w = x_parent.left
            end
            if truth0((let _bool_value = (let _bool_value = ((w.right === nothing)); truth0(_bool_value) ? _bool_value : ((w.right.color === C__Color__BLACK)) end); truth0(_bool_value) ? (let _bool_value = ((w.left === nothing)); truth0(_bool_value) ? _bool_value : ((w.left.color === C__Color__BLACK)) end) : _bool_value end))
                w.color = C__Color__RED
                x = x_parent
                x_parent = x.parent
            else
                if truth0((let _bool_value = ((w.left === nothing)); truth0(_bool_value) ? _bool_value : ((w.left.color === C__Color__BLACK)) end))
                    w.right.color = C__Color__BLACK
                    w.color = C__Color__RED
                    m__left_rotate(self, w)
                    w = x_parent.left
                end
                w.color = x_parent.color
                x_parent.color = C__Color__BLACK
                if truth0(((w.left !== nothing)))
                    w.left.color = C__Color__BLACK
                end
                m__right_rotate(self, x_parent)
                x = self._root
                x_parent = x.parent
            end
        end
    end
    if truth0(((x !== nothing)))
        x.color = C__Color__BLACK
    end
    return nothing
end

abstract type A__CallSign end
mutable struct C__CallSign <: A__CallSign
    _value
    C__CallSign(::Val{:raw}) = new(nothing)
end
function C__CallSign(args...)
    self = C__CallSign(Val(:raw))
    init__CallSign(self, args...)
    return self
end

function init__CallSign(self, value)
    self._value = value
    return nothing
end

function m_compare_to(self::A__CallSign, other)
    if truth0(((self._value == other._value)))
        return 0
    end
    if truth0(((self._value < other._value)))
        return -(1)
    end
    return 1
end

abstract type A__Collision end
mutable struct C__Collision <: A__Collision
    aircraft_a
    aircraft_b
    position
    C__Collision(::Val{:raw}) = new(nothing, nothing, nothing)
end
function C__Collision(args...)
    self = C__Collision(Val(:raw))
    init__Collision(self, args...)
    return self
end

function init__Collision(self, aircraft_a, aircraft_b, position)
    self.aircraft_a = aircraft_a
    self.aircraft_b = aircraft_b
    self.position = position
    return nothing
end

abstract type A__CollisionDetector end
mutable struct C__CollisionDetector <: A__CollisionDetector
    _state
    C__CollisionDetector(::Val{:raw}) = new(nothing)
end
function C__CollisionDetector(args...)
    self = C__CollisionDetector(Val(:raw))
    init__CollisionDetector(self, args...)
    return self
end

function init__CollisionDetector(self)
    self._state = C__RedBlackTree()
    return nothing
end

function m_handle_new_frame(self::A__CollisionDetector, frame)
    local all_reduced, collisions, motions, seen, to_remove
    motions = C_Vector()
    seen = C__RedBlackTree()
    function each(aircraft)
        local new_position, old_position
        old_position = m_put(self._state, aircraft.call_sign, aircraft.position)
        new_position = aircraft.position
        m_put(seen, aircraft.call_sign, true)
        if truth0(((old_position === nothing)))
            old_position = new_position
        end
        m_append(motions, C__Motion(aircraft.call_sign, old_position, new_position))
        return nothing
    end

    m_for_each(frame, each)
    to_remove = C_Vector()
    function for_removal(e)
        if truth0(!truth0(m_get(seen, e.key)))
            m_append(to_remove, e.key)
        end
        return nothing
    end

    m_for_each(self._state, for_removal)
    m_for_each(to_remove, (args...)->m_remove(self._state, args...))
    all_reduced = _reduce_collision_set(motions)
    collisions = C_Vector()
    function find_collisions(reduced)
        local collision, i, j, motion1, motion2
        for i in range0(m_size(reduced))
            motion1 = m_at(reduced, i)
            for j in range0(add0(i, 1), m_size(reduced))
                motion2 = m_at(reduced, j)
                collision = m_find_intersection(motion1, motion2)
                if truth0(((collision !== nothing)))
                    m_append(collisions, C__Collision(motion1.call_sign, motion2.call_sign, collision))
                end
            end
        end
        return nothing
    end

    m_for_each(all_reduced, find_collisions)
    return collisions
end

const _inf_positive = Inf
const _inf_negative = -Inf
function _is_in_voxel(voxel, motion)
    local fin, high_x, high_y, init, low_x, low_y, r, v_s, v_x, v_y, x0, xv, y0, yv
    if truth0((let _bool_value = ((voxel.x > MAX_X)); truth0(_bool_value) ? _bool_value : (let _bool_value = ((voxel.x < MIN_X)); truth0(_bool_value) ? _bool_value : (let _bool_value = ((voxel.y > MAX_Y)); truth0(_bool_value) ? _bool_value : ((voxel.y < MIN_Y)) end) end) end))
        return false
    end
    init = motion.pos_one
    fin = motion.pos_two
    v_s = GOOD_VOXEL_SIZE
    r = (PROXIMITY_RADIUS / 2.0)
    v_x = voxel.x
    x0 = init.x
    xv = (fin.x - init.x)
    v_y = voxel.y
    y0 = init.y
    yv = (fin.y - init.y)
    if truth0(((xv == 0.0)))
        low_x = (truth0(((((v_x - r) - x0) < 0.0))) ? _inf_negative : _inf_positive)
        high_x = (truth0((((add0(add0(v_x, v_s), r) - x0) < 0.0))) ? _inf_negative : _inf_positive)
    else
        low_x = (((v_x - r) - x0) / xv)
        high_x = ((add0(add0(v_x, v_s), r) - x0) / xv)
    end
    if truth0(((xv < 0.0)))
        (low_x, high_x) = (high_x, low_x)
    end
    if truth0(((yv == 0.0)))
        low_y = (truth0(((((v_y - r) - y0) < 0.0))) ? _inf_negative : _inf_positive)
        high_y = (truth0((((add0(add0(v_y, v_s), r) - y0) < 0.0))) ? _inf_negative : _inf_positive)
    else
        low_y = (((v_y - r) - y0) / yv)
        high_y = ((add0(add0(v_y, v_s), r) - y0) / yv)
    end
    if truth0(((yv < 0.0)))
        (low_y, high_y) = (high_y, low_y)
    end
    return (let _bool_value = (let _bool_value = (let _bool_value = ((xv == 0.0)); truth0(_bool_value) ? (let _bool_value = ((v_x <= add0(x0, r))); truth0(_bool_value) ? (((x0 - r) <= add0(v_x, v_s))) : _bool_value end) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = (let _bool_value = ((low_x <= 1.0)); truth0(_bool_value) ? ((1.0 <= high_x)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = (let _bool_value = ((low_x <= 0.0)); truth0(_bool_value) ? ((0.0 <= high_x)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = ((0.0 <= low_x)); truth0(_bool_value) ? ((high_x <= 1.0)) : _bool_value end) end) end) end); truth0(_bool_value) ? (let _bool_value = (let _bool_value = (let _bool_value = ((yv == 0.0)); truth0(_bool_value) ? (let _bool_value = ((v_y <= add0(y0, r))); truth0(_bool_value) ? (((y0 - r) <= add0(v_y, v_s))) : _bool_value end) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = (let _bool_value = ((low_y <= 1.0)); truth0(_bool_value) ? ((1.0 <= high_y)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = (let _bool_value = ((low_y <= 0.0)); truth0(_bool_value) ? ((0.0 <= high_y)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = ((0.0 <= low_y)); truth0(_bool_value) ? ((high_y <= 1.0)) : _bool_value end) end) end) end); truth0(_bool_value) ? (let _bool_value = ((xv == 0.0)); truth0(_bool_value) ? _bool_value : (let _bool_value = ((yv == 0.0)); truth0(_bool_value) ? _bool_value : (let _bool_value = (let _bool_value = ((low_y <= high_x)); truth0(_bool_value) ? ((high_x <= high_y)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = (let _bool_value = ((low_y <= low_x)); truth0(_bool_value) ? ((low_x <= high_y)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = ((low_x <= low_y)); truth0(_bool_value) ? ((high_y <= high_x)) : _bool_value end) end) end) end) end) : _bool_value end) : _bool_value end)
end

function _put_into_map(voxel_map, voxel, motion)
    local array
    array = m_get(voxel_map, voxel)
    if truth0(((array === nothing)))
        array = C_Vector()
        m_put(voxel_map, voxel, array)
    end
    m_append(array, motion)
    return nothing
end

function _recurse(voxel_map, seen, next_voxel, motion)
    if truth0(!truth0(_is_in_voxel(next_voxel, motion)))
        return nothing
    end
    if truth0(m_put(seen, next_voxel, true))
        return nothing
    end
    _put_into_map(voxel_map, next_voxel, motion)
    _recurse(voxel_map, seen, m_minus(next_voxel, _horizontal), motion)
    _recurse(voxel_map, seen, m_plus(next_voxel, _horizontal), motion)
    _recurse(voxel_map, seen, m_minus(next_voxel, _vertical), motion)
    _recurse(voxel_map, seen, m_plus(next_voxel, _vertical), motion)
    _recurse(voxel_map, seen, m_minus(m_minus(next_voxel, _horizontal), _vertical), motion)
    _recurse(voxel_map, seen, m_plus(m_minus(next_voxel, _horizontal), _vertical), motion)
    _recurse(voxel_map, seen, m_minus(m_plus(next_voxel, _horizontal), _vertical), motion)
    _recurse(voxel_map, seen, m_plus(m_plus(next_voxel, _horizontal), _vertical), motion)
    return nothing
end

function _reduce_collision_set(motions)
    local result, voxel_map
    voxel_map = C__RedBlackTree()
    m_for_each(motions, (motion)->_draw_motion_on_voxel_map(voxel_map, motion))
    result = C_Vector()
    function each(e)
        if truth0(((m_size(e.value) > 1)))
            m_append(result, e.value)
        end
        return nothing
    end

    m_for_each(voxel_map, each)
    return result
end

function _voxel_hash(position)
    local x, x_div, y, y_div
    x_div = fld(position.x, GOOD_VOXEL_SIZE)
    y_div = fld(position.y, GOOD_VOXEL_SIZE)
    x = mul0(GOOD_VOXEL_SIZE, x_div)
    y = mul0(GOOD_VOXEL_SIZE, y_div)
    if truth0(((position.x < 0.0)))
        x = (x - GOOD_VOXEL_SIZE)
    end
    if truth0(((position.y < 0.0)))
        y = (y - GOOD_VOXEL_SIZE)
    end
    return C__Vector2D(x, y)
end

function _draw_motion_on_voxel_map(voxel_map, motion)
    local seen
    seen = C__RedBlackTree()
    _recurse(voxel_map, seen, _voxel_hash(motion.pos_one), motion)
    return nothing
end

abstract type A__Motion end
mutable struct C__Motion <: A__Motion
    call_sign
    pos_one
    pos_two
    C__Motion(::Val{:raw}) = new(nothing, nothing, nothing)
end
function C__Motion(args...)
    self = C__Motion(Val(:raw))
    init__Motion(self, args...)
    return self
end

function init__Motion(self, call_sign, pos_one, pos_two)
    self.call_sign = call_sign
    self.pos_one = pos_one
    self.pos_two = pos_two
    return nothing
end

function m_delta(self::A__Motion)
    return m_minus(self.pos_two, self.pos_one)
end

function m_find_intersection(self::A__Motion, other)
    local a, b, c, discr, dist, init1, init2, radius, result, result1, result2, v, v1, v2, vec1, vec2
    init1 = self.pos_one
    init2 = other.pos_one
    vec1 = m_delta(self)
    vec2 = m_delta(other)
    radius = PROXIMITY_RADIUS
    a = m_squared_magnitude(m_minus(vec2, vec1))
    if truth0(((a != 0.0)))
        b = mul0(2.0, m_dot(m_minus(init1, init2), m_minus(vec1, vec2)))
        c = add0(mul0(-(radius), radius), m_squared_magnitude(m_minus(init2, init1)))
        discr = (mul0(b, b) - mul0(mul0(4.0, a), c))
        if truth0(((discr < 0.0)))
            return nothing
        end
        v1 = ((-(b) - sqrt(discr)) / mul0(2.0, a))
        v2 = (add0(-(b), sqrt(discr)) / mul0(2.0, a))
        if truth0((let _bool_value = ((v1 <= v2)); truth0(_bool_value) ? (let _bool_value = (let _bool_value = ((v1 <= 1.0)); truth0(_bool_value) ? ((1.0 <= v2)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = (let _bool_value = ((v1 <= 0.0)); truth0(_bool_value) ? ((0.0 <= v2)) : _bool_value end); truth0(_bool_value) ? _bool_value : (let _bool_value = ((0.0 <= v1)); truth0(_bool_value) ? ((v2 <= 1.0)) : _bool_value end) end) end) : _bool_value end))
            if truth0(((v1 <= 0.0)))
                v = 0.0
            else
                v = v1
            end
            result1 = m_plus(init1, m_times(vec1, v))
            result2 = m_plus(init2, m_times(vec2, v))
            result = m_times(m_plus(result1, result2), 0.5)
            if truth0((let _bool_value = ((result.x >= MIN_X)); truth0(_bool_value) ? (let _bool_value = ((result.x <= MAX_X)); truth0(_bool_value) ? (let _bool_value = ((result.y >= MIN_Y)); truth0(_bool_value) ? (let _bool_value = ((result.y <= MAX_Y)); truth0(_bool_value) ? (let _bool_value = ((result.z >= MIN_Z)); truth0(_bool_value) ? ((result.z <= MAX_Z)) : _bool_value end) : _bool_value end) : _bool_value end) : _bool_value end) : _bool_value end))
                return result
            end
        end
        return nothing
    end
    dist = m_magnitude(m_minus(init2, init1))
    if truth0(((dist <= radius)))
        return m_times(m_plus(init1, init2), 0.5)
    end
    return nothing
end

abstract type A__Aircraft end
mutable struct C__Aircraft <: A__Aircraft
    call_sign
    position
    C__Aircraft(::Val{:raw}) = new(nothing, nothing)
end
function C__Aircraft(args...)
    self = C__Aircraft(Val(:raw))
    init__Aircraft(self, args...)
    return self
end

function init__Aircraft(self, call_sign, position)
    self.call_sign = call_sign
    self.position = position
    return nothing
end

abstract type A__Simulator end
mutable struct C__Simulator <: A__Simulator
    _aircraft
    C__Simulator(::Val{:raw}) = new(nothing)
end
function C__Simulator(args...)
    self = C__Simulator(Val(:raw))
    init__Simulator(self, args...)
    return self
end

function init__Simulator(self, num_aircraft)
    local i
    self._aircraft = C_Vector()
    for i in range0(num_aircraft)
        m_append(self._aircraft, C__CallSign(i))
    end
    return nothing
end

function m_simulate(self::A__Simulator, time)
    local frame, i
    frame = C_Vector()
    for i in range0(0, m_size(self._aircraft), 2)
        m_append(frame, C__Aircraft(m_at(self._aircraft, i), C__Vector3D(time, add0(mul0(cos(time), 2.0), mul0(i, 3.0)), 10.0)))
        m_append(frame, C__Aircraft(m_at(self._aircraft, add0(i, 1)), C__Vector3D(time, add0(mul0(sin(time), 2.0), mul0(i, 3.0)), 10.0)))
    end
    return frame
end

abstract type A_CD <: A_Benchmark end
mutable struct C_CD <: A_CD
    C_CD(::Val{:raw}) = new()
end
function C_CD(args...)
    self = C_CD(Val(:raw))
    return self
end

function C_CD___benchmark(num_aircraft)
    local actual_collisions, collisions, detector, i, num_frames, simulator, time
    num_frames = 200
    simulator = C__Simulator(num_aircraft)
    detector = C__CollisionDetector()
    actual_collisions = 0
    for i in range0(num_frames)
        time = (i / 10.0)
        collisions = m_handle_new_frame(detector, m_simulate(simulator, time))
        actual_collisions = add0(actual_collisions, m_size(collisions))
    end
    return actual_collisions
end

function m_inner_benchmark_loop(self::A_CD, inner_iterations)
    return C_CD___verify_result(C_CD___benchmark(inner_iterations), inner_iterations)
end

function C_CD___verify_result(actual_collisions, num_aircraft)
    if truth0(((num_aircraft == 1000)))
        return ((actual_collisions == 14484))
    end
    if truth0(((num_aircraft == 500)))
        return ((actual_collisions == 14484))
    end
    if truth0(((num_aircraft == 250)))
        return ((actual_collisions == 10830))
    end
    if truth0(((num_aircraft == 200)))
        return ((actual_collisions == 8655))
    end
    if truth0(((num_aircraft == 100)))
        return ((actual_collisions == 4305))
    end
    if truth0(((num_aircraft == 10)))
        return ((actual_collisions == 390))
    end
    if truth0(((num_aircraft == 2)))
        return ((actual_collisions == 42))
    end
    println(add0(add0("No verification result for ", string(num_aircraft)), " found"))
    println(add0("Result is: ", string(actual_collisions)))
    return false
end

function m_benchmark(self::A_CD)
    throw(ErrorException("Should never be reached"))
end

function m_verify_result(self::A_CD, result)
    throw(ErrorException("Should never be reached"))
end


include("../class_support.jl")
# port of test/benchmark/awfy/python/benchmark.py; algorithms retain their original control flow.
# This code is based on the SOM class library.
#
# Copyright (c) 2001-2021 see AUTHORS.md file
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
abstract type A_Benchmark end
mutable struct C_Benchmark <: A_Benchmark
    C_Benchmark(::Val{:raw}) = new()
end
function C_Benchmark(args...)
    self = C_Benchmark(Val(:raw))
    return self
end

function m_benchmark(self::A_Benchmark)
    nothing
    return nothing
end

function m_verify_result(self::A_Benchmark, result)
    nothing
    return nothing
end

function m_inner_benchmark_loop(self::A_Benchmark, inner_iterations)
    local _
    for _ in range0(inner_iterations)
        if truth0(!truth0(m_verify_result(self, m_benchmark(self))))
            return false
        end
    end
    return true
end

# port of test/benchmark/awfy/python/som/constants.py; algorithms retain their original control flow.
# This code is based on the SOM class library.
#
# Copyright (c) 2001-2021 see AUTHORS.md file
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
const INITIAL_SIZE = 10
# port of test/benchmark/awfy/python/som/random.py; algorithms retain their original control flow.

abstract type A_Random end
mutable struct C_Random <: A_Random
    _seed::Int
    C_Random(::Val{:raw}) = new(0)
end
function C_Random(args...)
    self = C_Random(Val(:raw))
    init_Random(self, args...)
    return self
end

function init_Random(self)
    self._seed = 74755
    return nothing
end

function m_next(self::A_Random)
    self._seed = (add0(mul0(self._seed, 1309), 13849) & 65535)
    return self._seed
end

# port of test/benchmark/awfy/python/som/vector.py; algorithms retain their original control flow.
# This code is based on the SOM class library.
#
# Copyright (c) 2001-2021 see AUTHORS.md file
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
function vector_with(elem)
    local v
    v = C_Vector(1)
    m_append(v, elem)
    return v
end

abstract type A_Vector end
mutable struct C_Vector <: A_Vector
    _storage::Union{Nothing,Vector{Any}}
    _first_idx::Int
    _last_idx::Int
    C_Vector(::Val{:raw}) = new(nothing, 0, 0)
end
function C_Vector(args...)
    self = C_Vector(Val(:raw))
    init_Vector(self, args...)
    return self
end

function init_Vector(self, size=0)
    self._storage = (truth0(((size == 0))) ? nothing : mul0(Any[nothing], size))
    self._first_idx = 0
    self._last_idx = 0
    return nothing
end

function m_at(self::A_Vector, idx)
    if truth0((let _bool_value = ((self._storage === nothing)); truth0(_bool_value) ? _bool_value : ((idx >= length(self._storage))) end))
        return nothing
    end
    return get0(self._storage, idx)
end

function m_at_put(self::A_Vector, idx, val)
    local i, new_length, new_storage
    if truth0(((self._storage === nothing)))
        self._storage = mul0(Any[nothing], max(add0(idx, 1), INITIAL_SIZE))
    else
        if truth0(((idx >= length(self._storage))))
            new_length = length(self._storage)
            while truth0(((new_length <= idx)))
                new_length = mul0(new_length, 2)
            end
            new_storage = mul0(Any[nothing], new_length)
            for i in range0(length(self._storage))
                set0!(new_storage, i, get0(self._storage, i))
            end
            self._storage = new_storage
        end
    end
    set0!(self._storage, idx, val)
    if truth0(((self._last_idx < add0(idx, 1))))
        self._last_idx = add0(idx, 1)
    end
    return nothing
end

function m_append(self::A_Vector, elem)
    local i, new_storage
    if truth0(((self._storage === nothing)))
        self._storage = mul0(Any[nothing], INITIAL_SIZE)
    else
        if truth0(((self._last_idx >= length(self._storage))))
            new_storage = mul0(Any[nothing], mul0(2, length(self._storage)))
            for i in range0(length(self._storage))
                set0!(new_storage, i, get0(self._storage, i))
            end
            self._storage = new_storage
        end
    end
    set0!(self._storage, self._last_idx, elem)
    self._last_idx = add0(self._last_idx, 1)
    return nothing
end

function m_is_empty(self::A_Vector)
    return ((self._last_idx == self._first_idx))
end

function m_for_each(self::A_Vector, fn)
    local i
    for i in range0(self._first_idx, self._last_idx)
        fn(get0(self._storage, i))
    end
    return nothing
end

function m_has_some(self::A_Vector, fn)
    local i
    for i in range0(self._first_idx, self._last_idx)
        if truth0(fn(get0(self._storage, i)))
            return true
        end
    end
    return false
end

function m_get_one(self::A_Vector, fn)
    local e, i
    for i in range0(self._first_idx, self._last_idx)
        e = get0(self._storage, i)
        if truth0(fn(e))
            return e
        end
    end
    return nothing
end

function m_first(self::A_Vector)
    if truth0(m_is_empty(self))
        return nothing
    end
    return get0(self._storage, self._first_idx)
end

function m_remove_first(self::A_Vector)
    if truth0(m_is_empty(self))
        return nothing
    end
    self._first_idx = add0(self._first_idx, 1)
    return get0(self._storage, (self._first_idx - 1))
end

function m_remove(self::A_Vector, obj)
    local found, new_array, new_last
    if truth0((let _bool_value = ((self._storage === nothing)); truth0(_bool_value) ? _bool_value : m_is_empty(self) end))
        return false
    end
    new_array = mul0(Any[nothing], m_capacity(self))
    new_last = 0
    found = false
    function each(it)
        if truth0(((it === obj)))
            found = true
        else
            set0!(new_array, new_last, it)
            new_last = add0(new_last, 1)
        end
        return nothing
    end

    m_for_each(self, each)
    self._storage = new_array
    self._last_idx = new_last
    self._first_idx = 0
    return found
end

function m_remove_all(self::A_Vector)
    self._first_idx = 0
    self._last_idx = 0
    if truth0(((self._storage !== nothing)))
        self._storage = mul0(Any[nothing], length(self._storage))
    end
    return nothing
end

function m_size(self::A_Vector)
    return (self._last_idx - self._first_idx)
end

function m_capacity(self::A_Vector)
    return (truth0(((self._storage === nothing))) ? 0 : length(self._storage))
end

function m_sort(self::A_Vector, comparator)
    if truth0(((m_size(self) > 0)))
        m__sort(self, self._first_idx, (self._last_idx - 1), comparator)
    end
    return nothing
end

function m__sort(self::A_Vector, i, j, c)
    local di, dij, dj, ij, k, l, n
    if truth0(((c === nothing)))
        m__default_sort(self, i, j)
    end
    n = (add0(j, 1) - i)
    if truth0(((n <= 1)))
        return nothing
    end
    di = get0(self._storage, i)
    dj = get0(self._storage, j)
    if truth0(((m_compare(c, di, dj) > 0)))
        m__swap(self, self._storage, i, j)
        (di, dj) = (dj, di)
    end
    if truth0(((n > 2)))
        ij = fld(add0(i, j), 2)
        dij = get0(self._storage, ij)
        if truth0(((m_compare(c, di, dij) <= 0)))
            if truth0(((m_compare(c, dij, dj) > 0)))
                m__swap(self, self._storage, j, ij)
                dij = dj
            end
        else
            m__swap(self, self._storage, i, ij)
            dij = di
        end
        if truth0(((n > 3)))
            k = i
            l = (j - 1)
            while truth0(true)
                while truth0((let _bool_value = ((k <= l)); truth0(_bool_value) ? ((m_compare(c, dij, get0(self._storage, l)) <= 0)) : _bool_value end))
                    l = (l - 1)
                end
                k = add0(k, 1)
                while truth0((let _bool_value = ((k <= l)); truth0(_bool_value) ? ((m_compare(c, get0(self._storage, k), dij) <= 0)) : _bool_value end))
                    k = add0(k, 1)
                end
                if truth0(((k > l)))
                    break
                end
                m__swap(self, self._storage, k, l)
            end
            m__sort(self, i, l, c)
            m__sort(self, k, j, c)
        end
    end
    return nothing
end

function m__swap(self::A_Vector, storage, i, j)
    throw(ErrorException("not implemented"))
end

function m__default_sort(self::A_Vector, i, j)
    throw(ErrorException("not implemented"))
end

# port of test/benchmark/awfy/python/som/set.py; algorithms retain their original control flow.
# This code is based on the SOM class library.
#
# Copyright (c) 2001-2021 see AUTHORS.md file
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
abstract type A_Set end
mutable struct C_Set <: A_Set
    _items
    C_Set(::Val{:raw}) = new(nothing)
end
function C_Set(args...)
    self = C_Set(Val(:raw))
    init_Set(self, args...)
    return self
end

function init_Set(self, size=INITIAL_SIZE)
    self._items = C_Vector(size)
    return nothing
end

function m_size(self::A_Set)
    return m_size(self._items)
end

function m_for_each(self::A_Set, block)
    m_for_each(self._items, block)
    return nothing
end

function m_has_some(self::A_Set, block)
    return m_has_some(self._items, block)
    return nothing
end

function m_get_one(self::A_Set, block)
    return m_get_one(self._items, block)
    return nothing
end

function m_add(self::A_Set, obj)
    if truth0(!truth0(m_contains(self, obj)))
        m_append(self._items, obj)
    end
    return nothing
end

function m_collect(self::A_Set, block)
    local coll
    coll = C_Vector()
    m_for_each(self, (e)->m_append(coll, block(e)))
    return coll
end

function m_contains(self::A_Set, obj)
    return m_has_some(self, (it)->((it == obj)))
end

# port of test/benchmark/awfy/python/som/identity_set.py; algorithms retain their original control flow.
# This code is based on the SOM class library.
#
# Copyright (c) 2001-2021 see AUTHORS.md file
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
abstract type A_IdentitySet <: A_Set end
mutable struct C_IdentitySet <: A_IdentitySet
    _items
    C_IdentitySet(::Val{:raw}) = new(nothing)
end
function C_IdentitySet(args...)
    self = C_IdentitySet(Val(:raw))
    init_Set(self, args...)
    return self
end

function m_contains(self::A_IdentitySet, obj)
    return m_has_some(self, (it)->((it === obj)))
end

# port of test/benchmark/awfy/python/som/dictionary.py; algorithms retain their original control flow.
# This code is based on the SOM class library.
#
# Copyright (c) 2001-2016 see AUTHORS.md file
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
const _INITIAL_CAPACITY = 16
abstract type A_Entry end
mutable struct C_Entry <: A_Entry
    hash
    key
    value
    next
    C_Entry(::Val{:raw}) = new(nothing, nothing, nothing, nothing)
end
function C_Entry(args...)
    self = C_Entry(Val(:raw))
    init_Entry(self, args...)
    return self
end

function init_Entry(self, hash_, key, value, next_)
    self.hash = hash_
    self.key = key
    self.value = value
    self.next = next_
    return nothing
end

function m_match(self::A_Entry, hash_, key)
    return (let _bool_value = ((self.hash == hash_)); truth0(_bool_value) ? ((key == self.key)) : _bool_value end)
end

function _hash(key)
    local hash_
    if truth0(((key === nothing)))
        return 0
    end
    hash_ = m_custom_hash(key)
    return xor(hash_, (hash_ >> 16))
end

abstract type A_Dictionary end
mutable struct C_Dictionary <: A_Dictionary
    _buckets
    _size::Int
    C_Dictionary(::Val{:raw}) = new(nothing, 0)
end
function C_Dictionary(args...)
    self = C_Dictionary(Val(:raw))
    init_Dictionary(self, args...)
    return self
end

function init_Dictionary(self, size=_INITIAL_CAPACITY)
    self._buckets = mul0(Any[nothing], size)
    self._size = 0
    return nothing
end

function m_size(self::A_Dictionary)
    return self._size
end

function m_is_empty(self::A_Dictionary)
    return ((self._size == 0))
end

function m__get_bucket_idx(self::A_Dictionary, hash_)
    return ((length(self._buckets) - 1) & hash_)
end

function m__get_bucket(self::A_Dictionary, hash_)
    return get0(self._buckets, m__get_bucket_idx(self, hash_))
end

function m_at(self::A_Dictionary, key)
    local e, hash_
    hash_ = _hash(key)
    e = m__get_bucket(self, hash_)
    while truth0(((e !== nothing)))
        if truth0(m_match(e, hash_, key))
            return e.value
        end
        e = e.next
    end
    return nothing
end

function m_contains_key(self::A_Dictionary, key)
    local e, hash_
    hash_ = _hash(key)
    e = m__get_bucket(self, hash_)
    while truth0(((e !== nothing)))
        if truth0(m_match(e, hash_, key))
            return true
        end
        e = e.next
    end
    return false
end

function m_at_put(self::A_Dictionary, key, value)
    local current, hash_, i
    hash_ = _hash(key)
    i = m__get_bucket_idx(self, hash_)
    current = get0(self._buckets, i)
    if truth0(((current === nothing)))
        set0!(self._buckets, i, m__new_entry(self, key, value, hash_))
        self._size = add0(self._size, 1)
    else
        m__insert_bucket_entry(self, key, value, hash_, current)
    end
    if truth0(((self._size > length(self._buckets))))
        m__resize(self)
    end
    return nothing
end

function m__new_entry(self::A_Dictionary, key, value, hash_)
    return C_Entry(hash_, key, value, nothing)
end

function m__insert_bucket_entry(self::A_Dictionary, key, value, hash_, head)
    local current
    current = head
    while truth0(true)
        if truth0(m_match(current, hash_, key))
            current.value = value
            return nothing
        end
        if truth0(((current.next === nothing)))
            self._size = add0(self._size, 1)
            current.next = m__new_entry(self, key, value, hash_)
            return nothing
        end
        current = current.next
    end
    return nothing
end

function m__resize(self::A_Dictionary)
    local new_storage, old_storage
    old_storage = self._buckets
    new_storage = mul0(mul0(Any[nothing], length(old_storage)), 2)
    self._buckets = new_storage
    m__transfer_entries(self, old_storage)
    return nothing
end

function m__transfer_entries(self::A_Dictionary, old_storage)
    local current, i
    for i in range0(length(old_storage))
        current = get0(old_storage, i)
        if truth0(((current !== nothing)))
            set0!(old_storage, i, nothing)
            if truth0(((current.next === nothing)))
                set0!(self._buckets, (current.hash & (length(self._buckets) - 1)), current)
            else
                m__split_bucket(self, old_storage, i, current)
            end
        end
    end
    return nothing
end

function m__split_bucket(self::A_Dictionary, old_storage, i, head)
    local current, hi_head, hi_tail, lo_head, lo_tail
    lo_head = nothing
    lo_tail = nothing
    hi_head = nothing
    hi_tail = nothing
    current = head
    while truth0(((current !== nothing)))
        if truth0((((current.hash & length(old_storage)) == 0)))
            if truth0(((lo_tail === nothing)))
                lo_head = current
            else
                lo_tail.next = current
            end
            lo_tail = current
        else
            if truth0(((hi_tail === nothing)))
                hi_head = current
            else
                hi_tail.next = current
            end
            hi_tail = current
        end
        current = current.next
    end
    if truth0(((lo_tail !== nothing)))
        lo_tail.next = nothing
        set0!(self._buckets, i, lo_head)
    end
    if truth0(((hi_tail !== nothing)))
        hi_tail.next = nothing
        set0!(self._buckets, add0(i, length(old_storage)), hi_head)
    end
    return nothing
end

function m_remove_all(self::A_Dictionary)
    self._buckets = mul0(Any[nothing], length(self._buckets))
    self._size = 0
    return nothing
end

function m_get_keys(self::A_Dictionary)
    local current, i, keys
    keys = C_Vector(self._size)
    for i in range0(length(self._buckets))
        current = get0(self._buckets, i)
        while truth0(((current !== nothing)))
            m_append(keys, current.key)
            current = current.next
        end
    end
    return keys
end

function m_get_values(self::A_Dictionary)
    local current, i, values
    values = C_Vector(self._size)
    for i in range0(length(self._buckets))
        current = get0(self._buckets, i)
        while truth0(((current !== nothing)))
            m_append(values, current.value)
            current = current.next
        end
    end
    return values
end

# port of test/benchmark/awfy/python/som/identity_dictionary.py; algorithms retain their original control flow.

abstract type A__IdEntry <: A_Entry end
mutable struct C__IdEntry <: A__IdEntry
    hash
    key
    value
    next
    C__IdEntry(::Val{:raw}) = new(nothing, nothing, nothing, nothing)
end
function C__IdEntry(args...)
    self = C__IdEntry(Val(:raw))
    init_Entry(self, args...)
    return self
end

function m_match(self::A__IdEntry, hash_, key)
    return (let _bool_value = ((self.hash == hash_)); truth0(_bool_value) ? ((self.key === key)) : _bool_value end)
end

abstract type A_IdentityDictionary <: A_Dictionary end
mutable struct C_IdentityDictionary <: A_IdentityDictionary
    _buckets
    _size::Int
    C_IdentityDictionary(::Val{:raw}) = new(nothing, 0)
end
function C_IdentityDictionary(args...)
    self = C_IdentityDictionary(Val(:raw))
    init_Dictionary(self, args...)
    return self
end

function m__new_entry(self::A_IdentityDictionary, key, value, hash_)
    return C__IdEntry(hash_, key, value, nothing)
end

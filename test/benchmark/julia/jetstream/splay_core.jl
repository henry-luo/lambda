# port of test/benchmark/jetstream/splay.py; see ../LICENSE.md.
# JetStream Benchmark: splay (Octane) — Julia version
# Splay tree — self-balancing BST with frequent insert/delete
# Original: V8 project authors
# Measures allocation, GC pressure, and tree manipulation
#
const TREE_SIZE = 8000
const TREE_MODIFICATIONS = 80
abstract type A_SplayNode end
mutable struct C_SplayNode <: A_SplayNode
    key::Float64
    left::Union{Nothing,C_SplayNode}
    right::Union{Nothing,C_SplayNode}
    value
    C_SplayNode(::Val{:raw}) = new(0.0, nothing, nothing, nothing)
end
function C_SplayNode(args...)
    self = C_SplayNode(Val(:raw))
    init_SplayNode(self, args...)
    return self
end

const C_SplayNode____slots__ = ["key", "left", "right", "value"]
function init_SplayNode(self, key, value)
    self.key = key
    self.left = nothing
    self.right = nothing
    self.value = value
    return nothing
end

function next_random(state)
    local hi, lo, s
    s = get0(state, 0)
    hi = fld(s, 127773)
    lo = mod(s, 127773)
    s = (mul0(16807, lo) - mul0(2836, hi))
    if truth0(((s <= 0)))
        s = add0(s, 2147483647)
    end
    set0!(state, 0, s)
    return (s / 2147483647.0)
end

function splay_is_empty(root)
    return ((root === nothing))
end

function splay(root, key)
    local current, done, dummy, left, right, tmp
    if truth0(((root === nothing)))
        return root
    end
    dummy = C_SplayNode(0.0, nothing)
    left = dummy
    right = dummy
    current = root
    done = false
    while truth0(!truth0(done))
        if truth0(((key < current.key)))
            if truth0(((current.left === nothing)))
                done = true
            else
                if truth0(((key < current.left.key)))
                    tmp = current.left
                    current.left = tmp.right
                    tmp.right = current
                    current = tmp
                    if truth0(((current.left === nothing)))
                        done = true
                    end
                end
                if truth0(!truth0(done))
                    right.left = current
                    right = current
                    current = current.left
                end
            end
        else
            if truth0(((key > current.key)))
                if truth0(((current.right === nothing)))
                    done = true
                else
                    if truth0(((key > current.right.key)))
                        tmp = current.right
                        current.right = tmp.left
                        tmp.left = current
                        current = tmp
                        if truth0(((current.right === nothing)))
                            done = true
                        end
                    end
                    if truth0(!truth0(done))
                        left.right = current
                        left = current
                        current = current.right
                    end
                end
            else
                done = true
            end
        end
    end
    left.right = current.left
    right.left = current.right
    current.left = dummy.right
    current.right = dummy.left
    return current
end

function splay_insert(root, key, value)
    local node
    if truth0(((root === nothing)))
        return C_SplayNode(key, value)
    end
    root = splay(root, key)
    if truth0(((root.key == key)))
        return root
    end
    node = C_SplayNode(key, value)
    if truth0(((key > root.key)))
        node.left = root
        node.right = root.right
        root.right = nothing
    else
        node.right = root
        node.left = root.left
        root.left = nothing
    end
    return node
end

function splay_remove(root, key)
    local removed, right_tree
    if truth0(((root === nothing)))
        return (root, nothing)
    end
    root = splay(root, key)
    if truth0(((root.key != key)))
        return (root, nothing)
    end
    removed = root
    if truth0(((root.left === nothing)))
        root = root.right
    else
        right_tree = root.right
        root = root.left
        root = splay(root, key)
        root.right = right_tree
    end
    return (root, removed)
end

function splay_find(root, key)
    if truth0(((root === nothing)))
        return (root, nothing)
    end
    root = splay(root, key)
    if truth0(((root.key == key)))
        return (root, root)
    end
    return (root, nothing)
end

function splay_find_max(node)
    local current
    current = node
    while truth0(((current.right !== nothing)))
        current = current.right
    end
    return current
end

function splay_find_greatest_less_than(root, key)
    if truth0(((root === nothing)))
        return (root, nothing)
    end
    root = splay(root, key)
    if truth0(((root.key < key)))
        return (root, root)
    end
    if truth0(((root.left !== nothing)))
        return (root, splay_find_max(root.left))
    end
    return (root, nothing)
end

function count_nodes(root)
    local count, node, stack
    count = 0
    stack = [root]
    while truth0(stack)
        node = m_pop(stack)
        if truth0(((node === nothing)))
            continue
        end
        count = add0(count, 1)
        m_append(stack, node.right)
        m_append(stack, node.left)
    end
    return count
end

function generate_payload(depth, tag)
    if truth0(((depth == 0)))
        return Dict{Any,Any}("arr"=>collect(range0(10)), "str"=>"String for key " * string(tag) * " in leaf node")
    end
    return Dict{Any,Any}("left_p"=>generate_payload((depth - 1), tag), "right_p"=>generate_payload((depth - 1), tag))
end

function insert_new_node(root, key_set, rng)
    local found, key, payload, root_tmp
    key = next_random(rng)
    (root_tmp, found) = splay_find(root, key)
    root = root_tmp
    while truth0(((found !== nothing)))
        key = next_random(rng)
        (root_tmp, found) = splay_find(root, key)
        root = root_tmp
    end
    payload = generate_payload(5, key)
    root = splay_insert(root, key, payload)
    return (root, key)
end


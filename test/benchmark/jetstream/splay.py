#!/usr/bin/env python3
"""JetStream Benchmark: splay (Octane) — Python version
Splay tree — self-balancing BST with frequent insert/delete
Original: V8 project authors
Measures allocation, GC pressure, and tree manipulation
"""
import time

TREE_SIZE = 8000
TREE_MODIFICATIONS = 80


class SplayNode:
    __slots__ = ['key', 'left', 'right', 'value']

    def __init__(self, key, value):
        self.key = key
        self.left = None
        self.right = None
        self.value = value


def next_random(state):
    s = state[0]
    hi = s // 127773
    lo = s % 127773
    s = 16807 * lo - 2836 * hi
    if s <= 0:
        s += 2147483647
    state[0] = s
    return s / 2147483647.0


def splay_is_empty(root):
    return root is None


def splay(root, key):
    if root is None:
        return root

    dummy = SplayNode(0.0, None)
    left = dummy
    right = dummy
    current = root
    done = False

    while not done:
        if key < current.key:
            if current.left is None:
                done = True
            else:
                if key < current.left.key:
                    # rotate right
                    tmp = current.left
                    current.left = tmp.right
                    tmp.right = current
                    current = tmp
                    if current.left is None:
                        done = True
                if not done:
                    # link right
                    right.left = current
                    right = current
                    current = current.left
        elif key > current.key:
            if current.right is None:
                done = True
            else:
                if key > current.right.key:
                    # rotate left
                    tmp = current.right
                    current.right = tmp.left
                    tmp.left = current
                    current = tmp
                    if current.right is None:
                        done = True
                if not done:
                    # link left
                    left.right = current
                    left = current
                    current = current.right
        else:
            done = True

    # assemble
    left.right = current.left
    right.left = current.right
    current.left = dummy.right
    current.right = dummy.left
    return current


def splay_insert(root, key, value):
    if root is None:
        return SplayNode(key, value)
    root = splay(root, key)
    if root.key == key:
        return root
    node = SplayNode(key, value)
    if key > root.key:
        node.left = root
        node.right = root.right
        root.right = None
    else:
        node.right = root
        node.left = root.left
        root.left = None
    return node


def splay_remove(root, key):
    if root is None:
        return root, None
    root = splay(root, key)
    if root.key != key:
        return root, None
    removed = root
    if root.left is None:
        root = root.right
    else:
        right_tree = root.right
        root = root.left
        root = splay(root, key)
        root.right = right_tree
    return root, removed


def splay_find(root, key):
    if root is None:
        return root, None
    root = splay(root, key)
    if root.key == key:
        return root, root
    return root, None


def splay_find_max(node):
    current = node
    while current.right is not None:
        current = current.right
    return current


def splay_find_greatest_less_than(root, key):
    if root is None:
        return root, None
    root = splay(root, key)
    if root.key < key:
        return root, root
    if root.left is not None:
        return root, splay_find_max(root.left)
    return root, None


def count_nodes(root):
    count = 0
    stack = [root]
    while stack:
        node = stack.pop()
        if node is None:
            continue
        count += 1
        stack.append(node.right)
        stack.append(node.left)
    return count


def generate_payload(depth, tag):
    if depth == 0:
        return {'arr': list(range(10)), 'str': tag}
    return {'left_p': generate_payload(depth - 1, tag),
            'right_p': generate_payload(depth - 1, tag)}


def insert_new_node(root, key_set, rng):
    key = next_random(rng)
    # avoid duplicate keys
    root_tmp, found = splay_find(root, key)
    root = root_tmp
    while found is not None:
        key = next_random(rng)
        root_tmp, found = splay_find(root, key)
        root = root_tmp
    payload = generate_payload(5, key)
    root = splay_insert(root, key, payload)
    return root, key


def run_splay():
    root = None
    rng = [49734321]  # mutable seed

    # Setup: insert TREE_SIZE nodes
    for _ in range(TREE_SIZE):
        root, _ = insert_new_node(root, None, rng)

    # Run: 50 × TREE_MODIFICATIONS insert/delete cycles
    for _ in range(50):
        for _ in range(TREE_MODIFICATIONS):
            root, key = insert_new_node(root, None, rng)
            root_tmp, greatest = splay_find_greatest_less_than(root, key)
            root = root_tmp
            if greatest is None:
                root, _ = splay_remove(root, key)
            else:
                root, _ = splay_remove(root, greatest.key)

    return count_nodes(root)


def main():
    t0 = time.perf_counter_ns()
    count = run_splay()
    t1 = time.perf_counter_ns()

    if count == TREE_SIZE:
        print(f"splay: PASS (nodes={count})")
    else:
        print(f"splay: FAIL (nodes={count}, expected {TREE_SIZE})")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""JetStream Benchmark: hash-map (simple) — Python version
Open-addressing hash table with linear probing (chaining via next index)
Original: Apache Harmony HashMap (manual translation), now to Python
Tests hash table insert, lookup, and iteration at scale
"""
import time
import math

DEFAULT_CAPACITY = 16
LOAD_FACTOR = 0.75
EMPTY = -1


def compute_hash(key):
    h = key
    h ^= h >> 16
    h = h * 73244475
    h ^= h >> 16
    if h < 0:
        h = -h
    return h


def hashmap_new(capacity):
    cap = DEFAULT_CAPACITY
    if capacity > cap:
        cap = 1
        while cap < capacity:
            cap *= 2
    return {
        'keys':   [EMPTY] * cap,
        'values': [0] * cap,
        'hashes': [0] * cap,
        'nexts':  [EMPTY] * cap,
        'heads':  [EMPTY] * cap,
        'size': 0,
        'cap': cap,
        'threshold': int(math.floor(cap * LOAD_FACTOR)),
    }


def hashmap_rehash(hm):
    old_cap = hm['cap']
    new_cap = old_cap * 2
    new_heads = [EMPTY] * new_cap
    size = hm['size']
    nexts = hm['nexts']
    hashes = hm['hashes']

    for i in range(size):
        nexts[i] = EMPTY
    for i in range(size):
        bucket = hashes[i] % new_cap
        nexts[i] = new_heads[bucket]
        new_heads[bucket] = i

    hm['cap'] = new_cap
    hm['heads'] = new_heads
    hm['threshold'] = int(math.floor(new_cap * LOAD_FACTOR))

    # grow arrays if needed
    if hm['size'] + new_cap > len(hm['keys']):
        new_size = new_cap * 2
        old_keys = hm['keys']
        old_vals = hm['values']
        old_hsh  = hm['hashes']
        old_nxt  = hm['nexts']
        new_keys = [EMPTY] * new_size
        new_vals = [0] * new_size
        new_hsh  = [0] * new_size
        new_nxt  = [EMPTY] * new_size
        for i in range(size):
            new_keys[i] = old_keys[i]
            new_vals[i] = old_vals[i]
            new_hsh[i]  = old_hsh[i]
            new_nxt[i]  = old_nxt[i]
        hm['keys']   = new_keys
        hm['values'] = new_vals
        hm['hashes'] = new_hsh
        hm['nexts']  = new_nxt


def hashmap_put(hm, key, value):
    h = compute_hash(key)
    bucket = h % hm['cap']
    keys   = hm['keys']
    values = hm['values']
    hashes = hm['hashes']
    nexts  = hm['nexts']
    heads  = hm['heads']

    idx = heads[bucket]
    while idx != EMPTY:
        if keys[idx] == key:
            old = values[idx]
            values[idx] = value
            return old
        idx = nexts[idx]

    slot = hm['size']
    keys[slot]   = key
    values[slot] = value
    hashes[slot] = h
    nexts[slot]  = heads[bucket]
    heads[bucket] = slot
    hm['size'] += 1

    if hm['size'] > hm['threshold']:
        hashmap_rehash(hm)
    return EMPTY


def hashmap_get(hm, key):
    h = compute_hash(key)
    bucket = h % hm['cap']
    heads  = hm['heads']
    keys   = hm['keys']
    values = hm['values']
    nexts  = hm['nexts']

    idx = heads[bucket]
    while idx != EMPTY:
        if keys[idx] == key:
            return values[idx]
        idx = nexts[idx]
    return EMPTY


def run():
    COUNT = 90000
    hm = hashmap_new(COUNT * 2)
    # pre-allocate arrays large enough
    enough = COUNT + 100
    hm['keys']   = [EMPTY] * enough
    hm['values'] = [0] * enough
    hm['hashes'] = [0] * enough
    hm['nexts']  = [EMPTY] * enough

    for i in range(COUNT):
        hashmap_put(hm, i, 42)

    result = 0
    for _ in range(5):
        for i in range(COUNT):
            result += hashmap_get(hm, i)

    key_sum = 0
    value_sum = 0
    hm_keys   = hm['keys']
    hm_values = hm['values']
    for i in range(hm['size']):
        key_sum   += hm_keys[i]
        value_sum += hm_values[i]

    expected_result    = 42 * COUNT * 5
    expected_key_sum   = (COUNT * (COUNT - 1)) // 2
    expected_value_sum = 42 * COUNT

    if result != expected_result:
        print(f"hash-map: FAIL result={result} expected={expected_result}")
        return False
    if key_sum != expected_key_sum:
        print(f"hash-map: FAIL keySum={key_sum} expected={expected_key_sum}")
        return False
    if value_sum != expected_value_sum:
        print(f"hash-map: FAIL valueSum={value_sum} expected={expected_value_sum}")
        return False
    return True


def main():
    t0 = time.perf_counter_ns()
    ok = run()
    t1 = time.perf_counter_ns()

    if ok:
        print("hash-map: PASS")
    else:
        print("hash-map: FAIL")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()

// JetStream Benchmark: hash-map (simple) — TYPED version
// Open-addressing hash table with linear probing
// Original: Apache Harmony HashMap (manual translation to JS), now to Lambda
// Tests hash table insert, lookup, and iteration at scale
// Type annotations enable MIR JIT Phase 3 direct byte-offset field access

// Type definition — field order MUST match map literal order in hashmap_new
type HashMap = {keys: array, values: array, hashes: array, nexts: array, heads: array, size: int, cap: int, threshold: int}

// Hash map using open addressing with arrays
// Buckets: array of {key, value, hash, next_idx} entries
// Separate chaining via next_idx linking

let DEFAULT_CAPACITY = 16
let LOAD_FACTOR = 0.75
let EMPTY = -1

pn compute_hash(key: int) int {
    // Java-style int hash
    var h = key
    h = bxor(h, shr(h, 16))
    h = h * 73244475  // approximate of multiply-shift hash
    h = bxor(h, shr(h, 16))
    if (h < 0) {
        h = 0 - h
    }
    return h
}

pn hashmap_new(capacity: int) {
    var cap = DEFAULT_CAPACITY
    if (capacity > cap) {
        // round up to power of 2
        cap = 1
        while (cap < capacity) {
            cap = cap * 2
        }
    }
    var hm: HashMap = {keys: fill(cap, EMPTY), values: fill(cap, 0),
            hashes: fill(cap, 0), nexts: fill(cap, EMPTY),
            heads: fill(cap, EMPTY),
            size: 0, cap: cap, threshold: int(floor(float(cap) * LOAD_FACTOR))}
    return hm
}

// Find a free slot in the flat arrays
pn find_free_slot(hm: HashMap) int {
    // Simply use size as the next index (compact storage)
    return hm.size
}

pn hashmap_put(hm: HashMap, key: int, value: int) {
    var hash: int = compute_hash(key)
    var bucket: int = hash % hm.cap
    var hm_keys = hm.keys
    var hm_values = hm.values
    var hm_hashes = hm.hashes
    var hm_nexts = hm.nexts
    var hm_heads = hm.heads
    // Search chain
    var idx = hm_heads[bucket]
    while (idx != EMPTY) {
        if (hm_keys[idx] == key) {
            var old = hm_values[idx]
            hm_values[idx] = value
            return old
        }
        idx = hm_nexts[idx]
    }
    // Insert new entry
    var slot = hm.size
    hm_keys[slot] = key
    hm_values[slot] = value
    hm_hashes[slot] = hash
    hm_nexts[slot] = hm_heads[bucket]
    hm_heads[bucket] = slot
    hm.size = hm.size + 1

    // Rehash if needed
    if (hm.size > hm.threshold) {
        hashmap_rehash(hm)
    }
    return EMPTY
}

pn hashmap_rehash(hm: HashMap) {
    var old_cap = hm.cap
    var new_cap = old_cap * 2
    var new_heads = fill(new_cap, EMPTY)
    var hm_nexts = hm.nexts
    var hm_hashes = hm.hashes
    // Reset nexts
    var i: int = 0
    while (i < hm.size) {
        hm_nexts[i] = EMPTY
        i = i + 1
    }
    // Reinsert all entries
    i = 0
    while (i < hm.size) {
        var bucket = hm_hashes[i] % new_cap
        hm_nexts[i] = new_heads[bucket]
        new_heads[bucket] = i
        i = i + 1
    }
    hm.cap = new_cap
    hm.heads = new_heads
    hm.threshold = int(floor(float(new_cap) * LOAD_FACTOR))
    // Grow arrays if needed
    var hm_keys = hm.keys
    var hm_values = hm.values
    if (hm.size + new_cap > len(hm_keys)) {
        var new_keys = fill(new_cap * 2, EMPTY)
        var new_values = fill(new_cap * 2, 0)
        var new_hashes2 = fill(new_cap * 2, 0)
        var new_nexts = fill(new_cap * 2, EMPTY)
        i = 0
        while (i < hm.size) {
            new_keys[i] = hm_keys[i]
            new_values[i] = hm_values[i]
            new_hashes2[i] = hm_hashes[i]
            new_nexts[i] = hm_nexts[i]
            i = i + 1
        }
        hm.keys = new_keys
        hm.values = new_values
        hm.hashes = new_hashes2
        hm.nexts = new_nexts
    }
}

pn hashmap_get(hm: HashMap, key: int) int {
    var hash: int = compute_hash(key)
    var bucket: int = hash % hm.cap
    var hm_heads = hm.heads
    var hm_keys = hm.keys
    var hm_values = hm.values
    var hm_nexts = hm.nexts
    var idx = hm_heads[bucket]
    while (idx != EMPTY) {
        if (hm_keys[idx] == key) {
            return hm_values[idx]
        }
        idx = hm_nexts[idx]
    }
    return EMPTY
}

pn run() {
    let COUNT = 90000
    var hm: HashMap = hashmap_new(COUNT * 2)
    // Pre-allocate arrays large enough
    hm.keys = fill(COUNT + 100, EMPTY)
    hm.values = fill(COUNT + 100, 0)
    hm.hashes = fill(COUNT + 100, 0)
    hm.nexts = fill(COUNT + 100, EMPTY)

    // Insert COUNT entries
    var i: int = 0
    while (i < COUNT) {
        hashmap_put(hm, i, 42)
        i = i + 1
    }

    // Lookup all entries 5 times
    var result: int = 0
    var j: int = 0
    while (j < 5) {
        i = 0
        while (i < COUNT) {
            result = result + hashmap_get(hm, i)
            i = i + 1
        }
        j = j + 1
    }

    // Iterate all entries for key/value sums
    var key_sum: int = 0
    var value_sum: int = 0
    var hm_keys2 = hm.keys
    var hm_values2 = hm.values
    i = 0
    while (i < hm.size) {
        key_sum = key_sum + hm_keys2[i]
        value_sum = value_sum + hm_values2[i]
        i = i + 1
    }

    var expected_result = 42 * COUNT * 5
    var expected_key_sum = (COUNT * (COUNT - 1)) / 2
    var expected_value_sum = 42 * COUNT

    if (result != expected_result) {
        print("hash-map: FAIL result=" ++ string(result) ++ " expected=" ++ string(expected_result) ++ "\n")
        return false
    }
    if (key_sum != expected_key_sum) {
        print("hash-map: FAIL keySum=" ++ string(key_sum) ++ " expected=" ++ string(expected_key_sum) ++ "\n")
        return false
    }
    if (value_sum != expected_value_sum) {
        print("hash-map: FAIL valueSum=" ++ string(value_sum) ++ " expected=" ++ string(expected_value_sum) ++ "\n")
        return false
    }
    return true
}

pn main() {
    var __t0 = clock()
    let pass = run()
    var __t1 = clock()
    if (pass) {
        print("hash-map: PASS\n")
    } else {
        print("hash-map: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}

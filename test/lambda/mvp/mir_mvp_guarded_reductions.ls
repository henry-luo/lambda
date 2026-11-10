// loop entry witnesses are observations, not receiving contracts (D4.3.1).
pub fn near_limit(values, first: int, limit: int) any^ {
    var total = 0
    var i = first
    while (i <= limit) {
        total = total + values[i]
        i = i + 1
    }
    return [total, i]
}

pub fn filled_sum(count: int) any^ {
    var values = fill(count, 3)
    var total = 0
    var i = 0
    while (i < count) {
        total = total + values[i]
        i = i + 1
    }
    return total
}

pub fn sum(values, n) any^ {
    var total = 0
    var i = 0
    while (i < n) {
        total = total + values[i]
        i = i + 1
    }
    return total
}

pub fn dot(values, n) any^ {
    var total = 0.0
    var i = 0
    while (i < n) {
        total = total + values[i] * values[i]
        i = i + 1
    }
    return total
}

fn affine(values, first: int, stride: int) any^ {
    var total = 0
    var i = 0
    while (i < 3) {
        total = total + values[first + i * stride]
        i = i + 1
    }
    return total
}

fn lesser(a, b) {
    if (a < b) { return a }
    return b
}
fn least(a, b, c) any^ { return lesser(lesser(a, b), c) }
type One = 1
fn lesser_one(a: One, b) any^ {
    if (a < b) { return a }
    return b
}
pub fn one_selection(values, n: int) any^ {
    var selected = 0
    var i = 0
    while (i < n) {
        selected = lesser_one(values[i], 10)^
        i = i + 1
    }
    return selected
}
pub fn fold_least(values, n: int) any^ {
    var result = 100
    var i = 0
    while (i < n) {
        result = least(result, values[i], values[i] + 1)^
        i = i + 1
    }
    return result
}

fn recursive_increment(value, remaining: int) any^ {
    if (remaining == 0) { return value }
    return recursive_increment(value + 1, remaining - 1)^
}
pub fn recursive_fold(values) any^ {
    var total = 0
    var i = 0
    while (i < 2) {
        total = total + recursive_increment(values[i], 2)^
        i = i + 1
    }
    return total
}

pub fn forward_text(text, values) any^ {
    let count = len(text)
    var total = 0
    var i = 0
    while (i < count) {
        total = total + values[i]
        i = i + 1
    }
    return [total, i]
}

pub fn backward(values, start: int, stride: int, offset: int) any^ {
    var total = 0
    var i: int = start
    while (i >= 0) {
        total = total + values[offset + i * stride]
        i = i - 1
    }
    return [total, i]
}

pub pn backward_set(var target, source, start: int) any^ {
    var i: int = start
    while (i >= 0) {
        target[i] = source[i] + 1
        i = i - 1
    }
    return [target, i]
}

pub fn strict_backward(values, start: int) any^ {
    var total = 0
    var i: int = start
    while (i > 0) {
        total = total + values[i - 1]
        i = i - 1
    }
    return [total, i]
}

pub pn reverse(var values, first, limit) any^ {
    var lo = first
    var hi = limit
    while (lo < hi) {
        var temporary = values[lo]
        values[lo] = values[hi]
        values[hi] = temporary
        lo = lo + 1
        hi = hi - 1
    }
    return [values, lo, hi]
}

pub pn reverse_shared(var values, var lo, var hi) any^ {
    while (lo < hi) {
        var temporary = values[lo]
        values[lo] = values[hi]
        values[hi] = temporary
        lo = lo + 1
        hi = hi - 1
    }
    return values
}

pub fn nested_read(values, keys, i: int) any^ { return values[keys[i]] }
pn replace_during_key(var values) int {
    values = [99, 88]
    return 0
}
pub pn snapshot_key(var values) any^ { return values[replace_during_key(values)] }

fn independent_size(value) any^ {
    let count = len(value)
    return count * 1048576
}
fn finite_scale(value) { return value * 2.0 }
fn finite_projection() {
    let values = [1.5, 2.5, 3.5]
    var total = 0.0
    var i = 0
    while (i < 3) {
        total = total + finite_scale(values[i])
        i = i + 1
    }
    return total
}
fn offset_read(start) {
    let values = [7, 8]
    var total = 0
    var i = start
    while (i < 1) {
        total = total + values[i]
        i = i + 1
    }
    return total
}
fn forward_offset(start) any^ { return offset_read(start) }

pub fn update_numeric(value, count: int) any^ {
    var current = value + 1.0
    var i = 0
    while (i < count) {
        current = current * 2.0
        i = i + 1
    }
    return current
}
pub fn switch_numeric(value, mode: int) any^ {
    var current = value + 1.0
    if (mode == 1) { current = "changed" }
    if (mode == 2) { current = null }
    if (mode == 3) { current = error("transition") }
    if (mode == 4) { current = 1 }
    return current
}

fn twice(value) any^ { return value + value }
fn mixed(value) any^ { return value + value }
pub fn projected(values: float[], key: int) any^ { return twice(values[key]) }
pub pn copy_prefix(var destination, source, count: int) any^ {
    var i: int = 0
    while (i < count) {
        destination[i] = source[i]
        i = i + 1
    }
    return destination
}

pn main() {
    print([one_selection([1, 1], 2), one_selection([-1, 1], 2) is error,
        one_selection([2], 1) is error, one_selection([1.0], 1)]); print("\n")
    let edge = near_limit([], 9007199254740991, 9007199254740991)^
    let finite_edge = near_limit([], 9007199254740990, 9007199254740990)^
    print([near_limit([1, 2, 3], 0, 2), near_limit([1], 2, 1),
        edge[0] == null, edge[1] > 9007199254740991,
        finite_edge[0] == null, finite_edge[1] == 9007199254740991]); print("\n")
    print([filled_sum(3), filled_sum(0), filled_sum(-1)]); print("\n")
    print([sum([1, 2, 3], 3), sum([1, 2], 3), sum([null, 2], 2),
        sum([1.5, 2.5], 2), sum([1, 2], 0), sum([1, 2], -1)]); print("\n")
    print([dot([1.0, 2.0], 2), dot([1.0], 2), dot([null], 1), dot([1.0], 0)]); print("\n")
    print([type(sum(["x"], 1)) == error, type(dot(["x"], 1)) == error]); print("\n")
    print([affine([1, 2, 3, 4, 5], 0, 2), affine([1, 2, 3, 4, 5], 4, -2),
        affine([1, 2, 3, 4, 5], 0, 0), affine([1, 2], 0, 1)]); print("\n")
    print([backward([1, 2, 3, 4, 5], 2, 2, 0), backward([1, 2, 3, 4, 5], 2, -2, 4),
        backward([1, 2], -1, 1, 0), backward([1, 2], 2, 1, 0),
        backward([1.5, 2.5], 1, 1, 0), strict_backward([1, 2, 3], 3)]); print("\n")
    print([forward_text("abc", [1, 2, 3]), forward_text("aé", [4, 5]),
        forward_text("abc", [1]), forward_text("", [1])]); print("\n")
    print([fold_least([7, 2, 4], 3), fold_least([7.5, 2.5], 2),
        fold_least([7], 2), type(fold_least(["x"], 1)) == error]); print("\n")
    print([recursive_fold([1, 2]), type(recursive_fold(["x", 2])) == error]); print("\n")
    var target = [0, 0, 0]
    let prior_target = target
    print(backward_set(target, [4, 5, 6], 2)^); print("\n")
    print(prior_target); print("\n")
    print(backward_set(target, [4], 2)^); print("\n")
    var values = [1, 2, 3, 4]
    let saved = values
    print(reverse(values, 0, 3)^); print("\n")
    print(saved); print("\n")
    print(reverse(values, 2, 1)^); print("\n")
    var shared = [1, 2, 3, 4]
    var low = 0
    var high = 3
    print(reverse_shared(shared, low, high)^); print("\n")
    print([low, high]); print("\n")
    var text = ["a", "b", "c"]
    print(reverse(text, 0, 2)^); print("\n")
    print([projected([1.5, -0.0], 0), projected([1.5], 1),
        projected([1.5], -1), type(mixed(2)) == int, type(mixed(2.0)) == float]); print("\n")
    print([nested_read([4, 5], [1, 0], 0), nested_read([4, 5], [1.0], 0),
        nested_read([4, 5], [null], 0), nested_read([4, 5], [1], 1)]); print("\n")
    var staged = [10, 20]
    print([snapshot_key(staged)^, staged]); print("\n")
    var staged_float = [1.5, 2.5]
    print([snapshot_key(staged_float)^, staged_float]); print("\n")
    var staged_bool = [true, false]
    print([snapshot_key(staged_bool)^, staged_bool]); print("\n")
    var staged_boxed = ["x", 20]
    print([snapshot_key(staged_boxed)^, staged_boxed]); print("\n")
    print([update_numeric(1.5, 0), update_numeric(1.5, 2), update_numeric(2, 1),
        type(update_numeric("x", 1)) == error]); print("\n")
    print([switch_numeric(1.5, 0), switch_numeric(1.5, 1), switch_numeric(1.5, 2),
        type(switch_numeric(1.5, 3)) == error, type(switch_numeric(1.5, 4)) == int]); print("\n")
    print([finite_projection(), forward_offset(0), forward_offset(0.5)]); print("\n")
    print([independent_size("ab"), independent_size(1 to 10000000000) > 9007199254740991]); print("\n")
    var destination = [0, 0, 0]
    let original = destination
    print(copy_prefix(destination, [7, 8, 9], 3)^); print("\n")
    print(original); print("\n")
    print(copy_prefix(destination, ["a", 2.5, null], 3)^); print("\n")
}

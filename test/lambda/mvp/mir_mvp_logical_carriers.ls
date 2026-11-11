// nested selections must agree on boxed versus native Boolean transport (S3.4).
fn conjunction(c: string, n: int) bool => c != "[" and c != "]" and n < 0
fn disjunction(c: string, n: int) bool => c == "[" or c == "]" or n < 0
pn branch(raw: string, n: int) int^ {
    let c: string = slice(raw, 0, 1)^
    if (c != "[" and c != "]" and n < 0) { return 1 }
    return 0
}

pub fn indirect(values, index) any^ {
    let keys = [0, 1, 7]
    if (values[keys[index]]) { return 1 }
    return 0
}
pub fn indirect_twice(values, key) any^ {
    let keys = [0, 1, 7]
    let order = [1, 0, 3]
    if (values[keys[order[key]]]) { return 1 }
    return 0
}
pub fn choose(raw: any) any^ => if (raw == 0 and raw != 1) 1 else 2
pub pn require(raw: any) int^ {
    if ((raw == 0 and raw != 1)^) { return 1 }
    return 2
}
pub pn dispatch(raw: any) int^ {
    var selected: int = 0
    if ((raw == 43)^) { selected = 1 }
    if ((raw == 45)^) { selected = 2 }
    if ((raw == 46)^) { selected = 3 }
    return selected
}
pn increment(var key: int) { key = key + 1 }
pub pn changing() int^ {
    var key: int = 1
    var count: int = 0
    if (key == 1) { increment(key)^; count = count + 1 }
    if (key == 2) { increment(key)^; count = count + 1 }
    if (key == 3) { increment(key)^; count = count + 1 }
    return count
}

pub fn consecutive_equal(values, count: int) any^ {
    var matches = 0
    var i = 0
    while (i < count) {
        if (values[i] == values[i + 1]) { matches = matches + 1 }
        i = i + 1
    }
    return matches
}
pub fn pair_equal(left, right, i: int, j: int) any^ {
    return if (left[i] == right[j]) 1 else 0
}
pub pn pair_less(left, right, i: int, j: int) any^ {
    if ((left[i] < right[j])^) { return 1 }
    return 0
}

pn mark(var values, pivot) {
    var i: int = 0
    while (i < 2) {
        if (values[i] <= pivot) { values[i] = 0 }
        i = i + 1
    }
    return values
}

fn select(values, key) {
    var found: int = -1
    var i: int = 0
    while (i < len(values)) {
        if (found == -1) {
            var member = values[i]
            if (member == key) { found = i }
        }
        i = i + 1
    }
    return found
}

pn main() {
    print([conjunction("]", -1), conjunction("[", -1), conjunction("x", -1), conjunction("x", 0)]); print("\n")
    print([disjunction("]", 0), disjunction("[", 0), disjunction("x", -1), disjunction("x", 0)]); print("\n")
    print([branch("]", -1), branch("[", -1), branch("x", -1), branch("x", 0)]); print("\n")
    var values = [3, 1]
    print(mark(values, 2)); print("\n")
    print([select([3, 1], 1), select([3, 1], 5)]); print("\n")
    print([indirect([true, false], 0), indirect([true, false], 1),
        indirect([true, false], 2), indirect([true, false], 3),
        indirect([true, null], 1)]); print("\n")
    print([indirect_twice([true, false], 0), indirect_twice([true, false], 1),
        indirect_twice([true, false], 2), indirect_twice([true, false], 3),
        indirect([true, false], 1.0), indirect([true, false], -1)]); print("\n")
    print([consecutive_equal("aaa", 2), consecutive_equal(sort([1, 1, 1]), 2),
        consecutive_equal(null, 2)]); print("\n")
    print([pair_equal([1, 2], [2], 1, 0), pair_equal([1], [2], 0, 0),
        pair_equal([1.0], [1], 0, 0), pair_equal([true], [false], 0, 0),
        pair_equal([null], [null], 0, 0), pair_equal([1], [1], 1, 1),
        pair_equal(["a"], ["a"], 0, 0)]); print("\n")
    print([pair_less([1], [2], 0, 0), pair_less([2.0], [1.0], 0, 0),
        pair_less([1], [2], 1, 0), pair_less([1], [2], 0, 1)]); print("\n")
    let bad = error("predicate")
    print([type(pair_equal([bad], [1], 0, 0)) == error,
        type(pair_less([bad], [1], 0, 0)) == error]); print("\n")
    print([choose(0), choose(1), type(choose(bad)) == error,
        type(require(bad)) == error]); print("\n")
    print([dispatch(43), dispatch(45), dispatch(46), dispatch(99), dispatch(43.0),
        dispatch("43"), dispatch(null), type(dispatch(bad)) == error, changing()]); print("\n")
}

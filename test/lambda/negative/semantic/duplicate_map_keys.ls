// S2.3.1v2: written duplicates are E242, even on a runtime-shaped literal.
let direct = {a: 1, b: 2, a: 3}
let quoted = {a: 1, 'a': 3}
let decoded = {'a': 1, '\u0061': 3}
let normalized = {'\ud83d\ude00': 1, '😀': 3}
let spread = {a: 1, *:{b: 2}, a: 3}
let computed = {a: 1, ["b"]: 2, a: 3}
"bound:duplicate-map-keys"

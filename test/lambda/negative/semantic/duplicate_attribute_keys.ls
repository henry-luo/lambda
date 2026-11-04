// S2.3.1v2: element and nominal object attributes use the same static check.
type Record { a: int }
let el = <node a: 1, a: 3>
let quoted = <node a: 1, 'a': 3>
let spread = <node a: 1, *:{b: 2}, a: 3>
let computed = <node a: 1, ["b"]: 2, a: 3>
let record = <Record a: 1, a: 3>
"bound:duplicate-attribute-keys"

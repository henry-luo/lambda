// sort(), sort(v, key) and `order by` are stable in both directions, and a
// large reversed input sorts in O(n log n): the former insertion/bubble sorts
// took ~28 s on this 65,536-element case.
let rows = [{a: 2, b: 1}, {a: 1, b: 2}, {a: 1, b: 1}, {a: 2, b: 0}, {a: 0, b: 9}]
let big = sort([for (x in 0 to 65535) 0 - x])
let keyed = sort([for (x in 0 to 65535) {k: x % 3, v: x}], (r) => r.k);
[
    sort([5, 3, 9, 1, 3, 7]),
    sort([5, 3, 9, 1, 3, 7], 'desc'),
    [for (r in sort(rows, (r) => r.a)) r.b],
    [for (r in sort(rows, {dir: 'desc', by: (r) => r.a})) r.b],
    [for (r in rows order by r.a) r.b],
    [for (r in rows order by r.a desc) r.b],
    [big[0], big[32767], big[65535]],
    [keyed[0].v, keyed[1].v, keyed[21845].v, keyed[21846].v, keyed[65535].v]
]

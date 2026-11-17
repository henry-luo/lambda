// S10.6.1v2: shallow map merge keeps first key positions and later values.
fn merge_maps(a: map, b: map) map^ => a ++ b

let left = {a: 1, b: 2}
let right = {b: 20, c: 3}
let merged = merge_maps(left, right) ^ { {} };
[merged.a == 1, merged.b == 20, merged.c == 3, len(merged) == 3];
[[for (k, v in merged) string(k)] == ["a", "b", "c"], left.b == 2, right.b == 20];

let reversed_merge = merge_maps(right, left) ^ { {} };
[reversed_merge.b == 2, [for (k, v in reversed_merge) string(k)] == ["b", "c", "a"]];

let nested = merge_maps({child: {x: 1, y: 2}}, {child: {x: 3}}) ^ { {} };
[nested.child.x == 3, nested.child.y is null];

let chained = merge_maps(merged, {a: "changed", d: false}) ^ { {} };
[chained.a == "changed", chained.d == false, [for (k, v in chained) string(k)] == ["a", "b", "c", "d"]];

[(merge_maps({}, left)^) == left, (merge_maps(left, {})^) == left,
 (merge_maps(left, left)^) == left, (merge_maps({}, {})^) is map];
[(left ++ null) == left, (null ++ right) == right];

let spread_left = {*: left, a: 10};
[(merge_maps(spread_left, {b: 30})^).a == 10,
 (merge_maps(spread_left, {b: 30})^).b == 30]

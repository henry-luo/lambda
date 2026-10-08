import stack: lambda.chart.stack

fn close(a, b) => abs(a - b) < 0.0000001
fn sample(rows, group, x) => (rows |: ~.g == group and ~.x == x)[0]
fn stack_rows(rows, order = null) => stack.apply_stack(rows, "v", "g", "x", "wiggle", order)
let data = [{x: 0, g: "A", v: 1}, {x: 1, g: "A", v: 2}, {x: 2, g: "A", v: 1},
    {x: 0, g: "B", v: 3}, {x: 1, g: "B", v: 1}, {x: 2, g: "B", v: 4}]
let stream = stack_rows(data, ["A", "B"])
let center = stack.apply_stack(data, "v", "g", "x", "center")
let sparse = stack_rows([{x: 2, g: "A", v: 3, note: "retained"}, {x: 0, g: "A", v: 1},
    {x: 0, g: "A", v: 2}, {x: 1, g: "B", v: 4}], ["A", "B"])
let zeros = stack_rows([for (g in ["A", "B"], x in [0, 1, 2]) {g: g, x: x, v: 0}])
let gap = stack_rows([{g: "A", x: 0, v: 2}, {g: "A", x: 1, v: 0}, {g: "A", x: 2, v: 4}])
let single = stack_rows([{g: "A", x: 0, v: 3}, {g: "B", x: 0, v: 5}])
let waves = [for (series in [{g: "early", values: [8, 1, 0]}, {g: "middle", values: [0, 9, 0]},
    {g: "late", values: [0, 1, 10]}, {g: "flat", values: [2, 2, 2]}], x, v in series.values) {g: series.g, x: x, v: v}]
let inside = stack_rows(waves)
let forward = stack_rows(waves, "none")
let backward = stack_rows(waves, "reverse")
let small = stack_rows(waves, "ascending")
let large = stack_rows(waves, "descending")
let partial = stack_rows(waves, ["flat", "flat", "absent", "early"])
let signed = stack.apply_stack([{x: 0, g: "A", v: 2}, {x: 0, g: "B", v: -3}, {x: 0, g: "C", v: 4}],
    "v", "g", "x", "zero", ["C"])
let huge = stack_rows([for (row in data) {*:row, v: row.v * 1e200}], ["A", "B"])
let long_huge = stack_rows([for (g in ["A", "B"], x in 0 to 19) {g: g, x: x, v: 1e307}])
let ordered_positions = stack.apply_stack(data, "v", "g", "x", "wiggle", ["A", "B"], [2, 0, 1])
let checks = [
    {name: "wiggle reference baseline", ok: close(sample(stream, "A", 0)._y0, 0) and
        close(sample(stream, "A", 1)._y0, -1.0 / 3.0) and close(sample(stream, "A", 2)._y0, -19.0 / 30.0)},
    {name: "wiggle reference upper extent", ok: close(sample(stream, "B", 2)._y1, 131.0 / 30.0)},
    {name: "weighted baseline differs from silhouette", ok: stream[1]._y0 != center[1]._y0},
    {name: "thickness preserved", ok: len(stream |: not close(~._y1 - ~._y0, ~.v)) == 0},
    {name: "adjacent series remain contiguous", ok: len([for (x in [0, 1, 2]) x] |:
        not close(sample(stream, "A", ~)._y1, sample(stream, "B", ~)._y0)) == 0},
    {name: "sparse grid filled", ok: len(sparse) == 6 and sample(sparse, "A", 1).v == 0 and sample(sparse, "B", 2).v == 0},
    {name: "duplicate samples add", ok: sample(sparse, "A", 0).v == 3 and close(sample(sparse, "A", 0)._y1 - sample(sparse, "A", 0)._y0, 3)},
    {name: "authored attributes survive", ok: sample(sparse, "A", 2).note == "retained"},
    {name: "synthetic rows do not copy unrelated attributes", ok: sample(sparse, "A", 1).note == null},
    {name: "positions sorted per series", ok: (sparse |: ~.g == "A" |> ~.x) == [0, 1, 2]},
    {name: "zero data is finite and flat", ok: len(zeros |: ~._y0 != 0 or ~._y1 != 0) == 0},
    {name: "zero column keeps baseline", ok: gap[1]._y0 == gap[0]._y0 and gap[1]._y1 == gap[1]._y0 and close(gap[2]._y0, -2)},
    {name: "one position stays at zero", ok: min(single |> ~._y0) == 0 and max(single |> ~._y1) == 8},
    {name: "inside-out reference order", ok: close(sample(inside, "late", 1)._y1, sample(inside, "early", 1)._y0) and
        close(sample(inside, "early", 1)._y1, sample(inside, "flat", 1)._y0) and
        close(sample(inside, "flat", 1)._y1, sample(inside, "middle", 1)._y0)},
    {name: "inside-out is the default", ok: inside == stack_rows(waves, "inside_out")},
    {name: "natural order override", ok: sample(forward, "early", 0)._y0 == 0 and sample(forward, "middle", 0)._y0 == 8},
    {name: "reverse order override", ok: sample(backward, "flat", 0)._y0 == 0 and sample(backward, "late", 0)._y0 == 2},
    {name: "ascending totals order", ok: sample(small, "flat", 0)._y0 == 0 and sample(small, "early", 0)._y0 == 2},
    {name: "descending totals order", ok: sample(large, "late", 0)._y0 == 0 and sample(large, "middle", 0)._y0 == 0 and sample(large, "early", 0)._y0 == 0},
    {name: "partial order appends observed groups", ok: sample(partial, "flat", 0)._y0 == 0 and sample(partial, "early", 0)._y0 == 2 and sample(partial, "middle", 0)._y0 == 10},
    {name: "partial order retains signed stacking", ok: signed[0]._y0 == 4 and signed[1]._y1 == -3 and signed[2]._y0 == 0},
    {name: "large finite values avoid weight-product overflow", ok: close(huge[2]._y0 / 1e200, -19.0 / 30.0)},
    {name: "large cumulative series totals keep ordering finite", ok: len(long_huge) == 40 and
        sample(long_huge, "A", 19)._y0 == 0 and sample(long_huge, "B", 19)._y1 == 2e307},
    {name: "explicit position order", ok: (ordered_positions |: ~.g == "A" |> ~.x) == [2, 0, 1]},
    {name: "empty input", ok: stack_rows([]) == []},
    {name: "input unchanged", ok: data[0]._y0 == null and data[0].v == 1 and len(data) == 6},
    {name: "negative values rejected", ok: stack_rows([{x: 0, g: "A", v: -1}]) is error},
    {name: "nonfinite values rejected", ok: stack_rows([{x: 0, g: "A", v: nan}]) is error and stack_rows([{x: 0, g: "A", v: inf}]) is error},
    {name: "nonnumeric values rejected", ok: stack_rows([{x: 0, g: "A", v: "2"}]) is error},
    {name: "invalid position rejected", ok: stack_rows([{x: null, g: "A", v: 1}]) is error and stack_rows([{x: nan, g: "A", v: 1}]) is error},
    {name: "missing series rejected", ok: stack_rows([{x: 0, v: 1}]) is error},
    {name: "unknown order rejected", ok: stack_rows(data, "random") is error and stack_rows([], "random") is error},
    {name: "overflowing totals rejected", ok: stack_rows([{x: 0, g: "A", v: 1e308}, {x: 0, g: "B", v: 1e308}]) is error}
];
[for (check in checks where check.ok != true) check.name]

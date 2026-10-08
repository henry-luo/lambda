import transform: lambda.chart.transform
import vega: lambda.chart.vega
import chart: lambda.chart.chart

fn process_step(data, step) => transform.apply_transforms(data, [step])
let rows = [{g: "a", k: "b", v: 2}, {g: "b", k: "a", v: 5}, {g: "a", k: "a", v: 3}, {g: "a", k: "b", v: 4}];
let joined = process_step(rows, {type: "joinaggregate", groupby: ["g"], joinaggregate: [{op: "sum", field: "v", as: "total"}]});
let global = process_step(rows, {type: "joinaggregate", joinaggregate: [{op: "count", as: "count"}]});
let pivot = process_step(rows, {type: "pivot", pivot: "k", value: "v", groupby: ["g"]});
let partial = [{g: "a", x: 1, v: 10, note: "keep"}, {g: "b", x: 2, v: 20}, {g: "a", x: 3, v: 30}];
let filled = process_step(partial, {type: "impute", impute: "v", key: "x", groupby: ["g"], method: "mean"});
let framed = process_step([{x: 0, v: 10}, {x: 2, v: 30}, {x: 4, v: 100}], {
    type: "impute", impute: "v", key: "x", keyvals: {start: 0, stop: 5}, method: "mean", frame: [-1, 1]});
let stacked = process_step(rows, {type: "stack", stack: "v", groupby: ["g"], sort: [{field: "k", order: "ascending"}], as: ["start", "end"]});
let quantiles = process_step([{g: "a", v: 0}, {g: "b", v: 100}, {g: "a", v: 10}, {g: "a", v: null}], {
    type: "quantile", quantile: "v", groupby: ["g"], probs: [0.25, 0.5, 0.75]});
let converted = vega.convert({data: {values: rows}, mark: "bar", transform: [
    {joinaggregate: [{op: "sum", field: "v", as: "total"}], groupby: ["g"]},
    {calculate: "datum.v / datum.total", as: "fraction"},
    {stack: "fraction", groupby: ["g"], as: "fraction"}], encoding: {
        x: {field: "g", type: "nominal"}, y: {field: "fraction_end", type: "quantitative"},
        y2: {field: "fraction", type: "quantitative"}, color: {field: "k", type: "nominal"}}});
let checks = {
    join_groups: (joined |> ~.total) == [9, 5, 9, 9],
    join_order: (joined |> ~.k) == (rows |> ~.k),
    join_fields: joined[0].v == 2 and len(joined) == len(rows),
    join_global: (global |> ~.count) == [4, 4, 4, 4],
    join_markup: transform.apply_transforms(rows, <transform <joinaggregate groupby: ["g"], <agg op: "sum", field: "v", as: "total">>>) == joined,
    pivot: pivot == [{g: "a", a: 3.0, b: 6.0}, {g: "b", a: 5.0, b: 0.0}],
    pivot_limit: process_step(rows, {type: "pivot", pivot: "k", value: "v", groupby: ["g"], limit: 1}) == [{g: "a", a: 3.0}, {g: "b", a: 5.0}],
    pivot_count: process_step(rows, {type: "pivot", pivot: "k", value: "v", op: "count"}) == [{a: 2, b: 2}],
    impute_count: len(filled) == 6,
    impute_originals: slice(filled, 0, 3) == partial,
    impute_mean: [for (row in filled where row.g == "a" and row.x == 2) row.v] == [20.0],
    impute_groups: [for (row in filled where row.g == "b") row.v] == [20, 20.0, 20.0],
    impute_frame: [for (row in framed where row.x == 1 or row.x == 3) row.v] == [20.0, 65.0],
    impute_null: process_step([{x: 1, v: null}], {type: "impute", impute: "v", key: "x", keyvals: [2], value: null}) == [{x: 1, v: null}, {x: 2, v: null}],
    impute_ungrouped: process_step(partial, {type: "impute", impute: "v", key: "x"}) == partial,
    stack_order: (stacked |> ~.start) == [3.0, 0.0, 0.0, 5.0] and (stacked |> ~.end) == [5.0, 5.0, 3.0, 9.0],
    stack_normalized: process_step(rows, {type: "stack", stack: "v", groupby: ["g"], offset: "normalize", as: "s"})[3].s_end == 1.0,
    stack_center: (process_step([{v: 2}, {v: 4}], {type: "stack", stack: "v", offset: "center", as: "s"}) |> ~.s) == [-3.0, -1.0],
    stack_private: process_step([{v: 2, _y0: 99, _y1: 100}], {type: "stack", stack: "v", as: "s"})[0]._y0 == 99,
    quantiles: [for (row in quantiles where row.g == "a") row.value] == [2.5, 5.0, 7.5],
    quantile_group: [for (row in quantiles where row.g == "b") row.value] == [100, 100, 100],
    quantile_grid: process_step([{v: 0}, {v: 10}], {type: "quantile", quantile: "v", step: 0.5}) == [{prob: 0.25, value: 2.5}, {prob: 0.75, value: 7.5}],
    vega_render: chart.render_spec(converted) is element,
    vega_pivot: process_step(rows, vega.convert({transform: [{pivot: "k", value: "v", groupby: ["g"]}]}).transform[0]) == pivot,
    vega_impute: process_step(partial, vega.convert({transform: [{impute: "v", key: "x", groupby: ["g"], method: "mean"}]}).transform[0]) == filled,
    vega_quantile: process_step([{v: 0}, {v: 10}], vega.convert({transform: [{quantile: "v", probs: [0.5], as: ["p", "q"]}]}).transform[0]) == [{p: 0.5, q: 5.0}],
    bad_pivot: process_step([], {type: "pivot", pivot: "k", value: "v", limit: -1}) is error,
    bad_aggregate: process_step([], {type: "pivot", pivot: "k", value: "v", op: "bogus"}) is error,
    bad_impute: process_step([], {type: "impute", impute: "v", key: "x", frame: [1, -1]}) is error,
    bad_keys: process_step([], {type: "impute", impute: "v", key: "x", keyvals: {stop: 10, step: 0}}) is error,
    bad_stack: process_step([{v: inf}], {type: "stack", stack: "v", as: "s"}) is error,
    bad_stack_empty: process_step([], {type: "stack", stack: "v", as: ["s"]}) is error,
    bad_quantile: process_step([], {type: "quantile", quantile: "v", probs: [1.1]}) is error,
    bad_quantile_step: process_step([], {type: "quantile", quantile: "v", step: 0}) is error
};
[for (label, passed in checks where passed != true) string(label)]

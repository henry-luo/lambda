import tr: lambda.chart.transform
import chart: lambda.chart.chart
import vega: lambda.chart.vega

let data = [{id: "a", g: "A", k: 2, v: 2}, {id: "b", g: "B", k: 1, v: 8},
    {id: "c", g: "A", k: 1, v: 1}, {id: "d", g: "A", k: 2, v: 4}, {id: "e", g: "A", k: 3, v: 3}]
let ranked = tr.apply_transforms(data, <transform
    <window groupby: ["g"], sort: [{field: "k"}],
        <agg op: "sum", field: "v", as: "total">
        <agg op: "row_number", as: "number"> <agg op: "rank", as: "rank">
        <agg op: "dense_rank", as: "dense"> <agg op: "percent_rank", as: "percent">
        <agg op: "cume_dist", as: "cume"> <agg op: "ntile", param: 2, as: "tile">
        <agg op: "lag", field: "v", as: "previous"> <agg op: "lead", field: "v", as: "next">>>)
let sliding = tr.apply_transforms(data, [{type: "window", groupby: ["g"], sort: [{field: "k"}],
    frame: [-1, 0], ignore_peers: true,
    ops: ["sum", "first_value", "last_value", "nth_value"], fields: ["v", "v", "v", "v"],
    params: [null, null, null, 0], as: ["s", "f", "l", "n"]}])
let whole = tr.apply_transforms(data, <transform <window groupby: ["g"], frame: [null, null],
    <agg op: "mean", field: "v", as: "mean"> <agg op: "count", as: "count">>>)
let empty_frame = tr.apply_transforms([{v: 1}], <transform <window frame: [1, 2],
    <agg op: "sum", field: "v", as: "s"> <agg op: "count", as: "c">
    <agg op: "first_value", field: "v", as: "f">>>)
let no_sort = tr.apply_transforms([{v: 3}, {v: 3}], <transform
    <window <agg op: "rank", as: "r"> <agg op: "sum", field: "v", as: "s">>>)
let mixed = tr.apply_transforms([{g: 2, v: 1, id: "a"}, {g: 1, v: 2, id: "b"},
    {g: 1, v: 3, id: "c"}, {g: 1, v: 3, id: "d"}],
    <transform <sort field: ["g", "v"], order: ["ascending", "descending"]>>)
let numeric = tr.apply_transforms([{v: null}, {v: 2}, {v: nan}, {v: inf}, {v: "3"}],
    <transform <window frame: [null, null],
        <agg op: "sum", field: "v", as: "s"> <agg op: "mean", field: "v", as: "m">>>)
let joined = tr.apply_transforms([{key: 1, label: "old"}, {key: "1"}, {key: 3}],
    <transform <lookup field: "key", from: {name: "labels", key: "id", fields: ["label", "value"]},
        as: ["label", "amount"], default: "missing">>,
    {labels: [{id: 1, label: "numeric", value: 0}, {id: "1", label: "text", value: 9}]})
let joined_object = tr.apply_transforms([{id: 1}], [{type: "lookup", lookup: "id",
    from: {data: {values: [{key: 1, name: "whole"}]}, key: "key"}, as: "joined"}])
let converted = vega.convert({data: {values: data}, datasets: {labels: [{id: "a", title: "first"}]},
    transform: [{window: [{op: "sum", field: "v", as: "total"}], groupby: ["g"],
        sort: [{field: "k"}], ignorePeers: true},
        {lookup: "id", from: {data: {name: "labels"}, key: "id", fields: ["title"]}}], mark: "point"})
let adapted = tr.apply_transforms(data, converted.transform, converted.datasets)
let structured_keys = tr.apply_transforms([{g: {key: 1}, v: 2}, {g: map(["key", 1]), v: 3}],
    <transform <window groupby: ["g"], <agg op: "sum", field: "v", as: "total">>>)
let structured_join = tr.apply_transforms([{g: {key: 1}}],
    <transform <lookup field: "g", from: {data: [{key: map(["key", 1]), label: "match"}],
        key: "key", fields: ["label"]}>>)
let lookup_metadata = vega.convert({data: {values: [{id: 1}]}, transform: [{lookup: "id",
    from: {data: {values: [{key: 1, strokeWidth: 17}]}, key: "key", fields: ["strokeWidth"]}}]})
let poison_groups = tr.apply_transforms([{g: nan, v: 1}, {g: nan, v: 2}],
    <transform <window groupby: ["g"], <agg op: "sum", field: "v", as: "total">>>)
let poison_sort = tr.apply_transforms([{key: nan, v: 1}, {key: 2, v: 2}],
    <transform <window sort: [{field: "key"}], <agg op: "rank", as: "rank">
        <agg op: "sum", field: "v", as: "total">>>)
let waterfall = chart.render(<chart width: 320, height: 220,
    <data values: [{step: "Start", change: 10}, {step: "Loss", change: -4}, {step: "Gain", change: 2}]>
    <transform <window ignore_peers: true, <agg op: "sum", field: "change", as: "end">>
        <calculate as: "start", expression: (row) => row.end - row.change>>
    <mark type: "bar">
    <encoding <x field: "step", dtype: "ordinal">
        <y field: "start", dtype: "quantitative"> <y2 field: "end">
        <color value: "green", condition: {field: "change", lt: 0, value: "red"}>>>)
let checks = {
    source_order: (ranked |> ~.id) == ["a", "b", "c", "d", "e"],
    peers_share_sum: ranked[0].total == 7 and ranked[3].total == 7,
    ranks: (ranked |> ~.rank) == [2, 1, 1, 2, 4],
    dense: (ranked |> ~.dense) == [2, 1, 1, 2, 3],
    row_number: (ranked |> ~.number) == [2, 1, 1, 3, 4],
    percent: abs(ranked[0].percent - 1.0 / 3.0) < 1e-10 and ranked[1].percent == 0,
    distribution: ranked[0].cume == 0.75 and ranked[3].cume == 0.75,
    tiles: (ranked |> ~.tile) == [1, 1, 1, 1, 2],
    lag: ranked[0].previous == 1 and ranked[2].previous == null,
    lead: ranked[0].next == 4 and ranked[4].next == null,
    sliding_sum: (sliding |> ~.s) == [3, 8, 1, 6, 7],
    sliding_values: sliding[3].f == 2 and sliding[3].l == 4 and sliding[3].n == 2,
    whole_partition: whole[0].mean == 2.5 and whole[1].mean == 8 and whole[0].count == 4,
    empty_window: empty_frame[0].s == 0 and empty_frame[0].c == 0 and empty_frame[0].f == null,
    no_sort_peers: no_sort[1].r == 2 and no_sort[1].s == 6,
    mixed_stable_sort: (mixed |> ~.id) == ["c", "d", "b", "a"],
    valid_numeric: numeric[0].s == 2 and numeric[0].m == 2,
    lookup_typed_key: joined[0].label == "numeric" and joined[1].label == "text",
    lookup_zero: joined[0].amount == 0,
    lookup_default: joined[2].label == "missing" and joined[2].amount == "missing",
    lookup_whole_record: joined_object[0].joined.name == "whole",
    vega_window: adapted[0].total == 3 and adapted[3].total == 7,
    vega_lookup: adapted[0].title == "first" and adapted[1].title == null,
    structured_partitions: structured_keys[1].total == 5,
    structured_lookup: structured_join[0].label == "match",
    foreign_field_names: tr.apply_transforms([{id: 1}], lookup_metadata.transform)[0].strokeWidth == 17,
    poison_partition_rows: len(poison_groups) == 2 and poison_groups[0].total == 1 and poison_groups[1].total == 2,
    poison_sort_rows: len(poison_sort) == 2 and poison_sort[0].rank > 0 and poison_sort[0].total == 3,
    waterfall_bars: len(content(waterfall[1][0])) == 3,
    waterfall_loss: waterfall[1][0][1].fill == "red" and waterfall[1][0][1].height > 0,
    empty_input: tr.apply_transforms([], <transform <window <agg op: "row_number", as: "n">>>) == [],
    bad_operation: tr.apply_transforms(data, <transform <window <agg op: "unknown">>>) is error,
    bad_frame: tr.apply_transforms(data, <transform <window frame: [2, -2]>>) is error,
    bad_parameter: tr.apply_transforms(data, <transform <window <agg op: "ntile", param: 0>>>) is error,
    bad_field: tr.apply_transforms(data, <transform <window <agg op: "sum">>>) is error,
    bad_lookup: tr.apply_transforms(data, <transform <lookup field: "id", from: {name: "absent", key: "id"}, as: "v">>) is error,
    chart_lookup: chart.render_spec(converted) is element
};
[for (label, passed in checks where passed != true) string(label)]

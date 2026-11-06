// S14.1.1v2/S14.1.3: groups retain complete joined/cross-product tuple rows.
let rows = [{kind:"x", value:1}, {kind:"y", value:2}, {kind:"x", value:3}]
let tags = [10, 20]
let crossed = [for (row in rows, tag in tags group by row.kind into members)
    {kind:kind, pairs:[for (pair in members) [pair.row.value, pair.tag]]}]
let joined = [for (position, row in rows,
        tag_position, tag? in [{kind:"x", value:10}] on row.kind == tag.kind
        group by row.kind into members order by kind)
    {kind:kind, rows:[for (pair in members)
        [pair.position, pair.row.value, pair.tag_position, pair.tag.value]]}];
let cold_rows = [{id:1, real:1.0e-310, wide:9007199254740993i64}]
let retained = [for (row in cold_rows, key in [1] on row.id == key)
    {real:row.real, wide:row.wide}];
[
    crossed == [
        {kind:"x", pairs:[[1, 10], [1, 20], [3, 10], [3, 20]]},
        {kind:"y", pairs:[[2, 10], [2, 20]]}
    ],
    joined == [
        {kind:"x", rows:[[0, 1, 0, 10], [2, 3, 0, 10]]},
        {kind:"y", rows:[[1, 2, null, null]]}
    ],
    retained == [{real:1.0e-310, wide:9007199254740993i64}]
]

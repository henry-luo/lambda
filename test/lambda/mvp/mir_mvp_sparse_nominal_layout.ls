// D4.2.5: absent optional fields compact the runtime nominal layout.
type Reading { value: float, note?: string, rank: int = 3, tail?: bool, count: int = 7 }
let sparse = <Reading value: 1.25>
let partial = <Reading value: 2.5, note: "ready">
let full = <Reading value: 3.75, note: "set", tail: true>;
[
    [sparse.value, sparse.rank, sparse.count],
    [partial.value, partial.rank, partial.count],
    [full.value, full.rank, full.count],
    not ('note' at sparse) and not ('tail' at partial) and full.tail
]

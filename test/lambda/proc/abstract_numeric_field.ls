// Packed map fields whose contract does not name ONE concrete carrier are
// stored self-describing (`sizeof(TypedItem)` = 9 bytes), and the shape must
// stride by that (Lambda_Design_Compiling_Lane.md §10.2, §10.4b G3).
//
// Regression guards, all of which failed before 2026-08-20:
//  * the map-TYPE parser strode a flat 8 bytes per field, so the field AFTER an
//    `integer`/`number` slot began one byte inside it -- `type(n)` reported
//    `float`, and a wide BigInt died with `fn_string unhandled type: any`;
//  * an `integer` contract is not storage-valid, so it cannot take the
//    storage-valid adoption path; when that was the ONLY adoption path the
//    literal was re-packed into the contract shape and read back as `error`;
//  * pointer-lane scalar fields (`decimal`, `datetime`) were admitted by the
//    direct field read/write pair, which returns raw untagged bytes for them --
//    `{v: decimal}` reported its type as `raw_pointer`.
//
//  * the validator's `unwrap_type` dereferenced a compact global meta-type
//    (`number`/`integer` carry only the two-byte Type prefix), so a
//    `number`-contracted field aborted CONSTRUCTION on a global-buffer-overflow,
//    and `integer` membership could not be decided at all -- which is why
//    arithmetic on an `integer` field failed at the STORE.

type Counter = {v: integer}
type Counts = {n: integer, label: string}
type Measures = {q: number, label: string}
type Money = {amount: decimal, label: string}
type Stamped = {at: datetime, label: string}

pn main() {
    var c: Counter = {v: 0n}
    var multi: Counts = {n: 42n, label: "counts"}
    var wide: Counts = {n: 9007199254740993n, label: "wide"}
    var money: Money = {amount: 1.5m, label: "money"}
    var stamped: Stamped = {at: t'2020-01-01', label: "stamped"}
    print(type(c.v) ++ " " ++ c.v ++ " " ++ (c.v is integer) ++ "\n")
    print(type(multi.n) ++ " " ++ multi.n ++ " " ++ multi.label ++ "\n")
    print(wide.n ++ " " ++ wide.label ++ "\n")
    print(type(money.amount) ++ " " ++ money.amount ++ " " ++ money.label ++ "\n")
    print(type(stamped.at) ++ " " ++ stamped.label ++ "\n")

    // arithmetic round-trips through the slot: read, compute, store back
    var m: Measures = {q: 1.5, label: "measures"}
    m.q = m.q + 1.0
    multi.n = multi.n + 1n
    print(type(m.q) ++ " " ++ m.q ++ " | " ++ type(multi.n) ++ " " ++ multi.n ++ "\n")
}

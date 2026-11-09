// D4.3.1/D2.6.2: layout candidates preserve open fields and live retags.
fn packed(value, after) => {guard_value: value, guard_after: after}
fn read_value(row) => row.guard_value
fn read_after(row) => row.guard_after
fn read_tail(row) => row.guard_tail
fn read_flag(row) => row.guard_flag
pn replace_tail(var row, value) { row.guard_tail = value }

pn main() {
    var row = packed(7, [10, 20])
    let snapshot = row
    row.guard_value = "changed"
    let different = {prefix: 99, guard_value: 42}
    var link = {guard_tail: null, guard_flag: false}
    let old_tail = read_tail(link)
    replace_tail(link, packed(3.5, [30]))^
    link.guard_flag = "retagged"
    // the returned scalar must retain its owner across the next construction.
    let signed = read_value(packed(9223372036854775807i64, [1]))
    let unsigned = read_value(packed(18446744073709551615u64, [2]))
    print([
        read_value(snapshot) == 7,
        read_after(snapshot)[1] == 20,
        read_value(row) == "changed",
        read_value(different) == 42,
        read_value(null) == null,
        read_value({missing: 1}) == null,
        old_tail == null,
        read_value(read_tail(link)) == 3.5,
        read_after(read_tail(link))[0] == 30,
        read_flag(link) == "retagged",
        signed == 9223372036854775807i64 and type(signed) == i64,
        unsigned == 18446744073709551615u64 and type(unsigned) == u64
    ])
    print("\n")
}

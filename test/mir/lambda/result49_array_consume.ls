// A consumed local append must preserve snapshots and a self-concat source.
fn echo_array(values: array) array => values
let global_array: array = [9]
fn read_global_array() array => global_array
type SnapshotChild = {v: int}
type SnapshotRecord = {v: int, child: SnapshotChild}

pn concat_typed_array() {
    let base: SnapshotChild[] = [{v: 1}, {v: 2}]
    let loose: array = [{v: 3}]
    let extended: SnapshotChild[] = base ++ loose
    let again: SnapshotChild[] = extended ++ base
    let incompatible: array = base ++ [true]
    print(len(base)); print(" ")
    print(len(extended)); print(" ")
    print(len(again)); print(" ")
    print(again[4].v); print(" ")
    print(incompatible[2]); print("\n")
    var changed: SnapshotChild[] = extended
    changed[0].v = 9
    print(base[0].v); print(" ")
    print(extended[0].v); print(" ")
    print(changed[0].v); print("\n")
}

pn consume_open_array() {
    var changes: array = []
    changes = changes ++ [{v: 1}]
    let before: array = changes
    changes = changes ++ [{v: 2}]
    let first = changes[0].v
    changes = changes ++ changes
    print(len(before)); print(" ")
    print(len(changes)); print(" ")
    print(before[0].v); print(" ")
    print(changes[1].v); print(" ")
    print(first); print("\n")

    var mixed: array = [{v: 3}, 1.5]
    mixed = mixed ++ [{v: 4}, 2.5]
    print(len(mixed)); print(" ")
    print(mixed[1] == 1.5); print(" ")
    print(mixed[3] == 2.5); print("\n")

    let shared: array = [7]
    var received: array = echo_array(shared)
    received = received ++ [8]
    print(len(shared)); print(" ")
    print(len(received)); print("\n")

    var from_global: array = read_global_array()
    from_global = from_global ++ [10]
    print(len(global_array)); print(" ")
    print(len(from_global)); print("\n")

    var record: SnapshotRecord = {v: 11, child: {v: 12}}
    let saved_record: SnapshotRecord = record
    record.v = 13
    print(saved_record.v); print(" ")
    print(record.v); print(" ")
    print(saved_record.child.v); print("\n")
}

pn main() { consume_open_array(); concat_typed_array() }

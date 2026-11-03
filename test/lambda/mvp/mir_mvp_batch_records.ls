// admitted prefixes and container slots survive COW, recursion and GC (D3.2.4v4, D5.3).
type BatchNode = {value: int, next: BatchNode?}
type BatchRecord = {count: int, values: float[], child: BatchNode?, absent: float[]?}
type BatchCounter = {count: int}

fn batch_record(extra: bool) BatchRecord {
    if (extra) {
        return {extra: "kept", child: {value: 8, next: null},
            absent: null, values: [1.25, 2.5], count: 4}
    }
    return {count: 5, values: [3.5, 4.5], child: null, absent: [6.5]}
}

fn batch_read(record: BatchRecord?) any {
    return [record.count, record.values[1], record.child.value, record.absent[0]]
}

fn batch_lookup(records: BatchRecord[], at: int) any {
    return [records[at].count, records[at].values[0]]
}

fn batch_length(node: BatchNode?) int^ {
    var cursor = node
    var count: int = 0
    while (cursor != null) {
        count = count + cursor.value
        cursor = cursor.next
    }
    return count
}

fn batch_checked(value: any) float[]^ { return value }
fn batch_take(values: float[]) int { return len(values) }
fn batch_child(record: BatchRecord) float[] { return record.values }
pn batch_write(var values: float[]) any { values[0] = 99.0 }
pn batch_forward(value: any) any {
    var local = batch_checked(value)
    return batch_take(local)
}
pn batch_rebound() any {
    var local = batch_checked([1.0])
    local = ["wrong"]
    return batch_take(local)
}
pn batch_mutated() any {
    var local = batch_checked([1.0])
    local[0] = "wrong"
    return batch_take(local)
}

pn batch_recur(var counter: BatchCounter, var data: int[], depth: int) any {
    counter.count = counter.count + 1
    if (depth > 0) {
        var i: int = 0
        while (i < 2) {
            batch_recur(counter, data, depth - 1)^
            data[0] = data[0] + 1
            i = i + 1
        }
    }
}

pn main() {
    print(batch_read(batch_record(true))); print("\n")
    print(batch_read(batch_record(false))); print("\n")
    print(batch_read(null)); print("\n")
    let records: BatchRecord[] = [batch_record(false)]
    print(batch_lookup(records, 0)); print("\n")
    print(batch_lookup(records, -1)); print("\n")
    print([batch_length(null), batch_length({value: 8, next: {value: 3, next: null}})]); print("\n")
    print([batch_forward([2.0]), batch_forward("bad") is error,
        batch_rebound() is error, batch_mutated() is error]); print("\n")
    let original = batch_record(false)^
    var child: float[] = batch_child(original)
    batch_write(child)^
    print([original.values[0], child[0]]); print("\n")
    var projected: float[] = original.values
    projected[0] = 99.0
    print([original.values[0], projected[0]]); print("\n")
    var counter: BatchCounter = {count: 0}
    var data: int[] = [3, 7]
    let saved_counter = counter
    let saved_data = data
    batch_recur(counter, data, 2)^
    print([counter.count, data[0], saved_counter.count, saved_data[0]]); print("\n")
}

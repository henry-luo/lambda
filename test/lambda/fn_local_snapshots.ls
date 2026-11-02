// S9.1.2/S9.1.4v2: input copies and escaping pure captures remain snapshots.
fn edited(xs) {
    var work = xs
    let saved = work
    work[0] = 99
    return [saved, work]
}

fn reader(seed) {
    var value = seed
    let read = () => value
    value = value + 7
    return read
}

fn record() {
    var work = {inner: {value: 1}}
    let saved = work.inner
    work.inner.value = 2
    return [saved, work]
}

fn widened() {
    var value = 1
    value = "done"
    return value
}

fn container_reader() {
    var values = [1, 2]
    let read = () => values[0]
    values[0] = 3
    return read
}

let source = [1, 2]
let read = reader(40);
let read_container = container_reader();
[edited(source), source, read(), record(), widened(), read_container()]

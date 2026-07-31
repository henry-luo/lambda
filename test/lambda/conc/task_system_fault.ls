// ER-S5: a task-poll boundary owns a C14 fault after a worker starts.
fn overflow(n) => n + overflow(n + 1)

pn worker() {
    overflow(0)
}

pn main() {
    let handle = start worker()
    let value^err = wait(handle)
    print([value, ^err, err.code, err.message])
}

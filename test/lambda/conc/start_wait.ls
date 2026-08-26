pn worker() {
    sleep(1)
    return 42
}

pn main() {
    let handle = start(worker, [], {mode: 'task'})
    print(wait(handle)^)
}

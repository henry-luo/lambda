// ER-S3: a local error boundary must be fully retired before this later await
// spills the async procedure state.
pn child() {
    sleep(1)^
    3
}

pn main() {
    let value^err = 7
    let handle = start child()
    let result = wait(handle)^
    print([value, ^err, result])
}

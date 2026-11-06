// S13.1.2v2/D7.5.3: ordinary calls retain owned values while parked.
pn parked(value) {
    let saved = [value, 3e-310, "retained"]
    sleep(1)^
    return saved
}
pn invoke(operation, value) { return operation(value) }
pn main() {
    let operation = parked
    let result = invoke(operation, 17)
    print([result[0], result[1] == 3e-310, result[2]]); print("\n")
    var denied = false
    wait(null) ^ { denied = ^ is error }
    print(denied); print("\n")
    let snapshot = [1e-300]
    let second = parked(snapshot)
    print([second[0][0] == 1e-300, snapshot[0] == 1e-300]); print("\n")
}

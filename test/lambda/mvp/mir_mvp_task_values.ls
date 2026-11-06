// S13.1.1v3/S13.1.4: task argument vectors own values across parking and GC.
pn delayed(value) { sleep(1)^; return value }
pn main() {
    let packed = start(delayed, [17])
    print(wait(packed)^); print("\n")
    let operation = delayed
    let nested = start(operation, [[3e-310, 1e-300, "retained"]])
    let result = wait(nested)^
    print([result[0] == 3e-310, result[1] == 1e-300, result[2]]); print("\n")
    let captured = 9
    pn closure() { sleep(1)^; return captured }
    let captured_task = start(closure)
    print(wait(captured_task)^); print("\n")
    var values = [3e-310]
    let snapshot = start(delayed, [values])
    values[0] = 7
    let held = wait(snapshot)^
    print([held[0] == 3e-310, values[0] == 7]); print("\n")
}

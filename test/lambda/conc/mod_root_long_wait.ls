// S13.1.3v2: both root waits cross the injected watchdog with a real wake pending.
pn slow_child() {
    let result = io.exec("sleep", ["0.65"])^
    return result.exit_code
}
pn main() {
    let before = clock()
    sleep(250)^
    if (clock() - before >= 0.2) print("timer passed\n")
    let launched = clock()
    let child = start(slow_child)
    let code = wait(child)^
    if (code == 0 and clock() - launched >= 0.55) print("process passed\n")
}

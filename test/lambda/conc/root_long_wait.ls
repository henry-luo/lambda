// A root task's wait outlives the scheduler's 5 s idle watchdog when the loop
// still holds what will wake it: its own timer, a child parked on a timer, an
// offloaded child process. Each wait below failed with E502 ("task wait cannot
// complete: nothing else can run") once 5 s passed without a task step.
pn slow_child() {
    let r = io.exec("sleep", ["5.6"])^
    r.exit_code
}
pn main() {
    let child = start(slow_child)
    let t0 = clock()
    sleep(5500)^
    let waited = clock() - t0
    let code = wait(child)^;
    [waited >= 5.4, code == 0]
}

// T29-1 (D4.4.4v4): a record place spelled in several statements is navigated
// once. The first read binds the handle -- un-sharing the spine and the leaf,
// because a later statement writes through it -- and every later read and
// write is an offset through the handle. A place whose container is replaced
// between spellings is navigated again.
type Task = {state: int, prio: int, link: int}
type World = {tasks: Task?[], count: int}

pn step(var w: World, t: int) int {
    var s = w.tasks[t].state
    var p = w.tasks[t].prio
    if (s == 0) {
        w.tasks[t].state = 1
        return p
    }
    w.tasks[t].link = s + p
    return w.tasks[t].link
}

pn reset(var w: World, t: int) int {
    var s = w.tasks[t].state
    w.tasks = [{state: 0, prio: 0, link: 0}]
    return s + w.tasks[0].state
}

pn main() {
    var w: World = {tasks: [{state: 0, prio: 4, link: 0}, {state: 2, prio: 3, link: 0}], count: 2}
    print(step(w, 0)); print(" ")
    print(step(w, 1)); print(" ")
    print(w.tasks[0].state); print(" ")
    print(reset(w, 1)); print("\n")
}

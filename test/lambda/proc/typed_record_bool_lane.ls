// T29-2: a bool field read through an optional record element yields the
// nullable bool lane (0/1/2, D2.5.2v3). The member emitter published that raw
// lane with the wrapper contract's TypeId, so every consumer read it as an
// Item: a declared return passed the raw byte to the type check (a crash), and
// a local binding branched on it through is_truthy (a wrong answer). The
// descriptor now carries the lane's own TypeId (D2.4.1-D2.4.3).
type Task = {link: int, pp: bool, tw: bool, th: bool}
type World = {n: int, tasks: Task?[]}

pn ret_bool(w: World, i: int) bool { return w.tasks[i].tw }
pn ret_opt(w: World, i: int) bool? { return w.tasks[i].tw }

pn local_branch(var w: World, i: int) int {
    var x = w.tasks[i].tw
    if (x) { return 1 }
    return 0
}

pn local_eq(var w: World, i: int) int {
    var x = w.tasks[i].tw
    if (x == true) { return 1 }
    if (x == false) { return 2 }
    return 3
}

// the richards shape: three flags read into locals, then compared
pn held_or_waiting(var w: World, tid: int) int {
    var th = w.tasks[tid].th
    if (th == true) { return 1 }
    var pp = w.tasks[tid].pp
    var tw = w.tasks[tid].tw
    if (pp == false) {
        if (tw == true) { return 1 }
    }
    return 0
}

pn main() {
    var w: World = {n: 0, tasks: fill(3, null)}
    var waiting: Task = {link: 0, pp: false, tw: true, th: false}
    var running: Task = {link: 1, pp: true, tw: true, th: false}
    w.tasks[1] = waiting
    w.tasks[2] = running
    print("ret_bool=" ++ ret_bool(w, 1) ++ "\n")
    print("ret_opt=" ++ ret_opt(w, 0) ++ "," ++ ret_opt(w, 1) ++ "\n")
    print("branch=" ++ local_branch(w, 0) ++ "," ++ local_branch(w, 1) ++ "\n")
    print("eq=" ++ local_eq(w, 0) ++ "," ++ local_eq(w, 1) ++ "\n")
    print("held=" ++ held_or_waiting(w, 1) ++ "," ++ held_or_waiting(w, 2) ++ "\n")
}

// D4.4.4v4: consecutive writes through one typed record place bind a writing
// handle on the first store. A later index change must navigate again.
type Flags = {pp: bool, tw: bool, th: bool, marker: int}
type World = {tasks: Flags?[]}
type Mixed = {a: bool, b: any, c: bool}
type MixedWorld = {rows: Mixed?[]}

pn set_flags(var w: World, t: int) any {
    w.tasks[t].pp = true
    w.tasks[t].tw = false
    w.tasks[t].th = false
}

pn move_flags(var w: World, t: int) any {
    w.tasks[t].pp = true
    t = 1
    w.tasks[t].tw = true
}

pn set_mixed(var w: MixedWorld, i: int) any {
    w.rows[i].a = true
    w.rows[i].b = false
    w.rows[i].c = true
}

pn main() {
    var w: World = {tasks: [
        {pp: false, tw: true, th: true, marker: 17},
        {pp: false, tw: false, th: false, marker: 29},
        null
    ]}
    let snapshot = w
    set_flags(w, 0)
    print(w.tasks[0].pp); print(" ")
    print(w.tasks[0].tw); print(" ")
    print(w.tasks[0].th); print(" ")
    print(snapshot.tasks[0].tw); print(" ")
    move_flags(w, 0)
    print(w.tasks[0].pp); print(" ")
    print(w.tasks[1].tw); print(" ")
    print(w.tasks[0].marker); print(" ")
    print(w.tasks[1].marker); print(" ")
    var bad_null = false
    set_flags(w, 2) ^ { bad_null = true }
    print(bad_null); print(" ")
    var mixed: MixedWorld = {rows: [{a: false, b: 7, c: false}]}
    let before_mixed = mixed
    set_mixed(mixed, 0)
    print(mixed.rows[0].a); print(" ")
    print(mixed.rows[0].b); print(" ")
    print(mixed.rows[0].c); print(" ")
    print(before_mixed.rows[0].b); print("\n")
}

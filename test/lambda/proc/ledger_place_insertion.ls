// S9.1.2/S9.3.1: container insertion captures the selected borrowed value.
pn index_once(var counter) int { counter.n = counter.n + 1; return 0 }
pn main() {
    var rows = []
    push(rows, [1, 2])
    push(rows, [3, 4])
    var saved = {x:null}
    saved.x = rows[0]
    rows[0][1] = 9
    print([saved.x, rows[0]]); print("\n")
    var source = {child:{n:1}}
    var branch = {child:null}
    branch.child = if (true) source.child else null
    source.child.n = 2
    print([branch.child.n, source.child.n]); print("\n")
    var indexed = []
    push(indexed, [5, 6])
    var destination = [null]
    destination[0] = indexed[0]
    indexed[0][1] = 8
    print([destination[0], indexed[0]]); print("\n")
    var tree = {child:{n:3}}
    var retained = {child:null}
    retained.child = tree.child or null
    retained.child.n = 7
    print([tree.child.n, retained.child.n]); print("\n")
    var counted = []
    push(counted, [10, 20])
    var counter = {n:0}
    var output = {x:null}
    output.x = counted[index_once(counter)]
    counted[0][0] = 30
    print([counter.n, output.x, counted[0]]); print("\n")
    // LR12-34's reassignment half was fixed by the preceding alias pass.
    var row = [0]
    row = counted[0]
    counted[0][1] = 40
    print([row, counted[0]]); print("\n")
}

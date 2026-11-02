// S2.5.3v3: adding local state does not change fn result collection.
fn collected() {
    1
    var x = 0
    while (x < 4) { x = x + 1 }
    x
}

fn replacement() {
    1
    return 2
    3
}

fn no_result() { var x = 3 }
fn controlled() {
    1
    var x = 0
    if (true) { x = 1 }
    x
}
let arrow = (n) => {
    var x = n
    while (x < 5) { x = x + 1 }
    return x
}
let empty_map = () => {}
let map_arrow = (x) => {key: x};

{collected: collected(), controlled: controlled(), replacement: replacement(), no_result: no_result(),
 arrow: arrow(2), empty: empty_map(), mapped: map_arrow(7)}

// S9.1.2/S9.3.1: returning a retained module value preserves its snapshot.
type Box = {values: int[]}
pub let shared_box: Box = {values: [5]}

fn retained_box(n: int) Box^ => shared_box

pn main() {
    var local: Box = retained_box(0)^
    local.values[0] = 6
    print(shared_box.values[0] ++ " " ++ local.values[0] ++ "\n")
}

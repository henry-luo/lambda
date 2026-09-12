// A field present in only one open union arm cannot be a static projection
// proof. Its child still carries a map shape that can discharge Node admission.
type Node = {kind: string} | {kind: string, child: Node}

fn leaf() Node => {kind: "leaf"}
fn branch(child: Node) Node => {kind: "branch", child: child}
fn make(depth: int) Node =>
    if (depth == 0) leaf() else branch(make(depth - 1))
fn count(node: Node) int^ =>
    if (node.kind == "leaf") 1 else 1 + count(node.child)

pn main() {
    let tree: Node = make(10)
    var total = 0
    var iteration = 0
    while (iteration < 20) {
        total = total + count(tree)
        iteration = iteration + 1
    }
    var rejected = false
    count({kind: "branch", child: 42}) ^ { rejected = true }
    print([total, rejected])
}

type Node = {kind: string, value: int, child: Node?} |
    {kind: string, text: string, child: Node?}
type Open = {value: int} | {child: Open}

fn leaf() Node => {kind: "leaf", text: "seven", child: null}
fn make(depth: int) Node =>
    if (depth == 0) leaf() else {kind: "branch", value: 1, child: make(depth - 1)}
fn identity(node: Node) Node => node
fn count(node: Node) int =>
    if (node.kind == "leaf") 7 else 1 + count(node.child)
fn read(node: Node) int => count(identity(node))
pn missing(node: Node) any^ { return count(node.child) }
fn open_read(node: Open) any => node.value
pn extra(node: Open) any^ { return open_read(node.child) }

pn main() {
    let tree: Node = make(10)
    var total = 0
    var i = 0
    while (i < 20) {
        total = total + read(tree)
        i = i + 1
    }
    var failed = false
    missing(leaf()) ^ { failed = true }
    var extra_failed = false
    extra({value: 1, child: {invalid: true}}) ^ { extra_failed = true }
    print([total, failed, extra_failed])
}

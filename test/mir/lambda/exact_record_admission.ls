// D3.2.4v4: exact trusted records cross repeated and nullable boundaries
// unchanged; a differently laid-out dynamic record still needs admission.
type Node = {value: int, next: Node?}
type Box = {node: Node?, label: string}

fn relay(value: Node) Node => value
fn read_node(node: Node?) int => if (node == null) 0 else node.value
fn wrap(node: Node?) Box => {node: node, label: "held"}

let first: Node = {value: 1, next: null}
let second: Node = {value: 2, next: first}
let dynamic: any = {next: second, value: 3}
let third: Node = dynamic
let box: Box = wrap(relay(third));

[read_node(box.node), read_node(box.node.next), read_node(null), box.label]

// D4.2.5/S11.4.10: common field domains do not prove common offsets.
type Node = {tag: string, value: int, child: Node?} |
    {tag: string, text: string, child: Node?}
type Open = {tag: string} | {tag: string, child: Node}

fn walk(node: Node) int^ =>
    if (node.tag == "leaf") 0 else 1 + walk(node.child)

fn open_child(node: Open) int^ => walk(node.child)
fn erased(node: any) int^ => walk(node)

pn changed(node: Node) any {
    var alias = node
    alias.child = 42
    return [walk(alias) is error, walk(node)]
}

type Payload = {number: int, label: string}
type Envelope = {tag: string, payload: Payload} |
    {tag: string, payload: Payload, extra: bool}
fn payload_number(value: Payload) int => value.number
fn envelope_number(value: Envelope) int^ => payload_number(value.payload)

pn main() {
    let leaf: Node = {tag: "leaf", text: "end", child: null}
    let branch: Node = {tag: "branch", value: 1, child: leaf}
    let tree: Node = {tag: "branch", value: 1, child: branch}
    let parsed = parse("{\"tag\":\"raw\",\"payload\":{\"label\":\"x\",\"number\":9}}", "json")
    print([walk(tree), erased(null) is error,
        open_child({tag: "branch", child: 42}) is error, changed(tree), envelope_number(parsed)])
}

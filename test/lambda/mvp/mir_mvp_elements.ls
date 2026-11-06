// D3.3.1/S2.5.5v2: element content and attribute storage use distinct append rules.
fn make_node(label: string) {
    <node id: label,
        "a"
        {null}
        ""
        "b"
        {(1, 2)}
        {[3, 4]}
    >
}

fn echo_element(value: element) element { value }
fn echo_date(value: datetime) datetime { value }
fn echo_bytes(value: binary) binary { value }

pn main() {
    let node = echo_element(make_node("sample"))
    print([node.id, content(node), content(<empty>)]); print("\n")
    let key = "id"
    let attributes = {id: "first", retained: 7}
    let dynamic = <node [key]: "middle", *: attributes, [key]: "last", "new">
    print([dynamic.id, dynamic.retained, content(dynamic)]); print("\n")
    let stamp = echo_date(t'2025-04-26')
    let bytes = echo_bytes(b'\x010203')
    print([stamp.year, stamp.month, stamp.day, len(bytes)]); print("\n")
    let child = [1, 2]
    let retained = <node values: child, child>
    print([retained.values, content(retained), child]); print("\n")
}

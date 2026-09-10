type Node = {kind: string, value: int} |
    {kind: string, child: Node, ready: bool} |
    {kind: string, children: Node[]}

pn main() {
    let valid = {kind: "branch", child: {kind: "leaf", value: 7}, ready: true}
    let invalid = {kind: "branch", child: {bogus: true}, ready: true}
    let valid_list = {kind: "list", children: [valid]}
    let invalid_list = {kind: "list", children: [invalid]}
    let admitted: Node = valid
    print([valid is Node, invalid is Node, valid_list is Node,
        invalid_list is Node, admitted.child.value, admitted.ready])
}

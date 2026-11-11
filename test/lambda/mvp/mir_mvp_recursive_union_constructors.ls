// D4.2.5/D3.4.1: tagged recursive fields retain their descriptor and admission.
type Tree = {leaf: int} | {left: Tree, right: Tree}
fn build(depth: int) Tree {
    if (depth == 0) {
        var leaf = {leaf: depth}
        return leaf
    }
    var branch = {left: build(depth - 1), right: build(depth - 1)}
    return branch
}
fn checked(child: any) Tree {
    var branch = {left: child, right: {leaf: 1}}
    return branch
}
pn main() {
    print(build(1)); print("\n")
    let saved = build(3)
    let garbage = [for (i in 1 to 40) {value: string(i)}]
    print([saved.left.right.left.leaf, saved.right.left.right.leaf, len(garbage)]); print("\n")
    print(checked({leaf: 3})); print("\n")
    print(type(checked(7)) == error); print("\n")
}

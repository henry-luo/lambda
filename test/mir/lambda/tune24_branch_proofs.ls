type Leaf = {value: int}
type Tree = Leaf | {left: Tree, right: Tree}
type Branch = {left: Tree, right: Tree}

fn total(tree: Tree) int {
    if (tree is Leaf) tree.value
    else if (tree is Branch) total(tree.left) + total(tree.right)
    else 0
}

fn twice(tree: Tree) int => total(tree) + total(tree)

fn checked_child(tree: any) int {
    if (tree is Branch) {
        let first = twice(tree.left)
        first + twice(tree.left)
    } else 0
}

fn scoped(tree: any, take: bool) int {
    let ignored = if (take) checked_child(tree) else 0
    checked_child(tree) + ignored
}

fn dominance(tree: any, take: bool) int {
    let first = if (take) twice(tree) else 0
    first + twice(tree)
}

// Structural membership does not impose the named record's packed offsets.
let child = {extra: true, value: 7}
let tree = {padding: "before", right: {value: 3}, left: child};
[total(tree), checked_child(tree), scoped(tree, false), scoped(tree, true),
 checked_child({left: null, right: child}), total({value: 5}),
 dominance(tree, false), dominance(tree, true)]

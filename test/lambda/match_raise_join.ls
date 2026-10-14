// S11.2.1: raising one match arm must not erase sibling results on the JIT.
fn choose(n) element^ {
    if (n == null) { <empty> }
    else match n {
        case 1: <one "value">
        case 2: choose(1)^
        case 4: <outer choose(1)^>
        case 5: <outer value:choose(1)^>
        default: raise error("unsupported")
    }
}
fn after(n) int^ {
    let chosen = match n {
        case 1: 42
        default: raise error("unsupported")
    };
    chosen + 1
}
fn map_tree(n) => if (n < 0) map_tree(0) else if (n == 0) {leaf:true}
    else {child:map_tree(n-1)}
fn array_tree(n) => if (n < 0) array_tree(0) else if (n == 0) [0]
    else [array_tree(n-1)];
[name(choose(1)^) == 'one', name(choose(2)^) == 'one',
    name(choose(null)^) == 'empty', after(1)^ == 43,
    (choose(3) or "caught") == "caught", name(choose(4)^[0]) == 'one',
    name(choose(5)^.value) == 'one', map_tree(2).child.child.leaf == true,
    array_tree(2)[0][0][0] == 0]

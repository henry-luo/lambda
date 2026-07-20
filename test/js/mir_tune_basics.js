// Structural MIR probe: expressions, bindings, and branches.
function tuneEmpty() { return undefined; }
function tuneConstant() { return 42; }
function tuneNumeric(a, b) {
    let sum = a + b;
    let product = sum * 3;
    return product - 1;
}
function tuneDynamicAdd(a, b) { return a + b; }
function tuneBindings(a) {
    let first = a;
    const second = first;
    let third = second;
    third = first;
    return third;
}
function tuneBranch(x) {
    if (x > 0) return x + 1;
    return 1 - x;
}
function tuneLogical(a, b, fallback) { return (a && b) || fallback; }
function tuneTernary(x, a, b) { return x ? a : b; }
tuneEmpty();
tuneConstant();
tuneNumeric(1.5, 2.5);
tuneDynamicAdd("value:", 7);
tuneBindings({value: 1});
tuneBranch(4);
tuneLogical(true, "yes", "no");
tuneTernary(false, 1, 2);

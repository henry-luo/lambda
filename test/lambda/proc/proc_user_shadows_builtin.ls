// A user-defined function shadows a system function of the same name.
// This held for `fn` but not for `pn`: the shadow check in build_ast.cpp only
// accepted AST_NODE_FUNC, so a `pn` sharing a builtin's name AND arity was
// silently discarded in favour of the builtin — no error, no warning, and no
// output at all. `emit` is the natural probe: it is a sysproc of arity 2, so
// only a two-argument user definition collides with it.

pn emit(label, value) {
    print("user-pn-emit:")
    print(label)
    print("=")
    print(value)
    print("\n")
}

// `fn` shadowing already worked; keep it covered so the two stay in step
fn len(x) => 999

pn main() {
    emit("a", 1)
    emit("b", 2)
    print("fn-shadow-len:")
    print(len("abcd"))
    print("\n")
}

// S16.10.2 clarification: a declaration's own name is a binding; the members
// it introduces are NOT — fields and methods alike are data names, so both
// may be keywords, and both are reached through a receiver.
type Toggle {
    on: bool = false,        // keyword field name
    order: int = 1,          // clause-word field name
    fn state() => order      // keyword method name
    fn sum() => order + 1    // method named after a system function
    pn enable() { on = true }
}
pn main() {
    var t = <Toggle>
    print(t.state()); print("\n")
    print(t.on); print("\n")
    t.enable()
    print(t.on); print("\n")
    // a method named `sum` is NOT a shadow: S12.3.7 governs module bindings
    // only, and a method is reached solely as `t.sum()`.
    print(t.sum()); print("\n")
    print(sum([1, 2, 3]))
}

// S11.4.12: a type declared in a block binds from its declaration on, as a
// block-local function does; only a top-level type is visible before it.
fn g() {
  type L1 = L2[]
  type L2 = int
  let ok = [1] is L1
  ok
}
g()

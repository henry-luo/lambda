// @expect-error: E224
// @description: S12.1.1v2 (LR03-24): a `that` predicate is `fn` context
// wherever it is written, a `pn` body included, because `is` runs it for any
// caller. A declared type, a field constraint, an object-level constraint and
// an inline match arm each call a procedure here.
pn eff(x) { print("effect "); x }
type Bad = int that eff(~) > 3
type Obj { a: int that eff(~) > 0, that (eff(a) > 1) }
pn main() {
  let r = match 5 {
    case int that eff(~) > 3: "big"
    default: "small"
  }
  print([r, 5 is Bad])
}

// S9.1.1 (LR12-27): `push(b, v)` is `b' = b ++ [v]`. On an open numeric array
// the push was a silent no-op; it now keeps the lane when every item fits and
// widens in place otherwise, as an index write does. A past `int[]` admission
// is not the open binding's contract (SI3v2), and aliases keep their value.
pn g(x: int[]) { len(x) }
pn main() {
  var a = [1, 2, 3]
  push(a, 4)
  var b = [1, 2, 3]
  push(b, "s")
  var c = [1, 2, 3]
  push(c, 1.5)
  var d = [1, 2]
  push(d, (7, 8))
  var e = [1, 2]
  push(e, ())
  var f = [1.5, 2.5]
  push(f, 3.5)
  var h = [true, false]
  push(h, true)
  var u = [1, 2, 3]
  g(u)
  push(u, "s")
  var a0 = [1, 2, 3]
  let snap = a0
  push(a0, 9)
  var big = [for (i in 1 to 20) i]
  push(big, 21)
  print([a, b, c, d, e, f, h, u, a0, snap, len(big), big[20]])
}

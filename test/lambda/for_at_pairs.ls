// S8.1.3: axis selects membership, arity selects the projection.
// `at` walks name keys only; the paired form binds (key, value).
let m = {a: 1, b: 2, c: 3}
let pairs = for (k, v at m) k ++ "=" ++ v
let keys = for (k at m) k
let filtered = for (k, v at {a: 1, b: 5, c: 2} where v > 2) k
// the axis still restricts membership: attributes only, no children
let e = <d id: "m", cls: "c", "txt">
let attrs = for (k, v at e) k ++ "=" ++ v
let both = for (k, v in e) "" ++ v
// an IntKey is not a name, so paired `at` over an array is empty
let none = for (k, v at [10, 20]) k
let out = [pairs, keys, filtered, attrs, both, none]
out

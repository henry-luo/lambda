// LR07-46 (D5.3.3): iterating a map holds its keys in a native RootFrame that
// item_keys pushes above the caller's published frame. Each filter loop below
// has a call-free path (the compare's inline fast path), so publication
// analysis once failed to prove the loop published and re-stored the caller's
// frame top on each pass, rewinding side_root_top beneath that key frame. The
// next callee then overwrote the keys: a crash for int members, lost members
// for string ones. Every path into the loop has published, so it needs no store.

{a: 1, b: 2, c: 3} |: ~ != 2
{a: "x", b: "y", c: "z"} |: ~ != "y"
fn keep_unless(m, x) => m |: ~ != x
keep_unless({p: 5, q: 6, r: 5, s: 7}, 5)

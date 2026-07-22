// MT5 fixture: the array under construction is rooted before any collecting
// call. This is the BUG-001 shape -- JIT locals were not rooted across
// array_end when a GC could fire, so an in-progress array could be collected
// while still being filled. Each element here comes from a user function call,
// which is a safepoint.
// Checked by array_root_before_gc.mir-check (Stack API #7).

fn boxed(n: int) { n * 3 }

let items = [boxed(1), boxed(2), boxed(3)]

items

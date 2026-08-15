// Loop-carried representation widening.
//
// `var s = 0` is an int-lane binding; `s + <int64>` types as DECIMAL (int and
// int64 join in the exact INTEGER domain). The assignment therefore widens the
// binding to a boxed carrier — but a loop body is emitted in ONE pass, so the
// read of `s` at the loop top must already agree with that carrier, and the
// return-lane proof must see the loop's assignment as a reassignment.
//
// Before the fix this SEGFAULTED (a boxed decimal decoded through the stale
// int lane became `inf`, then the native-int return dereferenced it), and
// after the crash was fixed it still returned `inf` for sums past 2^53.
// Values here are deliberately far past int53 so a lane saturation shows up.

pn accumulate(n: int) {
  var s = 0
  for (i in 1 to n) { s = s + 999999999999999i64 }
  return s
}

// while-loop form: the same widening through the other loop node
pn accumulate_while(n: int) {
  var s = 0
  var k = 0
  while (k < n) {
    s = s + 999999999999999i64
    k = k + 1
  }
  return s
}

// nested loop: the inner body widens a binding declared outside BOTH loops
pn accumulate_nested(n: int) {
  var s = 0
  for (i in 1 to n) {
    for (j in 1 to 2) { s = s + 999999999999999i64 }
  }
  return s
}

// widening inside a conditional arm of the loop body
pn accumulate_guarded(n: int) {
  var s = 0
  for (i in 1 to n) {
    if (i > 0) { s = s + 999999999999999i64 }
  }
  return s
}

// CONTROL: a plain int accumulator must NOT be widened (stays on the int lane)
pn plain_int(n: int) {
  var acc = 0
  for (i in 1 to n) { acc = acc + i }
  return acc
}

// CONTROL: a declared int64 accumulator types as int64 throughout
pn declared_i64(n: int) {
  var s = 0i64
  for (i in 1 to n) { s = s + 999999999999999i64 }
  return s
}

pn main() {
  print([accumulate(3), accumulate(100), accumulate(2000)])
  print([accumulate_while(100), accumulate_nested(50), accumulate_guarded(100)])
  print([plain_int(10), declared_i64(3)])
  return 0
}

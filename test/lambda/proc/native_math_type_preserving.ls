// Type-preserving unary math lowered natively (generalizes what TRUNC alone had).
//
// floor/ceil/round/trunc return their argument UNCHANGED for an integer
// (`fn_numeric_rounding` literally does `return item;` for the int family), and
// for a float they are exactly `push_d(<C fn>(get_double()))` — so the native C
// call is the same computation without the Item round trip. `abs` shares the
// float form (`push_d(fabs(v))`) but NOT the int form, since |x| is not the
// identity; it stays on the boxed helper for integers. `sign` has no native C
// function at all and stays boxed everywhere.
//
// This pins SEMANTICS, which is what the boxed path was protecting: the result
// type must follow the argument's, and rounding must stay half-away-from-zero.

pn floats(x: float) { return [floor(x), ceil(x), round(x), trunc(x), abs(x)] }
pn ints(n: int) { return [floor(n), ceil(n), round(n), trunc(n), abs(n)] }

// type preservation is the whole reason these are excluded from the
// always-float whitelist — an int argument must not come back as a float
pn kinds(x: float, n: int) {
  return [type(floor(n)), type(ceil(n)), type(round(n)), type(trunc(n)),
          type(abs(n)), type(floor(x)), type(abs(x))]
}

pn main() {
  print(floats(2.5))     // half away from zero, positive
  print(floats(-2.5))    // half away from zero, negative
  print(floats(1.2))
  print(floats(-1.2))
  print(ints(5))
  print(ints(-5))        // abs(-5) must be 5 while the others stay -5
  print(kinds(1.5, 7))
  print([sign(-2.5), sign(3)])   // still boxed; must keep working
  return 0
}

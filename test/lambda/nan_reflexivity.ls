// Formal semantics 5: nan values never equal anything, THEMSELVES INCLUDED --
// the only breaks in reflexivity. That must hold across the whole nan family:
// float's bare nan, int.nan, integer.nan, decimal.nan.
//
// Regression guard. int.nan used to compare EQUAL to itself: an int lowers to a
// SIGNED runtime part via lambda_int_item_to_i64, which discards nan-ness, so
// the isnan() guard in lambda_numeric_compare never saw it and two identical
// int.nan sentinels came out equal.

fn dv(a, b) => a div b

"-- nan is never equal to itself --"
dv(0, 0) == dv(0, 0)                              // int.nan
dv(0n, 0n) == dv(0n, 0n)                          // integer.nan
dv(0m, 0m) == dv(0m, 0m); // decimal.nan
((1.0e308 * 10.0) - (1.0e308 * 10.0)) == ((1.0e308 * 10.0) - (1.0e308 * 10.0))

"-- but inf IS equal to itself, and signed infinities stay distinct --"
dv(1, 0) == dv(1, 0)
dv(1, 0) == dv(-1, 0)

"-- and poison still orders --";
[dv(1, 0) > 100, dv(-1, 0) < 0, dv(1, 0) > dv(-1, 0)]

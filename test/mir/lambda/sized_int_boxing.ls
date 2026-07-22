// MT5 fixture: uniform 64-bit integer boxing at the representation boundary.
// Neither INT64 nor UINT64 has an inline Item encoding, so both extremes go
// through the number-stack boxers rather than any inline tagging path. The
// i64 maximum is the value that used to collide with the retired push_l
// sentinel, so it is pinned here deliberately.
// Checked by sized_int_boxing.mir-check (Stack API #25).

let big_signed = 9223372036854775807i64
let big_unsigned = 18446744073709551615u64

[big_signed, big_unsigned]

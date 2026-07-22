// MT5 fixture: caller-donated canonical scalar home.
// A callee returning a pointer-backed 64-bit integer receives the caller's
// number-stack slot as a hidden trailing argument, and the boxed payload is
// adopted into that home before the callee number watermark is restored.
// Checked by scalar_home_donation.mir-check (Stack API #15, #16, #19, #21).

fn twice(a) { a * 2 }

twice(int64(4000000000))

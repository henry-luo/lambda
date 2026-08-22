// v3 fixture: a wide return travels on the companion lane.
// The boxed body has no caller-home parameter; its public wrapper publishes
// lane 2 through Context::mir_companion_slot and the caller resolves the pair
// in its active number extent. Checked by scalar_home_donation.mir-check
// (Stack API #15, #16, #19, #21).

fn twice(a) { a * 2 }

twice(i64(4000000000))

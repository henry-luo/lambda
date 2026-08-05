// An inferred raw specialization accepts only its exact non-null carrier.
// A nullable argument must use the boxed slow body so absence still propagates.
fn add_one(value) => value + 1

let present: int? = 4
let absent: int? = null
[add_one(present), add_one(absent)]

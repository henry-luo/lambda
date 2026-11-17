// S11.4.2v3 / S11.4.6v4: error unions admit constrained bases at boundaries.
type First = error that ~.message == "first"
type Nested = First that ~.code == 318
type MaybeError = (error | null) that true
fn failing(message:string) int | First => raise error(message)
fn relay(message:string) int | First => failing(message);
fn nested() int | Nested => raise error("first")
fn success() int | First => 7;
fn accept(value:int | First) int | First => value;
fn converted() float | First => 7;
fn converted_nullable() float | MaybeError => 7;
fn absent() float | MaybeError => null;
fn only_null() MaybeError => null
fn relay_null() MaybeError => only_null();
fn relay_null_explicit() MaybeError { return only_null() }
fn bind_null() MaybeError { let value: MaybeError = only_null(); value }
fn pass_null(value:MaybeError) MaybeError => value;
let null_step = relay_null();
let explicit_null = relay_null_explicit();
let bound_null = bind_null();
let passed_null = pass_null(only_null());
fn filtered(message:string) int | string | error => failing(message) ^First { "filtered" }^;
[(failing("first") ^ { ^.message }), (failing("other") ^ { ^.message }),
 (relay("first") ^ { ^.message }), (relay("other") ^ { ^.message }),
 (failing("first") ^First { "matched" } ^ { "missed" }),
 (failing("other") ^First { "matched" } ^ { "missed" }),
 (nested() ^ { ^.message }), success()^,
 (accept(error("first")) ^ { ^.message }), (accept(error("other")) ^ { ^.message }),
 (filtered("first") ^ { ^.message }), (filtered("other") ^ { ^.message }),
 type(converted()^) == float, type(converted_nullable()^) == float,
 (absent() ^ { 0 }) is null, null_step is null, explicit_null is null,
 bound_null is null, passed_null is null]

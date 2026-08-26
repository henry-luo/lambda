// Test: Variadic Arguments
// Layer: 2 | Category: function | Covers: ... variadic params, varg(), call()
// The variadic parameter is spelled `(...)`; arguments are read with varg().

// ===== Variadic function =====
fn sum_all(...) => sum(varg())
sum_all(1, 2, 3)
sum_all(10, 20, 30, 40)
sum_all(1)

// ===== varg() to access all args =====
fn count_args(...) => len(varg())
count_args(1, 2, 3)
count_args(7, 8)
count_args()

// ===== An argument list is not element content (LR09-9) =====
// varg() must preserve arity exactly: `null` is a real argument and adjacent
// string arguments are separate ones. Collecting them through the content
// builder used to merge and strip them, so f("a","b") arrived as ["ab"].
fn args_of(...) => varg()
args_of("a", "b")
count_args("a", "b")
args_of(1, null, 2)
count_args(1, null, 2)
args_of("x", null, "y")
count_args("x", null, "y")
count_args(null, null)

// An argument that *is* a content list stays ONE argument. This is the
// invariant the fix had to preserve, not remove: the verbatim append still
// flattens a nested content list exactly as the content builder did, so
// widening the fix any further would silently splice a `for` result into the
// caller's argument list.
args_of(for (x in [1, 2]) x)
count_args(for (x in [1, 2]) x)
count_args(for (x in [1, 2]) x, 9)

// varg(i) indexes the same list, so it only reads the right argument while
// arity is preserved — with merging it returned "q" for index 0.
fn arg_at(...) => varg(1)
arg_at("p", "q", "r")

// A fixed parameter followed by a rest list: only the rest is collected, and
// it keeps its `null` and its separate strings.
fn head_rest(a, ...) => [a, varg()]
head_rest(1, null, "s", "t")

// call() (S12.3.4) reaches the same rest-list builder through the dynamic
// adapter, so it must agree with a direct variadic call.
call(args_of, ["a", null, "b"])
call(count_args, ["a", null, "b"])

// ===== Variadic with leading params =====
fn prepend(prefix: string, ...) => varg() |> prefix ++ (~)
prepend("item_", 1, 2, 3)

// ===== Variadic forwarding — S12.3.4 call(), not spread (S12.3.5) =====
fn wrapper(...) => call(sum_all, varg())
wrapper(1, 2, 3)

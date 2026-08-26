// Formal semantics 7.6 + 8.1: `len(x)` is the number of iterations
// `for (i in x)` performs, and that law is what decides the error case.
//
//   for (x in null)  yields NOTHING  -- null is empty content  -> len(null) = 0
//   for (x in err)   yields an ERROR -- the content is a failure, not an
//                                       absence                -> len(err) = error
//
// So `len` is an ordinary value function: an error propagates. Its parameter is
// `any \ error` (7.7), so the error is rejected at the call boundary and skips;
// the return type stays a plain `int` and does not go viral through untyped
// code. Answering 0 would have put a failed computation on the same branch as
// an empty collection.
//
// Regression guard for the retired INT64_ERROR sentinel: len(err) once returned
// INT64_MAX, which a double lane cannot reject, so a piped error took it as an
// iteration bound and attempted repeated 2 GB allocations.

fn fail() int^ { raise error("boom") }

'-- absence is empty, so len is 0 --'
len(null)

'-- containment is not propagation: an error INSIDE a collection is one item --'
fn len_with_error_inside() {
    let a = fail() ^ { ^ }
    len([1, a, 3])
}
len_with_error_inside()

'-- ordinary lengths are unaffected --';
[len([1, 2, 3]), len("abc"), len({a: 1, b: 2})]

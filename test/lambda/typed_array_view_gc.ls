// View GC liveness — the view's base must survive GC cycles.
// Phase 2b §2 of Lambda_Typed_Array2.md
// Stress-test the GC tracer by allocating many short-lived arrays while
// holding a view onto a base array. If the GC tracer doesn't mark the
// view's base, the base buffer would be reclaimed and the view's data
// pointer would dangle.

let base = [100, 101, 102, 103, 104, 105, 106, 107, 108, 109]
let v = subview(base, 2, 6)            // [102, 103, 104, 105]
let v_initial_sum = sum(v)

'=== view captured ==='
v
v_initial_sum

// Allocate many short-lived arrays — each iteration creates a fresh array
// whose memory may be reclaimed. The base array is referenced only through
// the view: without GC tracing of shape->base, this would dangle.
let churn = for (i in 0 to 200) sum([i, i+1, i+2, i+3, i+4])

'=== after churn ==='
(sum(churn) > 0)                        // sanity: churn ran
v                                       // view must still resolve to [102..105]
sum(v)                                  // must still be 102+103+104+105 = 414
sum(v) == v_initial_sum                 // true if GC tracer kept base alive

// More stress with float arrays
let fbase = [1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5]
let fv = subview(fbase, 2, 6)          // [3.5, 4.5, 5.5, 6.5]
let fv_sum = sum(fv)

let fchurn = for (i in 0 to 100) sum([i * 1.5, i * 2.5, i * 3.5])

'=== float view after churn ==='
(sum(fchurn) > 0)                       // sanity
fv
sum(fv)
sum(fv) == fv_sum                       // true if base kept alive

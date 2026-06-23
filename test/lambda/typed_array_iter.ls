// Iteration sites over typed ArrayNum
// Phase 2a §4 of Lambda_Typed_Array2.md
// Verifies that for-in, where-filter, pipe, sum/avg/min/max,
// and inline predicates all work natively on ArrayNum without boxing.

// ============================================================
// for-in over int/float/bool ArrayNum
// ============================================================
'=== for-in int array ==='
for (x in [1, 2, 3, 4]) x

'=== for-in float array ==='
for (x in [1.5, 2.5, 3.5]) x

'=== for-in bool array (ELEM_BOOL) ==='
for (x in [true, false, true]) x

'=== for-in compact i8 array ==='
for (x in [1i8, 2i8, 3i8]) x

'=== for-in compact f32 array ==='
for (x in [1.5f32, 2.5f32, 3.5f32]) x

// ============================================================
// where-filter (inline) over ArrayNum
// ============================================================
'=== where on int array ==='
for (x in [1, 2, 3, 4, 5] where x > 2) x

'=== where on float array ==='
for (x in [0.5, 1.5, 2.5, 3.5] where x >= 2.0) x

'=== where + body transform ==='
for (x in [1, 2, 3, 4, 5] where x > 2) (x * 10)

// ============================================================
// pipe operator over ArrayNum (Lambda's `|` syntax)
// ============================================================
'=== pipe sum ==='
[1, 2, 3, 4, 5] | sum()
[1.5, 2.5, 3.5] | sum()
1 to 10 | sum()

'=== pipe avg ==='
[1, 2, 3, 4, 5] | avg()
[1.0, 2.0, 3.0, 4.0] | avg()

'=== pipe min/max ==='
[5, 2, 8, 1, 9] | min()
[5, 2, 8, 1, 9] | max()

'=== pipe all/any (bool array) ==='
[true, true, true] | all()
[true, false, true] | all()
[false, false, false] | any()
[false, true, false] | any()

'=== pipe len ==='
[1, 2, 3, 4, 5] | len()
[true, false] | len()
[] | len()

// ============================================================
// Chained operations
// ============================================================
'=== chained operations ==='
sum(math.sqrt([1, 4, 9, 16, 25]))     // 1+2+3+4+5 = 15
avg(clip([1, 5, 10, 15, 20], 5, 15))  // (5+5+10+15+15)/5 = 10
all(for (x in [1, 2, 3, 4]) (x > 0))  // true
any(for (x in [1, 2, 3, 4]) (x > 10)) // false

// ============================================================
// Predicates returning bool — composes with all/any
// ============================================================
'=== predicate chains ==='
let nums = [10, 20, 30, 40, 50]
all(for (n in nums) (n >= 10))
all(for (n in nums) (n > 25))
any(for (n in nums) (n > 25))
any(for (n in nums) (n < 0))

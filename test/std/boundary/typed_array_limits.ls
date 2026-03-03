// Test: Typed Array Limits
// Layer: 3 | Category: boundary | Covers: int[]/float[] large, auto-conversion

// ===== Basic typed array =====
let ia: int[] = [1, 2, 3, 4, 5]
ia
type(ia)
len(ia)

// ===== Float typed array =====
let fa: float[] = [1.1, 2.2, 3.3]
fa
type(fa)
len(fa)

// ===== Typed array indexing =====
ia[0]
ia[4]
fa[0]
fa[2]

// ===== Typed array arithmetic =====
let ia2: int[] = [10, 20, 30, 40, 50]
ia + ia2

// ===== Float array arithmetic =====
let fa2: float[] = [0.1, 0.2, 0.3]
fa + fa2

// ===== Scalar + typed array =====
ia * 2
fa * 3.0

// ===== Large typed int array =====
let big_int: int[] = for (i in 1 to 1000) i
len(big_int)
big_int[0]
big_int[999]
big_int | sum()

// ===== Large typed float array =====
let big_float: float[] = for (i in 1 to 100) float(i) * 0.1
len(big_float)
big_float[0]
big_float[99]

// ===== Typed array operations =====
ia | sum()
ia | avg()
ia | min()
ia | max()

fa | sum()
fa | avg()

// ===== Typed array sort =====
let unsorted: int[] = [5, 3, 1, 4, 2]
unsorted | sort()

// ===== Typed array reverse =====
ia | reverse()

// ===== Typed array filter (returns regular array) =====
ia | filter((x) => x > 2)

// ===== Typed array map =====
ia | map((x) => x * x)

// ===== Empty typed array =====
let empty_ia: int[] = []
len(empty_ia)

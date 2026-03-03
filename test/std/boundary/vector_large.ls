// Test: Vector Large
// Layer: 3 | Category: boundary | Covers: large vectors, cumsum, norm precision

// ===== Large vector creation =====
let big = for (i in 1 to 1000) i
len(big)
big[0]
big[999]

// ===== Sum of large vector =====
let s = big | sum()
s

// ===== Average of large vector =====
let avg = big | avg()
avg

// ===== Vector arithmetic on large =====
let doubled = big | map((x) => x * 2)
doubled[0]
doubled[999]

// ===== Large vector min/max =====
big | min()
big | max()

// ===== Large sort (already sorted) =====
let sorted = big | sort()
sorted[0]
sorted[999]

// ===== Large reverse =====
let rev = big | reverse()
rev[0]
rev[999]

// ===== Filter large vector =====
let evens = big | filter((x) => x % 2 == 0)
len(evens)
evens[0]
evens | last()

// ===== Reduce large vector =====
big | reduce((acc, x) => acc + x)

// ===== Large vector of floats =====
let floats = for (i in 1 to 100) float(i) / 10.0
floats[0]
floats[99]
floats | sum()

// ===== Squared values =====
let squares = for (i in 1 to 100) i * i
squares[0]
squares[99]
squares | sum()

// ===== Concatenation of large vectors =====
let a = for (i in 1 to 500) i
let b = for (i in 501 to 1000) i
let c = [*a, *b]
len(c)
c[0]
c[999]

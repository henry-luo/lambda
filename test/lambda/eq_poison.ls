'=== error poison ==='
error("x") == error("x")
error("x") != error("x")
error("x") == 1
1 == error("x")

'=== nan ==='
let n = 0.0 / 0.0
n == n
n != n
[n] == [n]

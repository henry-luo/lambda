// Phase 4 numeric model: compact-int overflow promotes to float and stays float.

"=== int overflow promotion ==="
let max = math.max_int
max + 1
type(max + 1)
(-max) - 1
type((-max) - 1)
max * 2
type(max * 2)
(max + 1) + 1
type((max + 1) + 1)


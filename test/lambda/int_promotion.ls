// v5 numeric model: finite int overflow saturates to shared signed infinity.

"=== int overflow saturation ==="
let max = math.max_int
max + 1
type(max + 1);
(-max) - 1
type((-max) - 1)
max * 2
type(max * 2);
(max + 1) + 1
type((max + 1) + 1)

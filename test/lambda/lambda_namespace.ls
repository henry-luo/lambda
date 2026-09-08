// LR02-16: the reserved root addresses sysfuncs, built-in modules, and docs.
let sum = 99
import util: lambda.doc.math.util
import builtin_math: lambda.math

"sys escape:"; lambda.sys.sum([1, 2, 3])
"math module:"; lambda.math.pi
"builtin alias:"; builtin_math.pi
"doc package:"; util.clamp(12, 0, 10)

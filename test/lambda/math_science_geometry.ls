// Exact predicate signs survive cancellation, overflow, and subnormal products.
fn native_erf(x: float) => math.erf(x)
fn native_erfc(x: float) => math.erfc(x)
fn dynamic_erf(x) => math.erf(x)

'=== error functions scalar vector and native calls ===';
[native_erf(0.0) == 0, native_erfc(0.0) == 1,
 abs(native_erf(1.0)-0.8427007929497149) < 1e-15,
 abs(native_erfc(8.0)/1.1224297172982928e-29-1) < 1e-14,
 abs(dynamic_erf(-1)+0.8427007929497149) < 1e-15,
 max(abs(math.erf([-1,0,1])+math.erfc([-1,0,1])-1)) < 1e-15,
 math.erf([]), math.erfc(null), math.erf("x") is error,
 math.erfc(1j) is error]

'=== oriented geometry conventions ==='
fn orientation_fn(a,b,c,d) any | error => math.orient3d(a,b,c,d)
let orientation = orientation_fn;
[math.orient2d([0,0],[1,0],[0,1]), math.orient2d([0,0],[0,1],[1,0]),
 math.orient2d([0,0],[1,1],[2,2]),
 math.orient3d([1,0,0],[0,1,0],[0,0,1],[0,0,0]),
 math.orient3d([0,1,0],[1,0,0],[0,0,1],[0,0,0]),
 math.orient3d([1,0,0],[0,1,0],[0,0,0],[1,1,0]),
 orientation([1,0,0],[0,1,0],[0,0,1],[0,0,0])]

'=== incircle orientation and degeneracy ===';
[math.incircle([1,0],[0,1],[-1,0],[0,0]),
 math.incircle([1,0],[-1,0],[0,1],[0,0]),
 math.incircle([1,0],[0,1],[-1,0],[0,-1]),
 math.incircle([1,0],[0,1],[-1,0],[0,-2]),
 math.incircle([1,0],[0,1],[-1,0],[0,-0.9999999999999999])]

'=== cancellation and extreme exponents ===';
[math.orient2d([0,0],[1,1.0000000000000002],[1.0000000000000002,1.0000000000000004]),
 math.orient3d([1,1.0000000000000002,0],[1.0000000000000002,1.0000000000000004,0],[0,0,1],[0,0,0]),
 math.orient2d([0,0],[1e308,0],[0,1e308]),
 math.orient2d([0,0],[5e-324,0],[0,5e-324]),
 math.incircle([1e308,0],[0,1e308],[-1e308,0],[0,0])]

'=== point validation and absence ===';
[math.orient2d(null,[1,0],[0,1]),
 math.orient2d([],[1,0],[0,1]) is error,
 math.orient3d([1,0],[0,1,0],[0,0,1],[0,0,0]) is error,
 math.orient2d([inf,0],[1,0],[0,1]) is error,
 math.orient2d([null,0],[1,0],[0,1]) is error,
 math.incircle([1j,0],[0,1],[-1,0],[0,0]) is error]

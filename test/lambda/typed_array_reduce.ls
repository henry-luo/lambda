// Whole-array reductions over compact typed arrays (Typed Array 4, Scope 2.3).
// These previously returned garbage for compact element types because the
// integer path read the byte/short/float buffers through `items` (int64).

"=== i8 sum/min/max ==="
sum([10i8, 20i8, 30i8])      // 60
min([10i8, 20i8, 30i8])      // 10
max([10i8, -20i8, 30i8])     // 30

"=== u8 sum/min/max ==="
sum([100u8, 200u8, 250u8])   // 550
min([100u8, 200u8, 250u8])   // 100
max([100u8, 200u8, 250u8])   // 250

"=== i16 / u16 ==="
sum([1000i16, -2000i16, 3000i16])  // 2000
max([100u16, 60000u16])            // 60000

"=== i32 / u32 (exceeds int32) ==="
sum([100000i32, 200000i32])        // 300000
sum([3000000000u32])               // 3000000000

"=== f32 ==="
sum([1.5f32, 2.5f32, 4.0f32])      // 8
min([1.5f32, 2.5f32, 4.0f32])      // 1.5
max([1.5f32, 2.5f32, 4.0f32])      // 4

"=== avg (always float) ==="
avg([10i8, 20i8, 30i8])            // 20
avg([1.0f32, 2.0f32, 3.0f32])      // 2

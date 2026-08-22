// ArrayNum-producing transforms must keep the numeric carrier and lane type.

let ints = [3, 1, 2]
let i8s = [3i8, 1i8, 2i8]
let f32s = [3.5f32, 1.5f32, 2.5f32]

'=== int ArrayNum carriers ===';
[
  ndim(sort(ints)),
  ndim(sort(ints, 'desc')),
  ndim(sort(ints, (x) => (x))),
  ndim(unique([3, 1, 3, 2])),
  ndim(reverse(ints)),
  ndim(take(ints, 2)),
  ndim(drop(ints, 1)),
  ndim(slice(ints, 1, 3)),
  ndim(unique(take(ints, 0))),
  ndim(take(ints, 0)),
  ndim(drop(ints, 3)),
  ndim(slice(ints, 1, 1))
]

'=== int ArrayNum values ===';
[
  sort(ints),
  sort(ints, 'desc'),
  sort(ints, (x) => (x)),
  unique([3, 1, 3, 2]),
  reverse(ints),
  take(ints, 2),
  drop(ints, 1),
  slice(ints, 1, 3)
]

'=== i8 lane preservation ===';
[
  sort(i8s)[0] is i8,
  unique([3i8, 1i8, 3i8])[0] is i8,
  reverse(i8s)[0] is i8,
  take(i8s, 2)[0] is i8,
  drop(i8s, 1)[0] is i8,
  slice(i8s, 1, 3)[0] is i8
]

'=== i8 values ===';
[
  sort(i8s),
  unique([3i8, 1i8, 3i8]),
  reverse(i8s),
  take(i8s, 2),
  drop(i8s, 1),
  slice(i8s, 1, 3)
]

'=== f32 lane preservation ===';
[
  sort(f32s)[0] is f32,
  unique([3.5f32, 1.5f32, 3.5f32])[0] is f32,
  reverse(f32s)[0] is f32,
  take(f32s, 2)[0] is f32,
  drop(f32s, 1)[0] is f32,
  slice(f32s, 1, 3)[0] is f32
]

'=== f32 values ===';
[
  sort(f32s),
  unique([3.5f32, 1.5f32, 3.5f32]),
  reverse(f32s),
  take(f32s, 2),
  drop(f32s, 1),
  slice(f32s, 1, 3)
]

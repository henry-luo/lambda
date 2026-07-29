// Complex literals, arithmetic, type relations, storage, and principal math.
'=== literals and arithmetic ==='
let z = 3 + 4j
[4j, -4j, 1e-3j, z + 2, z * 2j, z / 2]

'=== constructors and components ==='
[complex(3), complex(3, 4), real(z), imag(z), conj(z), abs(z)]

'=== type and equality ==='
[z is complex, z is number, complex(3) == 3, complex(3, 1) == 3, type(z)]

'=== maps, arrays, and total order ==='
let values = [{z: 1j}, {z: 2j}]
[values[0].z, values[1].z, [complex(2), 1, 3j, -2j] |> sort,
 unique([2, complex(2), complex(2, 0), 2j, 2j])]

'=== complex math ==='
[sqrt(-1 + 0j), exp(0j), sin(0j), cos(0j), tan(0j)]

'=== typed parameters ==='
fn reflect(value: complex) complex { conj(value) }
reflect(2 + 3j)

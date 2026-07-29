# Lambda Complex Numbers

> **Status: proposal.** This document proposes a first-class `complex` scalar
> type for Lambda. It is not an implementation plan that changes the normative
> rules in `doc/Lambda_Formal_Semantics.md` until accepted.

## Decision

Lambda gains a built-in `complex` type whose value is an ordered pair of
float64 (IEEE binary64) components, `(real, imag)`. The source spelling for an imaginary
literal is a numeric coefficient followed immediately by lowercase `j`:

```lambda
3 + 4j       // complex(3.0, 4.0)
4j           // complex(0.0, 4.0)
-2.5j        // complex(0.0, -2.5)
1e-3 + .5j   // complex(0.001, 0.5)
```

`3 + 4j` is an ordinary addition expression: `4j` is the imaginary literal;
there is no special two-component compound-literal production. This preserves
the normal precedence, constant folding, and error reporting of `+` and `-`.

The suffix is **lowercase `j` only**. Lambda deliberately does not accept
`4i` or `4J`.

`i` is already the visual and lexical prefix of Lambda's sized-integer
suffixes (`i8`, `i16`, `i32`, `i64`). Reserving it for an imaginary suffix
would make numeric spellings needlessly easy to misread or mis-tokenize. The
less common `j` spelling gives complex values an unambiguous, compact literal
without disturbing that established notation.

## Prior art

Python is the direct syntactic precedent. Its lexical specification says,
“Python uses a similar syntax, except the imaginary unit is written as `j`
rather than i,” and defines `3 + 4.2j` as addition of a real literal and an
imaginary literal. [Python lexical analysis — imaginary
literals](https://docs.python.org/3/reference/lexical_analysis.html#imaginary-literals)

Go demonstrates the more common alternative: “An imaginary literal represents
the imaginary part of a complex constant,” with a numeric literal followed by
lowercase `i`. [Go specification — imaginary
literals](https://go.dev/ref/spec#Imaginary_literals)

Lambda takes Python's `j` spelling while retaining the same useful model:
imaginary coefficients are literals, and ordinary arithmetic composes them
into general complex values. C99, Julia, R, Ruby, MATLAB, and Scheme likewise
treat a complex value as a language-level numeric value rather than an array
or a user-defined record. Their spellings vary; the representation principle
does not.

## Syntax and parsing

### Literal grammar

The grammar adds an imaginary-number token beside the existing numeric
literals:

```ebnf
imag_number ::= complex_component_literal "j"
complex_component_literal ::= decimal_integer | decimal_float | "inf" | "nan"
```

`complex_component_literal` accepts the same decimal digits, underscores,
decimal point, exponent forms, and special float spellings accepted by
Lambda's unsuffixed integer and float literals. Examples: `1j`, `1_000j`,
`1.0j`, `.25j`, `1e6j`, `1.5e-2j`, `infj`, and `nanj`. The `j` must be
adjacent to the coefficient; `4 j` is invalid.

The initial feature intentionally excludes `m`, `n`, and sized-number suffixes
inside an imaginary literal: `1mj`, `1nj`, `1i8j`, and `1f32j` are invalid.
Complex components are float64 (IEEE binary64), so accepting an exact-decimal spelling would
hide a precision loss. The author must write the conversion visibly:

```lambda
complex(float(1.25m), 0.0)
```

Unary signs remain operators rather than part of the literal. Thus `-4j`
parses as `-(4j)`, and `3-4j` parses as `3 - 4j`. This keeps tokenization and
operator precedence consistent with every other Lambda numeric literal.

### Type syntax

`complex` is a built-in type literal and belongs to `number`:

```lambda
let z: complex = 3 + 4j
z is complex       // true
z is number        // true
z is float         // false
```

`complex[]` is valid. Initially it is a boxed `Array` of complex `Item`s;
`LMD_TYPE_ARRAY_NUM` is not extended until a contiguous complex-array design
has separately established its vector and ABI rules.

### Required grammar workflow

The source of truth is `lambda/tree-sitter-lambda/grammar.js`. An
implementation edits that grammar, runs `make generate-grammar`, and never
edits the generated `parser.c` directly.

## Value model and conversions

### Component domain

A `complex` value has exactly two float64 (IEEE binary64) components. It is neither
a two-element `array` nor a `map`; no collection operation can observe or
mutate its storage.

Small `int`, `f16`, `f32`, and `float` values convert implicitly to `complex`
when an arithmetic operand is already complex. The converted value has zero
imaginary part. This is lossless for Lambda's `int` range, which is contained
in the exactly representable float64 (IEEE binary64) integer range.

`integer`, `decimal`, `i64`, and `u64` do **not** implicitly convert to
`complex`, because that would violate Lambda's “exact until you ask for float”
rule. The user must explicitly choose the lossy boundary with `float(x)` and
then construct or combine a complex value.

```lambda
2 + 3j                 // 2.0 + 3.0j
42i8 + 3j              // 42.0 + 3.0j
1.25m + 3j             // type error: decimal-to-complex is not implicit
float(1.25m) + 3j      // 1.25 + 3.0j
```

The constructor is:

```lambda
complex(real: int | f16 | f32 | float,
        imag: int | f16 | f32 | float = 0.0) -> complex
```

`float(complex)` and `int(complex)` are errors, even when the imaginary part
is zero; dropping a component must be explicit through `real(z)`. `complex`
itself is idempotent. `string(z)` uses the canonical printer described below.

## Arithmetic and functions

For `z = a + bj` and `w = c + dj`:

| Operation | Result |
| --- | --- |
| `z + w` | `(a + c) + (b + d)j` |
| `z - w` | `(a - c) + (b - d)j` |
| `z * w` | `(ac - bd) + (ad + bc)j` |
| `z / w` | complex division over float64 (IEEE binary64) components, with an overflow-safe scaled algorithm |
| `+z`, `-z` | identity and component-wise negation |
| `z ** w` | principal-value `exp(w * log(z))` |

`+`, `-`, `*`, `/`, and `**` accept one real operand through the conversion
rule above. A complex result is sticky: no operation silently demotes
`3 + 0j` to `3.0`.

`div`, `%`, `//`, shifts, and bitwise operators have no complex meaning and
are compile errors when the complex type is statically known; dynamic use
returns `error()` under Lambda's normal operator-error rule. Complex division
does not trap. A zero complex divisor produces a complex value whose affected
components follow IEEE-754 `inf`/`nan` propagation; it never produces an
integer-division error.

The initial standard-library surface is:

```lambda
real(z)       // float: real component
imag(z)       // float: imaginary component
conj(z)       // complex: a - bj
abs(z)        // float: hypot(a, b), scaled to avoid avoidable overflow
complex(a, b = 0.0)
sqrt(z), exp(z), log(z)
sin(z), cos(z), tan(z)
```

`sqrt`, `log`, and `z ** w` use their principal branches. The phase and
squared magnitude are intentionally derived operations rather than named
complex built-ins:

```lambda
fn complex_phase(z) => math.atan2(imag(z), real(z))
fn squared_magnitude(z) => real(z) * real(z) + imag(z) * imag(z)
fn from_rectangular(real_part, imag_part) => complex(real_part, imag_part)
fn from_polar(r, theta) =>
    complex(r * math.cos(theta), r * math.sin(theta))
```

`squared_magnitude` follows ordinary float multiplication and addition, so it
is subject to normal float overflow. `abs(z)` remains the overflow-aware way
to compute the magnitude itself. `from_rectangular` replaces a separate
rectangular constructor, while `from_polar` replaces a separate `rect(r,
theta)` helper. The helpers return ordinary float or complex values and
therefore inherit the float `nan` and infinity rules already used by Lambda.

No implicit conversion from `string`, `binary`, `datetime`, containers, or
maps is provided. Parsing a textual complex value belongs in an explicit
future `parse_complex` API, not in arithmetic coercion.

## Equality, hashing, ordering, and truthiness

### Equality and hash

Complex equality is value equality:

```lambda
3 + 4j == 3 + 4j    // true
3 + 0j == 3         // true
3 + 0j == 3.0       // true
3 + 4j == 3         // false
```

Two complex values are equal when their real parts are equal under Lambda's
existing real-number equality and their imaginary parts are equal under the
same rule. A complex value with a zero imaginary component additionally equals
the corresponding real numeric value. `+0.0j` and `-0.0j` are equal.

If either component is `nan`, the complex value is poison for equality:
it equals nothing, including itself. This extends the existing `nan != nan`
rule without inventing a second poison model.

Hashing uses the pair of canonical component hashes. For a zero-imaginary
value, the hash is exactly the canonical real-number hash, satisfying the
invariant `a == b => hash(a) == hash(b)` across the real/complex boundary.

### Ordering

Complex values have no magnitude ordering. `<`, `<=`, `>`, and `>=` between a
complex value and any value are invalid: statically known uses are compile
errors; dynamic uses return `error()`.

Lambda nevertheless needs a deterministic total order for `sort` and map-like
operations. The `number` band therefore orders complex values as follows:

1. A zero-imaginary complex value ties its equal real numeric value.
2. Non-real complex values follow real numeric values.
3. Non-real complex values compare lexicographically by `(real, imag)` using
   Lambda's float total-order rules.

This is a filing order, not a mathematical comparison. It preserves the
language invariant that the total order refines equality.

### Truthiness

All complex values are truthy, including `0+0j` and values with an infinite or
`nan` component. This matches Lambda's existing rule that every number is
truthy; truthiness remains a presence test rather than a numeric nonzero test.

## Printing, parsing, and external data

The Lambda printer uses a canonical, type-preserving spelling with an explicit
imaginary term:

```lambda
print(complex(3.0, 4.0))   // 3+4j
print(complex(3.0, -4.0))  // 3-4j
print(complex(3.0, 0.0))   // 3+0j
print(complex(0.0, 4.0))   // 0+4j
```

Each component uses Lambda's shortest-round-trip float printer. The explicit
`+0j` matters: printing `3+0j` as `3` would lose its `complex` type on parse.
The printer preserves a negative-zero component if the float printer already
distinguishes it. Printed complex values must parse to the same component bits
apart from the existing float `nan` equivalence exception.

JSON, YAML, CSV, XML, and TOML have no standard complex scalar. Their default
formatters must reject a complex value with a clear error rather than silently
serializing an array or a string. A caller that wants a wire representation
must choose one explicitly, for example:

```lambda
{ real: real(z), imag: imag(z) }
```

An optional future conversion helper may accept that explicit map shape. It
must not make generic document parsing guess whether `[3, 4]` or `"3+4j"` is a
complex value.

## Runtime representation and ownership

### New runtime type

Add a distinct `LMD_TYPE_COMPLEX` scalar tag and `TYPE_COMPLEX` /
`LIT_TYPE_COMPLEX` descriptors. Do not reuse `LMD_TYPE_DECIMAL`, `LMD_TYPE_ARRAY`,
or `LMD_TYPE_OBJECT`: each would either erase the type or overload an existing
ownership and semantic contract.

The heap object is a fixed-size, pointer-free value:

```c
typedef struct Complex {
    TypeId type_id;       // LMD_TYPE_COMPLEX
    double real;
    double imag;
} Complex;
```

The exact field order may include ABI padding, but `type_id` remains first as
for other tagged heap values. Constructors allocate with the normal Lambda GC
allocator and return a tagged `Item`; no `std::` container or host-language
object is involved. Since `Complex` has no outgoing `Item` or raw owned
pointers, the GC has no child-tracing work beyond retaining and releasing the
object itself.

All allocation paths—including literals materialized at runtime, arithmetic,
constructors, JIT calls, and formatter temporaries—must establish precise
`RootFrame` / `Rooted` ownership around any collecting call. Complex support
must not restore conservative native-stack scanning.

### Runtime and JIT surface

The runtime exposes shared helpers such as:

```c
Item complex_new(double real, double imag);
Item complex_add(Item left, Item right);
Item complex_subtract(Item left, Item right);
Item complex_multiply(Item left, Item right);
Item complex_divide(Item left, Item right);
Item complex_power(Item left, Item right);
Item complex_equal(Item left, Item right);
```

The interpreter and MIR-direct transpiler use those same helpers. The
transpiler may scalar-replace a non-escaping pair only after it preserves the
observable `Item` type, canonical printer, exact roots, and error behavior.
The frozen C2MIR path receives no new support.

`type()`, type inference, validation, literal-type construction, deep equality,
hashing, total ordering, copying, GC metadata, and every formatter dispatch
must recognize `LMD_TYPE_COMPLEX`. A new tag is not complete until every
`switch (get_type_id(...))` boundary either handles it or deliberately emits a
well-defined unsupported-type error.

## Required validation

Implementation is accepted only with focused unit and integration coverage for:

- lexical acceptance and rejection: `4j`, `1e-3j`, `4 j`, `4i`, `1mj`,
  `1i8j`, unary signs, and precedence;
- arithmetic, mixed `int`/float promotion, forbidden exact-domain mixing,
  scaled multiply/divide edge cases, and principal-branch functions;
- `real`, `imag`, `conj`, `abs`, derived phase/squared-magnitude expressions,
  and constructor conversions;
- equality, hash coherence, zero-imaginary real equality, `-0.0`, and `nan`;
- comparison errors plus deterministic total-order sorting;
- canonical print-to-parse round trips, including zero imaginary parts and
  special float components;
- GC stress around arithmetic, closures, arrays, formatter failures, and JIT
  call boundaries;
- explicit JSON/YAML/XML rejection and no accidental array/string coercion.

Every new Lambda integration script (`*.ls`) requires its matching expected
result (`*.txt`). Run the relevant Lambda baseline suite after unit coverage.

## Non-goals for the first release

- arbitrary-precision or decimal-component complex arithmetic;
- `i` or uppercase-`J` imaginary suffixes;
- implicit string parsing or document-format encodings;
- a packed `complex[]` representation or SIMD complex operations;
- complex matrix, FFT, linear-algebra, or polar-record abstractions;
- changes to Python, JavaScript, Ruby, Bash, or the frozen C2MIR guest paths.

Those features can build on this type only when they preserve the syntax,
value, exactness, ownership, and serialization invariants established here.

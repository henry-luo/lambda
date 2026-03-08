# JetStream Benchmark Results: Lambda MIR vs Node.js

Comparison of 15 JetStream benchmarks ported to Lambda Script (MIR JIT) against the original JavaScript running on Node.js.

## Test Environment

| **Machine** | Apple M3, 24 GB RAM             |
| ----------- | ------------------------------- |
| **OS**      | macOS 15.7.1                    |
| **Lambda**  | MIR Direct JIT (release build)  |
| **Node.js** | v22.13.0 (V8 JIT)               |
| **Date**    | 2026-03-08                      |
| **Runs**    | 3 (median reported)             |

## Results

All times in milliseconds (ms). Lower is better.

| Benchmark | Source | Lambda MIR (ms) | Node.js (ms) | Ratio (MIR / Node) |
|-----------|--------|----------------:|-------------:|--------------------:|
| nbody | SunSpider | 54.5 | 6.7 | 8.1x |
| cube3d | SunSpider | 63.6 | 19.3 | 3.3x |
| navier_stokes | Octane | 965.0 | 15.0 | 64.3x |
| richards | Octane | 338.7 | 9.5 | 35.7x |
| splay | Octane | 527.5 | 25.3 | 20.8x |
| deltablue | Octane | 21.1 | 11.1 | 1.9x |
| hashmap | simple | 122.0 | 17.6 | 6.9x |
| crypto_sha1 | SunSpider | 17.8 | 10.2 | 1.7x |
| raytrace3d | SunSpider | 402.3 | 20.5 | 19.6x |
| base64 | SunSpider | 8.4 | 11.2 | 0.7x |
| crypto_md5 | SunSpider | 23.0 | 5.3 | 4.3x |
| crypto_aes | SunSpider | 48.7 | 5.7 | 8.6x |
| crypto_rsa | SunSpider | 558.4 | 72.0 | 7.8x |
| regex_dna | Shootout | 72.4 | 78.0 | 0.9x |
| bigdenary | BigInt | 11.2 | 17.0 | 0.7x |
| **Geometric Mean** | | **81.5** | **15.3** | **5.3x** |

## Analysis

Lambda MIR JIT (release build) is on average **~5.3x slower** than Node.js V8 across these 15 benchmarks (geometric mean).

### Performance Tiers

**Faster than Node.js (< 1x)**
- **bigdenary** (0.7x) — Arbitrary-precision decimal arithmetic. Lambda's built-in `decimal` type (libmpdec) outperforms JS BigInt-based implementation.
- **base64** (0.7x) — Base64 encode/decode using pure integer byte arrays. Rewritten to eliminate string operations entirely.
- **regex_dna** (0.9x) — DNA pattern matching with regex. Lambda's RE2-based patterns are faster than Node.js regex.

**Near-parity (1x–3x slower)**
- **crypto_sha1** (1.7x) — Bitwise integer operations. MIR generates efficient code for `band`/`bor`/`shl`/`shr`.
- **deltablue** (1.9x) — Constraint solver with object-heavy logic. Lambda's map-based objects perform well here.
- **cube3d** (3.3x) — Array arithmetic with matrix transforms.

**Moderate gap (3x–10x slower)**
- **crypto_md5** (4.3x) — Bitwise operations with some string manipulation.
- **hashmap** (6.9x) — Custom hash map implementation with array indexing and linked list traversal.
- **crypto_rsa** (7.8x) — RSA with big integer arithmetic using manual multi-precision arrays.
- **nbody** (8.1x) — Floating-point physics simulation with tight loops.
- **crypto_aes** (8.6x) — AES encryption with S-box lookups and bitwise operations.

**Large gap (> 10x slower)**
- **raytrace3d** (19.6x) — Object-oriented ray tracer creating many temporary map objects per pixel.
- **splay** (20.8x) — Splay tree with intensive insert/delete/traversal of linked tree nodes. Improved from 57.2x (debug, untyped) via `type SplayNode` and `type SplayTree` annotations enabling direct field access.
- **richards** (35.7x) — OS task scheduler with heavy object field access and linked list manipulation.
- **navier_stokes** (64.3x) — Fluid dynamics with large 2D array traversals.

### Key Observations

1. **Lambda beats Node.js on built-in types**: bigdenary (0.7x), base64 (0.7x), and regex_dna (0.9x) show that Lambda's native decimal (libmpdec), integer array operations, and regex (RE2) implementations outperform their JavaScript counterparts.

2. **Object creation overhead**: Benchmarks that create many temporary objects (raytrace3d, splay, richards) show the largest gaps. Lambda's map-based objects have higher allocation overhead vs V8's optimized hidden classes.

3. **Integer array operations are fast**: base64 (0.7x) was rewritten from string-based to pure integer byte arrays, showing that Lambda's `fill()` arrays with integer indexing can outperform V8 when string overhead is eliminated.

4. **Array-heavy workloads are slower**: navier_stokes operates on a 128×128 fluid grid (~65K elements) with multiple passes. Lambda's array access through `fill()` arrays appears significantly slower than V8's typed arrays.

5. **Bitwise and simple arithmetic are competitive**: crypto_sha1 (1.7x), crypto_md5 (4.3x), and deltablue (1.9x) suggest MIR generates efficient code for integer operations and simple function dispatch.

6. **Tree/linked structures are costly**: richards (36x) involves frequent pointer chasing through map fields, where V8's inline caching gives it a large advantage.

7. **Type annotations help significantly**: splay improved from 57.2x (debug, untyped) to 20.8x (release, typed) — a **2.75x speedup** — by adding `type SplayNode` and `type SplayTree` definitions that enable direct byte-offset field access instead of runtime hash lookups. See [Tuning.md](Tuning.md) for details.

## Benchmark Descriptions

| Benchmark | Origin | Description |
|-----------|--------|-------------|
| nbody | SunSpider | N-body gravitational simulation (Sun + 4 planets) |
| cube3d | SunSpider | 3D wireframe cube rotation with matrix transforms |
| navier_stokes | Octane | 2D fluid dynamics solver (128×128 grid, 20 iterations) |
| richards | Octane | OS task scheduler simulation (scheduler, handler, device, worker tasks) |
| splay | Octane | Splay tree: 8000 insert/delete cycles with ordered traversal verification |
| deltablue | Octane | Incremental constraint solver (chain + projection tests) |
| hashmap | simple | Custom hash map: 90K inserts, lookups, and deletes |
| crypto_sha1 | SunSpider | SHA-1 hash of ~9KB text, 25 iterations |
| raytrace3d | SunSpider | Ray tracer: 30×30 pixel scene with spheres, planes, reflections |
| base64 | SunSpider | Base64 encode/decode of ~9KB text, 8 iterations |
| crypto_md5 | SunSpider | MD5 hash computation, multiple iterations |
| crypto_aes | SunSpider | AES-256 encrypt/decrypt, 40 iterations |
| crypto_rsa | SunSpider | RSA encryption/decryption with big integer arithmetic, 20 iterations |
| regex_dna | Shootout | DNA pattern matching (9 patterns) and IUPAC substitutions on 300KB sequence |
| bigdenary | BigInt | Arbitrary-precision decimal arithmetic: +, −, ×, ÷, cmp, neg (10K each) |

## Reproduction

```bash
# Lambda MIR (from project root)
python3 test/benchmark/jetstream/run_jetstream.py 3

# Node.js
node test/benchmark/jetstream/run_jetstream_node.js 3
```

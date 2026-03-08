# JetStream Benchmark Results: Lambda MIR vs Node.js

Comparison of 15 JetStream benchmarks ported to Lambda Script (MIR JIT) against the original JavaScript running on Node.js.

## Test Environment

| | |
|---|---|
| **Machine** | Apple M3, 24 GB RAM |
| **OS** | macOS 15.7.1 |
| **Lambda** | MIR Direct JIT (debug build) |
| **Node.js** | v22.13.0 (V8 JIT) |
| **Date** | 2026-03-07 |
| **Runs** | 3 (median reported) |

## Results

All times in milliseconds (ms). Lower is better.

| Benchmark | Source | Lambda MIR (ms) | Node.js (ms) | Ratio (MIR / Node) |
|-----------|--------|----------------:|-------------:|--------------------:|
| nbody | SunSpider | 49.8 | 6.3 | 7.9x |
| cube3d | SunSpider | 55.8 | 18.9 | 3.0x |
| navier_stokes | Octane | 831.2 | 15.5 | 53.6x |
| richards | Octane | 310.5 | 9.9 | 31.4x |
| splay | Octane | 1647.8 | 28.8 | 57.2x |
| deltablue | Octane | 18.7 | 16.2 | 1.2x |
| hashmap | simple | 103.4 | 17.8 | 5.8x |
| crypto_sha1 | SunSpider | 16.8 | 9.5 | 1.8x |
| raytrace3d | SunSpider | 389.0 | 20.5 | 19.0x |
| base64 | SunSpider | 467.0 | 11.2 | 41.7x |
| crypto_md5 | SunSpider | 22.1 | 5.3 | 4.2x |
| crypto_aes | SunSpider | 42.0 | 5.7 | 7.4x |
| crypto_rsa | SunSpider | 558.0 | 72.0 | 7.8x |
| regex_dna | Shootout | 64.0 | 78.0 | 0.8x |
| bigdenary | BigInt | 10.0 | 17.0 | 0.6x |
| **Geometric Mean** | | **104.6** | **14.1** | **7.4x** |

## Analysis

Lambda MIR JIT is on average **~7.4x slower** than Node.js V8 across these 15 benchmarks (geometric mean).

### Performance Tiers

**Faster than Node.js (< 1x)**
- **bigdenary** (0.6x) — Arbitrary-precision decimal arithmetic. Lambda's built-in `decimal` type (libmpdec) outperforms JS BigInt-based implementation.
- **regex_dna** (0.8x) — DNA pattern matching with regex. Lambda's RE2-based patterns are faster than Node.js regex.

**Near-parity (1x–3x slower)**
- **deltablue** (1.2x) — Constraint solver with object-heavy logic. Lambda's map-based objects perform well here.
- **crypto_sha1** (1.8x) — Bitwise integer operations. MIR generates efficient code for `band`/`bor`/`shl`/`shr`.
- **cube3d** (3.0x) — Array arithmetic with matrix transforms.

**Moderate gap (3x–10x slower)**
- **crypto_md5** (4.2x) — Bitwise operations with some string manipulation.
- **hashmap** (5.8x) — Custom hash map implementation with array indexing and linked list traversal.
- **crypto_aes** (7.4x) — AES encryption with S-box lookups and bitwise operations.
- **crypto_rsa** (7.8x) — RSA with big integer arithmetic using manual multi-precision arrays.
- **nbody** (7.9x) — Floating-point physics simulation with tight loops.

**Large gap (> 10x slower)**
- **raytrace3d** (19.0x) — Object-oriented ray tracer creating many temporary map objects per pixel.
- **richards** (31.4x) — OS task scheduler with heavy object field access and linked list manipulation.
- **base64** (41.7x) — Base64 encode/decode with heavy string concatenation and character operations.
- **navier_stokes** (53.6x) — Fluid dynamics with large 2D array traversals.
- **splay** (57.2x) — Splay tree with intensive insert/delete/traversal of linked tree nodes.

### Key Observations

1. **Lambda beats Node.js on built-in types**: bigdenary (0.6x) and regex_dna (0.8x) show that Lambda's native decimal (libmpdec) and regex (RE2) implementations outperform JavaScript's BigInt and regex engines.

2. **Object creation overhead**: Benchmarks that create many temporary objects (raytrace3d, splay, richards) show the largest gaps. Lambda's map-based objects have higher allocation overhead vs V8's optimized hidden classes.

3. **String operations are costly**: base64 (41.7x) relies on heavy string concatenation and per-character operations via `slice()`, exposing Lambda's immutable string overhead.

4. **Array-heavy workloads are slower**: navier_stokes operates on a 128×128 fluid grid (~65K elements) with multiple passes. Lambda's array access through `fill()` arrays appears significantly slower than V8's typed arrays.

5. **Bitwise and simple arithmetic are competitive**: crypto_sha1 (1.8x), crypto_md5 (4.2x), and deltablue (1.2x) suggest MIR generates efficient code for integer operations and simple function dispatch.

6. **Tree/linked structures are costly**: splay (57x) and richards (31x) involve frequent pointer chasing through map fields, where V8's inline caching gives it a large advantage.

7. **This is a debug build**: Lambda was compiled as a debug build. A release build (`make release`) would likely improve these numbers.

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

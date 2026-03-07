# JetStream Benchmark Results: Lambda MIR vs Node.js

Comparison of 9 JetStream benchmarks ported to Lambda Script (MIR JIT) against the original JavaScript running on Node.js.

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
| **Geometric Mean** | | **135.0** | **14.6** | **9.2x** |

## Analysis

Lambda MIR JIT is on average **~9x slower** than Node.js V8 across these benchmarks (geometric mean).

### Performance Tiers

**Near-parity (< 3x slower)**
- **deltablue** (1.2x) — Constraint solver with object-heavy logic. Lambda's map-based objects perform well here.
- **crypto_sha1** (1.8x) — Bitwise integer operations. MIR generates efficient code for `band`/`bor`/`shl`/`shr`.
- **cube3d** (3.0x) — Array arithmetic with matrix transforms.

**Moderate gap (3x–10x slower)**
- **hashmap** (5.8x) — Custom hash map implementation with array indexing and linked list traversal.
- **nbody** (7.9x) — Floating-point physics simulation with tight loops.

**Large gap (> 10x slower)**
- **raytrace3d** (19.0x) — Object-oriented ray tracer creating many temporary map objects per pixel.
- **richards** (31.4x) — OS task scheduler with heavy object field access and linked list manipulation.
- **navier_stokes** (53.6x) — Fluid dynamics with large 2D array traversals.
- **splay** (57.2x) — Splay tree with intensive insert/delete/traversal of linked tree nodes.

### Key Observations

1. **Object creation overhead**: Benchmarks that create many temporary objects (raytrace3d, splay, richards) show the largest gaps. Lambda's map-based objects have higher allocation overhead vs V8's optimized hidden classes.

2. **Array-heavy workloads are slower**: navier_stokes operates on a 128×128 fluid grid (~65K elements) with multiple passes. Lambda's array access through `fill()` arrays appears significantly slower than V8's typed arrays.

3. **Bitwise and simple arithmetic are competitive**: crypto_sha1 and deltablue are within 2x, suggesting MIR generates efficient code for integer operations and simple function dispatch.

4. **Tree/linked structures are costly**: splay (57x) and richards (31x) involve frequent pointer chasing through map fields, where V8's inline caching gives it a large advantage.

5. **This is a debug build**: Lambda was compiled as a debug build. A release build (`make release`) would likely improve these numbers.

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

## Reproduction

```bash
# Lambda MIR (from project root)
python3 test/benchmark/jetstream/run_jetstream.py 3

# Node.js
node test/benchmark/jetstream/run_jetstream_node.js 3
```

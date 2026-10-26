# Lambda Procedural Generation Package — `lambda.procgen`

> **Status:** proposal, 2026-10-26. Nothing is implemented. The package name `lambda.procgen` (PG1), the `math.*` surface for native kernels (PG7.4) fresh, unvendored C kernels (PG7.5) the initial tolerance-tier membership (PG2), `float64` fields in the API (PG5), the timing of shared geometry (PG9), PRNG migration (PG3) and the 3D/4D scope (§8) were accepted by the user on 2026-10-26. Every §9 open item is resolved; the design is ready for P0.
> **Scope:** a shipped script package of deterministic procedural-generation algorithms: keyed randomness, noise, scalar fields, point sampling, Delaunay/Voronoi, grid and dungeon generators, mazes, wave function collapse, L-systems, text grammars and terrain. Algorithms are written in Lambda first. A small set moves to native C under `lib/` only when a release-build measurement requires it (PG7).
> **Spec linkage:** D7.2.4v4 (the `lambda.*` root; a path names exactly one thing; `lambda.math` is the built-in math module), S12.1.1v3 (`fn` is pure and deterministic and can never call `pn`), S10.2.1 / S10.2.4 (element-wise arithmetic and its promotion), S4.1.5 (machine ints wrap), D7.1.1 (static-library layering), D6.4.1–D6.4.2 (system-function ABI and registration checklist). This proposal changes no formal ruling.
> **Authority:** package design ledger PG1–PG10, open items PG-O1–PG-O8. The `PG` series is new; no other vibe record uses it.

## 1. Purpose and scope

Procedural generation builds content from algorithms and a seed instead of authoring it by hand: terrain, maps, dungeons, textures, plant shapes, placements, names and text. Lambda already has scattered pieces of it, each built for one consumer:

| Existing piece | Where | What it does |
|---|---|---|
| `math.random(seed)` | `lambda/runtime/lambda-vector.cpp` over `lib/math_utils.h` `math_splitmix64` | One SplitMix64 step: `[value, new_seed]` |
| L-system expansion and 2D turtle | `lmd/package/doc/tikz/lsystem.ls` | TikZ `lindenmayersystems` support |
| Ear-clipping triangulation, polygon tests, clipping | `lmd/package/chart/geometry.ls` | Chart geometry |
| Mesh isolines (marching over faces) | `lmd/package/chart/mesh_contour.ls` | Chart contour marks |
| Exact orientation / incircle predicates | `math.orient2d`, `math.orient3d`, `math.incircle` | Robust geometry signs |
| Ad-hoc PRNGs | `chart/network.ls` (`randoms`, Park–Miller), `test/demo/photo-album/mod_collage.ls` (`rand`, an LCG hash), `math.random(seed + i)` in demos | One-off randomness per consumer |

The package gives these a single home, adds the standard algorithm families, and fixes one determinism contract for all of them (PG2). Its consumers are the demos, `lambda.image` (rasters), `lambda.chart` (heatmaps, contours), `lambda.scene3d` (terrain meshes), `lambda.doc.tikz` (L-systems), SVG output, and user scripts.

**In scope:** algorithms that turn a seed and parameters into ordinary Lambda data.

**Out of scope:** rendering (other packages do it, PG4); live animation and simulation stepping (`lambda.physics`, `lambda.motion`); procedural audio synthesis (a later `lambda.music` concern, though `text` and `rng` serve it); machine-learned generation; mesh boolean operations and 3D modelling beyond heightfield meshes.

## 2. Package name (PG1)

**Decided: `lambda.procgen`** (USER, 2026-10-26), shipped at `lmd/package/procgen.ls` with modules under `lmd/package/procgen/` (D7.2.4v4 maps `lambda.procgen.<m>` to `package/procgen/<m>.ls`).

| Candidate | Verdict | Reason |
|---|---|---|
| `lambda.math.proc` | Rejected | `lambda.math` is the built-in math module (D7.2.4v4). A shipped package beneath a built-in module's path blurs the "a path names exactly one thing" rule and would need a resolution exception. Also, *proc* already means **procedure** in Lambda (`pn`, `lambda.exe run`, "procedural script"), so `math.proc` reads as "math procedures". And most of the package is not math: text grammars, dungeons, mazes, L-systems. |
| `lambda.gen` / `lambda.generate` | Rejected | Too generic: it reads as code generation or generators/iterators. |
| `lambda.random` | Rejected | Names one module of the package (`rng`), not the package. |
| `lambda.noise` | Rejected | Same problem: one module. |
| `lambda.synth` | Rejected | Suggests audio synthesis. |
| **`lambda.procgen`** | **Chosen** | The established field term (the PCG community, the Wikipedia article title, Godot/Unreal PCG tooling); short; unambiguous; flat like `lambda.physics`, `lambda.graph`, `lambda.chart`. |

Module names avoid colliding with existing package roots: the text-grammar module is `procgen.text`, not `procgen.grammar`, because `lambda.grammar` already ships (`lmd/package/grammar/`).

## 3. Design principles

### PG2 — Determinism contract

Every public function is an `fn` (S12.1.1v3). Its output is a function of its seed key and parameters alone, so it never reads the clock, the environment or ambient randomness. The contract has two tiers:

- **Exact tier.** The same inputs give bit-identical output on every platform, on both execution tiers (T0 interpreter and MIR JIT, D8.1.1v18), and in any native port (PG7). Exact-tier kernels use only integer operations and IEEE-754 basic operations (`+ − × ÷`, `sqrt`, `floor`, comparisons). All of these are correctly rounded, so they agree everywhere. Gradient directions, kernel weights and similar values are **tabulated constants**, never computed at run time with `sin`/`cos`/`exp`.
- **Tolerance tier.** Functions that genuinely need transcendental functions (normal and exponential deviates via `log`, turtles turning by a free angle via `sin`/`cos`, Lloyd relaxation centroids) are deterministic per platform and agree across platforms only to libm accuracy. Each such function says so in its doc comment and in the user guide.

The exact tier is the default. The tolerance tier starts with exactly three members: `rng.normal` / `rng.exponential`, free-angle turtles in `procgen.lsystem`, and Lloyd relaxation in `procgen.geom` (USER, 2026-10-26, PG-O4). A new function joins it only with a stated reason, recorded here. *Why: generated content is often content-addressed. A world seed shared between users, a golden test, or a cached tile must reproduce. libm results are the usual source of cross-platform drift in noise libraries.*

### PG3 — Keyed (counter-based) randomness, not threaded state

`math.random(seed)` threads state: `[value, next_seed]`. That forces every generator into a sequential fold. Results then depend on evaluation order, and two independent sub-generators cannot draw without coordinating. The package's primary model is **keyed randomness** instead: a random value is a pure hash of a *key*, and keys are derived from a parent key plus a path.

```lambda no-run
import rng: lambda.procgen.rng

let world = rng.key(42)                       // root key from an integer seed
let trees = rng.derive(world, "trees")        // independent sub-stream by name
let chunk = rng.derive(world, "chunk", 12, -3) // by any path of ints/strings
rng.unit(rng.derive(chunk, 7))                // float in [0, 1), the 7th draw of the chunk
rng.below(rng.derive(chunk, 8), 6)            // int in [0, 6)
rng.pick(trees, ["oak", "pine", "birch"])
rng.shuffle(trees, cards)                     // keyed Fisher–Yates
```

Consequences:

- **Order independence.** The chunk at `(12, −3)` is the same whether it is generated first, last, lazily, or on another task. That makes infinite and chunked worlds possible, and safe under Lambda concurrency.
- **No seed plumbing.** A generator receives one key and derives what it needs. Adding a new random draw does not shift every later draw. It does with threaded state, a classic cause of "changing one thing reshuffled the world".
- **Same mixer as `math.random`.** Keys are `u64`. `derive` and `unit` use the SplitMix64 finalizer that `lib/math_utils.h` already defines (Appendix A), written in Lambda with `u64` wrapping arithmetic (S4.1.5) and `bxor`/`ushr`. A probe on 2026-10-26 confirmed that Lambda's `u64` multiply/xor/shift reproduces the C finalizer bit for bit. `math.random` stays as the sequential API. `rng.stream(key)` offers the same `[value, next]` shape for algorithms that are naturally sequential.

The ad-hoc generators in §1 (`chart/network.ls` `randoms`, `photo-album` `rand`) may migrate to `rng` when their owners next touch them (USER, 2026-10-26, PG-O7). The migration is not part of this package's phases.

### PG4 — Data out; rendering stays elsewhere

Outputs are ordinary Lambda values: numbers, arrays, maps and elements. The package imports nothing from Radiant and performs no IO (D7.1.1, D7.1.2v2). A thin `procgen.adapt` module constructs the data that other packages already accept: grayscale or colormapped pixel arrays for `lambda.image`, `grid`-kind data for `lambda.chart` heatmaps and contours, vertex/index arrays for `lambda.scene3d`, `<svg>` polyline elements. These constructors are pure. Rendering itself stays in those packages.

### PG5 — Shared data shapes

| Shape | Representation | Why |
|---|---|---|
| Scalar field | `float[][]`, row-major (`f[y][x]`) | Matches `lambda.chart`'s `grid` kind matrices. Element-wise arithmetic recurses through nested arrays (S10.2.1, probed 2026-10-26: `[[1.0, 2.0], [3.0, 4.0]] * 0.5 + 1.0` works). Field combinators are then plain expressions: `0.6 * a + 0.4 * b`, `f * mask`. |
| Point | `[x, y]` or `[x, y, z]` float array | Matches `chart/geometry.ls` and the `math.orient*` predicates. |
| Triangulation | `{points, triangles: [[i, j, k], …], halfedges}` | The Delaunator layout: compact, index-based, the de-facto interchange shape. Voronoi is derived from it. |
| Tile grid | `int[][]` of tile or cell codes | Mazes, dungeons, CA and WFC share it. |
| Graph | `{nodes, edges: [[a, b], …]}` | Matches `lambda.graph` input, so generated graphs can be laid out and drawn there. |

Fields are `float64` in the API (USER, 2026-10-26, PG-O5). A compact `f32` carrier may be revisited only inside a native kernel's internals, with parity defined against the `float64` Lambda reference (PG7.3). It is never an API shape. Exact-tier kernels compute in `float64` throughout; `f32` internals are allowed only for tolerance-tier functions (USER, 2026-10-26). *Why: `f32` rounding cannot reproduce the `float64` reference bit for bit, so it would break the exact tier's parity requirement.*

### PG6 — Purity and the native boundary

Because `fn` cannot mutate or call `pn` (S12.1.1v3), every algorithm in the package is written functionally: folds, recursion and comprehensions over persistent values. Most families are naturally functional: noise is a pure point function, fields are maps over coordinates, L-systems are rewriting, grammars are expansion. A few are naturally mutation-heavy in other languages:

- hydraulic erosion (millions of droplet steps mutating a heightmap),
- WFC propagation (repeated in-place shrinking of cell domains),
- incremental Delaunay (Bowyer–Watson edits a triangle soup).

These still get a pure Lambda formulation, which is the **reference implementation**. They are also the natural candidates for PG7. A native port is an optimization of a reference that exists and is tested. It is never the first implementation.

### PG7 — The native-port gate

An algorithm moves to C under `lib/` only when **all** of the following hold:

1. **Measured need.** A named benchmark (§7) on a **release build** (AGENTS rule 10) misses its stated budget by at least 5×. The fully vectorized Lambda formulation (PG5) and the MIR JIT tier have already been tried.
2. **Kernel shape.** The hot part is a tight numeric loop over flat data, small (on the order of a few hundred lines of C), with no allocation-heavy structure. The port covers the kernel, not the API. Parameter handling, validation and options stay in Lambda.
3. **Parity.** The native kernel reproduces the Lambda reference bit for bit for exact-tier functions (PG2), or within the stated tolerance otherwise. An exact-tier kernel therefore computes in `float64` throughout, and `f32` internals are allowed only for tolerance-tier functions (PG5; USER, 2026-10-26). A parity test runs both on the same inputs. The Lambda reference is never deleted: it remains the oracle and the fallback.
4. **Layering and ABI.** The C code is context-free under `lib/procgen/`: no `Item`, no Lambda runtime headers (D7.1.1). It is exposed as pure **batch functions in the built-in `math` module** (e.g. `math.noise_field`, `math.erode`; USER, 2026-10-26, PG-O2), following the D6.4.1–D6.4.2 registration checklist. Batch-shaped means one call fills a whole field or runs a whole erosion pass, so the boundary cost is paid once. These are general numeric kernels, so `math` is their home and the prelude namespace stays clean. The package path `lambda.procgen` cannot also be a built-in module (D7.2.4v4).
5. **Fresh kernels, no vendored library.** Kernels are written fresh from the published algorithms, citing references (Appendix B). FastNoiseLite and the other libraries there are cited references only, never vendored (USER, 2026-10-26, PG-O3). The Lambda reference defines the output (PG2, PG7.3). A vendored kernel would make the reference chase that library's `float32` arithmetic and hashing details, and could not be changed in place to match the reference (AGENTS rule 16).

The package API does not change when a kernel goes native: the module calls the system function when it is registered and falls back to the Lambda reference otherwise.

### PG8 — Bounded work and explicit errors

Every generator takes explicit sizes, iteration counts or symbol caps. It raises an error (a `T^E` return, S7.4.6) when a request exceeds a documented limit or its constraints are unsatisfiable (WFC contradiction after the retry budget, an over-full Poisson domain). It never truncates silently. `tikz/lsystem.ls` already does this (`expansion exceeds 100000 symbols`). That cap moves with the code (PG9).

### PG9 — Promote, don't copy

Where Lambda already has an algorithm, the package takes ownership of it and the old site imports it (AGENTS rule 13):

- `doc/tikz/lsystem.ls`: rule parsing, expansion and the turtle move to `procgen.lsystem`. `lambda.doc.tikz` keeps only its TikZ option adapter.
- `chart/geometry.ls` and `chart/mesh_contour.ls` stay in `lambda.chart`, and `procgen` imports them. They are general geometry, but moving them now would churn the chart package for no consumer benefit. If a third package needs them, they move to a shared geometry module then (USER, 2026-10-26, PG-O6).

### PG10 — Point functions plus field builders

Each noise and sampling family has two levels: a **point function** (`noise.simplex(key, x, y)`), which is the reference and the composable primitive, and a **field builder** (`noise.field(spec, w, h, key)`), which evaluates a spec over a grid. The field builder is the unit a native kernel replaces (PG7.4). User code that composes point functions keeps working unchanged, just at Lambda speed.

## 4. Module map

| Module | Contents | Reuses | Tier | Native candidate |
|---|---|---|---|---|
| `procgen.rng` | `key`, `derive`, `unit`, `below`, `range`, `chance`, `pick`, `weighted`, `shuffle`, `sample` (k of n), `stream`; `normal`, `exponential` (tolerance tier) | SplitMix64 finalizer (Appendix A) | Exact | No |
| `procgen.noise` | white, value, Perlin (improved, 2002), OpenSimplex2 (2D/3D), cellular/Worley (F1, F2, F2−F1; Euclidean, Manhattan, Chebyshev); fractals: `fbm`, `ridged`, `billow`; domain warp; periodic (tileable) variants; `field` builder | `rng` hashing for lattice gradients | Exact (tabulated gradients) | **Yes**: `field` batch |
| `procgen.field` | `make`, `map`, `normalize`, `remap`, `clamp`, `curve`, `terrace`, `radial` / `island` masks, box blur, gradient/slope, `threshold` → mask, bilinear `sample`, distance transform, `contours` | `chart/mesh_contour.ls` for isolines | Exact | Blur / distance transform only if measured |
| `procgen.sample` | Poisson disk (Bridson), jittered grid, best-candidate blue noise, Halton and R2 low-discrepancy sequences, density-weighted sampling over a field | `rng`, `field.sample` | Exact | Poisson for very dense domains, if measured |
| `procgen.geom` | Delaunay triangulation, Voronoi cells (dual), Lloyd relaxation, convex hull, half-edge navigation | `math.orient2d`, `math.incircle`, `chart/geometry.ls` | Exact (predicates exact; Lloyd tolerance) | **Yes**: Delaunay above ~10⁴ points, if measured |
| `procgen.grid` | cellular automata (B/S rules, cave smoothing), BSP room partition, drunkard's walk, rooms-and-corridors, flood fill / regions, distance maps; mazes: recursive backtracker, Kruskal, Prim, Wilson (uniform spanning tree) | `rng` | Exact | No |
| `procgen.wfc` | simple-tiled and overlapping models, adjacency inference from a sample grid, min-entropy selection, propagation, bounded retry | `rng`, `grid` shapes | Exact | **Yes**: propagation on large grids, if measured |
| `procgen.lsystem` | deterministic, stochastic (keyed) and parametric L-systems; 2D turtle, 3D turtle later; bracket stack; segment widths/depths | promoted from `doc/tikz/lsystem.ls` (PG9) | Exact (turn angles tabulated per system; tolerance when the angle is free) | No |
| `procgen.text` | Tracery-compatible grammars (`#symbol#`, `.capitalize`, `.s`, `.a`, actions); order-n character Markov models for names | `rng` | Exact | No |
| `procgen.terrain` | heightmap recipes (fBm + warp + island mask + terrace), thermal erosion, hydraulic droplet erosion, flow accumulation and rivers, moisture/temperature → biome (Whittaker-style table) | `noise`, `field` | Exact | **Yes**: hydraulic erosion |
| `procgen.adapt` | field → `lambda.image` pixels (grayscale, colormap); field → chart `grid` data; heightfield → `scene3d` vertex/index arrays; polylines → `<svg>` | `chart` color scales where they fit | Exact | No |

The facade `lambda.procgen` re-exports the common entry points so that `import pg: lambda.procgen` serves a quick script. Larger scripts import modules directly.

## 5. API sketches

These are illustrative. Exact spellings are settled in P0–P2.

```lambda no-run
import rng: lambda.procgen.rng
import noise: lambda.procgen.noise
import field: lambda.procgen.field
import sample: lambda.procgen.sample

// an island heightmap: fractal simplex noise, warped, shaped by a radial mask
let world = rng.key(2026)
let spec = {kind: 'simplex', octaves: 5, frequency: 1.0 / 160.0, lacunarity: 2.0, gain: 0.5,
    warp: {amplitude: 24.0, frequency: 1.0 / 300.0}}
let h = field.normalize(noise.field(spec, 512, 512, rng.derive(world, "height")))
let land = h * 1.2 - field.radial(512, 512) * 0.7      // element-wise (S10.2.1)
let trees = sample.poisson(rng.derive(world, "trees"), {width: 512.0, height: 512.0, radius: 6.0})
    |: field.sample(land, ~[0], ~[1]) > 0.35
```

```lambda no-run
import grid: lambda.procgen.grid
import geom: lambda.procgen.geom

let cave = grid.automaton(rng.key(7), {width: 80, height: 50, fill: 0.45, rule: "B5678/S45678", steps: 5})
let maze = grid.maze(rng.key(7), {width: 30, height: 20, method: 'wilson'})   // uniform spanning tree
let mesh = geom.delaunay(sample.poisson(rng.key(7), {width: 800.0, height: 600.0, radius: 20.0}))
let cells = geom.voronoi(mesh, {bounds: [0.0, 0.0, 800.0, 600.0]})
```

```lambda no-run
import lsys: lambda.procgen.lsystem
import text: lambda.procgen.text

let plant = lsys.expand({axiom: "X", rules: {X: "F+[[X]-X]-F[-FX]+X", F: "FF"}}, 6)
let lines = lsys.turtle(plant, {step: 4.0, angle: 25.0})        // polylines for adapt.svg

let tale = text.grammar({origin: "#hero# found #artifact.a#.", hero: ["Ada", "Ming"],
    artifact: ["owl", "ember", "lantern"]})
text.expand(tale, rng.key(3))
```

## 6. Relationship to existing code

| Existing | After |
|---|---|
| `math.random` | Unchanged; documented as the sequential API next to `rng` keys. |
| `lib/math_utils.h` `math_splitmix64` | Unchanged. Its finalizer constants are the `rng` mixer (Appendix A). A native port reuses the same header. |
| `doc/tikz/lsystem.ls` | Expansion and turtle move to `procgen.lsystem`; the tikz adapter imports them (PG9). |
| `chart/geometry.ls`, `chart/mesh_contour.ls` | Imported, not copied (PG9, PG-O6). |
| `math.orient2d` / `math.incircle` | The robustness basis of `geom.delaunay`. |
| Ad-hoc demo PRNGs | Optional migration (PG-O7). |

## 7. Verification and benchmarks

**Goldens.** Each module has unit scripts under `test/lambda/procgen/*.ls`, each with its `.txt` golden (AGENTS rule 8). Large outputs are not printed whole. A golden prints a few sampled values plus a digest: `hash` over the formatted field, which also pins the exact tier (PG2).

**Invariant tests** check properties rather than pictures:

- noise stays in its documented range, and its mean and variance are near the expected values;
- Poisson samples keep their minimum distance and cover the domain within a bound;
- Delaunay passes an empty-circumcircle check via `math.incircle`;
- a maze is a spanning tree (edges = cells − 1, connected);
- WFC output satisfies every adjacency rule;
- the CA, BSP and dungeon generators keep regions connected where they promise to;
- L-system expansion matches the promoted tikz golden output exactly.

**Tier parity.** The golden suite runs under `LAMBDA_EXEC_BACKEND=interp` and `jit`, and both must give identical digests. Once a native kernel exists, a parity test compares it with the Lambda reference (PG7.3).

**Benchmarks** (release build only) are the evidence PG7.1 asks for. Budgets are proposals to tune in P0:

| Benchmark | Proposed budget |
|---|---|
| 512×512 five-octave simplex `noise.field` | ≤ 1 s |
| 20k-point Poisson + Delaunay + Voronoi | ≤ 2 s |
| 64×64 overlapping WFC (N = 3) | ≤ 5 s |
| 256×256 hydraulic erosion, 50k droplets | ≤ 5 s |

**Showcase.** A `test/demo/procgen/` demo builds a world map from one seed: terrain, biomes, rivers, a placed town (Voronoi districts), a dungeon and generated names. It exercises the whole package and gives the benchmarks a realistic workload.

## 8. Phasing

| Phase | Delivers | Acceptance |
|---|---|---|
| **P0** | `rng`, point noise (value, Perlin, simplex in 2D and 3D, cellular, fBm, warp), `field` core, `adapt.image`, benchmark harness | Goldens + tier parity; first benchmark numbers recorded in the impl doc |
| **P1** | `sample` (Poisson, jitter, Halton/R2), `geom` (Delaunay, Voronoi, Lloyd, hull) | Invariant tests; Delaunay robust on degenerate (cocircular, collinear) inputs |
| **P2** | `grid`: CA, BSP, rooms-and-corridors, walks, mazes, regions | Invariant tests |
| **P3** | `lsystem` promotion from tikz (2D turtle); `text` grammars and Markov names | tikz goldens unchanged |
| **P4** | `wfc` simple-tiled and overlapping | Adjacency invariants; bounded retry errors |
| **P5** | `terrain`: recipes, thermal and hydraulic erosion, rivers, biomes; showcase demo | Demo runs; benchmarks recorded |
| **P6** | Native kernels, **only** for families that failed PG7.1 in P0–P5 | Parity tests; no API change |
| Later | 4D noise, the 3D turtle (PG-O8) | Scheduled when a consumer needs them |

The implementation record goes in `vibe/impl/Lambda_Impl_Proc_Gen.md` when P0 starts. A user guide section goes in `doc/Lambda_Packages.md` once P0 ships.

## 9. Open items

| ID | Question | Recommendation |
|---|---|---|
| ~~**PG-O1**~~ | ~~Package name (PG1).~~ | **Resolved** (USER, 2026-10-26): `lambda.procgen`; see PG1. |
| ~~**PG-O2**~~ | ~~Where native kernels surface: `math.*` batch functions, flat `procgen_*` system functions, or a full-path module.~~ | **Resolved** (USER, 2026-10-26): `math.*` batch functions; see PG7.4. Flat `procgen_*` names (the `physics_*` precedent) were rejected because they would crowd the prelude namespace. |
| ~~**PG-O3**~~ | ~~For native noise, write fresh C kernels or vendor FastNoiseLite (MIT) unpatched.~~ | **Resolved** (USER, 2026-10-26): write fresh C kernels; FastNoiseLite stays a cited reference only. See PG7.5. |
| ~~**PG-O4**~~ | ~~Which functions enter the tolerance tier (PG2).~~ | **Resolved** (USER, 2026-10-26): only `rng.normal`/`exponential`, free-angle turtles and Lloyd relaxation at first; each addition needs a stated reason. See PG2. |
| ~~**PG-O5**~~ | ~~`f32` compact carriers for large fields (memory) versus `float64` everywhere.~~ | **Resolved** (USER, 2026-10-26): `float64` in the API; `f32` only revisited inside a native kernel's internals, with parity defined against the `float64` reference. See PG5. |
| ~~**PG-O6**~~ | ~~Whether `chart/geometry.ls` general helpers move to a shared module now or when a third consumer appears.~~ | **Resolved** (USER, 2026-10-26): when a third consumer appears. See PG9. |
| ~~**PG-O7**~~ | ~~Whether to migrate the demos' and `chart/network.ls`'s ad-hoc PRNGs to `rng`.~~ | **Resolved** (USER, 2026-10-26): opportunistically, by each owner; not part of this package's phases. See PG3. |
| ~~**PG-O8**~~ | ~~3D/4D noise and 3D turtles: in P0/P3 or later.~~ | **Resolved** (USER, 2026-10-26): 3D simplex in P0 (needed for animated 2D noise via a time axis); 4D noise and the 3D turtle later. See §8. |

## Appendix A. Keyed RNG construction

Keys and draws use the SplitMix64 finalizer of `lib/math_utils.h`, so native and Lambda code share one definition:

```text
mix64(z):  z = (z ^ (z >>> 30)) * 0xbf58476d1ce4e5b9
           z = (z ^ (z >>> 27)) * 0x94d049bb133111eb
           return z ^ (z >>> 31)                      // all arithmetic mod 2^64 (u64)

key(seed)            = mix64(u64(seed) + 0x9e3779b97f4a7c15)
derive(k, p1, …, pn) = fold: k = mix64((k ^ part(p_i)) + 0x9e3779b97f4a7c15 * (i + 1))
part(int)            = u64(two's-complement of the int)
part(string)         = FNV-1a 64 over the UTF-8 bytes
unit(k)              = float(mix64(k) >>> 11) * 2^-53    // [0, 1), same mapping as math_splitmix64
below(k, n)          = Lemire multiply-shift on mix64(k), rejection keyed by derive(k, attempt)
```

All of this uses only `u64` wrapping arithmetic (S4.1.5), `bxor` and `ushr`, all available in Lambda today. It is exact-tier. The position-dependent multiplier in `derive` keeps `derive(k, a, b)` and `derive(k, b, a)` distinct. Statistical quality is validated in P0 by PractRand or TestU01 SmallCrush runs over `unit` streams of several derivation shapes. The results are recorded in the impl doc, not here.

## Appendix B. References

Algorithms are reimplemented from the published descriptions; no code is copied. Licenses are listed for libraries consulted as behavioral references.

| Family | Reference | License of the reference code |
|---|---|---|
| Perlin noise | Ken Perlin, *Improving Noise* (SIGGRAPH 2002) | — |
| Simplex family | OpenSimplex2 (K.jpg) | public domain (CC0 / Unlicense) |
| Noise parameterization | FastNoiseLite (Auburn) | MIT |
| Poisson disk | Robert Bridson, *Fast Poisson Disk Sampling in Arbitrary Dimensions* (2007) | — |
| Delaunay layout | Delaunator (Mapbox) | ISC |
| Voronoi maps | Red Blob Games, *Polygonal Map Generation* / mapgen2 | Apache-2.0 |
| Wave function collapse | Maxim Gumin, WaveFunctionCollapse | MIT |
| Roguelike generators | libtcod (BSP, CA, FOV) | BSD-3 |
| Mazes | Jamis Buck, *Mazes for Programmers*; Wilson (1996) uniform spanning trees | — |
| L-systems | Prusinkiewicz & Lindenmayer, *The Algorithmic Beauty of Plants* | — |
| Text grammars | Kate Compton, Tracery | Apache-2.0 |
| Hydraulic erosion | Hans Theobald Beyer, *Implementation of a method for hydraulic erosion* (2015) | — |

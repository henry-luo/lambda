# Lambda Procedural Art Package — `lambda.procart`

> **Status:** P0 (`rng`, `noise`, `field`, `adapt`), P1 (`sample`, `geom`), P2 (`grid`), P3 (`lsystem`, `text`), P4 (`wfc`) and P5 (`terrain`, showcase demo) have core implementations (2026-10-26), with remaining features listed in the [implementation record §6](impl/Lambda_Impl_Proc_Gen.md#6-residue). The 2026-10-27 continuation fills the remaining field, periodic-noise, density-sampling, adapter, width and climate features, registers the showcase, and delivers all three measured P6 batch kernels (WFC, Delaunay and hydraulic erosion). A further Lambda-side performance continuation preserves exact values (S1.6, PG2), reducing the 512² noise benchmark to 1.014 s and the full Poisson–Voronoi pipeline to 3.853 s, including conservative cell-coverage certificates and direct polygon output. Their 1 s / 2 s targets, cross-platform qualification and explicitly later features remain open. Progress and verification are in the [implementation record](impl/Lambda_Impl_Proc_Gen.md). The `math.*` surface for native kernels (PG7.4), fresh, unvendored C kernels (PG7.5), the initial tolerance-tier membership (PG2), `float64` fields in the API (PG5), the timing of shared geometry (PG9), PRNG migration (PG3) and the 3D/4D scope (§8) were accepted by the user on 2026-10-26. Every §9 open item is resolved.
> **P7:** the general drawing grammar and four-sample Form studies page are implemented (2026-10-28); [delivery and qualification](impl/Lambda_Impl_Procart_Grammar.md).
> **P8:** skeleton-first trees, parametric development, a 3D turtle, explicit LODs and the four-sample Tree studio are implemented (2026-10-28); [delivery and qualification](impl/Lambda_Impl_Procart_Plants.md).
> **P9:** reusable surface resources, role bindings, leaf/branch coordinates and object-space texture atlases are implemented (2026-10-28); [design](Lambda_Design_Procart.md#11-p9--reusable-surface-patterns-pg14), [qualification](impl/Lambda_Impl_Procart_Surfaces.md).
> **P10:** global environment data, solar lighting, responsive herbs, rooted native wind, and a larger meadow with a pond are implemented (2026-10-28); [design](Lambda_Design_Procart.md#12-p10--environment-and-a-living-meadow-pg15), [qualification](impl/Lambda_Impl_Procart_Environment.md).
> **P11:** stand-alone animal morphology, admitted activity poses, shared skin/feather/scale patterns and reusable butterfly art support Eden's Plant/Animal builders; [PG16 design](Lambda_Design_Procart.md#13-p11--procedural-animal-specimens-and-markings-pg16).
> **Scope:** a shipped script package for seeded procedural art: keyed randomness, noise, scalar fields, sampling, geometry, grids, L-systems, plant architecture, surface patterns, text grammars and terrain, plus stored profiles in PG11–PG16. Legacy Meadow botanical generators remain in the demo; P8's tree architecture and spatial turtle are package exports. Algorithms are written in Lambda first. A small set moves to native C under `lib/` only when a release-build measurement requires it (PG7).
> **Spec linkage:** D7.2.4v4 (the `lambda.*` root; a path names exactly one thing; `lambda.math` is the built-in math module), S12.1.1v3 (`fn` is pure and deterministic and can never call `pn`), S10.2.1 / S10.2.4 (element-wise arithmetic and its promotion), S4.1.5 (machine ints wrap), D7.1.1 (static-library layering), D6.4.1–D6.4.2 (system-function ABI and registration checklist). This proposal changes no formal ruling.
> **Authority:** package design ledger PG1–PG17, open items PG-O1–PG-O8. The user accepted semantic scene composition and the package rename to `lambda.procart` on 2026-10-28. PG11 documents Meadow, PG12 the general drawing grammar, PG13 plant architecture inspired by EZ-Tree and L-Py, PG14 reusable surface patterns and PG17 articulated avatar recipes. The existing PG IDs remain stable across the rename.

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

**In scope:** algorithms that turn a seed and parameters into ordinary Lambda data; P7 adds a stored composition grammar over drawing primitives and semantic objects.

**Out of scope:** rendering (other packages do it, PG4); live animation and simulation stepping (`lambda.physics`, `lambda.motion`); procedural audio synthesis (a later `lambda.music` concern, though `text` and `rng` serve it); machine-learned generation; mesh boolean operations and unrestricted mesh editing. P7 extends the original heightfield-only geometry scope with bounded primitive and path geometry.

## 2. Package name (PG1)

**Decided: `lambda.procart`** (USER, 2026-10-28), shipped at `lmd/package/procart.ls` with modules under `lmd/package/procart/` (**D7.2.4v4** maps `lambda.procart.<m>` to `package/procart/<m>.ls`). The name covers both the reusable generation algorithms and their composition into procedural artworks.

~~The original package name was `lambda.procgen`, accepted on 2026-10-26 for the algorithm library.~~ The user renamed the package after accepting semantic scenes such as Meadow. Imports and package/test paths move together; there is no compatibility facade at the former package path. Native numeric kernels remain internal to `lib/procgen/` and retain their `math.*` entry points (PG7.4).

| Candidate | Verdict | Reason |
|---|---|---|
| `lambda.math.proc` | Rejected | `lambda.math` is the built-in math module (D7.2.4v4). A shipped package beneath a built-in module's path blurs the "a path names exactly one thing" rule and would need a resolution exception. Also, *proc* already means **procedure** in Lambda (`pn`, `lambda.exe run`, "procedural script"), so `math.proc` reads as "math procedures". And most of the package is not math: text grammars, dungeons, mazes, L-systems. |
| `lambda.gen` / `lambda.generate` | Rejected | Too generic: it reads as code generation or generators/iterators. |
| `lambda.random` | Rejected | Names one module of the package (`rng`), not the package. |
| `lambda.noise` | Rejected | Same problem: one module. |
| `lambda.synth` | Rejected | Suggests audio synthesis. |
| **`lambda.procart`** | **Chosen, 2026-10-28** | Names procedural art, including authored semantic scenes and the algorithms that regenerate them; matches the Procart demo. |

Module names avoid colliding with existing package roots: the text-grammar module is `procart.text`, not `procart.grammar`, because `lambda.grammar` already ships (`lmd/package/grammar/`).

## 3. Design principles

### PG2 — Determinism contract

Every public function is an `fn` (S12.1.1v3). Its output is a function of its seed key and parameters alone, so it never reads the clock, the environment or ambient randomness. The contract has two tiers:

- **Exact tier.** The same inputs give bit-identical output on every platform, on both execution tiers (T0 interpreter and MIR JIT, D8.1.1v18), and in any native port (PG7). Exact-tier kernels use only integer operations and IEEE-754 basic operations (`+ − × ÷`, `sqrt`, `floor`, comparisons). All of these are correctly rounded, so they agree everywhere. Gradient directions, kernel weights and similar values are **tabulated constants**, never computed at run time with `sin`/`cos`/`exp`.
- **Tolerance tier.** Functions that genuinely need transcendental functions (normal and exponential deviates via `log`, turtles turning by a free angle via `sin`/`cos`, Lloyd relaxation centroids) are deterministic per platform and agree across platforms only to libm accuracy. Each such function says so in its doc comment and in the user guide.

The exact tier is the default. The tolerance tier starts with exactly three members: `rng.normal` / `rng.exponential`, free-angle turtles in `procart.lsystem`, and Lloyd relaxation in `procart.geom` (USER, 2026-10-26, PG-O4). A new function joins it only with a stated reason, recorded here. *Why: generated content is often content-addressed. A world seed shared between users, a golden test, or a cached tile must reproduce. libm results are the usual source of cross-platform drift in noise libraries.*

P7 adds free-angle primitive/sweep geometry to the tolerance tier, because
rotations and tessellation use trigonometric functions; see [PG12.9](Lambda_Design_Procart.md#99-package-and-renderer-boundary-pg129).
HSV conversion uses fixed arithmetic and byte quantization. Keyed choices,
identities, topology and counts retain their exact contracts.

### PG3 — Keyed (counter-based) randomness, not threaded state

`math.random(seed)` threads state: `[value, next_seed]`. That forces every generator into a sequential fold. Results then depend on evaluation order, and two independent sub-generators cannot draw without coordinating. The package's primary model is **keyed randomness** instead: a random value is a pure hash of a *key*, and keys are derived from a parent key plus a path.

```lambda no-run
import rng: lambda.procart.rng

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

Outputs are ordinary Lambda values: numbers, arrays, maps and elements. The package imports nothing from Radiant and performs no IO (D7.1.1, D7.1.2v2). A thin `procart.adapt` module constructs the data that other packages already accept: grayscale or colormapped pixel arrays for `lambda.image`, `grid`-kind data for `lambda.chart` heatmaps and contours, vertex/index arrays for `lambda.scene3d`, `<svg>` polyline elements. These constructors are pure. Rendering itself stays in those packages.

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
4. **Layering and ABI.** The C code is context-free under `lib/procgen/`: no `Item`, no Lambda runtime headers (D7.1.1). It is exposed as pure **batch functions in the built-in `math` module** (e.g. `math.noise_field`, `math.erode`; USER, 2026-10-26, PG-O2), following the D6.4.1–D6.4.2 registration checklist. Batch-shaped means one call fills a whole field or runs a whole erosion pass, so the boundary cost is paid once. These are general numeric kernels, so `math` is their home and the prelude namespace stays clean. The package path `lambda.procart` cannot also be a built-in module (D7.2.4v4).
5. **Fresh kernels, no vendored library.** Kernels are written fresh from the published algorithms, citing references (Appendix B). FastNoiseLite and the other libraries there are cited references only, never vendored (USER, 2026-10-26, PG-O3). The Lambda reference defines the output (PG2, PG7.3). A vendored kernel would make the reference chase that library's `float32` arithmetic and hashing details, and could not be changed in place to match the reference (AGENTS rule 16).

The package API does not change when a kernel goes native: the module calls the system function when it is registered and falls back to the Lambda reference otherwise.

### PG8 — Bounded work and explicit errors

Every generator takes explicit sizes, iteration counts or symbol caps. It raises an error (a `T^E` return, S7.4.6) when a request exceeds a documented limit or its constraints are unsatisfiable (WFC contradiction after the retry budget, an over-full Poisson domain). It never truncates silently. `tikz/lsystem.ls` already does this (`expansion exceeds 100000 symbols`). That cap moves with the code (PG9).

### PG9 — Promote, don't copy

Where Lambda already has an algorithm, the package takes ownership of it and the old site imports it (AGENTS rule 13):

- `doc/tikz/lsystem.ls`: rule parsing, expansion and the turtle move to `procart.lsystem`. `lambda.doc.tikz` keeps only its TikZ option adapter.
- `chart/geometry.ls` and `chart/mesh_contour.ls` stay in `lambda.chart`, and `procart` imports them. They are general geometry, but moving them now would churn the chart package for no consumer benefit. If a third package needs them, they move to a shared geometry module then (USER, 2026-10-26, PG-O6).

### PG10 — Point functions plus field builders

Each noise and sampling family has two levels: a **point function** (`noise.simplex2(table, x, y)`, over a permutation table from `noise.table(key)`), which is the reference and the composable primitive, and a **field builder** (`noise.field(gen, w, h)`, over a generator from `noise.make(key, spec)`), which evaluates a spec over a grid. The field builder is the unit a native kernel replaces (PG7.4). User code that composes point functions keeps working unchanged, just at Lambda speed.

### PG11 — Stored procedural art

**Current profile, accepted 2026-10-28.** A literal Mark document stores versioned recipes and seeded `flower`, `tree`, `stone` and `grass` objects, recursively composed through `group` elements under `meadow`. The artwork contains data; generator implementations remain code. Loading a document does not execute a Lambda function embedded in it. Generation is pure (**S12.1.1v3 / PG4**).

The first profile is implemented under `test/demo/procart/`: [schema](../test/demo/procart/mod_composition_schema.ls), [resolver](../test/demo/procart/mod_composition.ls), [model](../test/demo/procart/mod_meadow.ls) and [complete saved meadow](../test/demo/procart/meadow.mark). These are application modules, not additional `lambda.procart` exports. [Semantic scene design PG11.1–PG11.8](Lambda_Design_Procart.md) gives the rationale; the [implementation record](impl/Lambda_Impl_Procart_Meadow.md) records qualification and remaining failures.

**Structural schema.** The standalone type-only schema uses ordinary recursive element types (**S2.6.1v2 / S11.4.12**):

```lambda no-run
type Seed = int | string
type KeyPart = int | string
type Attributes = {id:string,recipe:string?,seed:Seed?,key_path:KeyPart[]?,settings:map?,count:int?,spread:number[]?,
    position:number[]?,rotation:number[]?,scale:number[]?,matrix:number[]?}
pub type Node = (<group Node*> | <flower> | <tree> | <stone> | <grass>) that (map(~) is Attributes)
pub type Recipe = <recipe id:string,kind:string,generator:string,version:int,parameters:map>
pub type Document = <meadow version:int,seed:Seed,width:number,depth:number,relief:number,terrain:map?,path_width:number?,
    <resources Recipe+>, Node*>
```

`map(~)` checks the shared attribute facet. Writing `<flower Attributes>` would describe a child value, not the element's attributes. The CLI validator does not execute arbitrary `that` predicates (**S11.4.6**), so the runtime resolver explicitly validates attributes, references and budgets. The structural envelope alone is not full scene admission.

| Stored part | Fields and meaning |
|---|---|
| `meadow` | Required `version:1`, integer/string `seed`, physical `width` and `depth` (4–32), `relief` (0–2); optional `terrain` map and `path_width` (0–3, default 1.05). Contains one `resources` block and 1–64 top-level nodes. |
| `resources` | Contains 1–32 `recipe` definitions. These are generator recipes, distinct from native Scene3D geometry/material resources. |
| `recipe` | Required `id`, semantic `kind`, registered `generator`, exact `version:1` and `parameters` map; no content. |
| Every node | Required `id`; optional `seed`, `key_path`, `count` (default 1), `spread`, and placement attributes. |
| `group` | Contains nodes recursively. Its transform and random stream form the parent context; it has no `recipe` or `settings`. |
| `flower`, `tree`, `stone`, `grass` | Required compatible `recipe` reference; optional `settings` map; no content. A `grass` object denotes a patch. |

IDs contain 1–32 ASCII letters, digits, hyphens or underscores. Recipe and authored node IDs are globally unique within the document. Matrix placement is exclusive with position/rotation/scale. Their defaults are zero position/rotation and unit scale; rotations are radians, matrices are affine column-major arrays of 16 numbers. The resolver uses the existing `lambda.scene3d.transform` operations. Plant branching angles remain recipe parameters in degrees.

| Semantic kind | Generator, version 1 | Effective recipe parameters |
|---|---|---|
| `flower` | `plant` | L-system `rules`, optional `axiom`, or `kind:"lavender"`; `order`, `angle`, stem/leaf/petal/center colors, `petals`, optional `leaf_mode` and `leaf_size`. |
| `tree` | `plant` | Branching-plant parameters with `petals:0`, plus `canopy_radius` and `canopy_colors`; no lavender kind. |
| `stone` | `rock.faceted` | `detail`, `radius`, `color`. |
| `grass` | `grass.blades` | `clumps`, `blades`, `radius`, `height`, `width`, `bend`, `colors`. |

The generator/kind pair and version must be supported. Effective parameters are the recipe map shallowly overlaid by node `settings`: a nested map or array is replaced as a whole. The model validates all effective parameters before producing geometry; unused definitions receive envelope validation. Unknown node attributes and effective generator parameters fail. The current profile exposes branching plants, lavender, tree crowns, faceted rocks and curved grass; other catalogue plant families remain later work.

**Example.** A reusable flower recipe and a repeated patch are stored together:

```mark
<meadow version:1,seed:2026,width:16.0,depth:12.0,relief:0.55,
    terrain:{width:33,height:25,octaves:3,island:0.35},
    <resources
        <recipe id:"cosmos",kind:"flower",generator:"plant",version:1,
            parameters:{rules:{X:"F[+X][-X]FX",F:"FF"},stem:"#54764b",leaf:"#7e995b",
                petals:8,leaf_size:0.55,order:3,angle:25}>
    >;
    <group id:"pink-patch",position:[-2.0,0.0,1.0],count:9,spread:[3.0,2.0],
        <flower id:"pink-flower",recipe:"cosmos",scale:[0.4,0.4,0.4],settings:{order:2}>>
>
```

**Seed and expansion rules.** The root key is `rng.key(meadow.seed)`. For each node, an explicit `seed` creates a new base key; otherwise the parent's key is the base. An explicit `key_path` derives from that base; absent a path, an explicit seed uses its key directly, otherwise the node ID derives the key. A key path has at most eight integer/string components. Reordering siblings preserves their random streams; explicit seeds/paths can preserve streams when IDs change (**PG3**).

Every copy uses `rng.at(node_key, copy_index)`, including a node with count 1. Count is 1–64; increasing it preserves the existing copies' random streams. `spread:[width,depth]` scatters copies in a local rectangle with dimensions 0–24 and seeded yaw. The `placement` and `model` derivation labels separate spatial and model randomness. Parent, authored and scatter transforms compose in that order. Groups pass the copy key and composed matrix to their children.

The resolver returns ordinary records `{id,kind,recipe,key,matrix,parameters}`. Expanded IDs include the named hierarchy and copy indices and are limited to 96 characters. The resolver admits at most eight group levels and 192 expanded leaf objects. The model additionally caps grass at 12,000 blades. Generator-specific ranges remain explicit errors (**PG8**); work is never silently truncated.

**Terrain, rendering and replay.** Terrain defaults to `{width:33,height:25,octaves:3,island:0.35}` when the entire `terrain` map is absent; otherwise the supplied map is passed to `procart.terrain.heightmap`. These width/height values count samples; root width/depth measure the scene. Whole plants/stones use their object's terrain anchor, grass samples each blade base, and the winding path excludes grass.

The pipeline is **stored semantic Mark → resolved objects → model parts/resources → existing Scene3D markup**. Groups, IDs, transforms, materials and mesh instancing reuse Scene3D contracts, while semantic tags and recipe definitions are resolved before renderer normalization. Camera, viewport and layer state belong to presentation (**S12.1.5v2 / D7.5.3v2**); visibility changes reuse the generated model. A seed change regenerates it. The page loads the Mark document separately from its implementation; a file editor/save dialog, external recipe resolver and baked archival output remain future work.

`meadow.version` pins the document profile; each recipe version pins its generator contract, including defaults, parameter meaning and random-key labels. Regeneration needs compatible code as well as the saved document. Numerical reproducibility follows PG2; versioning does not repair an execution-tier defect (**S1.6**). The known foliage JIT replay failure is recorded in the implementation record.

### PG12 — Composable drawing grammar (P7)

User direction, 2026-10-28: generalize Procart using Structure Synth and Context
Free as references. Add `box`, `grid`, `sphere`, `line`, `point`, `triangle`,
`mesh`, `cylinder` and `tube` alongside `tree`, `flower`, `stone` and `grass`.
Common attributes modify an invocation and its descendants:

```mark
<procart version:2,seed:2026,
    settings:{max_depth:32,max_objects:10000,min_size:0.001},
    <resources>;
    <group scale:0.5,color:"#6da35a",hue:30,
        <box scale:[0.9,0.1],hue:10>;
        <sphere position:[1.0,0.0,0.0]>>
>
```

The detailed contract is [PG12.1–PG12.11](Lambda_Design_Procart.md#9-p7--composable-drawing-grammar): a versioned general root, centered primitives, inherited geometric/color adjustments, named and weighted recursive rules, cumulative repetition, deterministic keys, and explicit termination/work limits. `scale:[x,y]` expands to `[x,y,1]`; `hue` adds degrees to inherited color. Attribute order has no execution meaning; nesting determines operation order. The same mechanisms apply to semantic objects and generated organ palettes.

Rules and variants use `name`; a direct `<R1 ...>` calls `<rule name:"R1">`.
An optional call-site `id` identifies the instance, for example
`<R1 id:"left-branch">`. Recipe and geometry references retain their required
`id` fields. Names cannot shadow built-in tags. This user-requested schema
revision and migration are [PG12.11](Lambda_Design_Procart.md#911-definition-names-and-direct-invocation-tags-pg1211).

Root `settings` holds global `max_depth`, `min_size`, optional `max_size`,
object/vertex/step budgets and per-primitive `defaults` (PG12.7). Explicit
primitive fields override document defaults. Artistic depth/size limits retire
branches with reported statistics; work-budget overflow is an error (PG8).

The public additions are available under `lambda.procart.scene`, `.adjust`,
`.primitive` and `.rules`. Existing pure algorithms remain usable independently
(**S12.1.1v3 / PG4**). Meadow version 1 retains its current schema and behavior;
P7's general version 2 is a separate contract, with its standalone envelope in
`lambda.procart.schema`. The [Form studies page](../test/demo/procart/grammar.ls)
loads four separate Mark samples. Delivery and acceptance are in the [P7 record](impl/Lambda_Impl_Procart_Grammar.md).

### PG13 — Plant architecture and development (P8)

User direction, 2026-10-28: add plant features inspired by EZ-Tree and L-Py.
[PG13.1–PG13.4](Lambda_Design_Procart.md#10-p8--plant-architecture-and-developmental-models-pg13)
define a skeleton-first tree generator with per-level branch controls, taper,
curvature, directional growth and spiral leaf placement; contextual parametric
development; and a stack-based 3D turtle. Both generation paths share swept
branch buffers, leaf instances and explicit levels of detail. Geometry follows
PG2's tolerance tier; keys, topology and counts remain exact, and same-platform
replay must agree across execution tiers (**S1.6**).

The new package modules are `lambda.procart.plant`, `.apex`, `.turtle3d` and
`.plant_schema`; `lsystem.derive` extends the existing rewriting module.
`plant.registry()` adds `plant.tree@1` and `plant.apex@1` to P7 semantic scenes.
Standalone specimens store a semantic `tree` envelope, seed and pinned generator
parameters separately from code. The [Tree studio](../test/demo/procart/trees.ls)
loads four saved specimens and exposes growth, bending, leaf visibility and
detail selection. [P8 delivery](impl/Lambda_Impl_Procart_Plants.md) records checks.
Automatic distance switching, animated wind and the full L-Py
language remain later increments.

**PG13.5 — Reproductive organs (2026-10-28).**
`lambda.procart.plant_organs.decorate(model,key,settings)` adds stable terminal-
branch attachments, blossoms, bells, catkins, apples, berries, aggregate raspberries,
strawberries, scaled cones and nuts. `plant.sample` exposes the existing centerline
sampler for attachments. `leaves.leaflets:3` adds trifoliate leaves; the default
remains one leaflet. Organ keys do not depend on leaf count or mesh detail.
Eden owns species, maturity and seasonal visibility; the package emits canonical
organs and never advances biological time (**S12.1.1v3**, **S1.6**). Settings,
units and bounds are in [PG13.5](Lambda_Design_Procart.md#105-reproductive-organs-pg135).

### PG14 — Reusable surface patterns (P9)

The user requested reusable surface bindings and plant coordinates on 2026-10-28.
The detailed contract is [PG14.1–PG14.5](Lambda_Design_Procart.md#11-p9--reusable-surface-patterns-pg14).
Versioned `<pattern id,generator,version,seed?,parameters?>` resources are stored
with P7's scene definitions. Built-ins are noise, stripes, marble, bark and leaf
veins. `surface` provides a whole-object binding; `surfaces` overrides generated
roles such as `bark` and `leaf`. Groups and rule calls inherit bindings with
explicit null clearing, using name membership (**S8.1.1v2 / S8.1.2v3**).

Leaf UVs run across and along the blade; branch UVs retain original skeleton
distance and transported frames across LODs. Generic UV bindings reuse authored
coordinates; object-space fields bake into padded triangle atlases. Unique jobs
share PNG textures and respect `max_surface_pixels` and `max_vertices` before
raster allocation. The adapter emits existing Scene3D standard materials and
textures (**S12.1.1v3 / S12.1.5v2 / D7.5.3v2**); it adds no renderer vocabulary.
The fifth Form studies sample combines patterned stones, bark and leaves.
Color patterns and scalar roughness/metalness are delivered; normal maps,
displacement and attached geometry remain follow-ups. See the [P9 record](impl/Lambda_Impl_Procart_Surfaces.md).

### PG15 — Global environment and responsive plants (P10)

The user requested an enlarged meadow, pond, responsive plants and environment
controls on 2026-10-28. [PG15.1–PG15.4](Lambda_Design_Procart.md#12-p10--environment-and-a-living-meadow-pg15)
defines one optional global `<environment>`, bounded geographic/solar/weather
settings, pure solar and plant response samples, reusable `plant.herb@1` species,
and rooted wind clips. Recipe keys and placement stay independent of condition
changes (**S12.1.1v3 / PG2**). Presentation uses existing Scene3D lights, groups,
materials and animation tracks (**S12.1.5v2 / D7.5.3v2**). The saved 28 × 20
Meadow adds lilies, daisies, poppies and reeds, a carved pond, and condition
controls that preserve the camera. This is an illustrative living scene; climate
and ecological simulation remain outside this phase.

### PG16 — Procedural specimens and animal markings (P11)

User direction, 2026-10-28: reusable procedural models and customizable skin,
feather and wing patterns, with working Plant/Animal builders in Eden. Stand-alone
`procart.animal` supplies six authored morphology families and native hinge
presentation; `procart.butterfly_atlas` promotes the existing demo art. New
surface kinds share PG14 baking, and curved blades share plant/feather/fin geometry.
[PG16.1–PG16.5](Lambda_Design_Procart.md#13-p11--procedural-animal-specimens-and-markings-pg16)
define coordinates, units, admission, recipe replay and limits. Pure generation
follows **S12.1.1v3**; biology, terrain placement and builder UI remain Eden-owned.

### PG17 — Articulated character recipes (P12)

`lambda.procart.avatar` adds versioned standalone `<avatar>` recipes for plush,
robot, human, fairy and genie forms. Proportions, palette, outfit, ears,
accessories and seeded markings regenerate separately from application code.
Shared resources and nested joint groups lower idle/walk/run/play/wave/sit
poses to ordinary native Scene3D tracks. Eden owns named presets, studio saves
and terrain-aware cast placement. Generation and playback retain
**S12.1.1v3 / S12.1.5v2**; these are authored characters, without biological
population rules. See [PG17](Lambda_Design_Procart.md#14-p12--articulated-character-recipes-pg17).

## 4. Module map

| Module | Contents | Reuses | Tier | Native candidate |
|---|---|---|---|---|
| `procart.rng` | `key`, `derive`, `at`, `unit`, `below`, `between`, `chance`, `pick`, `weighted`, `shuffle`, `sample` (k of n), `stream`; `normal`, `exponential` (tolerance tier) | SplitMix64 finalizer (Appendix A) | Exact | No |
| `procart.noise` | white, value, Perlin (improved, 2002), simplex (Gustavson; 2D/3D), cellular/Worley (F1, F2, F2−F1; Euclidean, Manhattan, Chebyshev); fractals: `fbm`, `ridged`, `billow`; domain warp; periodic (tileable) variants; `field` builder | keyed permutation tables from `rng` | Exact (tabulated gradients) | **Yes**: `field` batch |
| `procart.field` | `make`, `transform`, `normalize`, `remap`, `clamp`, `curve`, `terrace`, `radial` / `island` masks, box blur, gradient/slope, `threshold` → mask, bilinear `sample`, distance transform, `contours` | `chart/mesh_contour.ls` for isolines | Exact | Blur / distance transform only if measured |
| `procart.sample` | Poisson disk (phased dart throwing; constant or density-dependent radius), jittered grid, best-candidate blue noise, Halton and R2 low-discrepancy sequences, density-weighted sampling over a field | `rng`, `field.sample` | Exact | Poisson for very dense domains, if measured |
| `procart.geom` | Delaunay triangulation, Voronoi cells (dual), Lloyd relaxation, convex hull, half-edge navigation | `math.orient2d`, `math.incircle`, `chart/geometry.ls` | Exact (predicates exact; Lloyd tolerance) | **Yes**: Delaunay above ~10⁴ points, if measured |
| `procart.grid` | cellular automata (B/S rules, cave smoothing), BSP room partition, drunkard's walk, rooms-and-corridors, flood fill / regions, distance maps; mazes: recursive backtracker, Kruskal, Prim, Wilson (uniform spanning tree) | `rng` | Exact | No |
| `procart.wfc` | simple-tiled and overlapping models, adjacency inference from a sample grid, min-entropy selection, propagation, bounded retry | `rng`, `grid` shapes | Exact | **Yes**: propagation on large grids, if measured |
| `procart.lsystem` | deterministic, stochastic (keyed) and parametric L-systems; 2D turtle, 3D turtle later; bracket stack; segment widths/depths | promoted from `doc/tikz/lsystem.ls` (PG9) | Exact (turn angles tabulated per system; tolerance when the angle is free) | No |
| `procart.text` | Tracery-compatible grammars (`#symbol#`, `.capitalize`, `.s`, `.a`, actions); order-n character Markov models for names | `rng` | Exact | No |
| `procart.terrain` | heightmap recipes (fBm + warp + island mask + terrace), thermal erosion, hydraulic droplet erosion, flow accumulation and rivers, moisture/temperature → biome (Whittaker-style table) | `noise`, `field` | Exact | **Yes**: hydraulic erosion |
| `procart.adapt` | field → `lambda.image` pixels (grayscale, colormap); field → chart `grid` data; heightfield → `scene3d` vertex/index arrays; polylines → `<svg>` | `chart` color scales where they fit | Exact | No |
| `procart.plant` | Admitted per-level tree parameters, stable 3D skeletons, tapered bark, leaf instances, explicit LODs, stored specimens and semantic registry | `rng`, `primitive`, `turtle3d`, `apex`, Scene3D | Tolerance geometry; exact topology | No |
| `procart.environment` | Bounded global settings, solar coordinates, weather lighting, plant responses and rooted wind tracks | `rng`, `lambda.color`, Scene3D | Tolerance solar/motion; exact admission | No |
| `procart.herb` | Sunflower, day-flowering lily, daisy, poppy, cosmos and reed; shared organs, UVs and environment-driven poses | `environment`, `rng`, Scene3D | Tolerance poses; exact seeded topology | No |
| `procart.avatar` | Versioned plush/robot/human/fairy/genie recipes, nested joints and six native pose clips | `rng`, Scene3D shapes/transforms | Tolerance geometry/poses; exact admission and identities | No |
| `procart.surface` | Pattern admission, seeded fields, whole/role bindings, UV transforms and object-atlas baking | `rng`, `noise`, `lambda.color`, `lambda.image`, Scene3D | Tolerance patterns; exact admission and sharing | No |
| `procart.turtle3d` / `procart.apex` | Spatial frame, push/pop, yaw/pitch/roll, tapered forward steps, leaves and tropism; parametric bud development | `lsystem.derive`, Scene3D vectors | Tolerance geometry; exact rewriting | No |

The facade `lambda.procart` re-exports the common entry points so that `import pg: lambda.procart` serves a quick script. Larger scripts import modules directly.

## 5. API sketches

These are illustrative. Exact spellings are settled in P0–P2.

```lambda no-run
import rng: lambda.procart.rng
import noise: lambda.procart.noise
import field: lambda.procart.field
import sample: lambda.procart.sample

// an island heightmap: fractal simplex noise, warped, shaped by a radial mask
let world = rng.key(2026)
let spec = {kind: 'simplex', octaves: 5, frequency: 1.0 / 160.0, lacunarity: 2.0, gain: 0.5,
    warp: {amplitude: 24.0, frequency: 1.0 / 300.0}}
let h = field.normalize(noise.field(noise.make(rng.derive(world, "height"), spec)^, 512, 512))
let land = h * 1.2 - field.radial(512, 512) * 0.7      // element-wise (S10.2.1)
let trees = sample.poisson(rng.derive(world, "trees"), {width: 512.0, height: 512.0, radius: 6.0})
    |: field.sample(land, ~[0], ~[1]) > 0.35
```

```lambda no-run
import grid: lambda.procart.grid
import geom: lambda.procart.geom

let cave = grid.automaton(rng.key(7), {width: 80, height: 50, fill: 0.45, rule: "B5678/S45678", steps: 5})
let maze = grid.maze(rng.key(7), {width: 30, height: 20, method: 'wilson'})   // uniform spanning tree
let mesh = geom.delaunay(sample.poisson(rng.key(7), {width: 800.0, height: 600.0, radius: 20.0}))
let cells = geom.voronoi(mesh, {bounds: [0.0, 0.0, 800.0, 600.0]})
```

```lambda no-run
import lsys: lambda.procart.lsystem
import text: lambda.procart.text

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
| `doc/tikz/lsystem.ls` | Expansion and turtle move to `procart.lsystem`; the tikz adapter imports them (PG9). |
| `chart/geometry.ls`, `chart/mesh_contour.ls` | Imported, not copied (PG9, PG-O6). |
| `math.orient2d` / `math.incircle` | The robustness basis of `geom.delaunay`. |
| Ad-hoc demo PRNGs | Optional migration (PG-O7). |

## 7. Verification and benchmarks

**Goldens.** Each module has unit scripts under `test/lambda/procart/*.ls`, each with its `.txt` golden (AGENTS rule 8). Large outputs are not printed whole. A golden prints a few sampled values plus a digest: `hash` over the formatted field, which also pins the exact tier (PG2).

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

**Showcase.** A `test/demo/procart/` demo builds a world map from one seed: terrain, biomes, rivers, a placed town (Voronoi districts), a dungeon and generated names. It exercises the whole package and gives the benchmarks a realistic workload.

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
| **P7** | [Composable drawing grammar (PG12)](Lambda_Design_Procart.md#9-p7--composable-drawing-grammar): nine drawing primitives, common adjustments, reusable weighted rules, bounded recursion/repetition, semantic-object adapters and native Scene3D output | [P7 acceptance](impl/Lambda_Impl_Procart_Grammar.md): adjustment algebra, all primitive geometries, stable seeded expansion, hard budget errors, retained rendering and Meadow v1 compatibility |
| **P8** | [Plant architecture and development (PG13)](Lambda_Design_Procart.md#10-p8--plant-architecture-and-developmental-models-pg13): per-level trees, tapered sweeps, leaves, shared-skeleton LODs, contextual parametric productions, 3D turtle and saved Tree studio samples | [P8 acceptance](impl/Lambda_Impl_Procart_Plants.md): topology/attachment oracles, turtle coordinates/state, deterministic replay, detail identity, invalid input and native controls |
| **P9** | [Reusable surfaces (PG14)](Lambda_Design_Procart.md#11-p9--reusable-surface-patterns-pg14): versioned pattern resources, inherited role bindings, organ coordinates, texture sharing and object-space atlases | [P9 acceptance](impl/Lambda_Impl_Procart_Surfaces.md): seeded fields, null overrides, LOD coordinates, atlas corner oracles, UV transforms, budgets, native pixels and seed replay |
| **P10** | [Environment and living meadow (PG15)](Lambda_Design_Procart.md#12-p10--environment-and-a-living-meadow-pg15): solar/weather settings, responsive herbs, rooted wind, pond and controls | [P10 acceptance](impl/Lambda_Impl_Procart_Environment.md): cross-tier coordinates/poses, condition replay, native weather/motion/control pixels and retained camera |
| Later | 4D noise, the 3D turtle (PG-O8) | Scheduled when a consumer needs them |

The implementation record goes in `vibe/impl/Lambda_Impl_Proc_Gen.md` when P0 starts. A user guide section goes in `doc/Lambda_Packages.md` once P0 ships.

## 9. Open items

| ID | Question | Recommendation |
|---|---|---|
| ~~**PG-O1**~~ | ~~Package name (PG1).~~ | **Resolved** (USER, 2026-10-28): `lambda.procart`; supersedes the original 2026-10-26 choice. See PG1. |
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
part(string)         = FNV-1a 64 over the code points (the UTF-8 bytes, for ASCII)
part(other)          = part(its JSON text), so key and derive never fail
unit(k)              = float(mix64(k) >>> 11) * 2^-53    // [0, 1), same mapping as math_splitmix64
below(k, n)          = floor(unit(k) * n)                   // bias at most n / 2^53
```

All of this uses only `u64` wrapping arithmetic (S4.1.5), `bxor` and `ushr`, all available in Lambda today. It is exact-tier. The position-dependent multiplier in `derive` keeps `derive(k, a, b)` and `derive(k, b, a)` distinct. Statistical quality is validated in P0 by PractRand or TestU01 SmallCrush runs over `unit` streams of several derivation shapes. The results are recorded in the impl doc, not here.

## Appendix B. References

Algorithms are reimplemented from the published descriptions; no code is copied. Licenses are listed for libraries consulted as behavioral references.

| Family | Reference | License of the reference code |
|---|---|---|
| Perlin noise | Ken Perlin, *Improving Noise* (SIGGRAPH 2002) | — |
| Simplex noise | Stefan Gustavson, *Simplex noise demystified* (2005); Perlin's simplex patent expired 2022 | public domain reference code |
| Simplex alternative (later kind) | OpenSimplex2 (K.jpg) | public domain (CC0 / Unlicense) |
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

## Appendix C. Prior art — procedural scenes and art

Primary references reviewed 2026-10-28. The final column records design
inferences for Procart; it does not claim equivalent APIs or interchange formats.
The algorithm references in Appendix B cover individual kernels, while these
projects inform generators, authoring and scene composition.

| Project | Established approach | Lesson for Procart (design inference) |
|---|---|---|
| [Infinigen](https://infinigen.cs.princeton.edu/) | Blender/Python generation of natural environments, including terrain, plants and materials, from randomized mathematical rules with configurable parameters. | Closest overall landscape reference: compose asset generators and environment placement into a complete scene. |
| [ProcFunc](https://github.com/princeton-vl/procfunc) | Function-oriented procedural 3D primitives, tracing and translation of Blender geometry-node graphs into Python; the Infinigen 2 generator library builds on it. | Compose small generator functions and expose their parameters consistently. Its API remains evolving; [Infinigen 2 is a preview with limited asset coverage](https://infinigen.cs.princeton.edu/docs/latest/), so the Nature generator and newer function library are distinct references. |
| [EZ-Tree](https://github.com/dgreenheck/ez-tree) | A JavaScript/Three.js tree library with a seed, per-level branch controls, leaf controls, presets and geometry detail variants sharing a skeleton. | P8 / PG13 adopts per-level architecture, taper, directional growth, saved presets and shared-skeleton detail selection. Lambda uses its own sweeps and folded leaf geometry; texture assets and automatic LOD switching are deferred. |
| [L-Py](https://github.com/openalea/lpy) / [PlantGL](https://github.com/openalea/plantgl) | L-system plant modeling with [parametric/contextual productions and separate interpretation](https://lpy.readthedocs.io/en/latest/user/lsystems.html), plus a [3D turtle](https://lpy.readthedocs.io/en/latest/user/turtleBasic.html). | P8 / PG13 adds simultaneous contextual callbacks, numeric apex development, yaw/pitch/roll, complete branch state, taper and leaves. Immediate sequence context is supported; L-Py's full axial matching, Python syntax and simulation environment are deferred. |
| [Blender Geometry Nodes](https://docs.blender.org/manual/en/5.3/modeling/geometry_nodes/point/distribute_points_on_faces.html) | Seeded surface point distributions, density control and stable point IDs used by random-value and instancing nodes. | Add mask/density-based patches and maintain per-instance random identity as density or placement changes. |
| [Houdini heightfields](https://www.sidefx.com/docs/houdini/heightfields/) and [scattering](https://www.sidefx.com/docs/houdini/heightfields/scattersop.html) | Terrain layers and masks feed scattering of plants, rocks, meadows and ecosystems; [HeightField Scatter](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_scatter-.html) exposes density, spacing and a seed. | Let terrain fields govern habitat and exclusion rather than relying only on rectangular scatter groups. |
| [Structure Synth / EisenScript](https://structuresynth.sourceforge.net/reference.php) | Text rules compose 3D primitives through repetition, nested transforms, weighted alternatives and explicit random seeds; rule-call branches maintain random sequences. | Closest small declarative 3D composition precedent: named reusable shapes, hierarchical placement and reproducible variation. |
| [Context Free / CFDG](https://github.com/MtnViewJohn/context-free) | Compact text grammars generate 2D images through recursively composed shapes; [its language summary](https://raw.githubusercontent.com/wiki/MtnViewJohn/context-free/images/CF3_nutshell_US.pdf) covers weighted alternatives and inherited shape adjustments. | Treat the editable artwork description as a durable artifact; use composition and inheritance to express families of variations. |

Procart's chosen form combines registered generator recipes with semantic Mark
objects, explicit seed/key ownership and ordinary Scene3D output (PG11).
Generation follows **S12.1.1v3**, while camera and viewer state stay with the
presentation (**S12.1.5v2**). Density masks, richer placement operators and
distance-based detail are follow-up design directions, not promises of the
current Meadow profile.

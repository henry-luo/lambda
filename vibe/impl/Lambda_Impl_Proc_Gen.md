# Lambda Procedural Generation — Implementation Record

> **Design:** [Lambda_Pkg_Proc_Gen.md](../Lambda_Pkg_Proc_Gen.md) (PG1–PG10, all open items resolved 2026-10-26).
> **Status:** P0–P5 core implementations and P6's native WFC solver are present (2026-10-26), uncommitted in the main checkout. Release evidence qualifies Delaunay and hydraulic erosion for later P6 work (§5); remaining package features are listed in §6. `temp/procgen_release/` holds saved release executables and benchmark artifacts, not a Git worktree.

## 1. Delivered

| File | Contents |
|---|---|
| `lmd/package/procgen.ls` | Facade: `key`, `derive`, `unit`, `noise_field` |
| `lmd/package/procgen/rng.ls` | Keyed SplitMix64 randomness: `key`, `derive`, `at`, `unit`, `stream`, `units`, `below`, `between`, `chance`, `pick`, `weighted`, `shuffle`, `sample`, `perm`; tolerance tier `normal`, `exponential` |
| `lmd/package/procgen/noise.ls` | `table`; point kernels `value2/3`, `perlin2/3`, `simplex2/3`, `cellular2/3`, `white2/3`; generators `make`, `sample`, `sample3`, `field` with fBm/ridged/billow and domain warp |
| `lmd/package/procgen/field.ls` | `make`, `transform`, `width`, `height`, `bounds`, `normalize`, `remap`, `clamp`, `terrace`, `threshold`, `radial`, `sample`, `blur` |
| `lmd/package/procgen/adapt.ls` | `gray`/`terrain` colormaps, `pixels`, `image` (`lambda.image` raster), `chart_grid` |
| `lmd/package/procgen/sample.ls` (P1) | `poisson`, `jitter`, `radical_inverse`, `halton`, `r2`, `best_candidate`, `weighted` |
| `lmd/package/procgen/grid.ls` (P2) | `random_fill`, `evolve`, `automaton`, `regions`, `keep_largest`, `distances`, `walk`, `bsp`, `dungeon`, `maze_edges`, `maze` |
| `lmd/package/procgen/lsystem.ls` (P3) | `expand` (string, stochastic and parametric rules), `turtle`, `polylines` |
| `lmd/package/procgen/text.ls` (P3) | Tracery-style `expand`; `markov`, `name` |
| `lmd/package/doc/tikz/lsystem.ls` (P3) | Now only the TikZ adapter: option and rule-set parsing over `procgen.lsystem` |
| `lmd/package/procgen/wfc.ls` (P4) | `tiled`, `tiled_from_sample`, `overlapping`, `run`, `render`, `generate` |
| `lmd/package/procgen/terrain.ls` (P5) | `heightmap`, `thermal`, `hydraulic`, `fill`, `accumulation`, `rivers`, `moisture`, `biomes`, `biome_colors`, `world` |
| `test/demo/procgen/` (P5) | The showcase (`world.ls`, `mod_world.ls`, `mod_map.ls`, `world.css`), its model golden and a headless button replay |
| `lmd/package/procgen/geom.ls` (P1) | `delaunay`, `next_edge`, `prev_edge`, `neighbors`, `hull`, `voronoi`, `centroid`, `lloyd` |
| `test/lambda/procgen/{rng,noise,field,sample,geom,grid,lsystem,text,wfc,terrain}.ls` + `.txt` | Goldens (functional, auto-discovered; directory registered in `test/test_lambda_gtest.cpp`) |
| `test/lambda/procgen/oracle.py` | Independent Python port of the exact-tier RNG, kernels, samplers, grid generators, stochastic rewriting, Markov names, WFC and terrain; checks the goldens' exact values |
| `test/lambda/procgen/_bench_{noise,voronoi,wfc,erosion}.ls`, `bench.py` | Benchmark harness (§7 budgets) |
| `lib/procgen/wfc.{c,h}` (P6) | Fresh, context-free counting solver; exposed as boxed `math.wfc` in `lambda/runtime/lambda-eval-science.cpp` (D7.1.1, D6.4.1–D6.4.2) |
| `test/lambda/procgen/wfc_native.ls` + `.txt` | Native/reference parity for weighted, overlapping, periodic, contradictory and keyed random models, plus boundary errors and automatic fallback |

User guide: [Lambda_Packages.md §26](../../doc/Lambda_Packages.md#26-procgen--procedural-generation).

## 2. Choices made during P0

These refine the proposal's illustrative sketches; the proposal body now states them.

1. **Prepared generators.** `noise.make(key, spec)` builds a generator holding one permutation table per octave, and `noise.field(gen, width, height)` evaluates it. Point kernels take a table from `noise.table(key)`, not a key. Rebuilding a 256-entry table per sample would dominate the cost.
2. **Lattice hashing by permutation table.** Kernels hash lattice coordinates through Perlin's doubled 512-entry table using `band(i, 255)` and int lookups only, rather than mixing a `u64` key per lattice point. It is exact-tier and cheap on both tiers. The pattern repeats every 256 lattice units, as in the reference algorithms.
3. **Classic simplex, not OpenSimplex2.** The kernels follow Gustavson's *Simplex noise demystified* (2D and 3D). The simplex patent expired in January 2022, the algorithm is shorter, and its reference is public domain. OpenSimplex2 remains a possible later kind.
4. **`rng.below` is `floor(unit * n)`.** Its bias is at most n/2^53, so the proposal's Lemire rejection loop buys nothing measurable.
5. **FNV-1a over code points**, not UTF-8 bytes. They are identical for ASCII names and avoid an encode step.
6. **Key parts never fail.** Ints keep their bits; strings hash; any other part hashes its JSON text. `key`/`derive` are therefore infallible and need no `^` at every call site.
7. **Names.** `range` is a reserved word, so the float draw is `rng.between`. `rng.at(k, i)` is the infallible int case of `derive`, used internally for indexed draws.
8. **Default frequency 1/32.** Gradient noise is zero at integer lattice points, so a frequency of 1.0 over an integer grid gave an all-zero field.
9. **Runtime reuse (AGENTS rule 13).** `field.blur` calls the runtime's native running-sum `blur` (kernel 2r+1, edge replication); `field.threshold` calls the runtime `threshold`; the runtime `transpose` replaced a package copy.

## 2a. Choices made during P1

1. **Poisson disk as phased dart throwing.** Bridson's algorithm mutates a grid and an active list, so the pure formulation (PG6) follows Wei's parallel dart throwing (2008). Cells of side r/√2 hold at most one point, and cells whose indices agree mod 3 can never conflict. So each of the 9 phases fills its cells from the previous grid in one comprehension. Each round gives every empty cell one keyed candidate; `attempts` rounds (default 20) approach a maximal sampling: in the golden, over 99% of probe points lie within r of a sample. Points come out in row-major cell order.
2. **Delaunay as Bowyer–Watson over ghost triangles.** A ghost triangle `[u, v, -1]` stands for the outside of a hull edge (Shewchuk), so no super-triangle is needed and hull triangles are never lost. Conflicts use the exact `math.orient2d`/`math.incircle`; the strict incircle test gives cocircular sets a valid triangulation. Repeated points keep their first index; a collinear set has no triangles and a two-point hull. Each insertion scans and rebuilds the immutable triangle list, so the algorithm is O(n²); it is capped at 20,000 points.
3. **Two hulls.** `delaunay(...).hull` keeps collinear boundary points, since they are triangulation vertices; `geom.hull` (Andrew's monotone chain) drops them.
4. **Voronoi by half-plane clipping.** Each cell is the bounds rectangle clipped (Sutherland–Hodgman) by the bisector with each Delaunay neighbour. It handles hull cells and degenerate meshes the same way, needs no circumcentre ordering, and uses basic arithmetic only. The cells tile the bounds (areas sum to the box within 1e-6 in the golden).
5. **Lloyd stays in the tolerance tier** as ruled (PG2), although the shoelace centroid uses basic arithmetic only.
6. `best_candidate` is O(n²·m) and capped at 4,000 points; `weighted` inverts the cumulative sum by binary search.

## 2b. Choices made during P2

1. **Flat grids inside, rows outside.** Tile grids are `int[][]` rows (0 floor, 1 wall, as PG5's tile-grid shape); every generator works on a flat row-major array and converts at the end.
2. **Cellular automata** are life-like rules written `"B<digits>/S<digits>"` (default `"B5678/S45678"`, the cave-smoothing rule). Cells beyond the edge count as `edge` (default 1, walls), so caves close at the border. `automaton` is `random_fill` then `evolve`.
3. **Regions by min-label propagation with pointer jumping.** Union-find mutates, so each round takes the minimum label over a cell and its floor neighbours, then jumps each label through itself. Labels only ever name cells of the same component, so rounds converge to the component minimum, in about O(log diameter) rounds on corridors. Region ids follow the row-major order of first appearance.
4. **Distance maps** are breadth-first layers, one full-grid comprehension per layer, run in a bounded `reduce` rather than deep recursion.
5. **Reflected walks from one `cumsum`.** A drunkard's walk clamped at the border would need a sequential fold. Instead an unbounded ±1 walk is folded into the interior with period 2n, which reflects it: an outward step becomes a pause. Positions then come from `math.cumsum`, and the carve keeps the first `ceil(fill × interior)` distinct cells in order of first visit, so the floor is connected and exactly sized when the step budget allows.
6. **One rooms-and-corridors generator, on BSP.** `bsp` returns the partition's leaves; `dungeon` puts one keyed room in each leaf (margin 1) and, at every split, joins the last room of the first half to the first room of the second with an L-shaped corridor. A tree of corridors over room centres connects every room by construction.
7. **Mazes** are spanning trees of a cell grid, rendered at (2w + 1) × (2h + 1) tiles. They are capped at 10,000 cells because every method updates an O(cells) array per step:
   - *backtracker*: depth-first search over parent pointers, so backtracking follows the parent chain and no stack is kept;
   - *kruskal*: keyed-shuffled edges, relabelling the absorbed component on each join;
   - *prim*: the randomized frontier variant (one status array: outside, frontier, tree), since a true Prim over keyed weights would draw the same tree as Kruskal;
   - *wilson*: loop-erased walks generated in doubling chunks by the reflected `cumsum`. A reflected walk pauses at the border; the pause only adds self-loops, which loop erasure removes. Every tree therefore keeps weight (1/4)^(n−1), and the draw stays uniform. The golden confirms this statistically: 3,000 draws on a 3 × 2 grid reach all 15 spanning trees, with χ² below the 0.1% critical value (36.1, 14 d.f.).

## 2c. Choices made during P3

1. **Promotion, not a copy (PG9).** Rewriting and the turtle moved out of `doc/tikz/lsystem.ls`. The TikZ module keeps what is TikZ's own: option and `rule set` parsing, "a later rule for the same head wins", and the single-path restriction, so `f`, `[` and `]` are still rejected with the same messages. The dragon-curve sample (order 11) converts to byte-identical HTML and SVG. The conversion went from 0.41 s to about 0.5 s (debug), spent on bracket parsing that TikZ's bracket-free systems do not need.
2. **Two sequence forms.** A system is a string of one-character symbols, or an array of modules (`"F"` or `["F", 2.0]`). A rule is a replacement; an array of `[weight, body]` pairs for a stochastic rule, chosen by `rng.at(rng.at(key, generation), position)`; or a function of the module for a parametric rule, whose conditions are ordinary Lambda. Expansion is capped at 100,000 symbols (PG8).
3. **The turtle** parses brackets into nested blocks: pairs are found by grouping bracket positions by depth, where opens and closes alternate. It walks each run by halving, as the TikZ turtle did, so stack depth stays logarithmic. It uses the TikZ turtle's arithmetic (degrees, `heading × π / 180`), which is why the TikZ output is unchanged. It returns segments with their bracket depth; `polylines` joins them on request. Turning by a free angle is tolerance-tier (PG2), so the goldens print turtle coordinates rounded to 1e-9.
4. **Tracery semantics, strict on errors.** Tags take modifiers; actions push the options expanded at action time (as Tracery does) or `POP`; a tag's leading actions hold only while the tag expands, while top-level actions persist; a backslash escapes. The modifiers reproduce Tracery's base English set exactly, quirks included ("day" gives "dayd", "hour" gives "a hour"), so existing grammars read the same. Unlike Tracery's `((symbol))` output, an unknown symbol, unknown modifier, unclosed tag or recursion deeper than 64 raises (PG8). `grammar` is a Lambda keyword, so the module never binds that name.
5. **Markov names.** An order-n character model with `chr(2)`/`chr(3)` start and end marks. A name is drawn with keyed retries until it fits `min`..`max` and, by default, is not a training sample.

## 2d. Choices made during P4

1. **One model shape.** Every model is `{count, weights, values, masks, ...}`, where `masks[d][p][q]` is 1 when pattern q may sit at p's neighbour in direction d (0 right, 1 down, 2 left, 3 up). The three builders are:
   - `tiled`: explicit `[a, b, "x" | "y"]` rules, mirrored into the opposite direction;
   - `tiled_from_sample`: tiles, weights and adjacency observed in a grid;
   - `overlapping`: Gumin's n × n patterns, with his symmetry-variant order and counts as weights. `agrees` tests overlap consistency.
   `render` maps indices back to tiles, or to each pattern's top-left value.
2. **Smallest domain, not Shannon entropy.** Gumin picks the cell of least weighted entropy, which needs `log`, so it would sit in the tolerance tier (PG2). The solver picks the smallest domain instead, with ties and the weighted collapse keyed by step, which keeps the solver exact-tier. Both heuristics share the flood behaviour of a lopsided tile set: once land is placed, the land/coast frontier is always the smallest, so coast maps come out mostly land or mostly sea. The golden's adjacency check therefore uses a map that holds all three tiles.
3. **Counting propagation, swept.** Each cell keeps, per direction, a support-count vector over the patterns its domain allows next to it. A revision is then one element-wise product of the neighbours' vectors (positive exactly where every direction supports a pattern) followed by a filter. A shrinking domain subtracts only its removed patterns' rows, or recounts from the kept side when that is smaller. Sweeps revise the neighbours of changed cells until nothing shrinks, and sorted updates are spliced in with native slices. Arc consistency has a unique fixpoint, so `oracle.py` uses plain AC-3 and still reproduces every exact grid. A contradiction restarts with the next keyed attempt, up to `retries` (10), then raises (PG8). Grids are capped at 10,000 cells and models at 1,000 patterns.

## 2e. Choices made during P5

1. **Islands blend; they do not subtract.** Subtracting a falloff and renormalizing lets the corners set the new minimum, which lifts the coast back above sea level. `heightmap` instead blends the noise toward a dome `1 − r²` by `island` (0 to 1) and clamps (Red Blob Games). Here `r` is measured per axis, so the island fits a rectangle.
2. **Thermal erosion is a pure flux step.** Each cell moves `rate` × its largest excess slope to its lower 4-neighbours, in proportion to their excess (Olsen 2004). Mass is conserved exactly: the golden checks a 20-iteration run to 1e-9.
3. **Hydraulic erosion runs in batches, with a stabilizer.** Beyer's droplets mutate the map at every step, so droplets run in batches against one snapshot and their changes are summed per cell. Overlapping droplets then dig the same cell several times. The first runs diverged to ±6×10¹⁰ on a 120 × 80 island. A batch therefore may not create new extremes: each touched cell stays within the range of its old 4-neighbourhood, which bounds every height by the input's range (the golden checks this). Two further departures from Beyer: speed grows downhill (`v² − Δh·g`, where his formula slows it), and erosion and deposition use the four cell corners rather than a radius brush. Sediment still carried when a droplet leaves the map or expires is dropped, as in Beyer.
4. **Depressions are filled before flow.** Planchon–Darboux runs in synchronous sweeps from the outlets (the map edge and the sea), raising pits by `epsilon` per step so every land cell has a strictly lower neighbour. Flow is D8 steepest descent. Accumulation is one fold from the highest cell down, with each update spliced by native slices. Rivers are the segments from cells draining at least `threshold` cells (default 0.5% of the map).
5. **Moisture and biomes.** Moisture is half keyed noise and half nearness to water (sea or river cells), using `grid.distances`. That call passes thousands of sources, so `grid.distances` now looks sources up in a map rather than with a linear `in`; the grid golden is unchanged. Biomes follow mapgen2's elevation × moisture table and colours.
6. **The showcase stays out of the demo catalog for now.** Registering a named demo means editing `test/demo/index.ls`, `mod_demo_art.ls`, `README.md` and `lambda/main.cpp`, all of which carry unrelated uncommitted work. The demo runs with `lambda view test/demo/procgen/world.ls`.
7. **Element content takes a single spread.** In element content, nothing may follow a `*[...]` spread. An expression item followed by an element, or an unparenthesized `if` as an attribute value, also misparses. The demo groups mixed content into one `*[...]` and parenthesizes conditional attributes. This is noted as an observation, not changed here.

## 2f. Native WFC (P6)

1. **Measured eligibility (PG7.1).** The original release binary takes 34.173 s on AUTO and 28.111 s on forced JIT for the 64²/104-pattern benchmark, respectively 6.83× and 5.62× its 5 s budget. The support-count Lambda solver is already the vectorized reference. The forced-JIT result is saved as `temp/procgen_release/wfc_jit_before.txt`.
2. **One batch boundary (D7.1.1, D6.4.1–D6.4.2).** `math.wfc(key, weights, masks, [width, height, periodic, retries])` returns a flat owned int array or an error. Model building, package options and reshaping stay in Lambda. `lib/procgen/wfc.c` sees no Items or runtime state; it allocates four bounded flat buffers once per call and frees them on every exit. GC roots in the wrapper retain its sources and current rows across N-D row-view creation (D5.3.3).
3. **Removal events replace immutable sweeps.** Each pattern removal is queued once per cell; removing its outgoing mask row decrements support counts and queues any newly unsupported neighbour pattern. Counts are at most 1,000, so `uint16_t` is sufficient. Initial propagation includes missing supports and periodic self-neighbours. At 10,000 cells and 1,000 patterns, scratch is about 130 MB, excluding the boundary's mask/output buffers.
4. **Exact choices (PG2, PG7.3).** Both propagators converge to the same domains. Observation scans cells in row-major order, patterns in ascending order, and uses the reference's retry/step/draw key derivations. Weighted prefixes are sequential binary64 sums. The SplitMix64 finalizer is extracted from `lib/math_utils.h`, preserving `math.random`'s mapping. Floating-point contraction is disabled by the library file rule and a C `FP_CONTRACT OFF` pragma covering direct-source builds.
5. **Oracle and fallback remain.** `wfc.run_reference` uses the original solver with the same validation. `wfc.run` selects the native path only when registered and when weights are positive `int`/`float` values whose prefixes fit exactly representable integer range; other numeric domains, including zero weights, keep the reference. Running the existing WFC golden with the saved pre-kernel executable confirms fallback. Native failures propagate as errors and are never converted to fallback success (PG8).

## 3. Runtime fix found by P0: `u64(any)` under the JIT

`rng.key` converts an untyped part with `u64(p)`. Under `LAMBDA_EXEC_BACKEND=jit`, `fn f(p) => u64(p); f(42)` segfaulted: the script's root Item was the raw payload `0x2a`.

**Root cause.** In `emit_call_value` (`lambda/runtime/transpile-mir.cpp`), the `u64` type-call always unboxed `coerce_uint64`'s Item into the raw U64 lane. `mir_call_value_from_result` then named the register by the call's carrier type. With an `any` operand that carrier is an Item, so the raw payload was published as an Item, and its error check also read the raw bits. A failed conversion's error would likewise have been flattened.

**Fix.** Unbox only when the call's carrier type is `u64`; otherwise return the boxed `coerce_uint64` result as an Item. AUTO hid the defect because a cold function runs on T0 first.

**Regression.** `test/lambda/u64_conversion_untyped.ls` + `.txt`, run on interp, auto and jit by `NumericConversionTests.UntypedU64ConversionAgreesAcrossTiers`.

## 3a. Runtime fix found by P1: unfiltered exact predicates

`math.orient2d`, `math.orient3d` and `math.incircle` ran every call through `decimal_geometry_sign` (`lambda/core/lambda-decimal.cpp`). That path uses mpdecimal at about 2,500 digits with 26 heap numbers and a `pow` per coordinate: 36 µs per incircle call on the debug build. A 1,686-point triangulation took 130 s.

**Fix.** `science_predicate_filter` in `lambda/runtime/lambda-eval-science.cpp` is Shewchuk's stage-A filter: the binary64 determinant decides the sign whenever |det| > errboundA × permanent, using his constants ((3 + 16ε)ε, (7 + 56ε)ε, (10 + 96ε)ε, ε = 2^-53). Only undecided cases reach the exact path. The bounds assume no underflow or overflow, so the filter declines unless every nonzero coordinate difference lies in [1e-60, 1e60]. Each product is a separate statement, so FMA contraction cannot change the rounding the bounds assume. Results are unchanged by construction.

**Check.** 3,000 cases with signs computed in exact rationals (Python `Fraction`) gave zero mismatches on auto and jit. The cases were random, near-collinear, near-cocircular (perturbations of 1e-17), exactly cocircular integer rings, 2^-50 grids, and magnitudes of 1e-30 and 1e70; 816 of them had exactly zero incircles. Cost fell to about 0.85 µs per call, and the 1,686-point triangulation to 1.75 s (debug).

## 3b. Builtin capability references

P6's availability check exposed that `math.wfc` resolved as a call but not as a standalone builtin reference. `build_ast.cpp` now reuses the unambiguous builtin lookup and callable-contract builder for qualified members, canonical `lambda.*` paths and import aliases. Local bindings still shadow short module names (S12.3.7). This uses the existing identity/name representation for native builtin references. Those references still fail closed if invoked through Core's dynamic-call entry without a typed adapter (D6.2.1–D6.2.2v2); package dispatch calls the selected system function directly.

## 4. Verification

- **Exactness.** `oracle.py` recomputes, in Python binary64, the keys, draws, permutation, all eight kernels at four points, cellular metrics, the five generator kinds, the cell variants and a warped 3D sample. Every value equals the golden bit for bit. Independently, `rng.stream(u64(s))[0] == math.random(s)[0]`.
- **Tier parity.** `ProcgenTests.KeyedNoiseIsExactAcrossTiers` runs the RNG, noise, sampling, geometry, grid, L-system, text, WFC, terrain and native WFC parity fixtures on interp, auto and jit against their goldens. `field.ls` retains the T0 limitation below.
- **Samplers.** `oracle.py` also ports the Poisson, jitter, Halton, R2, best-candidate and weighted samplers; the golden's exact samples match bit for bit.
- **Grids (P2).** `oracle.py` ports `random_fill`, `evolve` and `walk`; the golden's exact grids match. Porting `walk` found a defect before it shipped: the positions' `cumsum` was not offset by the start, so the walk jumped from the centre to the corner after its first step. Invariants cover the rest:
  - a glider translates one cell diagonally in four generations, and a blinker flips;
  - region labels, `keep_largest`, and multi-source distances match hand-worked grids;
  - walks carve exactly the requested count as one region inside a wall border;
  - BSP leaves tile the rectangle exactly and respect `min_size`;
  - dungeons have one room per leaf, n − 1 corridors and one floor region;
  - every maze method yields cells − 1 adjacent edges, one region, and distances that change by exactly one between neighbouring floor tiles (a tree).
- **L-systems and text (P3).** `oracle.py` ports stochastic rewriting and Markov name generation, and both match. The goldens also check:
  - algae lengths follow Fibonacci; dragon and Koch expansions match the known strings;
  - stochastic frequencies follow the weights;
  - parametric rules build the expected module tree;
  - turtle squares close, bracket depths and polylines are right, and `f` and `|` behave;
  - Tracery modifiers, articles, push/pop, scoped tag actions and escapes give the expected text;
  - keyed choices are uniform;
  - the Markov table for `["ab", "ac"]` is exact, and names respect length and novelty.
- **WFC (P4).** The oracle's AC-3 port reproduces the checkerboard, coast, periodic-stripe and index grids. The golden also checks:
  - every adjacent pair satisfies the model, for explicit, sample-learned and overlapping models;
  - on a mixed coast map, sea never touches land;
  - every 3 × 3 window of a periodic overlapping output is one of the sample's patterns;
  - weights set tile frequencies, and keys decide outputs;
  - each rejected input raises its own error.
- **Native WFC (P6).** `wfc_native.ls` compares both solvers on more than 300 seeded cases: int/fractional/tiny weights, overlapping models at all symmetry settings, keyed random constraints, retries, periodic contradictions and 1-cell dimensions. It checks native boundary validation, null propagation, canonical/alias capability references, local shadowing and the reference-only weight domains. The existing WFC golden and this fixture pass on interp, auto and jit. A small native/reference probe also agrees with every-allocation GC and freed-memory poisoning on interp and jit.
- **Terrain (P5).** `oracle.py` ports `heightmap`, `thermal` and `fill`, and all match. The golden checks:
  - a hand-worked pit fills to 2.75/3.0 toward its lowest outlet, a ramp accumulates [1, 2, 3, 4], and a bowl drains 9 cells to its centre;
  - thermal erosion conserves mass and reduces the steepest slope;
  - hydraulic erosion stays within the input's range and is keyed;
  - the filled surface drains everywhere while the raw one does not, and rivers flow strictly downhill from land;
  - moisture stays in [0, 1], and 8 elevation/moisture probes land on the right biomes.
- **Showcase (P5).** `tests/procgen_world.ls` pins seed 2026's realm, lore, towns (all on land) and biome counts. It also checks that switching erosion off changes the land but not the lore or the ruin. `tests/world_events.json` replays New world, the rivers toggle and Previous headlessly (5/5 assertions). The page was also inspected as a rendered PNG.
- **Delaunay (P1 acceptance).** On a 7×7 grid (fully cocircular), the grid plus repeated points, an exactly cocircular integer ring with its centre, collinear runs and a Poisson set, every triangle is counterclockwise and every circumcircle empty (exact predicates). Half-edges are symmetric, and t = 2n − 2 − h holds over the used points. Meshes with 0, 1, 2 or only collinear points give no triangles, a correct hull and line neighbours. `field.ls` runs on the default tier only: strict `interp` cannot load `lambda.image` (E501, `AST_NODE_INDEX_EXPR` in `lmd/package/image.ls`), a pre-existing T0 gap outside procgen.
- **Invariants** in the goldens: draw ranges and moments, uniform die counts, shuffles are permutations, zero weights never chosen, kernel ranges within [-1, 1] with near-zero means over 2000 points, cellular `f1 ≤ f2`, determinism and key sensitivity of generators, every fractal in range, rejected specs.
- **Aggregate qualification (P6, 2026-10-26).** All 8,292 Lambda/input baseline cases and 1,723 library cases were verified. The sandbox runs reported 8,288/8,292 and 1,717/1,723: four HTTP cases could not bind loopback listeners, and six library cases needed local fake servers or macOS trust roots. The HTTP filter passed 4/4 with the required access; the library retry filter passed 7/7 (including one already passing tunnel case). Original logs retain those sandbox failures in `temp/procgen_lambda_baseline.log` and `temp/procgen_library_tests.log`; retries are `temp/procgen_serve_recheck.log` and `temp/procgen_library_recheck.log`. The rebuilt math-utils tests passed 13/13 and the Premake generator checks passed 5/5. `git diff --check` is clean.

## 5. Performance

**Release measurements (2026-10-26).** `make build-release-compile` built a `release_native` binary (-O3, LTO), copied to `temp/procgen_release/`. Runs used `bench.py`, the auto tier, and best of 3 for noise and 1 run otherwise. The machine was shared (load average about 6.5 on 10 cores).

| Benchmark | Release | Budget | Ratio | PG7.1 (≥ 5×) |
|---|---|---|---|---|
| noise: 512² simplex, 5 octaves | 2.30 s (JIT 1.95 s) | 1 s | 2.3× | no: stays Lambda |
| voronoi: 19,158 Poisson points, Delaunay + Voronoi | 169.8 s | 2 s | 84.9× | **yes** |
| wfc: 64² overlapping, 104 patterns | 34.2 s | 5 s | 6.8× | **yes** |
| erosion: 256², 50k droplets | 44.9 s | 5 s | 9.0× | **yes** |

Release was only about 20% faster than debug on the noise benchmark (2.3 s against 2.9 s): the work is JIT-compiled Lambda and runtime calls, not optimized C. The P6 candidates by PG7.1 are therefore Delaunay (quadratic, 85×), hydraulic erosion and WFC propagation. PG7.2–7.5 (kernel shape, bit-exact parity, layering through `math.*` batch functions, fresh C) still apply to each.

**P6 WFC result (release, best of three, 2026-10-26).** The same 64²/104-pattern benchmark now takes **0.282 s on AUTO** and **0.460 s on JIT**, both below the 5 s budget, with unchanged checksum `[104, 64, 64]`. The measured reference was 34.173 s on AUTO (original record) and 28.111 s on JIT (rerun before porting). The saved new executable is `temp/procgen_release/lambda_wfc.exe`; results are `wfc_native_auto.txt` and `wfc_native_jit.txt` beside it. Delaunay and hydraulic erosion remain candidates, not delivered native kernels.

**Earlier indicative debug numbers:**

The terrain `world` recipe on the debug build took 1.3 s at 96 × 64 and 2.9 s at 120 × 80. The 120 × 80 time included 4,800 droplets in batches of 64, depression filling, flow, moisture and biomes. The later release erosion measurement is in the table above.

WFC's Lambda reference, overlapping model trained on a 6 × 6 maze (n = 3, symmetry 8: 104 patterns), periodic output, debug build: 16 × 16 in 0.73 s, 32 × 32 in 6.65 s. The first solver, which summed neighbour domains on every revision, took 4.2 s and 47 s; the support counts, spliced updates and product test gave the gains. Time grew faster than the cell count, mostly from the per-observation scan of all cells and interpreted bookkeeping spread across the solver. The release measurements above confirmed eligibility and qualified the native replacement.

Mazes of 50 × 50 cells on the debug build: backtracker 0.17 s, kruskal 0.59 s, prim 0.92 s, wilson 0.65 s. The grid golden runs in about 1.2 s.

Delaunay on the debug build (auto), Poisson input: 431 points 0.23 s, 883 points 0.60 s, 1,686 points 1.75 s. That is quadratic: about 250 s extrapolated for the §7 voronoi benchmark's 20k points against a 2 s budget. The release result above confirmed it as a PG7 candidate.


The debug build (not evidence under AGENTS rule 10) evaluates the §7 noise benchmark — 512×512, five octaves of simplex, 1.3M kernel calls — in about 2.6 s on an unloaded machine (AUTO). The budget is 1 s on a release build. PG7.1 requires a release measurement missing the budget by 5× before any native port; run:

```bash
python3 test/lambda/procgen/bench.py --exe <release lambda.exe> --tier auto
```

**Note for P6.** A native exact-tier kernel must compile with floating-point contraction disabled (`-ffp-contract=off`). Otherwise the compiler may fuse `a * b + c` into one FMA on arm64, which changes rounding and breaks PG7.3 parity with the Lambda reference.

## 6. Residue

P6: native Delaunay and hydraulic erosion remain pending; WFC's native solver is delivered with the Lambda oracle retained.

P5: drawing rivers as polylines (they are segments), a temperature/latitude axis for biomes, registering the showcase in the demo catalog. P3: the 3D turtle (later, PG-O8), context-sensitive L-systems, SVG output for turtle polylines (`adapt`). P1: Poisson with a variable radius over a density field (only point-density `weighted` sampling exists). Earlier: periodic (tileable) noise, `field` contours (via `chart/mesh_contour.ls`), gradient/slope and distance transform, `adapt` SVG and Scene3D adapters, 4D noise (PG-O8, later).

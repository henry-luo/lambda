# DOOM in Lambda Script — implementation plan

**Status:** P0–P5 implemented and verified. All nine maps run through the native
Lambda Script application. Six named map/camera poses pass targeted browser/native
landmark checks, including the Spectre's SVG filter. P6 release measurements,
retained-memory reporting and final baseline validation are in progress.

**Date:** 2026-10-09

**Application and resources:** `test/demo/doom/`

**Reference:** [NielsLeenheer/cssDOOM](https://github.com/NielsLeenheer/cssDOOM), pinned to
`438d2e17fb9f75fa3dbc54e1d21a3bc088ad009d`.

**Authority:** Implementation plan under [Doc_Convention §5](../../doc/Doc_Convention.md).
This plan changes no language or ownership ruling. Application choices below are
proposed implementation choices, not new formal semantics.

## 1. Goal and scope

Port cssDOOM's gameplay, input policy, world construction, visibility calculations,
HUD and lifecycle into **Lambda Script**. The runnable entry is
`./lambda.exe view test/demo/doom/doom.ls`. Application modules are `.ls` source;
HTML elements and CSS are rendered by native Radiant. JavaScript, Vite, npm,
`gamecontroller.js`, Three.js and a browser/webview are not runtime dependencies.
Upstream JavaScript may be used as an offline comparison oracle only.

Retain the CSS 3D representation: textured walls and polygonal floors/ceilings,
camera-relative transforms, billboard sprites, sector lighting and a DOM HUD.
Correct missing general Radiant behavior in the engine. Do not substitute a
raycaster, a WebGL renderer, fixed camera screenshots, scene-specific `z-index`
values or hand-reordered map geometry to conceal CSS 3D defects.

The first playable milestone is E1M1 with keyboard movement, collision, doors,
pickups, enemies, pistol combat, death/restart and a working exit. Full delivery
extends this to the nine bundled episode maps, the weapon/enemy/pickup types and
mechanics actually represented by the pinned upstream implementation, sound,
native mouse control and the upstream spectator views. It does not promise
original DOOM demo-file compatibility, multiplayer, arbitrary WAD loading or
features absent from cssDOOM. Fullscreen, touch and gamepad are follow-on input
features; keyboard controls must provide a complete playable path.

Use the full host: **D7.1.6** explicitly excludes Radiant and JS/TS from
`lambda-cli`; **D7.1.4v2** distinguishes native-window, runtime-headless and
null-windowing profiles. A Lambda-only application does not require changing
those build profiles or removing the host's existing JS capability.

## 2. Evidence and dependencies

The pinned repository contains 54 JavaScript files (7,919 physical lines), 27 CSS
files (2,415 physical lines), nine preconverted map JSON files, 874 PNG files and
55 WAV files. These line inventories include comments and blanks; the common
feature comparison excluding both is recorded in §9. These are inventory counts,
not estimates of the resulting Lambda port. E1M1
contains 697 walls, 85 sectors and 138 things before rendering expansion/culling.
The asset loader already consumes JSON; a WAD parser is unnecessary for this port.

The feasibility checks used a native `lambda.exe` with SHA-256
`df696beb92634ada0384ea6cf8eb086be3e8e75fedc52a829043312a531014ad`
only as an observed baseline; rerun the checks against the implementation build.
This fingerprint identifies the probe binary, not a release or the current tree.

| Area | Observed state | Consequence |
|---|---|---|
| CSS geometry math | Three focused parser/math tests passed; `calc(hypot(60,80) * 1px)` painted a 100 px box. | Reuse the shared CSS math and transform resolvers. |
| CSS 3D depth | For two overlapping parallel planes, Chromium painted the nearer red plane in both DOM orders; Radiant painted blue/red according to DOM order. | Correct 3D composition before declaring a playable scene. |
| Other CSS | The current support matrix records gaps in `shape()`, registered-property interpolation, sprite background-position animation and individual-transform animation. | Add focused conformance/paint checks; implement required consumers rather than assuming parser acceptance is support. |
| Scheduling/DOM | `dom.request_frame`, `dom.cancel_frame` and `dom.presentation_style_set_property` already exist. | Reuse document-owned frame delivery and native presentation writes. |
| Browser APIs | The probe found animation frames/fetch, but no Web Audio, pointer lock, fullscreen or gamepad globals. | The `.ls` port uses native input/resource services; sound needs a real Lambda-accessible mechanism. |
| Direct upstream launch | JavaScript startup exception and unresolved assets; render still exited zero. | Exit status alone is not evidence that the game ran. Vite/browser startup is not part of the new application. |

The browser API observations do not establish whether equivalent native services
exist. Audit those services before adding an API. Gameplay and frame rate have
not been validated by the feasibility checks.

Relevant implementation starting points:

- `radiant/view_transform.cpp`: `compute_transform_matrix_3d`,
  `transform_preserves_3d`; `radiant/view_pool.cpp`:
  `view_accumulated_transform_3d` and backface/bounds consumers.
- `radiant/render_state.cpp`: `render_state_push_transform`;
  `radiant/render_walk.cpp` and `radiant/stacking_order.cpp`:
  existing traversal and `radiant_stack_sort_in_paint_order`.
- `lambda/input/css/`, `radiant/resolve_css_style.cpp`,
  `radiant/css_animation.cpp`: shared value computation and animation sampling.
- `lambda/dom/dom_api.def`: frame requests and presentation-style operations;
  `lambda/module/radiant/radiant_module.cpp`: corresponding engine bridge.
- `test/demo/tetris/` and `test/demo/superlambda/`: Lambda rules/view separation;
  `lmd/package/slide/live.ls`: document-owned frame tokens, `evt.time_stamp`,
  `evt.detail`, cancellation and incremental presentation writes.
- [HTML/CSS/SVG support](../../doc/HTML_CSS_SVG_Support.md),
  [Radiant render walk](../../doc/dev/radiant/RAD_13_Render_Walk_Painters.md),
  [test map](../../test/README.md).

## 3. File layout and source mapping

All game-specific code, CSS, maps, assets, development tools, fixtures and recorded
references belong beneath `test/demo/doom/`. Shared engine fixes remain in their
own modules; engine regression registration may live in the existing `test/`
harnesses and `test/ui/` wrappers, referencing canonical demo resources.

```text
test/demo/doom/
  README.md, upstream.json, LICENSE.cssDOOM.txt, ATTRIBUTION.md
  doom.ls                         # entry and reactive shell
  doom.css                        # scene, sprites, HUD and controls
  mod_data.ls                     # map/catalog loading and validation
  mod_state.ls                    # initial/restart/transition state
  mod_geometry.ls                 # polygon, segment and distance operations
  mod_world.ls                    # sector lookup, spatial index, sight lines
  mod_input.ls                    # held controls, edges and input normalization
  mod_player.ls                   # movement, collision, floor tracking
  mod_combat.ls                   # weapons, hitscan, damage, projectiles
  mod_ai.ls                       # enemy state machines and targeting
  mod_mechanics.ls                # doors, lifts, crushers, triggers, teleporters
  mod_pickups.ls                  # inventory, keys, powerups and sector damage
  mod_sim.ls                      # ordered simulation step and gameplay clock
  mod_scene.ls                    # static CSS 3D DOM and resource references
  mod_present.ls                  # dynamic scene/HUD updates and culling
  mod_audio.ls                    # sound-event policy and native playback calls
  mod_events.ls                   # shared sound/visual event identities and lifetime
  mod_weapons.ls                  # weapon switching, animation and HUD presentation
  mod_camera.ls                   # player/follow/top camera controls
  mod_sprites.ls                  # directional sheets, corpses and transient poses
  data/catalog.json
  data/maps/E1M1.json ... E1M9.json
  assets/{textures,flats,sprites,hud,weapons,menu,icons,sounds}/
  tools/                         # resource inventory and oracle/capture tools
  tests/                         # deterministic .ls tests, each with .txt golden
  replay/                        # native UI input/event files
  reference/                     # camera poses, images and comparison manifests
```

`mod_` prefixes distinguish helper modules from runnable golden tests. Introduce
these files as their phase is implemented, rather than checking in empty modules.

| Upstream area | Lambda destination |
|---|---|
| `shared/maps.js`, `game/state.js`, constants | `mod_data`, `mod_state`, immutable module constants |
| `game/geometry.js`, `physics.js`, `spatial-grid.js`, `line-of-sight.js`, `sound-propagation.js` | `mod_geometry`, `mod_world` |
| `input/*`, `game/movement.js` | `mod_input`, `mod_player` |
| `game/entities/*`, `game/player/*` | `mod_combat`, `mod_ai`, `mod_pickups` |
| `game/mechanics/*`, `game/index.js` | `mod_mechanics`, `mod_sim` |
| `renderer/scene/*`, HUD/weapons/effects, UI | `mod_scene`, `mod_present`, `doom.css`, shell |
| `audio/audio.js` | `mod_audio` over a native service, without a JS adapter |

Keep shared geometry and phase/state-machine helpers centralized. Search for
existing helpers before writing new ones. Extract common door/lift/crusher
motion, actor damage and sprite-frame selection shapes before introducing a
third equivalent implementation.

## 4. Lambda application architecture

### State and effects

Keep validated level geometry and asset metadata immutable. A game instance owns
the mutable player/inventory, sector motion, actors, projectiles, input, RNG,
simulation time and presentation session. Stable integer IDs identify sectors,
walls, actors and projectiles; DOM wrappers are presentation handles and never
part of the pure simulation value.

**S9.1.7** says "No global mutable state"; **S9.1.4** makes closure captures
snapshots, not shared cells. Put state in the game view instance, not in mutable
module variables or callback captures. Follow **S12.1.3**: template construction
is pure; input, frame, loading and shutdown effects run in `on` handlers/`pn`.

The core step takes world, prior game, normalized controls, elapsed simulation
time and RNG state, and returns the next game plus ordinary sound/visual events.
Prefer pure transformations for individually testable rules. For measured hot
paths, procedures may mutate an explicitly local working copy. **S9.2.4v2**
forbids passing view-state directly as a `var` argument: prepare a local working
value, update it, then publish the replacement state. Respect snapshot/iteration
semantics under **S9.1.2**, **S9.1.3** and **S9.2.3**.

### Clock and input

Use a stable shell/scene owner with one outstanding `dom.request_frame` token.
Validate `evt.detail` against that token, use `evt.time_stamp`, and request the
next frame only after the current update completes. Cancel on shutdown, unload,
pause and level replacement. Resume resets the last presentation timestamp;
discard late events from previous owner/level generations.

Proposed simulation policy: fixed steps of `1/35` second, an accumulator, at most
four catch-up steps per presented frame, and explicitly discarded excess stalled
time. Define and test this policy in `mod_sim`; do not depend on render frequency.
Convert upstream millisecond timeouts into simulation deadlines, including door
closing, weapon cooldowns, AI, damage and powerup expiry. Visual interpolation
may use accumulator remainder; it cannot change collision or gameplay state.

Use held controls for walking/turning/firing and edge commands for use, weapon
selection, pause and restart. Consume an edge once across catch-up steps. Clear
held controls on blur and pause. Keyboard movement/turning is mandatory; native
relative mouse capture follows once the gameplay milestone passes. Seed the RNG
explicitly and make replay streams deterministic; preserve upstream probability
distributions without claiming equivalence to its unseeded `Math.random()` runs.

### Scene and presentation

Construct static level geometry once per level. Attach stable IDs to sector and
entity nodes. Keep this scene outside a view subtree that rebuilds every time
player position changes; reactive state publication must not recreate hundreds
of walls. Use a stable presentation owner, as the slide player does, to apply
only changed camera properties, sector motion, actor poses and HUD values.

Use the existing DOM/style catalog before adding a patch API. Verify that
presentation-style writes support the required custom properties, transforms and
background values; extend shared property consumers when they do not. Preserve
ordinary cascade rules and the distinction between authored and presentation
state. Cache handles with generation checks and release them at level teardown.

Preserve the upstream coordinate conversion `(x, y, z) -> (x, -z, -y)` and inverse
camera transform. Initially retain upstream CSS geometry math; profile before
moving repeated invariant calculations into level preparation. Frustum/distance
culling is a visibility optimization, never a substitute for depth occlusion.

Gameplay owns door clearance, projectile collision, death and effect expiration.
CSS transitions/keyframes supply presentation; their completion events cannot
decide whether a wall is passable or an attack hit. Rendering errors stop the
session with a visible diagnostic instead of silently dropping essential geometry.

## 5. Radiant work required by the port

### Shared CSS 3D contexts

Extend the current planar painting path into a real preserved 3D context. Reuse
the existing 4×4 matrix, perspective-origin, backface, clipping, effects and
paint-fragment mechanisms. Keep 4×4 transforms through the shared context and
project once at the appropriate flatten/composition boundary. Share accumulated
geometry between painting, bounds/culling and hit testing.

Use [CSS Transforms 2 §§4.1–4.2](https://drafts.csswg.org/css-transforms-2/#3d-rendering)
as the rendering reference, with draft revision and browser version recorded in
the test manifest. In particular, test context membership, grouping/flattening,
accumulated transforms and perspective boxes crossing the viewer plane. These
are CSS conformance references, not additions to Lambda's formal specifications.

The proposed CPU-capable implementation collects planar paint fragments in
camera space, clips against the near plane, and establishes back-to-front order
with plane splitting/BSP or an equivalent intersection-correct algorithm. Reuse
existing projective image painting for clipped textured fragments; validate
texture coordinates after splitting and homogeneous clipping. Center-depth
sorting alone cannot handle intersecting planes and is not an acceptance path.
Resolve coplanar ties using CSS paint order, and preserve alpha, local clips and
group flattening. The implementation spike must confirm the algorithm and its
cost before the full level is wired in.

Keep this generic and usable by windowless CSS rendering. An optional accelerated
path may reuse native graphics services, but must not make ordinary CSS rendering
silently depend on a live GL context or break **D7.1.4v2**'s headless profiles.
Do not modify ThorVG or another vendor to implement the feature in place.

### CSS consumers and native services

- Implement `clip-path: shape(evenodd ...)` for the move/line/close, percentage
  and multiple-boundary forms needed by floors; share the shape representation
  with existing polygon/path clipping and verify holes in projected geometry.
  Follow the [CSS Shapes 1 draft](https://drafts.csswg.org/css-shapes/#shape-function)
  for the relevant grammar and reference-box rules.
- Close required CSS animation consumers: registered numeric/length properties
  (`--light`, `--player-z`, scroll offsets), background-position/frame changes,
  relevant discrete image changes, individual transforms and lighting filters.
  Reuse existing timing, interpolation and restyle paths; no DOOM-only samplers
  in the engine. Verify paused/resumed and removed effects, `steps()` endpoints
  and animation lifecycle events.
- Choose a Lambda-authored HUD layout with the weapon and status bar in the same
  layout structure. Preserve their visual relationship without introducing CSS
  anchor positioning as a prerequisite for a redesigned UI.
- Audit native sound acquisition/decode/playback. Expose the smallest generic
  Lambda-callable service that existing mechanisms can support; sound selection,
  attenuation policy and simulation events stay in `.ls`. Never route through a
  hidden JavaScript `AudioContext`. Audio failure must be reported; a documented
  muted development mode is not completion of the sound milestone.
- Reuse native keyboard/mouse events. If relative mouse capture needs an API,
  expose a generic document/window operation with release on blur/close; do not
  make browser pointer-lock globals a prerequisite for keyboard play.
- Account for the Spectre's SVG filter chain (`feTurbulence`, displacement,
  color matrices and composition) and its animated seed. CSS `filter: url(...)`
  is currently recorded as unsupported. Implement general filter/reference
  consumers before claiming this effect; a static translucent sprite is not
  equivalent completion. Other decorative effects follow the P0 inventory.

**D7.1.2v2** requires "All resource IO lives in `lambda-io`"; use its acquisition
and decoding paths, not direct fetches in Radiant. **D7.5.3** keeps the
Lambda↔Radiant embed/module contracts explicit, and **D7.1.1** preserves the
leftward static-library layering. New general services stay behind those seams.

Native render data follows **D4.5.1v4** and **D4.5.2**: document-owned copies or
registered roots, generation-checked handles, scoped scratch and reclaimed old
fragments. Any retained Lambda callback/value uses **D5.3.3** precise roots.
Never restore native-stack scanning. Use C+ library equivalents, distinct log
prefixes, concise root-cause comments and `float` layout positions/dimensions.

## 6. Implementation phases and acceptance

| Phase | Deliverable | Required evidence |
|---|---|---|
| P0 — resources and contracts | Pin upstream, inventory types/features, import E1M1 and its resource closure, define level/game/input/event records and reproducible probes. | Asset paths/hashes validate; malformed/missing map data fails clearly; initial state and coordinate tests pass. |
| P1 — renderer gate | Correct shared CSS 3D contexts, perspective textures, near clipping, depth/alpha and required floor shapes. | Browser/native reference cases pass for both DOM orders, intersections, nested transforms, backfaces, holes and camera motion. No game-specific ordering rules. |
| P2 — Lambda walkthrough | Static E1M1, native frame loop, keyboard controls, sector lookup/collision, floor height, use actions, doors/lifts and HUD. | A native replay walks an authored route, stops at walls, slides correctly, changes height and crosses an opened door; pause/resume and restart work. |
| P3 — playable E1M1 | Seeded enemies/AI, pistol/hitscan/projectiles, pickups/inventory, damage, death/restart and level exit; required sprite/lighting consumers. | Deterministic combat goldens and native replay demonstrate a kill, blocked shot, pickup, damage, death/restart and reaching the exit. |
| P4 — episode mechanics | All nine maps and their supported weapons/enemies, keyed doors, timed lifts/crushers, teleporters, switches, hazards, powerups and level transitions. | Feature inventory is accounted for; every map loads, runs a bounded replay and has validated resources; relevant mechanics have focused goldens. |
| P5 — sound and controls | Native sound, relative mouse, spectator cameras and remaining visual effects. | Sound events match simulation fixtures; actual decode/playback and close/blur lifecycle pass; camera/input captures agree with intended behavior. |
| P6 — delivery gate | Performance, retained-memory/GC checks, baselines, documentation and registered regressions. | Release measurements and required engine gates pass; sustained play, repeated level changes, forced GC and close leave no unbounded owned storage. |

P1 is the critical path for visible gameplay. Pure Lambda rules and asset
validation may progress while P1 is under development, but a top-down diagnostic
or blank/native frame does not satisfy P2 or P3. E1M1 completion is a milestone,
not permission to mark the full episode port complete. Track each phase as
planned/in progress/verified with build revision, commands and evidence paths.

## 7. Resources, tests and delivery

### Resource import

Keep exact upstream revision, original paths, destination paths, SHA-256 hashes
and provenance in the catalog/manifests. Preserve upstream code license text and
credits for translated code; record map/art/sound provenance separately instead
of assuming the source-code license covers every resource. Begin with E1M1's
transitive texture, sprite, HUD and sound closure, then expand by map/type.

Load through paths relative to the importing module/demo. **D7.2.6** makes
relative imports resolve against the importing script; retain that behavior.
Test launching from a different working directory. Eliminate Vite `public/`
mapping and root `/assets/...` assumptions. The release demo must run offline
without downloading assets during startup. Generated test results, logs and
temporary captures belong in `./temp/doom/`, never `/tmp`.

### Deterministic and visual checks

Each new Lambda unit-test `.ls` gets its expected `.txt` in the same change.
Goldens cover map preparation, point/segment/polygon cases, sector holes, spatial
queries, movement/sliding/clearance, input edges, RNG advancement, combat/AI,
inventory, hazard timing, mechanical phases, level transitions and frame-clock
pause/stall behavior. Include boundaries and failure cases, not only happy paths.

Compare selected upstream functions with fixed inputs, explicit elapsed time and
controlled random streams in an offline oracle. Record state/event expectations
in portable JSON; the native test runner never executes the oracle. Record
differences introduced by the fixed-step policy rather than claiming bit parity.

Visual references use named map/camera/time poses at matched viewport, scale and
settings. Check opaque occlusion, alpha sprites, perspective texture landmarks,
floor holes, doors/lifts, lighting and HUD. Do not use a permissive whole-image
threshold to accept missing walls or sprites. Use focused geometry/pixel probes
alongside images. Chromium capture uses bundled `chrome-headless-shell` on macOS.

Native UI replays test simultaneous held keys, use/fire, blur, pause/resume,
restart, death, level switch and close. Verify actual simulation/DOM state and
nonblank scene output, not merely process exit zero. Register demo tests through
the existing harness; a native lifetime/renderer regression belongs in the
shared test runner while game-specific fixtures remain in this directory.

Direct commands:

```sh
mkdir -p temp/doom
./lambda.exe view test/demo/doom/doom.ls
LAMBDA_EXEC_BACKEND=interp ./lambda.exe test/demo/doom/tests/core_test.ls > temp/doom/core.interp.txt
LAMBDA_EXEC_BACKEND=jit ./lambda.exe test/demo/doom/tests/core_test.ls > temp/doom/core.jit.txt
./lambda.exe view test/demo/doom/doom.ls --headless \
  --event-file test/demo/doom/replay/e1m1_smoke.json \
  --event-result temp/doom/e1m1_smoke_result.json
```

Register a demo-aware runner that reuses the existing golden comparison helpers
and strips CLI framing under the same convention as the core suite; compare both
captured tier outputs with `tests/core_test.txt`. Do not relocate module source
text to run it, since that would change relative import resolution. Exercise
interpreter, automatic tiering and MIR JIT. Runtime changes require
`make test-lambda-baseline`; Radiant/DOM/CSS changes require
`make test-radiant-baseline`, applicable focused CSS/render/lifetime suites and
`make lint ARGS='--rule ^no-int-cast-radiant$'`. Broaden gates only for the changed
source areas. Read Developer_Guide §7 before worktree builds or environmental
failure diagnosis. Edit build registration in `build_lambda_config.json`, never
generated Lua/Makefiles or vendor sources.

### Performance and completion

Measure release builds only: `make release` in the main checkout, or
`make build-release-compile` in a worktree under Developer_Guide §7. Record the
machine, build identity, viewport, map, pose/input replay, visible/total nodes,
warmup and sample counts. Profile simulation, presentation/cascade, layout,
depth composition and raster separately. Verify that camera movement does not
rebuild the entire level and that culling does not allocate a replacement world
every tick.

Proposed delivery target: at least 30 presented FPS at 640×400 on a named native
reference machine during a fixed E1M1 replay, with reported median/p95 frame time
and no correctness shortcuts. This is a target, not a feasibility result.
Measure larger maps separately. Establish measured high-water limits after level
warmup and require a plateau across repeated play/transition cycles, including
forced-GC/poisoning and close with a pending frame and active sound.

Completion requires Lambda-only runtime source, an offline runnable demo, all
in-scope map/type/mechanic inventory items accounted for, validated references,
reproducible tests, passing affected engine gates and recorded release results.
Keep a feature checklist and unresolved implementation work here; do not create
a second project issue ledger or relabel missing behavior as supported.

## 8. Implementation record — 2026-10-09

- [x] P0 resources, contracts and upstream oracle.
- [x] P1 shared renderer and visual references.
- [x] P2 native walkthrough and input lifecycle.
- [x] P3 combat, pickups, death/restart and E1M1 exit.
- [x] P4 all nine episode maps and represented mechanics.
- [x] P5 native sound, relative mouse, spectator views and effects.
- [ ] P6 final release measurements, ownership plateau and delivery record.

### Resources and application

The import contains 958 exact files, 8,455,564 bytes, including all nine maps,
874 PNGs and 55 WAVs. `data/resources.json` records hashes and original paths;
`ATTRIBUTION.md` separates source-code licensing from DOOM resource provenance.
`tools/import_upstream.py --verify` checks the complete import offline.

The entry and 19 application modules are Lambda Script. The document owns its
session and frame token (**S9.1.7**, **S12.1.3**); map geometry is prepared once
per generation and presentation updates retained nodes. The 35 Hz clock has at
most four catch-up steps and discards excess stalled time. Gameplay deadlines,
seeded randomness and effect lifetime depend on simulation time. Pause, blur,
restart, level replacement and close cancel or invalidate pending work.

All five difficulty settings are selectable. Restart preserves difficulty;
episode transitions preserve difficulty and inventory. The production native
episode replay verifies nightmare loading and restart as well as all nine maps.
Player, follow and top cameras share simulation state; top view supplies pan,
rotation, wheel/keyboard zoom and pointer dragging.

Thirteen gameplay goldens and the map validator run through the existing Lambda
harness on interpreter, automatic tiering and MIR JIT: 42 fixture/tier checks.
The 35-case upstream oracle records unmodified source hashes and explicit
fixed-clock/RNG differences in `reference/upstream-oracle.json`. Coverage includes
geometry, spatial queries, movement/sliding, clearance, input edges, AI/combat,
pickups, mechanics, animation, camera, sound policy and pause/stall behavior.
Every runnable `.ls` fixture has its `.txt` golden; `mod_fixture.ls` is a helper.

The registered native walkthrough checks wall collision, sliding, door clearance,
stairs, pause/resume, stable scene identity and restart. The exit route checks
combat and pickups through the actual E1M1 exit into E1M2. A separate route checks
natural enemy damage, death and restart. The episode and camera replays check map
identity, generation, selectors, input and geometry. The lifecycle replay visits
all nine maps three times. Production state is reached by recorded controls,
without injected game state. These six replays run forced collection every 5,000
allocations, poisoned freed storage and tracked close ownership.

### Shared engine work

The generic preserved-3D compositor retains 4×4 transforms, splits intersecting
planes with a CPU BSP, preserves coplanar paint order and clips before homogeneous
division. Painting, bounds and backface consumers share transform accumulation.
Lambda-side inverse homography samples perspective PNGs because ThorVG's image
matrix is affine. Texture density, image-rendering and premultiplied transparent
sampling are retained. Shape/path fill rules pass through paint IR, display lists
and raster/PDF/SVG bridges. No vendor sources or map ordering rules were changed.

The focused native fixtures cover both DOM orders, intersections, alpha,
flattening, nested transforms, backfaces, viewer crossings, floor holes and texture
landmarks at densities 1 and 2. Six named map/camera poses validate 19 texture and
geometry patches against Chromium 154.0.8037.57. An actor-visible/actor-hidden
pair independently verifies the E1M8 Spectre's SVG fuzz graph. Canonical browser
and native images, binary/image hashes and numeric results live in `reference/`.
`tools/capture_poses.cjs` rejects a missing landmark or filtered actor.

The reduced `tests/render/viewer-concave.html` fixture records a Chromium 154
limitation: reversing intersecting planes removes a near-clipped ceiling. The
native geometric test requires the ceiling in both orders under CSS Transforms 2
§§4.1.2 and 4.2. Contradictory browser pixels do not weaken that assertion.

Shared CSS consumers now handle required polygon shapes, registered custom
properties, individual transforms, sprite-position animation, filter chains,
SVG URL filters and radial masks. Spectre seed updates follow simulation time.
Multi-position radial-gradient stops retain both positions. Isolated blur cannot
sample or overwrite the surrounding backdrop. SVG offscreen groups reset projected
clip bounds into their local coordinates. Filter LUTs preserve exhaustive scalar
rounding; row sampling can borrow the existing render-worker pool.

Computed numeric substitutions use a scoped scratch owner; retained expressions
keep their owner, and immutable background URLs reuse the canonical string cache.
Exact template-handler lookup avoids scheduling nonexistent animation listeners.
Font cache lookup builds its key without a per-hit arena allocation. Immutable
map transitions reuse the full admitted type contract rather than creating a new
shape per mutation (**D3.4.3v5**, **D3.4.5**); the 4,096-mutation regression checks
bounded ownership. Promoted native document transforms restore the caller's stack
limit after their large-stack initialization worker retires (**D5.3.6v2**).
These fixes retain normal automatic function promotion.

The E1M6 release trace exposed a cross-language shape contamination: JS bootstrap
branded the global neutral `EmptyMap`, so later Lambda JSON inputs inherited JS
metadata and contract admission bypassed the bounded external-parent cache.
JS now selects a separate Input-owned branded root; neutral Lambda roots remain
neutral (**D3.4.7**). The mixed-runtime regression checks JSON object prototypes,
post-bootstrap Lambda inputs and reuse of their admitted numeric shape. Final
release measurements must verify that the affected runtime pool plateaus.
The short native E1M6 debug probe now holds runtime-pool live storage at
3,623,830–3,628,561 bytes, compared with the earlier unbounded release trace.
Close reports zero tracked live bytes and allocations. The new mixed-runtime
regression, all 195 JS script tests, all 493 JS tests and all 91 JS optimization
tests pass; the latter two use separate reruns after shared result-file
interference in the full baseline invocation.

### Wrap-up checkpoint — 2026-10-09

The user requested wrap-up with P6 still open. Do not mark this plan complete or
use pre-fix measurements as the final release record. The latest code includes
the Input-owned JS root fix and the focused native memory verification above.
All nine native effect/input/audio probes pass with forced GC on every allocation,
poisoning, immediate promotion and zero tracked close ownership. The fresh E1M1
walkthrough, exit, death and episode replays also pass with forced GC every 5,000
allocations; the episode replay completed during wrap-up. Camera and three-cycle
lifecycle rechecks after the final root fix were not started.

The required full Lambda baseline invocation completed with unresolved failures
in `latex_test_latex_phase3_corpus` and `edit_view_only`; its shared JS result files
were inconsistent with the passing isolated JS runs. Preserve these failures for
investigation rather than adjusting tests or goldens. Resource verification
(958 files/nine maps), Python/Node tool syntax, Radiant dimension lint and
`git diff --check` pass.

Remaining P6 work: rebuild release after the root fix; rerun isolated E1M1 and
E1M6 performance, sustained play and all three episode cycles; summarize the
ownership plateaus and close results; refresh all six final visual poses; rerun
the affected baseline gates without shared output collisions; publish the final
release measurement/delivery records and update completion status. Existing
pre-fix measurements remain diagnostic evidence only.

Relative resource resolution works from a different working directory. Shared
RFC 3986 dot-segment normalization preserves the slash needed after `../`;
imports remain governed by **D7.2.6**, acquisition by **D7.1.2v2**. All 79 URL
regressions pass. The live-select regression covers native option selectedness.

Native relative mouse capture releases on blur, navigation and close. macOS sound
uses AVAudioPlayer over Lambda-acquired WAV bytes, with document-generation tokens
and 32 voices. Pause/resume, stale-token rejection and teardown are tested under
**D4.5.1v4** and **D4.5.2**. Other platforms visibly report an absent audio backend.
Nine CSS/effects/input/audio probes run collection on every allocation, poisoning,
first-call promotion and synchronous satellite publication, and close with zero
tracked live allocations.

### Episode inventory

| Map | Walls | Sectors | Things | Doors | Lifts | Crushers | Teleporters | Triggers |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| E1M1 | 697 | 85 | 138 | 4 | 2 | 0 | 0 | 2 |
| E1M2 | 1473 | 200 | 262 | 14 | 5 | 0 | 0 | 11 |
| E1M3 | 1434 | 177 | 380 | 15 | 6 | 1 | 0 | 7 |
| E1M4 | 1214 | 139 | 254 | 14 | 4 | 0 | 0 | 5 |
| E1M5 | 1153 | 143 | 293 | 20 | 3 | 0 | 10 | 5 |
| E1M6 | 1793 | 250 | 463 | 17 | 4 | 0 | 0 | 9 |
| E1M7 | 1343 | 170 | 358 | 15 | 5 | 1 | 0 | 12 |
| E1M8 | 543 | 74 | 126 | 3 | 1 | 0 | 8 | 0 |
| E1M9 | 948 | 147 | 237 | 13 | 8 | 0 | 0 | 12 |

The six enemy types are zombieman (3004), shotgun guy (9), imp (3001), demon
(3002), Spectre (58) and baron (3003). The weapon table implements fist, pistol,
shotgun, chaingun, rocket launcher and chainsaw; plasma/BFG do not occur in the
bundled maps or the upstream weapon table. Inventory includes the represented
ammo, backpack, health/armor bonuses, armor, three card keys, invisibility,
radiation suit, map and light powerups. Static corpses, torches and decorations
use the imported sprite/radius tables. Player/deathmatch starts are spawn
metadata and teleporter destinations remain mechanical markers. Focused AI,
combat, pickups and mechanics fixtures account for these categories, keyed and
occupied doors, lift phases, crushers, switches, W1 teleports/telefragging,
hazards, normal/secret exits, nightmare respawn and E1M8's tag-666 floor.

## 9. Source LOC comparison — 2026-10-09

### Counting method and scope

This snapshot, refreshed after the intrinsic-image refinement, compares the
application sources in `test/demo/doom/` with cssDOOM
revision `438d2e17fb9f75fa3dbc54e1d21a3bc088ad009d`. The upstream archive SHA-256 is
`8291a037e4a2430c9c80170510586c917b4aea75f5f80331dfb222edf9ed7db8`.
The full Lambda application contains 20 `.ls` files, comprising the entry and 19
modules: **1,827 source LOC**, or 1,900 physical lines including comments/blanks.
The common feature selection below contains 1,735 Lambda Script LOC.

Count stored physical lines that retain code, data or configuration after
removing comments and excluding blank lines. Lines containing only delimiters
still count; mixed code/comment lines count once. JavaScript comments are
identified with Babel, HTML comments with parse5, and Lambda/CSS comments with
string-aware scanning. Files are not reformatted, and JSON/config files loaded
at runtime count in full, including unused entries and embedded provenance.

The common scope covers world geometry, movement/collision, simulation, enemy
AI, combat/pickups, episode mechanics, CSS 3D rendering, HUD/effects, keyboard,
mouse and spectator cameras, menus, map loading and application lifecycle.
Identifiable non-common sections are excluded: fullscreen, touch, gamepad, help
and debug interfaces; upstream sky-wall occlusion, extra HUD inventory panels,
head bob, spectator button grids and loading fades. Audio playback backends are
outside this gameplay/rendering/control comparison; sound-event policy on mixed
gameplay lines remains counted. A retained line serving both common and extra
behavior counts once.

Exclude tests, fixtures, offline development/import/capture tools, dependency
implementations, browser/Radiant/Lambda engine code and binary/SVG art assets.
Also exclude hosting-only `wrangler.toml` and the Lambda offline manifests
`upstream.json`, `data/catalog.json` and `data/resources.json` (7,472 lines
combined). This is an audited source inventory, not proof of identical behavior
or complete cssDOOM feature parity. P6's outstanding delivery gates remain open.

### Comparison by module and feature

Lambda paths below are relative to `test/demo/doom/`; bare module names refer to
their `.ls` files. Rendering and application rows include the relevant CSS/HTML
and package configuration as well as script source.

| Module / feature | cssDOOM LOC | Lambda DOOM LOC | Lambda modules / data |
|---|---:|---:|---|
| World, movement, state and simulation | 605 | 373 | `mod_geometry`, `mod_world`, `mod_player`, `mod_state`, `mod_sim` |
| Enemy AI | 309 | 165 | `mod_ai` |
| Combat, projectiles, damage and pickups | 719 | 269 | `mod_combat`, `mod_pickups`, `mod_events` |
| Doors, lifts, crushers, switches and teleporters | 427 | 150 | `mod_mechanics` |
| Rendering, HUD, effects and cameras | 2,199 | 642 | `mod_scene`, `mod_present`, `mod_effects`, `mod_sprites`, `mod_weapons`, `mod_camera`, `doom.css` |
| Input, map loading, menus, application and package config | 549 | 274 | `mod_input`, `mod_data`, `doom.ls` |
| Rule and visual constant tables | 218 | 513 | `data/rules.json` (424), `data/visuals.json` (89) |
| Sprite/weapon sheet and animation metadata JSON | 0 | 256 | `data/images.json` |
| Shared maps and asset metadata JSON | 14 | 14 | Nine maps and five asset metadata files |
| **Total including runtime JSON/config** | **5,040** | **2,656** | |

The 218 upstream constant-table lines are JavaScript; their Lambda counterparts
are JSON. Both rule/visual tables were checked against the pinned constants,
normalizing upstream sets to arrays and infinite values to the stored sentinel.
Moving constants into JSON does not remove them from the comparison.

### Source and data subtotals

This table regroups the same counted lines by file type; it is not additional LOC.

| File type | cssDOOM LOC | Lambda DOOM LOC |
|---|---:|---:|
| JavaScript / Lambda Script | 3,745 | 1,735 |
| CSS | 1,163 | 138 |
| HTML | 95 | 0 |
| **Source subtotal** | **5,003** | **1,873** |
| JSON, including package config | 37 | 783 |
| **Total including runtime JSON/config** | **5,040** | **2,656** |

The Lambda HTML element construction is already counted in `.ls` source.
Lambda uses **62.6% fewer source LOC** on this scope and **47.3% fewer stored lines
including JSON/config** after removing the redundant PNG dimension inventory.
The earlier 3,981-JavaScript-LOC estimate covered broader module families; the
3,745-JavaScript-LOC count here removes further identifiable non-common behavior.
Neither percentage measures language efficiency: multi-statement source lines,
CSS formatting and pretty-printed versus minified JSON materially affect LOC.

All 14 shared JSON files are byte-identical between the two implementations and
occupy one stored line each: **6,183,439 bytes per side**. Formatting them all with
two-space JSON indentation would produce **552,870 lines per side**. That
diagnostic expansion is not included in the stored-LOC totals above.

### Generated image metadata

`data/images.json` is counted in full but shown separately. It now retains
**15 sprite-sheet definitions, six weapon-sheet definitions and seven animation
layouts**, providing frame sizes, grid dimensions and attack/death frame
selection. Revision/source hashes remain for import verification. All retained
values match the pre-refinement metadata exactly.

The removed `images` table inventoried the dimensions of **873 PNGs** and occupied
**3,494 lines**. Static pickups and decorations now use intrinsically sized
`<img>` elements in `mod_scene.thing_tree`, centered above the actor's floor anchor
with CSS. `mod_sprites.definition` returns their path without dimensions, and
`mod_present.paint` skips sheet-only background/animation writes for these images.
Animated actors, pickups, weapons and the player marker retain cropped background
sheets as in original cssDOOM. The generator no longer scans/stores every PNG's
size; the offline verifier checks each retained sheet grid directly against its
PNG header. No native image-size API or engine change was needed.

| Lambda DOOM common scope | Before refinement | After refinement |
|---|---:|---:|
| Script, CSS and HTML source | 1,871 | 1,873 |
| JSON/config | 4,277 | 783 |
| Of which `data/images.json` | 3,750 | 256 |
| **Total including runtime JSON/config** | **6,148** | **2,656** |

This removes **3,492 net stored lines** without reformatting the application or
JSON. The new `fixtures/_sprites.ls` and `replay/sprites.json` regression exercises
the production actor tree: two static `<img>` elements with no width/height
attributes, two cropped sheets, intrinsic dimensions, horizontal centering,
floor anchoring and rendered pixels. It is registered with the native DOOM
forced-GC/close probes. The sprite golden also verifies a static pose without
any dimension table. Tests and offline tools remain excluded from these totals.

Six fresh browser/native pose comparisons passed against HeadlessChrome
148.0.7778.97, including the Spectre's paired visible/hidden filter check.
Captures were directed to
`temp/doom/refined-reference/` using `DOOM_REFERENCE_DIR`; the checked-in reference
images were preserved. The deterministic game/resource fixtures passed on
interpreter, automatic tiering and MIR JIT (42 fixture/tier checks).
The resource check verifies all 958 imported hashes and the retained sheet grids;
an all-map/all-difficulty sprite audit resolves all 26 distinct static image paths.
The rebuilt `RadiantViewTest.Doom*` checks passed all ten native probes and all six
gameplay/camera/lifecycle replays, including three cycles through the nine maps.
The probes use collection on every allocation and immediate promotion; gameplay
uses collection every 5,000 allocations. Both require poisoned freed storage and
zero tracked live allocations at close. macOS AVAudioPlayer decoding failed in
the sandbox; the complete native gate passed outside it. These targeted checks do
not close the remaining P6 release-performance and aggregate-baseline gates.

The local audit used `temp/cssdoom-loc/count_comparable.cjs` and
`temp/cssdoom-loc/compare_runtime.cjs`; its detailed
`temp/cssdoom-loc/common-runtime-loc.json` records per-file counts, SHA-256 hashes,
excluded line ranges and shared-JSON checks. A fresh rerun reproduced both tables
and reconciled all totals. These are temporary audit artifacts; this section
retains the measured snapshot independently of their lifetime.

# Scene3D: further Three.js feature coverage — implementation plan

**Date:** 2026-10-27. **Status:** implementation started in worktree
`temp/scene3d-three-features`, branch `codex/scene3d-three-features`.
M0–M2 are delivered with the aggregate limits below. M3–M7 remain planned.

**Baseline inspected:** `691aba4b8`. Scope is native `lambda.scene3d`, distinct
from running Three.js in LambdaJS. The comparison oracle is the repository's
pinned Three.js **0.186.1** in
[the manifest](../../test/demo/scene3d/vendor/three/manifest.json). Current
upstream documentation informs the roadmap; it does not silently upgrade the
oracle or establish compatibility with every Three.js backend or addon.

## 1. Outcome and delivery order

Deliver better model viewers, product scenes, interiors and scientific scenes
through reusable native features. Preserve Lambda's declarative construction,
shared playback and HTML/SVG composition. Three.js API compatibility is not the
target: each delivered feature has a named native profile and observable limits.

The first delivery is **M0–M4**: a qualified HDR/postprocessing path, improved
imported-model fidelity, and clearcoat/sheen materials. Close that delivery
before starting **M5–M7**: glass, dedicated drawables and scene management.
Section 7 orders subsequent work. A serious shared correctness or lifetime bug
may preempt this order; record its reproducer and owner before expanding scope.

| Order | Milestone | User-visible result | Relative effort / main risk |
|---|---|---|---|
| M0 | Baseline and acceptance inventory | Existing scenes have reproducible correctness, resource and release-cost controls | Small; environmental qualification |
| M1 | Linear HDR rendering and pass resources | A reusable scene-color/output boundary preserves existing appearance | Large; color, alpha and target ownership |
| M2 | Native postprocessing core | Bloom, FXAA and one custom color-pass contract | Medium; bandwidth and output ordering |
| M3 | Imported-model fidelity | Independent map transforms, UV0/UV1, vertex alpha and imported cameras | Medium; shader/loader/export agreement |
| M4 | Physical materials, first profile | Clearcoat and sheen with factors, maps and glTF import | Large; energy balance and sampler pressure |
| M5 | Transmission, first profile | Refractive glass with IOR, thickness and absorption | Large; scene-color ordering and bounded approximations |
| M6 | Points, sprites and lines | Point clouds, camera-facing markers and useful continuous/segmented lines | Medium; size, clipping and transparency |
| M7 | Culling and LOD | Off-screen meshes stop drawing; distant objects select simpler geometry | Medium; conservative animated bounds |

These are sequencing and complexity estimates, not calendar commitments or
performance claims. Each milestone is independently reviewable and complete
only after its stated behavior and required gates are qualified.

## 2. Current capabilities and authority

### Existing work to continue

The current tree already contains:

- Basic, Lambert and metallic-roughness materials; normal/occlusion/emissive
  maps; alpha masks; HDR/procedural environments; directional/spot/point shadows.
  See [the PBR record](Radiant_Scene3d_PBR_Shadows.md#11-progress).
- Custom GLSL materials, named instance attributes, animated uniforms and
  retained programs; raster and SVG textures; image-effect presets. See
  [shader M1/M2](Radiant_Impl_Scene3d_Shaders.md#7-progress) and
  [the effects record](Radiant_Impl_Scene3d_Effects.md).
- GPU simulation with RGBA32F ping-pong targets, fixed stepping, rewind and
  retained state across compatible reprojections, plus the butterfly demo.
  This is **implemented**, not new work in this roadmap. Its aggregate and
  release-performance qualification limits remain those recorded in
  [shader M3–M5](Radiant_Impl_Scene3d_Shaders.md#7-progress).
- Clip blending, skeletal animation, two relative morph targets, selected
  model loaders, Meshopt decoding, static glTF/GLB export and rigid-body physics.
  [Asset profiles](../radiant/Radiant_Design_WebGL.md#15-asset-loading-into-native-scenes)
  and [motion](Lambda_Impl_Motion.md) remain the detailed records.

The shader plan's original starting-point section and the older
[Three.js review](../radiant/Radiant_Scene3D_Review.md) predate some of these
deliveries. Use current source and the dated progress entries when selecting
work; do not recreate implemented shader, SVG or simulation facilities.

### Contracts this plan preserves

| Authority | Consequence for every milestone |
|---|---|
| **S12.1.1v3**, **S12.1.3** in [formal semantics](../../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system) | Constructors and geometry transforms remain pure values; GPU work and playback belong to the host; event mutations remain procedural. |
| **D7.2.4v4**, **D7.5.3v2** in [formal design](../../doc/Lambda_Formal_Design.md) | Public helpers stay under `lambda.scene3d.*`; native rendering stays behind the existing Lambda/Radiant boundary. |
| **D4.5.1v4**, **D4.5.2** | Retained descriptions, shader source and buffers are document/native-owned. Borrowed GC values never become unrooted retained pointers. |
| **D4.5.3v2** | Preserve presentation ownership and reuse valid downstream state through template regeneration. |
| **D2.11.4** | Fixed-time captures settle the selected frame; taking another snapshot does not independently advance simulation or animation. |
| **D7.4.1v2**, **D7.5.2** | Keep native resources behind branded handles; use the existing resource-acquisition boundary and quotas. |
| **D7.3.5** | Every new module surface names its conformance gate and joins the existing test lanes. |

The existing scene design supplies the graphics-specific contracts: its
[scene vocabulary](../radiant/Radiant_Design_WebGL.md#4-lambda-package-and-scene-elements),
[renderer](../radiant/Radiant_Design_WebGL.md#5-radiant-scene-renderer-and-shared-native-gl-core)
and approved **SH1–SH7** in [the shader plan](Radiant_Impl_Scene3d_Shaders.md#6-decisions-for-the-user).
This implementation proposal introduces no normative ruling IDs. New API names
and defaults below are recommendations, not claims of an existing ruling.
Resolve uncovered public-design decisions before implementing the affected
surface; record accepted decisions in the existing scene design. Change both
the formal spec and working design, and regenerate the formal index, only when
an actual formal ruling changes, following
[Doc_Convention](../../doc/Doc_Convention.md).

## 3. Implementation boundaries and shared mechanisms

### Source map

| Owner | Existing source / entry points | Planned responsibility |
|---|---|---|
| Pure public API and validation | `lmd/package/scene3d.ls`, `scene3d/model.ls`, `shape.ls`, `camera.ls` | Constructors, schema validation, geometry utilities and pure camera/picking calculations |
| Scene projection and drawing | `radiant/scene3d.cpp`: `scene3d_compile`, `scene3d_source_key`, `scene3d_snapshot`, `scene3d_draw_mesh` | Build normalized render data, execute passes, preserve invalidation and publish the final image |
| Material and shader state | `scene3d_shader_material`, `scene3d_shader_validate`, `scene3d_shader_prune`, `scene3d_program` | Shared shader features, reflection, binding plans and retained programs |
| Native GL waist | `radiant/gl_core.hpp/.cpp`: `native_gl_float_target`, `native_gl_begin_region`, `native_gl_draw`, `native_gl_snapshot` | Typed target formats, resolves, sampling, draw state, accounting and context loss |
| Playback and simulation | `radiant/animation_mixer.*`, `scene3d_animation.cpp`, `scene3d_simulations_advance` | Sample new properties through the existing clock and preserve simulation ownership |
| Asset conversion | `lambda/input/input-scene3d-gltf.cpp`, `input-scene3d.cpp`, `input-scene3d.hpp` | Preserve richer source data in the shared scene schema |
| Asset export | `lambda/format/format-gltf.cpp` | Encode the same supported schema and report unsupported constructs |
| Qualification | `test/scene3d/`, `test/lambda/scene3d/`, `test_input_model_gtest`, native units, Radiant View | Package, import, pixels, interaction, lifetime and release cost |

Keep `scene3d_snapshot` as the presentation entry point. Extract cohesive pass,
material or bounds helpers when multiple consumers need them; do not duplicate
the current renderer into an effects renderer. Promote shared static helpers
to the relevant module header rather than copying them. Search `lib/`, chart
geometry and the existing camera helpers before adding geometry or ray code.

### Resource and shader rules

- Begin with an **ordered color-pass chain**, not a public general render
  graph. Each pass reads a completed texture and writes another target;
  reading the active render attachment is rejected. Simulation buffers remain
  distinct from disposable postprocessing scratch targets.
- Keep an unchanged frame cached. A camera move must not rebuild geometry,
  environments, shadow maps or programs unless their own inputs changed.
  A uniform edit must not relink an unchanged program. Continue the existing
  compatible-reprojection rules for playback and simulation.
- Allocate targets by dimensions, format, sample count and lifetime; reuse
  compatible allocations. Check overflow and peak live bytes before allocation,
  including old targets retained by consumers during replacement. Preserve the
  current **512 MiB context budget** and **4096-pixel raster-axis bound** until
  measurements justify a separately documented change.
- Query device limits and build a checked sampler/attribute plan. The current
  waist exposes 12 bindings; layered maps, transmission and four shadow casters
  can exceed it. Do not silently drop maps or raise a constant without checking
  hardware and accounting. Preserve existing standard-material combinations;
  document and diagnose unsupported new combinations.
- Compile feature variants on demand with normalized keys. Numeric factor,
  camera and playback changes are uniforms; do not eagerly compile a Cartesian
  product of every material, map, shadow and effect option.
- Stage new resource sets and release partial allocations on every error.
  Teardown and context loss release each owner once. Diagnostics stay visible;
  allocation failure must not silently disable the requested feature.

## 4. First delivery: M0–M4

### M0 — qualify the starting point

**Outcome:** distinguish current feature work from inherited failures and know
which part of a frame costs time before introducing more passes.

Reuse Ringworld, Dreamhome, the PBR oracle, Make waves and Butterflies. Record
their source revision, executable profile, GPU/driver, logical size, raster
scale, virtual time and readiness condition. Capture an opaque scene, a
transparent scene over HTML/SVG, a textured imported model and a simulated
scene. Keep artifacts under `temp/scene3d-three-features/m0/`.

**Acceptance:** existing Scene3D checks are classified once; fixed-time repeat
captures agree; camera-only motion retains projection/environment/shadow/program
generations; a compatible uniform edit preserves simulation state. Measure
release p50/p95 after warmup and separate projection, shadow/environment,
simulation, scene draw, readback and page composition where instrumentation
supports that distinction. Add only missing counters needed for later gates.

Do not import old FPS numbers as a target or promise 60 FPS before measuring
the named workload. This milestone makes no renderer rewrite.

### M1 — establish linear HDR scene color and one output stage

**Problem and owner:** `scene3d_draw_mesh` currently selects a tone-mapping
variant for each draw, while `scene3d_snapshot` renders directly to the
published target. Effects need unclipped scene radiance before tone mapping.
The GL waist already owns float targets; extend it rather than constructing
untracked framebuffers inside Scene3D.

**Implement:** render scene radiance to a linear RGBA16F color target with
depth and a supported MSAA/resolve path. Reuse that resolve path for later
sampling. Move scene tone mapping, exposure and output conversion into one
final stage shared by ordinary and effect-enabled scenes. Preserve custom
materials' existing linear/premultiplied output contract. Apply nonlinear color
operations to straight color with a defined zero-alpha case, then publish
through the existing `ImageSurface` alpha/color contract.

Preserve the existing scene-design contract that translucent surfaces blend
after tone mapping: render opaque/masked geometry into HDR, perform the output
conversion, transfer opaque depth, then draw ordered transparent surfaces into
the display target. This resolves the proposal's potential tone/composition
conflict without changing translucent color semantics. Bloom's first profile
therefore observes the opaque HDR image. Do not bless a changed golden as
evidence that the migration is correct.

Keep environment and shadow passes in their existing color/depth domains.
Introduce internal pass descriptors and retained target ownership only where
needed by this path. Expose capability/format failures before drawing.

**Acceptance:**

- `HdrIdentityPreservesStandardLambertAndShaderScenes`: no-effect output agrees
  with the existing oracle for all three tone curves and exposure controls.
- `HdrResolveRetainsRadianceAboveOne`: an emissive patch remains above 1 in
  the intermediate target and reaches the final output with one tone mapping.
- `HdrOutputPreservesTransparentComposition`: translucent edges and RGB at
  partial/zero alpha composite correctly over light, dark and SVG backgrounds
  at 1x/2x; repeated export uses the same selected frame.
- Resize, 100 compatible replacements, allocation failure and context loss
  leave no stale attachment, monotonic retained-resource growth or leaks.

**Boundary:** no bloom or new physical material yet. Close image correctness
before evaluating performance; a simple scene's extra output pass is measured
against M0 and any material regression is explained before closure.

### M2 — bloom, FXAA and a bounded custom color pass

**Outcome:** a native equivalent of the useful core of Three.js
[EffectComposer](https://threejs.org/docs/pages/EffectComposer.html), with
independent scene construction and host execution.

**Proposed API:** `postprocessing(children)` holds ordered `effect(kind,
options)` elements. Initial effects are `bloom`, `fxaa` and `shader`; a shader
effect reads the previous color texture, uses the existing uniform/reflection
machinery and may use scene textures. Constructors live in
`lambda.scene3d.postprocessing`. Names and exact fields are proposed.

**Implement:** bloom threshold/downsample/blur/composite operates in linear HDR;
tone mapping follows bloom; FXAA operates on the defined display-color output.
Specify custom-pass input/output color domain explicitly and reject invalid
orders rather than guessing. Reuse ping-pong scratch targets and the shared
full-screen draw helper; take one final CPU snapshot, not one per effect.
Animate numeric parameters through existing property bindings.

**Acceptance:** an emissive object produces a controlled halo; zero bloom
strength and an identity shader preserve the baseline; FXAA reduces a diagonal
edge's aliasing against the pinned reference; changing only strength retains
programs and targets. Invalid effect names, attachment feedback, incompatible
domains and exhausted budgets produce diagnostics. Resize, disabling effects,
template regeneration and page teardown retire unused resources.

**Boundary:** bloom initially supports opaque viewports; transparent bloom
requires an explicit glow/coverage policy and is diagnosed until that profile
is delivered. Transparent no-effect rendering stays supported. SSAO, SSR,
temporal history and depth-dependent custom passes are subsequent milestones.

### M3 — preserve imported texture and camera fidelity

**Problem and owner:** `Gltf::load_materials` and its UV-transform helper
currently bake the first map's transform into shared UV0. The native vertex
layout stores RGB, one UV set and no imported camera selection.

**Implement:** add UV0/UV1 and RGBA vertex color to the shared geometry profile,
using only the required vertex lanes. Store a texture coordinate selector and
UV transform per material map; apply the transform while sampling instead of
rewriting a primitive's one shared UV stream. Generate or derive the normal-map
tangent frame from that map's actual UVs, including mirrored transforms.
Preserve perspective/orthographic glTF camera nodes with stable IDs; explicit
viewport camera selection wins. Export the delivered attributes and map
transforms through the existing exporter.

**Acceptance:** one glTF fixture uses UV0 for base color and UV1 for occlusion;
another uses different rotation/offset/scale for base color and normal maps;
neither loses its appearance on import. Vertex alpha affects blend/mask as
declared while opaque alpha handling stays correct. Both imported camera types
produce the expected framing; explicit camera selection overrides them.
Out-of-range selectors and absent requested UV sets diagnose clearly.
Round-trip a static fixture and compare its decoded attributes and pixels.

**Boundary:** UV0 and UV1 are the selected profile; arbitrary additional UV
sets, compressed textures and new material extensions remain explicit gaps.
Every new mapped slot must reuse this per-map representation.

### M4 — clearcoat and sheen as a physical-material profile

**Outcome:** coated wood/paint and cloth differ visibly from ordinary standard
materials, following the relevant subset of
[MeshPhysicalMaterial](https://threejs.org/docs/pages/MeshPhysicalMaterial.html).

**Proposed API:** `material('physical', options)` extends standard shading.
First properties are clearcoat factor/roughness/normal and sheen
factor/color/roughness, with their map slots and numeric/color tracks. Use
zero-valued extension defaults so an unextended physical material agrees with
standard shading. Final field names and range/default contracts belong in the
scene design before implementation.

**Implement:** share the standard BRDF, environment sampling and direct lights;
add layered lobes with correct energy handling and map color spaces. Select
lazy shader features and a checked binding plan. Import/export
`KHR_materials_clearcoat` and `KHR_materials_sheen` only once their represented
factors/maps are rendered correctly. Use M3's transforms and UV selectors.

**Acceptance:** coat highlights, coat roughness, a separate coat normal and
cloth grazing-angle sheen match named reference scenes under direct and HDR
lighting. Zero factors recover standard output. A textured glTF sample keeps
its separate base/coat/sheen maps; animated factors change appearance without
relinking. Malformed factors, missing maps and unsupported sampler combinations
report precise diagnostics. Existing standard scenes and shadow combinations
retain their qualified behavior.

**Boundary:** anisotropy, iridescence, dispersion, transmission and volume are
not implied by the name `physical`. Publish the implemented extension matrix.

## 5. Second delivery: M5–M7

### M5 — transmission and absorption for glass

Depends on M1, M3 and M4. Introduce transmission factor/map, IOR, thickness and
attenuation color/distance. Capture opaque scene color before transmissive
surfaces, build only the needed filtered/mip resources, then shade those
surfaces before ordinary alpha-blended content. Share output conversion and
resource accounting with M1/M2; never sample the active attachment.

The first profile approximates refraction through scene color plus environment
fallback. It is not ray tracing. Define behavior at the viewport edge, outside
the opaque image and for objects hidden behind glass. Implement
`KHR_materials_transmission`, `KHR_materials_ior` and the represented subset of
`KHR_materials_volume`; explicitly list unsupported nested/overlapping media.

**Acceptance:** a thin pane refracts a checkerboard with stable Fresnel
reflections; IOR changes the displacement; finite thickness/attenuation colors
a block; zero transmission recovers the opaque material. A glTF glass sample
imports correctly. Camera motion, an animated glass factor, an empty opaque
background and an edge-of-screen object remain deterministic and finite.
Overlapping transmissive objects have a documented approximation, without a
claim of full Three.js pixel parity.

**Stop boundary:** no caustics, dispersion or arbitrary participating media.
One opaque-scene capture is shared across the selected profile rather than
recapturing the page for each object.

### M6 — useful points, sprites and lines

Extend the scene schema with point, sprite and line drawables, sharing
geometry, material, transform, visibility and instance parsing. Give draw
records an explicit topology; keep the `mesh` triangle contract intact.
Reuse camera clipping/billboard/ribbon math where appropriate.

Close three finite submilestones in order:

| Submilestone | Selected profile | Acceptance |
|---|---|---|
| M6a | Points with per-point color/size; textured sprites with center anchor and explicit screen/world-size modes | A point cloud with no triangle faces loads/renders; sprite markers face perspective and orthographic cameras; resize at 1x/2x preserves the declared sizing mode. |
| M6b | Continuous and independent line segments; pixel-width ribbons with near-plane clipping and bounded bevel joins | A plotted polyline and segmented wireframe have stable width, clip correctly through the near plane and honor opacity; degenerate segments do not create NaNs. |
| M6c | Shared bounds, basic picking and resource lifetime for these objects | Existing pure ray/triangle helpers are reused for ribbons/sprites; point hits obey the declared radius; removal and context loss release resources. |

Use triangle expansion for portable wide lines instead of assuming driver
support for wide GL lines. Preserve source positions and IDs for picking.
Limit the initial material profile to basic/textured color and explicit
alpha modes. Dashed lines, complex joins and lit particles stay in the backlog.

### M7 — conservative culling and distance-based LOD

**Problem and owner:** `scene3d_snapshot` currently iterates visible scene
meshes for drawing; environment-atlas mip levels are not object LOD. Add a
separate camera-frustum visibility stage using shared bounds/math, then a
distance-based LOD node with ordered distance thresholds and hysteresis.

Static bounds include world and instance transforms. For skinned/morphed or
shader-displaced geometry, use a proven conservative bound or an explicit
authored bound; when neither exists, keep drawing. Do not cull from stale
undeformed bounds. Each shadow/environment view performs its own appropriate
visibility decision, independent of the presentation camera.

**Acceptance:** a repeated-object scene renders the same pixels with fewer
draws when objects leave the camera; an off-camera caster still shadows an
on-camera receiver; an animated character and shader-displaced swarm do not
pop. A three-level object switches at declared distances without oscillation.
Camera movement leaves geometry, programs and simulation ownership intact.
Record release CPU/draw/frame costs and memory against the same scene with
culling disabled, with and without LOD.

**Boundary:** mixed-geometry batching, spatial streaming, occlusion queries and
GPU-driven indirect rendering are separate work. Optimize source hashing or
readback only after a trace identifies them as the current limiting stage.

## 6. Dependencies and proposed decisions

Delivery order is M0 through M7. Technical dependencies are narrower:

| Milestone | Technical prerequisite |
|---|---|
| M1 | M0's reference and resource controls |
| M2 | M1's linear color, output stage and reusable targets |
| M3 | Existing loader/schema; requalify its shaders through M1's output path |
| M4 | M3's per-map representation and M1's color/output contract |
| M5 | M1's scene-color resources, M3's map representation and M4's material model |
| M6 | Existing camera/geometry helpers and a shared topology/material contract |
| M7 | M6's drawable bounds plus the existing instance, animation and simulation profiles |

Keep one milestone active and reassess priority at its boundary instead of
starting parallel feature tails.

Before the affected implementation starts, settle the following uncovered
surface decisions through the existing scene-design process. Recommendations
make this plan concrete; none is an additional formal ruling.

| Decision | Recommended first profile | Needed by |
|---|---|---|
| Pass vocabulary and domains | An ordered postprocessing subtree; explicit scene-linear and display-color domains; no public arbitrary render graph | M2 |
| Transparent bloom | Diagnose it initially; retain transparent ordinary rendering; select a separate glow/coverage contract before extending bloom | M2 |
| Physical material naming | `physical` with an explicit extension support table and zero extension defaults | M4 |
| Sampler pressure | Validate against queried limits; preserve current supported combinations; evaluate packing/atlasing only when a required combination cannot fit | M4 |
| Glass approximation | Shared opaque-color capture plus environment fallback; document nested-glass limitations | M5 |
| Marker and line sizing | Explicit screen/world units; screen pixels are logical viewport pixels, scaled once for output density | M6 |
| Animated/shader bounds | Optional authored conservative bounds; uncullable is the safe default until a bound is known | M7 |

## 7. Ordered follow-on backlog

These are separate finite deliveries after M0–M7, not acceptance conditions
that keep the first delivery open. Reorder only with measured asset/page impact
or a user-selected use case.

| Order | Feature | First useful milestone / acceptance | Main dependency or deliberate limit |
|---|---|---|---|
| F1 | Draco and Basis/KTX2 | Load one Draco-only glTF and ETC1S/UASTC KTX2 examples; validate corrupt/truncated inputs and decoded budgets; preserve core fallback rules | Reuse reviewed, pinned upstream codecs through Lambda-owned adapters. No vendor patches or custom replacement codecs. CPU RGBA fallback must report its real upload/memory cost; native compressed upload is a separately qualified profile. [GLTFLoader](https://threejs.org/docs/pages/GLTFLoader) identifies the Three.js extension comparison. |
| F2 | More morph targets and shader deformation/shadows | A facial rig blends at least eight relative targets; shared deformation code yields matching view/depth poses; custom shaders can opt into an explicit depth/shadow vertex contract | Retain resource limits based on device capacity, not an arbitrary claim of unlimited targets. Share the existing skin palette and deformation math; author-provided shader bounds remain required for culling. |
| F3 | Geometry construction | Cylinder/cone/torus, then planar polygon extrusion with holes and bevel-disabled first profile; arrays have valid indices, normals and UVs and produce reference silhouettes | Extend `scene3d.shape` and reuse triangulation helpers. Curve tubes, bevels, lathe, text meshes and NURBS each get a later bounded profile; do not add new native geometry cases for every shape. |
| F4 | Animated glTF/GLB export | Round-trip one skinned/morphing animated fixture at selected times and embed image bytes in GLB without external-file dependencies | Shared import/export schema, F2 where needed; respect the existing acquisition boundary. No automatic network fetch inside a pure formatter. |
| F5 | Depth effects | First expose resolved depth with explicit near/far/projection conventions; then qualify SSAO and depth of field separately | M1/M2; test orthographic and perspective reconstruction, resize and depth discontinuities. SSR and temporal accumulation remain separate. |
| F6 | Area lights and cascaded shadows | A rectangular emitter lights standard/physical materials; independently, a large outdoor scene preserves near/far shadow detail across cascade transitions | Area lights do not imply area-light shadows. Reuse direct BRDF/target accounting; settle cascade sampler pressure and caster bounds before promising every combination. |
| F7 | Fog, clipping and draw-state controls | Linear/exponential fog; finite world-space clipping planes; then named additive blending and explicit depth settings | Shared shader features across built-in materials and relevant depth passes. Full arbitrary stencil/custom-blend state is a later profile. |
| F8 | Mixed-geometry batching | A repeated-material static scene draws fewer calls with identical visibility/picking results and bounded update cost | M7 measurements first; retain ordinary instancing. No BatchedMesh API clone or unsupported multi-draw dependency. |
| F9 | Video and volume textures | A locally decoded video updates a surface on the existing media clock; independently, a small 3D scalar volume renders a deterministic slice | Reuse media/image acquisition and disposal. Volume texture allocation does not imply a general volume renderer; no second playback loop. |
| F10 | Remaining physical extensions | Qualify anisotropy, then iridescence, then dispersion individually against named assets | M4/M5, tangent/UV correctness and sampler budget; no blanket `MeshPhysicalMaterial` parity claim. |

**Separate architecture projects:** WebGPU/TSL/node materials, a new portable
graphics backend, WebXR and broad multi-platform qualification. Existing
desktop GLSL is not made portable by renaming the backend. A future backend
proposal must cover shader representation, resource formats, host/window
integration, fallback policy and the same lifetime/capture contracts. Three.js
has distinct backend behavior in
[WebGPURenderer](https://threejs.org/docs/pages/WebGPURenderer.html); it is not
an acceptance oracle for the native OpenGL milestones here.

## 8. Verification budget and completion records

### Focused loop and milestone gates

Follow [Radiant's workflow](../../radiant/README.md), the
[test map](../../test/README.md) and
[developer guide §7](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas).
Each acceptance name above describes a proposed behavior case, not a test
already registered or a passing result.

| Work | Checks during implementation | Required qualification boundary |
|---|---|---|
| Pass/GL changes, M1/M2 | Relevant native units plus selected View event cases; existing transparent-composition and simulation controls | `make test-scene3d`, `make test-radiant-baseline`; selected shared WebGL controls when the GL waist changes |
| Schema/loader/material changes, M3/M4/M5 | Package goldens, affected `test_input_model_gtest` cases and named native/reference scenes | Scene3D and Radiant baselines; `make test-lambda-baseline` when package, input, exporter or runtime behavior changes |
| Drawables/bounds, M6/M7 | Geometry/bounds units and selected view/picking/animation cases | Scene3D and Radiant baselines; Lambda baseline for changed package or loader behavior |
| Follow-on modules | The selected milestone's fixture, one causal mutation and its ownership/error control | Relevant module gate wired into the existing lanes, plus the same affected aggregate gates |
| Documentation only | Relative links, current formal IDs, proposed/current status, fenced-example and whitespace checks | No engine build or regression sweep |

Use the existing `test/scene3d/oracle/run.sh` and capture helpers for native vs
Chromium comparisons; pin new addon/asset provenance explicitly. Keep equal
cameras, geometry, light units, exposure, color spaces, raster dimensions and
selected time. Use `CHROME_HEADLESS_SHELL` on macOS. The existing PBR comparison
requires 99% of RGB channel samples within 8/255; keep that gate intact and
declare any new feature's metric before implementation. Add targeted region,
alpha or numeric probes where whole-frame similarity could hide a missing
effect; verify nonempty visible output.

Run targeted checks while editing, the affected aggregate gates once at each
milestone's qualification boundary, and broaden/repeat only for new changes,
failures or unresolved concerns. Record independent failures with evidence;
never weaken assertions, edit references to hide an engine defect, or modify
Test262 harness behavior. A milestone with unresolved attributable failures is
implemented-but-unqualified, not complete.

Every new test script `*.ls` receives its corresponding expected `*.txt`.
Helper modules use the repository's `mod_*`/`_*` convention. Native units,
event inventories and goldens verify behavior/ownership instead of mirroring
implementation details. Use precise `RootFrame`/`Rooted` lifetime tests where
the Lambda seam is exercised, never native-stack scanning.

### Performance and resource budget

Measure only release builds: `make release` in the main checkout, or
`make build-release-compile` in a worktree as documented in the developer
guide. Do not replace a binary while another check uses it.

Reuse M0's named workloads and run paired before/after captures at the same
size/scale with warmup. Report p50/p95, CPU/GPU allocation, draw/pass counts,
program links, target reallocations and readbacks. Include full page
composition when claiming an interactive improvement; GPU draw time alone is
not a page-frame measurement. Effects intentionally add work: report their
incremental cost, not an invented universal FPS target.

Required invariants are one final readback per changed viewport frame, no
draw/readback for an unchanged cached frame, no per-frame program linking or
target allocation at steady size, no camera-only environment/shadow rebuild,
and stable retained bytes after 100 equivalent edit/resize/revert cycles
settle and resources retire. Existing simulation stepping stays tied to the
selected timeline; captures and postprocessing do not add simulation steps.
If a deferred feature cannot keep an invariant, name the changed contract and
qualify it explicitly before calling that feature complete.

### Documentation and handoff

At each closure update the scene design's supported vocabulary, package guide,
affected demo README and test map, with links rather than copied histories.
Keep the implementation checkpoint here short:

| Milestone | Delivered behavior | Open behavior | Verification / evidence | Next action |
|---|---|---|---|---|
| M0 | Untouched release baseline: 44 native units and 88 viewer cases pass; six focused scene controls pass | No stage-level timings beyond existing measurement support | `temp/scene3d-three-features/m0/`; Dreamhome 120-frame p50/p95 70.708/77.920 ms at 732 × 359, with zero projection/environment/shadow/GPU-byte growth | Qualify M1 against this binary and these controls |
| M1 | Multisampled HDR textures, resolved/per-sample target bindings, shared output triangle, per-sample tone/background conversion and after-tone-map transparency | Existing aggregate failures remain; the qualified profile is native macOS OpenGL 4.1 | 47/47 native units; 15 focused viewer cases; all 11 pinned Three.js cases pass the unchanged 99% gate (99.7864–99.9412%); Radiant baseline 6,383 passed / 8 failed / 141 partial / 2 skipped, detailed below | Continue with M2 |
| M2 | Ordered bloom, directional FXAA and custom color passes; retained targets/programs, uniform tracks and template updates | Inherited Lambda/Radiant failures and external Node shutdown limit remain | 50/50 native units including 100 resize cycles and failed-projection recovery; 99/99 Scene3D/render cases; 21 package checks; all 11 pinned Three.js cases pass the unchanged 99% gate | Continue with M3 |
| M3–M7 | None under this plan | Planned feature milestones | No implementation qualification | Continue in order after M2 |

No formal semantics or design ruling changes are required merely to create
this plan. Do not mark a broad feature family complete because one fixture
renders; publish the selected usable profile and keep the ordered residue
visible without extending the active milestone indefinitely.

### M1 qualification record — 2026-10-27

The first output-pass experiment resolved HDR before tone mapping, brightening
MSAA silhouettes and reducing the room comparison to 98.8076% (untouched
99.8703%). The delivered path retains multisample color textures and evaluates
the output shader per sample before the display resolve. The room result is
99.8702%; `HdrOutputToneMapsEachCoveredSampleBeforeDisplayResolve` pins the
cause. [Khronos gl_SampleID](https://registry.khronos.org/OpenGL-Refpages/gl4/html/gl_SampleID.xhtml)
defines the per-sample evaluation used by the native 4.1 profile. Existing
instancing coverage now expects its one instanced draw plus the output draw.

On Apple M4, release `Dreamhome` at 732 × 359 over 120 camera frames measured
p50/p95 **67.758/71.022 ms**, versus **70.708/77.920 ms** before. This paired
sample establishes no observed regression, not a causal speedup. GPU storage
increased from 42,034,806 to 56,772,255 bytes for the HDR target/output resources;
both runs retain zero projection, environment, shadow and GPU-byte growth.
Artifacts: `temp/scene3d-three-features/m0/`, `m1-*.log`,
`dreamhome-orbit-metrics.json`, and `temp/pbr_oracle/` in this worktree.

`make test-radiant-baseline RADIANT_TEST_CONFIG=release_native` preserves these
qualification limits: CSS cascade memory, `dom_browser_global_profile` and six
WPT layout cases (text one, inline one, sizing four). The untouched archived
executable reproduces the browser profile's 8/10 assertions and identical
initial/recascade CSS-role byte counts: jQuery UI 384,514, Linux Mint 671,003 and
B-school 732,116. The layout classification matches the prior PBR record;
layout code is unchanged in this branch. The Radiant View aggregate passes
238 cases with one skip, including the native scene and shared WebGL controls.
The full aggregate is not green, and no assertion, budget or reference was
weakened to hide those failures.

### M2 qualification record — 2026-10-27

The delivered native profile is `lambda.scene3d.postprocessing`: at most eight
ordered effects, linear passes before display passes, opaque-background bloom,
directional FXAA and GLSL color passes that preserve coverage. It reuses the
shader/reflection cache and shared fullscreen draw; one final snapshot publishes
the frame. Numeric tracks use existing playback (S12.1.1v3, D2.11.4), while
targets and programs stay native-owned (D4.5.1v4–D4.5.3v2). Failed projections
roll back newly linked programs and cannot pin unused cache entries.

The native checks include 100 resize/revert cycles, two output densities,
identity effects, unclipped bloom, 40 distinct failed shader reflections and
successful repair. The 20-event view case covers uniform playback, stop restore,
template edits, context recovery and cached selected-frame captures. All 50
native units and all 99 Scene3D/render cases pass. Eleven Three.js comparisons
retain the unchanged 99% gate. The new package example passes; the documentation
runner still reports existing examples at Lambda_Packages lines 711 (E318),
1642 (E201), 1838/1863 (E7801) and 1926 (E7800).

Release Apple M4 Dreamhome at 732 × 359, 120 camera frames per configuration:
no effects p50/p95 **72.159/77.863 ms**; bloom plus FXAA **71.999/77.153 ms**.
The difference is within this run's variability; no causal speedup is claimed.
GPU storage is 56,772,746 versus 68,343,631 bytes. Both configurations retain
zero CPU/GPU-byte, projection, environment and shadow growth. Evidence:
`temp/scene3d-three-features/m2-*.log` and `m2-dreamhome-*.json`.

The release Radiant aggregate reports **6,387 passed / 9 failed / 140 partial /
2 skipped**. Eight failures retain M1's CSS-memory, DOM-profile and WPT-layout
classification. The ninth is an external Node 24.7.0 shutdown deadlock after
the rendering runner prints all 211 eligible baseline cases passing (207 pixel
passes, four expected failures, one skip). A process sample pins the main
thread joining V8 workers while a baseline-compiler worker waits for GC; no
native renderer remains active. The completed child was terminated to let the
aggregate report this harness failure. No harness assertions or vendor code
were changed. Stack evidence: `m2-render-shutdown.sample` beside the milestone
logs. The 239-case Radiant View result has one additional skip.

The Lambda/input aggregate is **8,326/8,327 passing**, including all new
Scene3D package checks and the forced-GC lane. Its sole failure is
`tests_greetings_model`: undo/redo differ from that fixture's golden. The
untouched M0 executable and current executable produce byte-identical output
for that script in this checkout (`m2-greetings-{base,current}.txt`). This is
an inherited limit, not a postprocessing regression. M2 closure is 2026-10-28;
the aggregate gates remain explicitly non-green for the recorded independent
failures. M3 is the next active feature milestone.

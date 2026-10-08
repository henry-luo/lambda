# Native scene3d — Phase I implementation

**Status:** Phase I implemented, 2026-10-08; native/package checks pass. Existing
aggregate-baseline failures and build-profile limitations are recorded below.
Scope and acceptance are
[Radiant_Design_WebGL.md §§4–9 and Appendix A](../radiant/Radiant_Design_WebGL.md).

The explicit `lambda.scene3d` module constructs ordinary elements. Pure helpers
follow S12.1.1v2 and D7.2.1/D7.2.4. Radiant owns rendering behind the DOM boundary
(D7.5.3); native projections copy values and use generation-checked DOM identities
(D4.5.1v4/D4.5.2). No borrowed GC value is retained.

Delivery sequence:

1. Native GL waist: explicit core contexts, branded resources, shader services,
   FBOs, linear-light shading, normalized immutable snapshots, quotas and teardown.
2. Native scene projection: hierarchy, camera, geometry, materials, textures,
   lights, shared resources, instancing, validation and invalidation.
3. Shared SVG sizing/fitting and retained page presentation.
4. Source package, Lambda goldens, native pixel/lifecycle/page corpus and gates.

The full host's existing `LAMBDA_HEADLESS_GLFW_WINDOW=1` switch explicitly enables
hidden GLFW infrastructure for rendered tests. Ordinary headless execution and
profile C remain windowless (D7.1.4v2). Runtime-only and WASM acquire no GL dependency.

The worktree branch is `codex/scene3d-phase1`, following checkpoint
`62f3c21d2`. Validation logs and matched measurements are under
`temp/scene3d/` in that worktree; these generated artifacts are not runtime assets.

## Implemented behavior

`lmd/package/scene3d.ls` is the explicit `lambda.scene3d` entry (D7.2.4,
S16.9.6/S16.9.8). Constructors return ordinary elements; `normalize`/`validate`
reject malformed descriptions, duplicate/missing references and invalid cameras,
geometry, transforms and instances. Transform helpers use column-major matrices,
right-handed Y-up coordinates, Euler XYZ radians and camera FOV degrees.

`radiant/scene3d.cpp` copies DOM attributes and numeric arrays into a document-owned
projection (D4.5.2). Basic/Lambert materials support sRGB colors, ambient/directional
lights, local decoded RGBA textures, indexed/non-indexed buffer geometry, boxes,
planes, parent transforms, visibility and scene-local geometry/material/texture IDs.
Opaque instances use actual instanced GL draws, batched by winding; transparent
instances join the global back-to-front mesh pass. The renderer uses inverse
transpose normals and premultiplied linear-light blending, then publishes a
top-down premultiplied sRGB `ImageSurface`.

`radiant/gl_core.*` owns isolated explicitly requested OpenGL 4.1 core contexts on
macOS, loads entry points, uses the desktop driver compiler/linker, reflects uniforms,
and brands resources and uniform locations by generation. It validates uploaded
attribute/index/instance ranges and stale dependencies before draws. Four-sample
color/depth targets resolve into initialized snapshots; internal readback restores
read/draw FBOs, pixel-pack parameters and pack-buffer binding. A current-context
scope restores the shell context after each graphics operation. Other desktops
request GL 3.3 core but remain unverified by this implementation record.

The shared SVG path supplies natural dimensions, ratio, CSS sizing and viewBox
fitting, including numeric Lambda presentation attributes. CSS display overrides
preserve replaced internals. Media content paints inside its box's transform,
clipping and opacity scope. Inline scene viewports use the same media dispatcher.
Immutable snapshots carry display-list leases; replacement/removal invalidates
the old generation while retaining pixels until the last consumer releases them
(D4.5.1v4). Projection changes are prepared before retained ancestor-fragment reuse.

Repeated UI lifetimes also exposed a pre-existing tile-pool lifecycle defect:
shutdown destroyed the pool but a once-only initializer prevented recreation.
Pool creation/dispatch/shutdown now share a lock and recreate workers as needed.

The Ringworld example additionally exposed an attribute-override lifetime bug:
`fn_map_set` retained newly added UI attributes but skipped retention when
replacing an existing field. Camera options replaced the default vectors, then
procedural geometry allocation collected the replacements before validation.
Overrides now use the same `ui_prepare_element_field` helper as initial stores
(D4.5.2/D5.3.3). The forced, poisoning-GC UI regression checks both a replacement
vector and computed replacement text; it fails with the previous host and passes
with the fix.
Follow-up validation passes all 6,405 Lambda/input baseline checks, the 22 native
scene tests, and the forced-GC UI regression on `auto`, `interp` and `jit`.

## Bounds and unavailable profiles

Initial limits are 8 viewports per document, 32 contexts per process, 4,096 GPU
resource records and 128 MiB accounted resource bytes per context, 4,096 pixels
per target/texture axis, 8,192 scene nodes/draw records, hierarchy depth 64,
262,144 geometry vertices, 16,384 instances per mesh and eight directional lights.
Buffer accounting includes a CPU upload copy used for exact index-range checks.
Target accounting includes multisample color/depth and resolved color storage;
its byte limit can reject dimensions below the per-axis limit. Texture decode
checks dimensions before allocating the full decoded image. Readback and native
projection storage have corresponding dimension/component bounds. Published images
held by display lists follow the existing display-list/cache lifetimes.

Missing graphics returns null with a diagnostic; it never substitutes a blank
successful snapshot. `make build-headless` succeeds, and `otool -L`/`nm -u` show
no GLFW/OpenGL dependencies or imports. The package construction golden also
passes under `lambda-headless.exe` (D7.1.4v2). The core additionally compiles
alone with `LAMBDA_NO_GUI`. No ANGLE or software renderer is bundled.
Page export and JS WebGL/Three.js remain the separately scoped follow-on phases.

## Public package and schema

Import `lambda.scene3d` explicitly (S16.9.6/S16.9.8). The entry exports `Scene`,
`Vector3`, `Matrix4`; `scene`, `group`, `camera`, `light`, `box`, `plane`,
`geometry`, `material`, `texture`, `mesh`, `resources`; `normalize`, `validate`;
and pure `transform`, `multiply`, `point` helpers (D7.2.4/S12.1.1v2).
See [the mixed Lambda page](../../test/scene3d/mixed.ls) for executable syntax.
The [Ringworld demo](../../test/demo/scene3d/ringworld.ls) provides a larger
visual example: procedural banded sphere and annular geometry, three moons,
directional lighting, shared resources and 160 instanced stars. Open it with
`./lambda.exe view test/demo/scene3d/ringworld.ls` from this worktree.
Constructors preserve ordinary element attributes and accept an options map;
normalization returns an element or an error, and validation returns true or
an error. Renderer validation also applies to literal scenes without the package.

Object position/rotation/scale and camera target/up have three numeric components.
Primitive size is `[width, height, depth]` (plane ignores depth). Buffer geometry
accepts flat positions, optional normals/colors with three components per vertex,
UVs with two components per vertex, and optional triangle indices. Missing normals
are derived from triangle faces. Colors are sRGB hex (or `transparent`); vertex
color components are sRGB values in `[0,1]`. Instance data is an array of
nonsingular affine column-major 16-component matrices. The native HTML attribute
path additionally accepts whitespace/comma-separated numeric lists.

IDs are nonempty strings/symbols shorter than 128 bytes, unique within a viewport.
`resources` contains geometry/material/texture definitions. Mesh `geometry` and
`material` attributes refer to those IDs, or the mesh has one inline child of
each kind. Material `texture` refers to a texture ID or one inline texture child.
The root `camera` selects a camera ID; absent selection uses the first camera.
Camera `target` selects look-at orientation; without it, Euler orientation looks
along local -Z. Material `side` is `front`, `back`, or `double`; alpha/opacity,
texture alpha and explicit `transparent` select ordered transparent painting.

## Focused evidence

`./test/test_scene3d_gtest.exe --gtest_output=xml:temp/scene3d/native-results.xml`
passes 22/22 on Apple M4, OpenGL `4.1 Metal - 90.5`, GLSL `4.10`. Pixel checks use
3–4 channel-value tolerances and analytic linear/sRGB expectations. They cover
real cube/texture/light/instance pixels, transparency over SVG, MSAA edge coverage,
CSS composition (including Canvas 2D), SVG sizing parity, resize/density, updates/removal, old snapshot
leases, isolated contexts, loss/rebuild, rejected uploads, driver compile/link
failures, reflection, excluded-provider behavior and moving GC. The mixed Lambda
page uses the public source package and typed attributes, without JS bootstrap.

`test/lambda/scene3d/` contains construction/type, normalization/invalid input,
shared resources and transform goldens. `test/scene3d/mixed.ls` has its own text
golden and an additional native page-pixel assertion. The native runner is part
of `make test-radiant-baseline`, and `make test-scene3d` is its standalone gate
(D7.3.5).

## Aggregate validation and existing failures

| Check | Recorded result |
|---|---|
| `make test-lambda-baseline` | Pass: 4,301/4,301 runtime checks plus 2,104/2,104 input checks. |
| Focused package runner (`AutoDiscovered/*scene3d*`) | Pass: 4/4 goldens, repeated with the final optimized host. |
| Native scene runner | Pass: 22/22, including moving GC, resource/snapshot lifetimes, driver pixels and final invalid-input cases. |
| `make test-radiant-baseline` with debug host | 4,088 passed, 350 recorded partial passes, 2 failed: Bootstrap page harness timeout and existing CSS memory budget failures. |
| `make test-radiant-baseline` with optimized host | All layout gates, page snapshot, 386 UI fixtures, DOM UI, visual baseline and 109/109 page loads pass. Overall 4,084 passed, 350 recorded partial passes, 6 failed: CSS memory and five stripped-log assertions. |
| `node test/test_run.js --target=radiant --category=baseline` | 567/571 checks pass; three missing test binaries from existing link failures and the same CSS memory failure. Native scene, vector, layout, view reuse, animation, media and PDF writer checks pass. |
| `make build-headless` | Pass; no GLFW/OpenGL linkage. |
| `make build-release-compile` | Pass; host-export audit passes. |
| `make lint ARGS='--rule ^no-int-cast-radiant$'`; `git diff --check` | Pass. |

The CSS-role live-byte failures are identical in three runs of the preserved
pre-change release executable (`7bf75a0a466277b7b1c9ef38b848cea3f0ff5241`):
jqueryui 380,794 bytes versus a 373,940-byte budget, linuxmint 677,194 versus
667,901, and bschool 734,028 versus 731,361, both initially and after recascade.
No memory budget or golden was raised. The debug Bootstrap timeout is reported
as a harness result; the optimized aggregate loads all 109 pages successfully.

Five release view-command checks require `log_info` strings that `NDEBUG`
removes. Those strings are absent from both pre-change and new release binaries;
the debug view-command run passes 78/78. The affected checks are
`StaticHeadlessViewClosesRecursivePostLoadTimers`,
`ArchivesAndDirectoriesOpenAsFileTrees`, `SkipsDefaultCumulativeBrowserScriptBudget`,
`SkipsExternalScriptResponseThatIsHtml`, and
`ExecutesExternalDependencyInStaticHeadlessView`.

The isolated `test_retained_display_list_gtest` and `test_state_store_gtest`
targets already lack `view_get_layout_position`; the latter also lacks
`radiant_input_peek_live_value`. `test_display_list_gtest` already lacks
`constrain_corner_radii` and `resolve_border_radius_percentages`. Their source
references and target dependency lists predate this work. No substitute stubs
were added. These failures keep the broader Radiant gate red and are separate
from the implemented Phase I functionality.

## Matched release and resource measurements

Both executable builds use arm64 Apple clang 17.0.0 (`clang-1700.6.3.2`),
`make build-release-compile`, C++17, `-O3 -DNDEBUG -flto=thin`, function/data
sections, hidden visibility, `-march=native`, deployment minimum macOS 15.7,
dead stripping, the existing host-export list and post-link `strip -x`.
The enabled native API is the bounded buffer/VAO/texture/program/uniform,
MSAA color/depth target, indexed/non-indexed instanced draw and snapshot waist
in `gl_core.hpp`; shader sources are built-in desktop GLSL 330. No JS WebGL
surface, GLSL translator or additional GPU library ships.

| Artifact | Before | Phase I | Delta |
|---|---:|---:|---:|
| Release executable | 19,533,400 B | 19,583,016 B | +49,616 B |
| Executable, gzip level 9 | 8,866,608 B | 8,894,450 B | +27,842 B |
| Runtime USTAR+gzip | 11,452,586 B | 11,481,665 B | +29,079 B |

The reproducible runtime bundle consists of the executable plus the entire
tracked `lmd/` tree (333 files before, 336 after), with normalized USTAR metadata
and gzip level 9/mtime zero. Developer documentation and demo/test samples are
excluded from this runtime comparison. The only added runtime files are
`lmd/package/scene3d.ls`, `lmd/package/scene3d/model.ls`, and
`lmd/package/scene3d/transform.ls`; shaders are embedded in the executable.
`temp/scene3d/release-measurement.json` records the complete runtime manifest,
flags and executable hashes.

For the tested 128×128 indexed cube, native counters report 592,293 accounted
GPU bytes (including program-source accounting), 2,494,776 core CPU bytes
(context/resource table and upload shadows), 1,776 live projection bytes in
3,072 reserved pool bytes, and a 65,536-byte current snapshot. These are separate
owned/allocation counters, not a measurement of driver-private memory or the
existing document asset cache. Retained consumers can additionally hold prior
snapshots until their display lists release them. An unchanged second frame
reuses the snapshot and performs no additional GL frame/readback.

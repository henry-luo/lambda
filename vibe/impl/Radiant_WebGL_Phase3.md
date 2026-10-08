# Radiant WebGL Phase III — implementation record

**Status:** implemented for the selected macOS profile in `codex/webgl-phase3`, based on merged master
`3a1a4d3c7`. Scope is the user-selected [Phase III design](../radiant/Radiant_Design_WebGL.md#phase-iii--richer-interactive-scenes-and-shared-svg3d-animation).

## Contracts

The existing SVG animation engine supplies shared sampling and playback
mechanisms. SVG keeps its SMIL policy; native scenes and the declared Three.js
bridge apply clip/action/mixer policy above the same evaluator. RC1 keeps
sampling and binding updates on the page thread; D7.4.2v2 keeps events on the
existing task loop. Host interfaces and resource identities follow
D7.4.1v2/D7.4.4, retained JS edges follow D4.5.2/D5.3.3, and published images
keep the D4.5.1v4 snapshot ownership contract. Lambda helpers produce pure
clip/track data under S12.1.1v2.

## Work and evidence

| Area | Implementation and validation |
|---|---|
| Shared SVG/3D sampling | Shared keyframe lookup, numeric composition and typed sampler serve SVG, CSS quaternion interpolation and native/JS scene adapters. All 22 pinned typed-track oracle cases pass. |
| Clip/action/mixer policy | Six pinned operation/event scenarios cover seeks, reverse/repeat/ping-pong, scheduling, fades, warped crossfades, additive mixing and base restoration. |
| Native/JS bindings | Native transform/material pixels, GPU bone/morph deformation, automatic sampling before rAF, forced GC, detachment, exclusive ownership, callback disposal and repeated uncache pass. |
| Texture loading and formats | Independent native/browser fixtures pass 37 image/canvas upload assertions and 11 float/depth rendering assertions, alongside the retained 49 API and 100 selected Khronos assertions. |
| Rich scene | Unmodified RoomEnvironment/PMREM, MeshStandardMaterial, PCF shadows and RenderPass/FXAA/OutputPass run with native animation. Five fixed-time/feature-toggle browser comparisons pass. |
| Input | Unmodified OrbitControls responds to native drag/wheel/capture/cancel; satellite raycasts pass with CSS translate/scale, resized display size and 1x/2x density. |
| Lifecycle | Loss during a warped crossfade preserves mixer time and pose. Three complete restorations, independent canvases, resize/density and disposal preserve bounded resources. |
| Focused gates | 42 native scene/WebGL tests, 96 vector tests and 192 view/resource tests pass. |
| SVG regression | 32 controlled-time fixtures × two densities × two cache modes: 128 runs, 3,864 assertions, all pass; includes `<use>`, external SVG, images, events, timing and interpolation. |
| Lambda/JS/input baseline | `make test-lambda-baseline`: 6,419/6,419 pass, including 2,104 input and 4,315 runtime checks. |
| Radiant aggregate | 4,094 pass, 350 partially passing, one pre-existing CSS-memory failure; details below. |
| Jube loader | Rejection/cleanup matrix passes; the local compiler requires `CPLUS_INCLUDE_PATH=/opt/homebrew/opt/mpdecimal/include`. |
| Test262 | `make test262-baseline`: 40,261/40,261 pass, zero regressions. |
| Release | 144-frame measurement, owned storage/cache bounds, recovery and final binding/GC checks pass; measurements below. |

The independent Chromium 154/ANGLE SwiftShader reference uses upstream
`AnimationMixer`; Radiant uses the native bridge. The audited manifest records
94 distinct selected GL calls and 30 generated shader sources. References,
hashes and comparisons are checked in under
[`test/demo/scene3d/reference/phase3`](../../test/demo/scene3d/reference/phase3/).
The declared threshold is at least 99% of RGB channel samples within 8 levels
on the 800×500 canvas. All five comparisons exceed 99.83%:

| Pose / feature | Channel samples within 8 levels |
|---|---:|
| 0 seconds | 99.8367% |
| 1.25 seconds | 99.8435% |
| 3.5 seconds | 99.8442% |
| Shadows disabled | 99.8502% |
| FXAA disabled | 99.9020% |

The aggregate Radiant gate retains one CSS memory failure on jqueryui,
linuxmint and bschool in this worktree. A clean `3a1a4d3c7` build reproduces all
three. Running that base binary from the same working directory gives exactly
the same jqueryui CSS total, 380,794 bytes versus the old 373,940-byte budget.
The path-sensitive baseline is unchanged; no test or budget was relaxed.
All other aggregate layout, UI, DOM, vector, view and rendering suites pass.

The bridge is first-party `NativeAnimationMixer` around the unmodified pinned objects. The browser uses upstream `AnimationMixer` as its independent oracle. The host interface owns copied track storage and precisely rooted callbacks in document resources (D7.4.1v2/D7.4.4, D4.5.2/D5.3.3); wrappers carry numeric identities. No borrowed JS array survives a call. The Jube interface catalog now grows through the existing `ArrayList`, because registering the added optional interface exposed its former fixed 64-record limit.

## Release measurements

Apple M4, arm64, macOS 26.3, native OpenGL `4.1 Metal - 90.5` / GLSL 4.10.
Build with `make build-release-compile` and the `release_native` test runner;
never use `make release` in a worktree (Developer Guide §7.2).
The 144-frame capture advances the native mixer by 1/60 second, renders the
800×500 composer, reads back its snapshot and composes the page. The JS
callback is compiled once; frame timing excludes per-frame eval parsing.
Sampling includes the native evaluator and JS property-binding callbacks.

| Measurement | Median | 95th percentile |
|---|---:|---:|
| Animation sampling / bindings | 3.393 ms | 3.876 ms |
| Full PBR frame / composition | 307.676 ms | 342.583 ms |

This rich Three.js demo currently renders at about 3 FPS on the measured host;
rendering dominates sampling. The measurements establish the current cost,
not a 60 FPS guarantee or display/vsync pacing. Renderer/JS optimization remains
future work without changing the selected feature behavior.

Owned GL CPU bytes stay at 2,962,416 and GPU bytes at 46,927,896 across the
capture and three complete restorations (70 resources before/after restoration).
Full factory disposal leaves zero GPU bytes/resources and 2,821,864 bytes of
service metadata until document/context teardown. Counts exclude driver-private
storage and process RSS. The image-paint cache reaches its 128-entry ceiling;
combined vector-cache entries grow from 7 to 130, including two static paths,
and reported metadata grows from 656 to 6,560 bytes. Those metadata figures
exclude ThorVG image payloads; the entry cap bounds retained image paints.

The checked-in [validation record](../../test/demo/scene3d/reference/phase3/validation.json)
contains raw measured properties, hardware, host hash, gate counts and the
baseline exception. [The test guide](../../test/webgl/README.md) gives reproduction
commands. This evidence defines the selected profile, not general browser or
Three.js conformance.

## Declared Phase III profile

The selected binding surface follows the Phase III design and D7.4.1v2/D7.4.4.
`NativeAnimationMixer` takes one Three.js root and one owning canvas; separate
roots use separate mixers. The adapter uses the pinned `PropertyBinding`, copies
tracks once and retains callback closures through precise `RootVector` homes
(D4.5.2/D5.3.3). Binding ownership is exclusive until uncache/dispose, including whole-property/indexed overlap and resolved named/numeric morph aliases. Native and
upstream mixers must not drive the same property. `setAutomatic(true)` attaches
the document scheduler; explicit update then rejects. `setTime(seconds)` stops
automatic advancement and samples exactly. Native samples precede rAF drawing
at its host timestamp. Finished, paused or zero-scale playback parks its driver.

| Binding | JS bridge | Native scene |
|---|---|---|
| TRS | position/scale vector3, quaternion4; indexed numeric components | `id.position`, `id.scale`, `id.quaternion` |
| Discrete | visible boolean, name string | `id.visible`, `id.name` |
| Materials/lights | color/emissive color3; opacity/metalness/roughness/intensity number | material/light color, material opacity, light intensity; `mesh.material.color/opacity` resolves inline or referenced material |
| Camera | fov/near/far/aspect number; updates projection matrix | corresponding camera ID properties |
| Bones/morph | pinned PropertyBinding bone paths and morphTargetInfluences, up to 16 components | bone TRS, mesh `morph-weights` vector1/2 |

Tracks use float32 key/value input, copied to native double storage. Number,
vector and color tracks accept discrete, linear, pinned smooth and Bezier
sampling. Quaternion tracks accept discrete/linear spherical interpolation;
boolean/string tracks are discrete. Strings are bounded to 255 UTF-8 bytes and
contain no NUL. Unresolved/type-mismatched bindings, nonfinite values, unordered
keys, zero quaternions and malformed tangent arrays reject before playback.
Bezier tangents are absolute time/value pairs, distinct from SMIL timing easing.
The shared sampler also serves SVG keyframe lookup/composition and CSS quaternion
interpolation. SVG retains its own interval/sandwich/event policy.

The core supports 256 actions, 1,024 properties and 256 tracks per clip. The JS
adapter admits 32,768 keys per track and 16 MiB of copied track storage per host;
64 document-owned hosts can coexist. Native scenes admit 256 clips, a combined
1,048,576 numeric entries and 8 MiB of discrete text. These are rejection limits,
not silently truncated data. Native skinning supports 16 DFS-ordered bones,
four normalized influences per vertex and two relative morph targets with
optional normal deltas; skinned instancing rejects. GPU palettes/weights update
without reallocating unchanged geometry.

Core loop/finished traces follow the pinned order and payloads. Binding writes
commit before bridge event delivery; mutation of the same host during a native
callback rejects, while disposal defers safely until the callback unwinds.
Native `<scene3d>` emits `loop`/`finished` through the existing timing task queue
(D7.4.2v2), carrying clip/action, loopDelta/direction and mixer time. Removing a
JS object from its root disposes affected playback; DOM target generations and
connectivity validate each native access. Root replacement/teardown retire
resources. Authored scene data remains untouched; inactive overlays expose
current authored values and reactivation captures a fresh base.

HTML image sources retain intrinsic decode handles independently of layout
props. Source replacement clears old intrinsic state and queues load/error with
generation-checked DOM pins; image completion batches grow instead of dropping
requests after 16 entries. DOM image/canvas uploads copy RGBA into the declared
format for the duration of the call, honor source skips/rectangles, flip and
premultiply, and restore client unpack state. Selected float/depth formats enter
the profile with the `formats.html` render/read/sample checks; advertised float
extensions require actual native renderability probes.

The observatory factory owns its canvas context. Full disposal releases scene,
composer/environment/shadow resources, playback and listeners, then loses that
owned context to release renderer-internal fallback textures as well. Restoration
rebuilds GPU-derived PMREM/composer/shadow targets from retained scene data while
keeping the current CPU animation pose. Native pointer capture uses pending and
current generation-checked targets for the selected single native pointer stream;
input uses [Pointer Events 3 §4.1.3.2](https://www.w3.org/TR/pointerevents3/#process-pending-pointer-capture).
Multitouch IDs and arbitrary rotated/skewed picking surfaces are outside this
selected input fixture; translate/scale, CSS sizing and density are covered.

## Additional root-cause fixes

- Document population preserves animation state created by load-time scripts;
  resetting the alias left live drivers without their scheduler.
- Raster image-paint cache keys include the generation-checked surface identity.
  Pixel-buffer addresses and local generation 1 can recur across distinct
  immutable snapshots; address-only caching replayed earlier animated poses.
- The common scheduler defers freeing entries removed during its sampling walk,
  including self-cancellation and cancellation of a subsequent driver.

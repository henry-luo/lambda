# Radiant 3D and WebGL Design — Lambda Scenes First

**Status:** revised design proposal. On 2026-10-08 the user selected a
Lambda-element scene package and native Radiant rendering for Phase I, with
the JS WebGL wrapper and Three.js execution deferred to Phase II. First-party
system OpenGL, ANGLE as reference only, and desktop GLSL acceptance remain
selected. The user also selected `<scene3d>` and SVG-style HTML sizing through
`viewBox`. The package spelling and detailed 3D mapping below remain
recommendations; this document does not implement the package or renderer.

**Date:** 2026-10-08

**Scope:** Phase I provides a proposed `lambda.scene3d` package and
`<scene3d>` element, embedded alongside HTML/SVG and rendered by Radiant over
system OpenGL. Phase II adds the JS WebGL API around the shared graphics core
and validates Three.js. A Three.js-like scene model is the Phase I vocabulary,
not a dependency on Three.js or a promise of its complete feature set.

**Authority:** [documentation convention](../../doc/Doc_Convention.md).
This proposal applies existing formal contracts without revising them. It
supersedes the analysis formerly at `vibe/idea/WebGL.md`.
Superseded decisions from the original ANGLE-based proposal are retained in
Appendix S. The file remains at the originally requested `vibe/radaint/` path.

## 1. Direction

Implement a declarative Lambda scene model and a native Radiant 3D renderer
first. Radiant owns scene traversal, transforms, camera projection, geometry,
materials, lighting, GPU resources, draw ordering, and page presentation.
Execute graphics with system desktop OpenGL. ANGLE is an
engineering reference for implementation choices and edge cases; do not link,
load, or ship ANGLE, its standalone translator, `libEGL`, or `libGLESv2`.
Consult its context/state/resource handling and shader adaptation as reference;
the applicable API specifications and this desktop profile define our contract.
[ANGLE source](https://chromium.googlesource.com/angle/angle/+/main/)

A scene viewport owns an offscreen drawing buffer; Radiant composites its
completed image in normal document paint order. Lambda elements are the
authoritative declarative scene description; native scene records are a
derived rendering projection. System OpenGL compiles and links shaders and
executes GPU work. Neither a GPU driver nor a GPU compiler is being recreated.

**Phase I is tested through Lambda scenes and native rendered output.** It
does not require JS, `canvas.getContext("webgl2")`, or loading Three.js.
Model a bounded set of Three.js concepts with ordinary Lambda elements.

**Phase II adds the JS WebGL adapter and Three.js workload.** Its modern
`WebGLRenderer` requires WebGL2; WebGL1 support was removed in r163. Audit and
pin that later workload before defining the adapter's complete startup/API
manifest. Phase I scene success does not imply JS/WebGL compatibility.
[Three.js renderer documentation](https://threejs.org/docs/pages/WebGLRenderer.html)

**Desktop GLSL acceptance is intentional.** Accept native desktop shaders that
the active OpenGL context accepts, including constructs outside browser
WebGL's shader restrictions. Phase I uses native desktop shaders for built-in
materials; GLSL ES source adaptation belongs to the Phase II JS adapter.
Delegate shader parsing and semantic validation to the driver.
Browser-equivalent GLSL ES validation and full WebGL conformance are
outside the initial contract; the compatibility difference is documented and
tested explicitly rather than treated as an implementation bug.

## 2. Formal contracts

| Authority | Ruling applied here |
|---|---|
| **D7.2.1 / D7.2.3 / D7.2.4** | The scene helper package is a source-distributed Lambda module under the reserved `lambda.*` root; graphics resources belong to the renderer, not module-scope mutable state. |
| **S12.1.1v2** | Pure scene construction/normalization stays in `fn`; rendering effects belong to Radiant's host lifecycle. |
| **S16.9.6 / S16.9.8** | Use ordinary dotted package paths and existing resolution roots; no new import syntax or implicit directory entry point. |
| **D7.4.1v2** | "raw C pointers never become script-visible"; native resources use opaque resource IDs and host projections. |
| **D7.4.4** | "Declared interfaces + record-owned hooks are the ONLY host-object protocol"; WebGL contexts and objects use that protocol. |
| **D7.5.3** | "Lambda reaches Radiant only through the `radiant-dom` module"; graphics stays behind the DOM host boundary. |
| **D4.5.1v4** | Radiant has native lifetime ownership; its seam is "pin, gen-check, copy-as-value". |
| **D4.5.2** | "Radiant never retains a GC pointer"; keep a script value only by copying it or holding a registered root. |
| **D5.3.3** | Native helpers use "slot-backed `RootFrame` / `Rooted<T>`" and continuously rooted handoffs. |
| **D7.4.2v2** | Modules are "fully shielded from the async substrate"; completion and event delivery use existing host facilities. |
| **D7.3.5** | "Every module kind names its conformance gate, wired into CI"; Phase I uses package/native-rendering corpora, and Phase II adds applicable web-platform slices. |
| **D7.1.4v2** | Headless profile C retains its null windowing backend and existing dependency exclusions. |
| **D7.1.7v6** | `lambda-wasm` remains the browser evaluation profile; this native graphics proposal does not expand it. |

The rulings above are in [Lambda Formal Design](../../doc/Lambda_Formal_Design.md).
The S-numbered rulings are in
[Lambda Formal Semantics](../../doc/Lambda_Formal_Semantics.md).
They prescribe ownership, host boundaries, scheduling, and build profiles;
none selects ANGLE or mandates a shader translator. The user's revised
provider and GLSL decisions therefore do not revise a formal ruling.
Where the formal spec does not prescribe rendering/thread coordinates, use
existing **RC1** (one page thread for script, style, layout, and display-list
construction) and **RSC1/RSC6/RSC7/RSC8** (logical CSS geometry, explicit raster
conversion, distinct density/zoom, centralized coordinate conversions).
See [concurrency](../radiant/Radiant_Design_Concurrency.md) and
[scale](../radiant/Radiant_Scale.md). No new ruling-ID series is introduced.

## 3. Current foundation and architectural boundary

The existing [Canvas 2D design](../radiant/Radiant_Design_Canvas.md) provides a
document-owned canvas registry, bitmap allocation, native drawing state,
paint invalidation, and raster canvas painting. The current `getContext()`
dispatcher admits only `"2d"`; other names return `null`.

Reuse document-owned graphics lifetimes, snapshot publication, and image
painting from the canvas foundation. Add a native scene viewport with its own
scene/state payload. A scene is not implemented by requiring a hidden JS
canvas. Its GPU drawing buffer and scene resources are native; an
`ImageSurface` is a published snapshot, not the authoritative scene description.
Extract shared surface/presentation helpers instead of copying Canvas 2D code.

```mermaid
flowchart TD
    LAMBDA[Lambda elements and scene3d package: Phase I] --> SCENE[Radiant native scene renderer]
    SCENE --> CORE[Shared native GL resources and commands]
    THREE[Three.js in LambdaJS: Phase II] -.-> WEBGL[JS WebGL adapter and ES source normalization]
    WEBGL -.-> CORE
    CORE --> GL[System desktop OpenGL]
    GL --> GPU[Offscreen drawing buffer]
    GPU --> SNAP[Resolved ImageSurface snapshot]
    SNAP --> PAINT[Radiant image painting and CSS composition]
    SNAP --> EXPORT[Raster image embedded in export]
    GPU -. future shared texture .-> COMPOSITOR[Compatible GPU compositor]
```

The retired idea note's directions remain distinct from the new scene path:

| Direction | Relationship to this proposal |
|---|---|
| Declarative Lambda 3D mixed with HTML/SVG | Phase I: scene elements → Radiant scene renderer → shared native GL core. |
| JavaScript WebGL in native `<canvas>` | Phase II: JS adapter → shared native GL core; Three.js owns the scene on that path. |
| GPU acceleration of Radiant's own 2D page painter | Independent optimization behind `RdtVector`; not a prerequisite for native 3D. |
| Radiant running in a browser through WASM | Separate future proposal; browser-provided WebGL is a different host boundary, and current D7.1.7v6 does not include Radiant. |

Existing GLFW infrastructure supplies context creation and platform function
loading. Its current shell context is not automatically the required modern
core context, and desktop GL does not supply a scene renderer or DOM/WebGL API.
Create an explicitly configured graphics context and render to framebuffer
objects, preserving normal page stacking and clipping. Hidden provider windows
are context infrastructure, not native child-window overlays in the page.

## 4. Lambda package and scene elements

### Selected viewport name and recommended package

**`<scene3d>` is the selected embedded viewport name.** It identifies the 3D
scene when mixed with HTML and SVG. Its HTML sizing follows SVG viewport and
`viewBox` behavior (§7). The earlier tag alternatives are retained in Appendix S.

Recommend **`lambda.scene3d`** for the general 3D graphics package; that package
spelling remains proposed independently of the selected element name.

The earlier `import graphics: lambda.3d` suggestion fails current parsing, while
`lambda.scene3d` uses the existing dotted-name syntax (S16.9.6/S16.9.8).
This recommends an ordinary name, not a grammar exception. Under D7.2.4 this
is a general graphics package beneath `lambda.<package>`; document-format
adapters may consume it without changing the reserved namespace hierarchy.

Provide an explicit public `lmd/package/scene3d.ls` entry module, with helpers
under `lmd/package/scene3d/`, following the existing `lambda.slide` pattern.
There is no implicit directory index. The package provides scene types,
constructors, reusable geometry/material descriptions, validation, and pure
transform helpers. GPU creation and drawing remain in Radiant; package
functions produce data and do not hide effectful GL calls inside `fn`
(D7.2.1/D7.2.3, S12.1.1v2).

### Element vocabulary and embedding

The root is a native Radiant visual element, usable directly inside an HTML
element tree like inline SVG. A literal scene does not need a JS bootstrap,
an imperative render call, or an imported package solely to make its tag
recognizable. Importing the package supplies the construction/type helpers.
Inside the scene root, children describe 3D objects and resources rather than
ordinary HTML layout boxes. This is a renderer vocabulary boundary, not new
Lambda syntax. The following data example parses today; native scene rendering
is proposed work.

```lambda
<div class: "preview",
    <h2 "Native 3D preview">
    <scene3d width: 640, height: 360, viewBox: "0 0 640 360",
        preserveAspectRatio: "xMidYMid meet", camera: "main", background: "#20242b",
        <camera id: "main", type: 'perspective', fov: 50,
            near: 0.1, far: 100.0, position: [0.0, 0.0, 4.0],
            target: [0.0, 0.0, 0.0]>
        <light type: 'ambient', color: "#ffffff", intensity: 0.3>
        <light type: 'directional', color: "#ffffff", intensity: 0.8,
            position: [3.0, 4.0, 5.0], target: [0.0, 0.0, 0.0]>
        <group rotation: [0.0, 0.5, 0.0],
            <mesh id: "cube",
                <geometry type: 'box', size: [1.0, 1.0, 1.0]>
                <material type: 'lambert', color: "#4c8bf5">
            >
        >
    >
    <svg width: 32, height: 32,
        <circle cx: 16, cy: 16, r: 12, fill: "#4c8bf5">
    >
    <p "HTML, a native 3D viewport, and SVG share one document.">
>
```

### Bounded Three.js-like model

Three.js's `Scene`/`Object3D` hierarchy and its separation of mesh geometry and
material are conceptual references. Phase I implements the equivalent selected
concepts natively, without instantiating Three.js objects or reproducing every
class and option.
[Scene](https://threejs.org/docs/pages/Scene.html),
[Object3D](https://threejs.org/docs/pages/Object3D.html),
[Mesh](https://threejs.org/docs/pages/Mesh.html)

| Lambda description | Native meaning / Three.js concept |
|---|---|
| `<scene3d>` | Scene plus embedded viewport; SVG-style width/height, `viewBox`/`preserveAspectRatio`, background, active camera ID, and a 3D subtree. |
| `<group>` | Hierarchy and inherited transforms, corresponding to Group/Object3D. |
| `<camera type:'perspective'>` | Perspective camera; position/target or orientation, FOV, near/far planes. Orthographic cameras can extend the same shape. |
| `<light type:'ambient'/'directional'>` | Initial light types, color/intensity, and direction/target where applicable. |
| `<mesh>` | Renderable object: transform, visibility, geometry, material, and optional instance data. |
| `<geometry>` | Box/plane primitives and indexed buffer geometry with positions, normals, UVs, and indices. |
| `<material>` / texture descriptions | Initial basic/unlit and Lambert shading, color, opacity, culling, and local image textures. |
| Scene-local resource definitions/references | Optional shared geometry/material/texture IDs; native caches own derived GPU resources. |

Common object attributes include `id`, position, rotation, scale, and visibility.
Proposed conventions match familiar Three.js usage: right-handed coordinates,
Y up, cameras looking along local -Z, rotation angles in radians, and camera
FOV in degrees. Initial rotation is Euler XYZ; quaternion/matrix input is
additional explicit coverage rather than an ambiguous simultaneous override.
Camera aspect defaults to the logical projected frame ratio defined in §7;
with a valid `viewBox`, CSS resizing fits that frame without reframing the
camera. Resolve the named active camera; missing/duplicate IDs, invalid near/far
ranges, malformed arrays, or unknown geometry/material types produce diagnostics
instead of silent output.

The root's CSS properties affect its page box. 3D transforms/materials come
from the scene schema; CSS layout does not position individual meshes.
HTML/SVG labels or overlays remain normal page siblings around the viewport;
arbitrary HTML nested in the 3D subtree is not an initial feature.

## 5. Radiant scene renderer and shared native GL core

### Context provider and API mapping

Use a narrow native provider for context creation, current-context management,
function loading, capability discovery, and destruction. Start on macOS with
an explicitly requested forward-compatible OpenGL 4.1 core context. For other
desktops, an OpenGL 3.3 core context is a candidate baseline for the initial
subset; verify actual entry points, formats, and limits rather than equating
a GL version with complete WebGL2 support. The existing shell context need
not change its profile to accommodate a scene viewport.

GLFW can create a hidden window/context for offscreen execution. Use FBOs for
the viewport drawing buffer rather than depending on that hidden window's size.
Create and destroy platform windows through the shell on the thread required
by GLFW; make their GL contexts current only on their owning page thread.
Reuse the host's event scheduling instead of introducing another polling loop.
[GLFW context guide](https://www.glfw.org/docs/latest/context_guide.html),
[context hints](https://www.glfw.org/docs/latest/window_guide.html#window_hints_ctx)

Load OpenGL entry points through the provider and keep native format/state
mapping centralized. Phase I needs only mechanisms exercised by the native
scene renderer. The Phase II WebGL adapter adds ES/API compatibility such as
default-framebuffer emulation, precision queries, GLES format mapping, and
immutable texture-storage rules. Where required, missing `glTexStorage*` can
be implemented by mip-level allocation plus resource-record validation.
No EGL or GLES runtime is involved.

### Native scene processing

Validate and project the document-owned scene elements into native scene
records. Resolve resource/camera references, compose parent/world/view/projection
matrices, derive normal matrices and bounds, select visible objects, choose
material shader variants, upload changed resources, and submit draws. Handle
opaque depth/culling and transparent ordering/blending explicitly. Radiant now
owns this work on the Lambda path; Three.js performs none of it in Phase I.

Basic materials provide unlit color/texture rendering. Lambert materials add
ambient/directional lighting with correct transformed normals. Define the
linear-light shading and sRGB input/output conversions together with snapshot
composition; test them rather than assuming a Three.js-identical appearance.
Instancing uses the same geometry/material path with per-instance transforms,
not a separate renderer. Advanced PBR, shadows, skeletal/morph animation,
environment maps, GLTF loaders, and arbitrary upstream Three.js features are
follow-on scope.

### Shared graphics mechanisms

| Native mechanism | Phase I use / later reuse |
|---|---|
| Contexts and capabilities | Native scene viewport setup and explicit driver diagnostics; later WebGL context setup. |
| Buffers, textures, VAOs | Validated native-owned geometry, instances, and decoded images; later JS uploads use the same resource mechanisms. |
| Shader/program services | Native desktop material shaders, compile/link logs, reflection, uniform updates, and cache generations. |
| GL state and draw submission | Depth, culling, blending, viewport/scissor, indexed/non-indexed and instanced draws; state scoped to the client/context. |
| Drawing buffers and snapshots | FBO allocation, optional multisample resolve, readback, image publication, and page composition for both scene viewports and future WebGL canvases. |
| Resource accounting | Allocation limits, generation checks, cleanup, and failure diagnostics without exposing native GL names. |

Keep this core below both the high-level scene renderer and future JS adapter.
It must not depend on a JS realm, TypedArray object layout, WebIDL conversions,
or JS callbacks. Conversely, the Phase II imperative WebGL adapter must be able
to submit GPU operations directly without constructing a Lambda scene for each
draw. Share applicable provider/resource/state helpers; the two clients need
different scene/API policy above that waist.

### Ownership and validation

A document-owned registry tracks scene viewport, scene projection, asset,
context, and GPU-resource generations. Initially use isolated contexts per
viewport and share resources only within their valid context. Resource IDs
and native GL names have separate identities. Changes/removal invalidate the
appropriate projection or cache entries; stale retained paint data keeps its
published image alive until released (D4.5.1v4).

Scene elements and Lambda numeric arrays are ordinary values. During
projection/upload, borrow only for the call; copy retained scene/mesh/material
data into native/document-owned storage. Retained IDs do not pin a borrowed GC
pointer. Any retained script-identity value uses an explicit registered root
(D4.5.2/D5.3.3). GPU release runs with the correct context on the owning thread,
and document teardown releases resources without JS finalizers or `dispose()`.

Validate finite transforms, geometry component counts, index/attribute bounds,
byte-size overflow, image dimensions/formats, framebuffer completeness, and
allocation limits before issuing affected GL calls. Resource storage is
initialized before visible reads. Asset acquisition uses existing document
resource policies; failed acquisition or malformed scene input produces a
diagnostic rather than a successful blank viewport.

Build shared conversion/dispatch helpers instead of repeated per-type copies.
All first-party code follows Lambda's C+ conventions and C++17 with existing
`lib` containers, ownership facilities, and logging. Future host objects and
JS wrappers follow D7.4.1v2/D7.4.4; they are not Phase I prerequisites.

## 6. GLSL policy: native shaders first, ES adaptation in Phase II

### Accepted shader language

For native desktop GLSL, preserve the source and let the active driver
determine language, extension, compile, and link acceptance. Acceptance
is limited by that context's supported versions and capabilities; desktop
targeting does not imply support for every GLSL version on every machine.
Unversioned source uses the driver's default desktop dialect. Explicit ES
version declarations select the source-adaptation path below; legacy ES 1.00
support is additional scope if needed by a fixture.

Phase I compiles Radiant's own desktop GLSL for the selected materials,
initially targeting GLSL 330 core where supported. Test native shader services
and driver diagnostics directly; no ES translator or browser-specific
shader-language validator is a Phase I dependency.

In Phase II, for Three.js-generated GLSL ES, implement a first-party,
token-aware source adapter. Its job is source normalization, not a complete
GLSL compiler. Keep
macros and conditional compilation for the desktop preprocessor; delegate
parsing, type checking, optimization, linking, and reflection to OpenGL. Do not
add an ANGLE translator, a duplicate GLSL AST/type checker, or an ES-only
rejection pass to the desktop profile.

Browser WebGL2 restricts the accepted ES language and forbids some desktop-only
constructs. Radiant intentionally accepts the active desktop compiler's wider
language. Track this policy separately from API implementation gaps. Full
browser shader validation would require a future explicit design change.
[WebGL2 shader-language rules](https://registry.khronos.org/webgl/specs/latest/2.0/#5.6)

### Phase II: Three.js source adaptation

Current Three.js resolves shader chunks and unrolls selected loops before
submitting source; ordinary built-in materials and `ShaderMaterial` receive a
GLSL ES 3.00 prefix. This makes ES-to-desktop source normalization necessary
even though Radiant accepts desktop GLSL. Pin and capture the actual submitted
sources rather than rewriting Three.js itself.
[Three.js program construction](https://github.com/mrdoob/three.js/blob/dev/src/renderers/webgl/WebGLProgram.js)

The Phase II adapter handles its pinned shader corpus with these rules:

- Convert `#version 300 es` to a compatible desktop version, initially
  `#version 330 core` on contexts supporting it; preserve explicit desktop
  versions. Keep version/directive placement valid.
- Normalize ES precision declarations/qualifiers only where required by the
  desktop dialect. Preserve line positions or maintain a source map for logs.
- Preserve Three.js's emitted input/output and texture aliases when they are
  already valid desktop GLSL. Add legacy ES 1.00 adaptation only if a required
  fixture uses it; this is additional coverage, not automatic WebGL1 support.
- Handle ES extension directives and `GL_ES`/`__VERSION__` conditionals
  deliberately for the captured source; do not blindly replace identifiers
  inside comments, unrelated names, or preprocessor bodies. Unsupported ES
  constructs produce compile diagnostics rather than silently changed meaning.

Native desktop source bypasses ES normalization. Preserve the original string
for `getShaderSource()` and retain the submitted desktop source for diagnostics.
`getShaderInfoLog()`/`getProgramInfoLog()` expose actual driver failures, with
line mapping where normalization changes positions. Reflection uses linked
driver results and generation-checked uniform locations. Precision queries map
to the actual desktop numeric representation; they must support the renderer's
precision selection without pretending mobile precision tiers were compiled.

Phase I acceptance covers native desktop material shaders, actual reflection,
uniform updates, and driver compile/link failures. Phase II acceptance adds
generated vertex/fragment pairs from every required Three.js scene,
macros and precision guards, native desktop-only constructs, compile/link
failures, source retrieval, reflection, and uniform updates. Browser renders
are pixel references for common shaders; desktop-only shader tests use native
expected results because a browser may reject their source by design.

## 7. Drawing, presentation, and page composition

### Execution and frame scheduling

Render dirty native scenes on the RC1 page thread after the necessary scene
projection and box layout. A context-current guard restores prior bindings.
No new GL worker queue, script/layout split, or nested page loop is needed for
the first implementation.

Scene/resource changes mark the viewport dirty and request paint; box changes
also use normal layout invalidation. Update existing scene elements through
the normal Lambda view/DOM path. Do not introduce an imperative GL API or a
new animation loop for Lambda authors. A scene description can be sampled at
an explicit time for deterministic animation tests; a full animation-track
language is later work.

In Phase II, WebGL draws/clears and existing animation-frame callbacks dirty
their canvas; the same presentation boundary resolves its image. Script-visible
events use existing host scheduling (D7.4.2v2). Library `flush()` calls are not
required to repaint, and synchronous readback observes preceding GPU writes.

### First presentation path: GPU → bitmap

Resolve multisampling, read the dirty viewport drawing buffer into native staging
storage, normalize row direction and pixel representation, and publish an
`ImageSurface` snapshot. Read once per changed viewport presentation, not once
per display-list consumer or paint fragment. Publish its generation only after
the pixels are complete; retained paint data must not observe a partly written
surface. Keep prior snapshots alive while a retained consumer uses them.
Use the same image-painting mechanism for native scene viewports and later
WebGL canvases rather than implementing separate compositors.

Internal resolve/readback must restore application framebuffer bindings,
pixel-pack state, and any other state it touches. A hidden default-framebuffer
implementation must preserve Phase II's observable framebuffer-zero behavior.
Separate render generation, published snapshot generation, and context
generation so an unchanged snapshot cannot hide a context reset.

The normal page painter applies CSS position, clipping, transforms, opacity,
and stacking. Normalize premultiplied versus straight alpha explicitly for
the `ImageSurface` contract. Validate color-space conversion, texture orientation,
and antialiasing with native rendered-output assertions. Browser/Three.js
pixel comparisons join the Phase II workload.

Native scene frames explicitly clear/render their target when dirty and keep
their published snapshot for subsequent page repaints. Phase II WebGL
presentation also observes `preserveDrawingBuffer`: retain the page's published
snapshot for repaint, while clearing/discarding the underlying drawing buffer
at the specified boundary when preservation is disabled. A cached page image
must not accidentally make the drawing buffer persistent. Define captures and
canvas-source reads against the correct drawing-buffer timing, not merely the
last published page snapshot.
[drawing-buffer rules](https://registry.khronos.org/webgl/specs/latest/1.0/)

### SVG-style HTML sizing, viewBox, and resize

**Selected policy:** `<scene3d>` follows inline SVG sizing within HTML. CSS
determines its viewport; `viewBox` determines how a logical projected frame
fits that viewport. Use the existing replaced-content sizing machinery for
width/height, percentages, `auto`, min/max constraints, and flex/grid placement.
Width/height attributes supply presentation hints that CSS can override.

Absolute width/height can supply natural dimensions. A `viewBox` alone supplies
a ratio, not a natural pixel size; intrinsic-ratio precedence follows SVG.
Resolve an automatic dimension from the available ratio through CSS sizing.
When neither dimension is definite, use the applicable SVG/CSS fallback
algorithm rather than assigning every scene an intrinsic 300 × 150 size.
[SVG intrinsic sizing (§8.12)](https://www.w3.org/TR/SVG2/coords.html),
[CSS object sizing](https://www.w3.org/TR/css-images-3/#default-sizing)

Accept `viewBox: "minX minY width height"` and `preserveAspectRatio`, defaulting
to `xMidYMid meet`. `meet` fits uniformly; `slice` fills with cropping; `none`
stretches independently on each axis. Support SVG alignment values. Without
`viewBox`, ignore `preserveAspectRatio`. Malformed/negative extents invalidate
the attribute; zero extents suppress scene rendering while retaining its CSS
layout box. Clear any previous snapshot on that transition.
[SVG viewBox and fitting](https://www.w3.org/TR/SVG2/coords.html#ViewBoxAttribute)

**Proposed 3D mapping:** the camera projects world geometry into a 2D frame
with logical bounds `(0, 0, viewBox.width, viewBox.height)`. Apply the shared SVG
fitting transform, including `minX`/`minY` translation, to that projected frame.
The default perspective-camera aspect is `viewBox.width / viewBox.height`;
without a valid `viewBox`, use the CSS viewport ratio and an identity logical
mapping. World coordinates, FOV, near/far planes, and camera zoom remain scene
properties. `viewBox` is a projected-image rectangle, not a 3D bounding box.
Fixed `viewBox` composition therefore scales/fits on CSS resize; it does not
implicitly change camera aspect to match every new page box. An explicit
camera aspect remains an override. The fitting coordinate system is X-right,
Y-down, separate from the Y-up 3D world. The initial offscreen image is bounded
by the root viewport; ordinary page clipping/stacking applies to its snapshot.

For `viewBox: "0 0 640 360"`, CSS `width:320px; height:auto` yields a
320 × 180 viewport. A definite 320 × 240 viewport with default fitting keeps
the projected frame at 320 × 180, centered with 30 logical pixels above/below.
The root background covers the viewport. `slice` crops horizontally in that
same box; `none` stretches to 320 × 240.

Allocate the drawing target from the **used CSS viewport × destination raster
scale**, with centralized integer rounding and resource limits, never from
raw `viewBox` numbers (RSC1/RSC6/RSC7/RSC8). Layout and fitting stay float.
Resize updates the raster target and fit; only the no-`viewBox` default camera
aspect follows the viewport ratio. Density changes rebuild raster resources
without changing logical sizing or camera framing. Authors do not manage pixel
ratio or framebuffers manually.

Reuse `calculate_svg_intrinsic_size()`, `svg_parse_viewbox()`, and
`svg_viewbox_transform()` through shared sizing/validation helpers. These are
existing integration seams, not evidence that all required SVG edge cases
already pass. Validate the shared path against the named sizing/fitting
fixtures; fix any shared-policy gap there instead of copying a scene-only
algorithm.

Phase II `<canvas>` retains its distinct Web API contract: bitmap dimensions
are script-selected integer pixel counts, independent of the CSS box, without
an extra implicit device-ratio multiplier. Three.js `setPixelRatio()` and
`setSize()` must work. WebGL resize clears/reallocates its buffer while retaining
required GL state and does not automatically reset the viewport. Do not impose
that imperative-canvas policy on the native scene element.

Both paths handle zero-sized targets and allocation failure explicitly, with
limits on dimensions, pixels, and native/GPU resources under document
accounting. Exact initial quotas need measurement.
[resize semantics](https://registry.khronos.org/webgl/specs/latest/1.0/)

### Later presentation path: shared GPU texture

If readback is a bottleneck, add an external-image/texture abstraction consumed
by a compatible Radiant GPU compositor. It needs synchronization fences,
generation ownership, format/alpha/color-space metadata, and a snapshot path
for CPU consumers. An OpenGL texture cannot be handed to the present CPU painter
as if it were an `ImageSurface`. Establish the compositor seam before promising
zero-copy performance; changing the whole page renderer is not an initial gate.

## 8. Delivery phases and acceptance

### Phase I — Lambda scenes rendered natively

Deliver the scene package, native viewport integration, bounded scene model,
and shared GL mechanisms needed to render it. The package is authored/tested
as Lambda source and the renderer as Radiant/native code. JS WebGL wrappers,
Three.js imports, JS shader generation, TypedArrays, and ESM startup are not
Phase I dependencies or completion gates.

| Required Phase I fixture | What it establishes |
|---|---|
| Lambda construction/types and invalid scenes | Element model, helper package, normalization, invalid IDs/cameras/geometry diagnostics, and pure data construction. |
| Indexed colored cube inside an HTML document | Native camera/projection, buffers, shader/program services, depth/culling, real GL pixels, and normal page composition. |
| Parent group transforms and nonuniform scale | Hierarchy, world transforms, normal matrices, visibility, and correct lighting as objects/cameras change. |
| Textured plane/cube and transparent meshes | Local asset decode, UV/orientation, material state, blending/order, color conversion, and alpha over HTML/SVG. |
| Ambient/directional Lambert scene | Radiant's native light/material implementation, with deterministic pixel expectations. |
| Multiple meshes and instancing | Shared resource descriptions, resource reuse within a context, per-instance transforms, and native instanced draws. |
| Mixed HTML/SVG/scene layout, resize, and display scale | Inline/replaced-content behavior, flex/grid use, clipping, CSS transforms/stacking, correct camera aspect and raster size. |
| SVG sizing and viewBox fitting | ViewBox-only ratio, width/height versus CSS, auto/percent/min/max sizing, alignment, meet/slice/none, nonzero origins, absent/invalid/zero viewBox, and stable camera framing across resize/density changes. |
| Scene update/removal, two viewports, and teardown | Invalidation, snapshots, context isolation, resource retirement, forced-GC safety, and bounded allocations without JS disposal. |
| Native shader failures and unavailable graphics | Driver diagnostics, rejected uploads, useful failure states, and explicit excluded-profile behavior. |

Use fixed Lambda data, local assets, chosen frame times, analytic geometry/color
checks, and native image assertions with declared tolerances. Verify rendered
scene pixels and the surrounding page, not only normalized data or GL return
codes. A black viewport cannot pass as successful rendering. Add the `.txt`
golden for every new Lambda unit-test `.ls` fixture; rendered-output checks are
additional evidence, not replaced by text goldens.

### Phase II — JS WebGL around the native graphics core

Add DOM `canvas.getContext("webgl2")`, declared/branded WebGL host objects,
WebIDL/TypedArray conversion, WebGL error/state rules, ES-source normalization,
capability/extension publication, and context-loss events. This is additional
adapter work above the Phase I core; native scene tests do not establish those
contracts. A separate high-level JS scene facade can be considered later, but
does not substitute for the WebGL API required by Three.js.

A canvas selects one mode on its first successful context request; repeated
requests preserve identity and incompatible modes return `null`. Keep Canvas
2D state separate. Use context/kind/generation-branded resource wrappers and
program/link-generation uniform locations. Follow the ownership and declared
host-interface contracts in D7.4.1v2/D7.4.4; never expose GL names or retain a
borrowed JS buffer pointer. Validate detachment, offsets, sizes, and ranges
before borrowing/copying uploads. Preserve application state and errors across
internal resolve/readback.

Pin an official modern Three.js package/revision with source hashes/licenses.
Run its unmodified `WebGLRenderer` and real ESM/addon import path. Audit both
startup and scene calls: renderer initialization may use APIs not evident in
the visible scene, such as placeholder 3D/array texture allocation. Move all
required calls into the Phase II manifest; no false-success stubs or library
patches. Advertise the partial desktop profile honestly and publish a WebGL
extension only when its native support and adapter behavior are implemented.
[Three.js initialization](https://github.com/mrdoob/three.js/blob/dev/src/renderers/webgl/WebGLState.js)

On this path, Three.js retains its own scene graph in LambdaJS and submits
imperative WebGL calls. It does not require a second native scene graph or
round-trip through `<scene3d>` for each draw. Both clients reuse the GL provider,
resource mechanisms, shader services, and image-presentation path.

Phase II's initial library fixtures cover renderer startup/indexed drawing,
textures/transparency, basic/Lambert materials, instancing, resize/density,
multiple canvases, disposal, and actual image output. Validate the WebGL API
with named Khronos/WPT slices separately from those library scenes. Add
browser reference images and real pointer tests when controls/picking land.
JS/DOM/module/animation dependencies are part of this phase and may reveal
existing engine gaps.

### Further coverage

PBR/standard materials, shadow maps, postprocessing, more formats/UBOs/MRT,
transform feedback, queries/syncs, full loss/restoration, OrbitControls,
advanced asset loaders, and additional platforms each need their own scope
and evidence. Native scene features and JS API compatibility may progress
independently. Full browser shader validation remains a separate decision;
WebGPU/TSL, WebXR, and every Three.js example are not implied deliverables.

## 9. Loss, limits, and unavailable graphics

Phase I requires resource accounting, generation checks, unavailable-driver
diagnostics, and clean teardown. A failed/reset native provider invalidates its
graphics generation and stops using the affected resources; a scene can rebuild
its native projection/resources from the retained document description when
recovery is supported. Do not report stale pixels as a successful new frame.

Phase II adds the WebGL lost-context API rules and `webglcontextlost` /
`webglcontextrestored` events. Old script wrappers remain invalid after recovery;
the application/Three.js recreates its resources. Stop presentation work while
teardown or loss is active. Full scripted restoration has its own later gate.

Measure and bound texture/buffer allocations, staging snapshots, simultaneous
contexts, and shader/program caches. Initialization, shader compilation,
readback, and device failures need distinct diagnostics. Native driver work can
outlive a script timeout; existing OS/device recovery and context-loss handling
must be tested. A hardened GPU-process isolation policy is a separate design
decision before claiming hostile-content containment.

The full native Radiant host may explicitly enable a hidden-window desktop GL
provider for rendered tests. Ordinary headless runs remain windowless; do not
silently create a GLFW window for every headless test. Failure to acquire a
valid provider returns `null`. Record the actual driver, including whether it
uses hardware or software; Radiant bundles no software renderer. Keep the
D7.1.4v2 headless-C profile on its null backend with native 3D/WebGL unavailable.
Adding graphics support to that profile requires a separately reviewed
formal-profile change. The runtime-only CLI and current WASM profile do not
acquire graphics dependencies.

## 10. Export

For PNG/JPEG page output, composite the scene viewport's resolved raster image.
For SVG/PDF page output, embed a raster image at its CSS content box;
3D shader execution does not become vector primitives. Capture the scene at a
defined render/capture point and apply the same drawing-buffer lifetime rules
as on screen. Animation capture uses an explicit chosen frame, not wall-clock
luck.

Page export is follow-on work after the native Phase I core. The current
Canvas 2D design records that SVG/PDF canvas dispatch is deferred.
Therefore SVG/PDF scene and WebGL export are new work, not capabilities obtained
by producing an `ImageSurface`. Share the eventual export path and validate
the rendered PDF/SVG output, including alpha and transforms. Standalone canvas
`toBlob`/`toDataURL` APIs are additional surface, not required to reuse page
export.

## 11. Dependencies and binary size

The runtime dependencies are the existing native context infrastructure and
system desktop OpenGL. Ship the native scene renderer and GL resource/state
services with Radiant, plus the Lambda package as source. The JS API and ES
source adapter are Phase II additions. No ANGLE/GLES/EGL/translator library or
browser installation is required. Initialize the graphics provider lazily when
a scene viewport or supported future canvas context needs it. Keep the
D7.1.4v2 null backend for builds excluding native graphics, without stripping the common
Radiant source set or leaking GL types into runtime-facing headers.

Build declarations belong in `build_lambda_config.json`; generated Lua is never
edited manually. Reuse existing dependency declarations and helpers before
adding another platform loader. Linux/Windows native GL bring-up and any future
Metal/Vulkan backend require their own verified API mapping; they are not
obtained automatically from this macOS design.

System drivers add no bundled library bytes, but the host implementation,
function tables, diagnostics, caches, staging images, and GPU allocations all
have costs. Phase I now includes native scene traversal, geometry generation,
materials, and lighting; it is larger in scope than a thin WebGL wrapper.
Neither phase includes an independent GLSL compiler. Earlier translator-only
size estimates do not predict the size of the native 3D renderer or JS adapter.
Do not promise an installed size before implementing and linking it.

Measure release executable/module growth and compressed package delta with
matched baseline/new builds. Record architecture, compiler/link flags, symbol
policy, enabled API manifest, and all runtime files. Measure runtime CPU/GPU
memory separately. Reference ANGLE measurements are preserved in Appendix B;
they are not dependencies or a prediction of first-party implementation size.

## 12. Performance questions and retained ideas

CPU readback is the main Phase I presentation risk. A 1920 × 1080 RGBA8 image
is about **7.91 MiB**; reading one such image at 60 frames/s moves about
**475 MiB/s** of pixel payload before conversion and compositing. Driver
synchronization may cost more than the raw transfer. Measure draw submission,
GPU execution, resolve/readback, conversion, page composition, and end-to-end
frame pacing separately, including multiple dirty viewports.

Use release builds only. Record GL/GLSL versions, driver/device, dimensions,
density, shaders, Lambda scene revision, warmup, and correctness output. Measure
scene projection/resource updates and driver compilation separately from
steady-state frames; add ES-source adaptation measurements in Phase II.
Cache shaders/programs by source, adaptation policy, and context generation;
invalidate on loss or incompatible context changes. Do not promise a speedup from
enabling a GPU backend alone. The retired idea note's unmeasured 10–50× speedup,
8 MB base executable, LOC totals, and 3–6 MB ANGLE estimate are not acceptance
evidence and are replaced here by explicit measurements or open gates.

Useful ideas retained from that note are the separation of native WebGL,
page-renderer acceleration, and browser hosting; reuse of the rendering
abstraction; explicit shader compilation and resource management; and
separate dependency/binary-growth accounting. Three.js supplies the concrete
model reference and Phase II workload. Phase I acceptance is the Lambda scene
corpus, not Three.js execution.

## 13. Decisions still requiring implementation evidence or consultation

- Final package spelling and entry exports: `lambda.scene3d` remains
  recommended. `<scene3d>` and SVG-style HTML sizing are selected.
- Exact Phase I package exports, scene/resource schema, transform conventions,
  material/light equations, color-space policy, and initial quotas.
- Pinned Three.js revision, submitted ES-shader corpus, and complete startup/draw
  API manifest for Phase II, not prerequisites for native scene bring-up.
- Platform deployment minimums and verified GL/GLSL capabilities, including
  whether the proposed GL 3.3 baseline suffices for other desktops.
- Phase II ES-source adaptation and desktop precision-query mapping;
  native desktop shader acceptance is already selected.
- Initial resource quotas and a measurable frame-pacing target for the required
  scenes on named hardware.
- Whether WebGL1 is ever added as a separate API; it is deferred initially.
- The Phase II optional-extension manifest and whether advanced asset loaders
  join either phase's required workload set.
- GPU-process containment and any expansion of existing headless profiles.

These are open details of the proposal, not silent changes to the formal
specification. No implementation-complete or cross-platform claim is made.

## Appendix A — implementation seams and delivery gates

Keep detailed implementation progress in a future `vibe/impl/` plan. These
locations describe proposed work; no package, scene dispatcher, GL core, or
focused scene runner is implemented by this revision.

| Location | Responsibility |
|---|---|
| Proposed `lmd/package/scene3d.ls` and `lmd/package/scene3d/` | Explicit public module, scene types/constructors, pure normalization and transform/geometry helpers (D7.2.1–D7.2.4, S12.1.1v2). |
| Existing Lambda element/DOM admission and `radiant-dom` boundary | Admit the root/scene subtree as document-owned values; no direct runtime-to-GL link (D7.5.3). |
| `radiant/layout_block.cpp`, `radiant/render_svg_inline.cpp`, shared declarations in `radiant/render.hpp`, and existing replaced-content dispatch | Reuse/extract SVG intrinsic sizing, viewBox validation, and fitting helpers; keep scene children out of HTML box layout; use float layout dimensions. |
| Proposed native scene module in `radiant/` | Scene projection with SVG-style projected-frame fitting, references, transforms/cameras, geometry, materials/lights, draw ordering, invalidation, and document-owned caches. |
| Proposed shared GL module/provider in `radiant/` | Context/resource tables, shader services, validated native uploads, scoped state/draws, FBOs, resolve/readback, and accounting. |
| `radiant/ui_context.cpp` / `radiant/window.cpp` | Explicit core-context creation, function loading, owning-thread lifetime, unavailable-provider handling. |
| `radiant/canvas_2d.cpp`, `render_canvas_content`, and existing image painting | Extract shared snapshot/image-presentation helpers; preserve Canvas 2D behavior. |
| `radiant/render.hpp`, SVG/PDF dispatch, and image-generation support | Snapshot lifetime and follow-on scene/canvas export. |
| Phase II `lambda/dom/dom_canvas.cpp` / declared host interfaces | Context selection, JS resource wrappers and argument conversion above the shared native GL core (D7.4.1v2/D7.4.4). |
| `build_lambda_config.json` | First-party build wiring and existing system-GL dependencies; no ANGLE dependency. |

| Gate | Required evidence |
|---|---|
| Phase I: package/model | Explicit module import, helper/type/normalization cases, `.ls`/`.txt` goldens, diagnostics, and mixed-element example syntax. |
| Phase I: provider/core | Explicit desktop core context, real shader/draw/readback pixels, reflection/uniforms, failure paths, native resource bounds/generations, and measured release growth. |
| Phase I: scene rendering | Every native scene fixture in §8 with analytic/image assertions for cameras, hierarchy, normals, lighting, geometry, textures, transparency, and instancing. |
| Phase I: page/lifecycle | Mixed HTML/SVG layout and pixels, SVG sizing/viewBox fixture matrix, clipping/stacking/scale, updates/removal, multiple viewports, forced GC, resource accounting, teardown, and Canvas 2D regressions. |
| Phase II: WebGL adapter | Actual context/object semantics, JS conversions/uploads, shader adaptation, state/error preservation, extensions/loss, and named Khronos/WPT API slices. |
| Phase II: Three.js | Unmodified pinned package and ESM path, complete selected startup/draw manifest, native canvas pixels and browser references, JS/DOM/event dependencies. |
| Follow-on | Advanced native materials/scenes, broader WebGL API coverage, controls/picking, rendered PNG/SVG/PDF export, additional desktops, and measured frame pacing for each claimed scope. |

Add a focused native scene runner and a Lambda scene corpus to the relevant
baseline. Proposed locations are `test/test_scene3d_gtest.cpp`,
`test/lambda/scene3d/`, and rendered scene/page fixtures using existing render
infrastructure. Their final runner commands belong in the implementation plan
and `test/README.md`; no `make test-scene3d` target exists today.

Use existing aggregate gates after Phase I implementation:

```sh
make test-radiant-baseline
make test-lambda-baseline
```

Phase II also runs the JS runtime gate:

```sh
make test262-baseline
```

Test262 checks ECMAScript, not scene rendering or WebGL conformance. Report
focused native rendering, Lambda goldens, JS/Three.js, selected Khronos/WPT,
and aggregate results separately. A missing graphics backend cannot count as a
passed rendered test on a platform claiming native 3D support.

## Appendix B — historical ANGLE size references

These local measurements informed the earlier alternatives; no listed library
is selected by this revision. They are not matched builds of the same revision.

| Measured macOS arm64 artifact | Installed MiB | gzip MiB |
|---|---:|---:|
| Chrome for Testing 148.0.7778.97 `libGLESv2.dylib` + `libEGL.dylib` | 6.5949 | 2.5929 combined |
| Optional SwiftShader dylib from that distribution | 15.7592 | Not recorded here |
| Standalone GLSL-only ANGLE translator size probe | 0.9429 | 0.3300 |

The 148 pair is 6,915,248 bytes: GLES 6,823,296 and EGL 91,952. Cached
143.0.7499.169 and 142.0.7444.59 pairs measured 6.8175 and 6.9750 MiB.
These are browser-distributed libraries, not custom Metal-only builds. Their
recorded dependency lists contain only system libraries; no separate
`.metallib` was present in the sampled distribution. Paths, hashes, and full
compression provenance remain in
[`angle-size-evidence.json`](../../temp/webgl-design/angle-size-evidence.json).

The standalone translator probe used ANGLE revision
`36b74e4dd47d2704de451621e57573ec8dd3c9be`, GLSL output only, `-Oz`, dead
stripping, and stripped symbols. Its dylib measured 988,672 bytes. An upstream
sample linked against it translated valid ES3 shaders and rejected an invalid
shader; GPU rendering, Three.js, and full conformance were not tested. Its
4.7872 MiB static archive is an intermediate, not the shipped contribution.
This custom out-of-tree size probe is documented in
[`measurement.json`](../../temp/angle-translator-size/measurement.json).
Scratch artifacts are local evidence, not durable release inputs.

## Appendix S — superseded decisions

The following earlier directions were replaced by the user's 2026-10-08
revisions. They remain as history and do not constrain the current design.

- ~~The root name remains proposed, with `<scene>` as an alternative to `<scene3d>`.~~ Replaced by §4: the user selected `<scene3d>`. `<scene>` was more generic; numeric-leading `<3d_scene>` failed bare parsing; `<canvas type:'webgl'>` described a backend rather than the declarative vocabulary. The package spelling remains a recommendation.
- ~~Default intrinsic size is 300 × 150 CSS pixels; resize updates the auto-aspect camera to the used viewport ratio.~~ Replaced by §7: SVG-style intrinsic sizing and viewBox fitting. A valid viewBox fixes the default camera's logical frame ratio; CSS resize fits that frame. The viewport ratio supplies the default camera aspect only without a valid viewBox.
- ~~Implement JavaScript WebGL through the existing Canvas/DOM boundary, using ANGLE for OpenGL ES execution and shader translation.~~ Replaced by §1/§5: first-party host implementation over system desktop OpenGL; ANGLE is reference only.
- ~~One native graphics provider owns its EGL display/device services. Each canvas owns an isolated EGL context and drawing-buffer resources.~~ Replaced by §4/§5: native desktop GL contexts and private FBO drawing buffers.
- ~~Request `EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE` and robust resource initialization when the pinned provider supports the corresponding extensions. Require equivalent correctness if an extension is unavailable; do not silently use a less constrained context.~~ Replaced by §5/§6: first-party host/resource validation with intentionally broader desktop GLSL acceptance.
- ~~The WebGL2 target includes ES3 shader behavior, vertex arrays, instancing, uniform buffers, integer attributes/textures, multiple render targets, framebuffer blits, transform feedback, queries, samplers, and sync behavior. Implement and test the required API even where a particular Three.js scene does not exercise it.~~ Replaced by §5/§8: native Lambda scene core in Phase I, JS/API manifest in Phase II, then incremental coverage; full browser shader validation is separate.
- ~~Package a pinned standalone ANGLE provider with only the chosen platform backend and necessary shader translators. The initial recommendation is Metal on macOS; D3D11 on Windows and Vulkan or desktop GL on Linux are follow-on providers.~~ Replaced by §11: system desktop OpenGL without bundled ANGLE/GLES/EGL/translator libraries; additional native backends need separate evidence.
- ~~Standard material with lights and a shadow map~~, ~~Render target and a simple postprocessing pass~~, ~~OrbitControls with real pointer/wheel interaction~~, and ~~Resize, density change, two canvases, disposal and restoration~~ were initial required fixtures. Replaced by §8: native Lambda fixtures first, JS fixtures in Phase II, and advanced coverage later. Native teardown remains a Phase I gate.
- ~~Three.js is the initial compatibility target, using its WebGL2 entry point.~~ Replaced by §1/§8: native Lambda scenes are Phase I; JS WebGL and Three.js are Phase II.
- ~~Radiant needs no duplicate native 3D scene graph for this workload. Its retained page display list references the canvas image; Three.js's retained scene lives in the script heap.~~ Replaced by §4/§5: Lambda elements drive a native rendering projection in Phase I. The quoted ownership still describes the separate Phase II Three.js path.
- ~~For Three.js-generated GLSL ES, implement a first-party, token-aware source adapter.~~ Its placement in initial delivery is superseded by §6: native desktop material shaders first; ES normalization joins Phase II.

# Radiant Scene3D — PBR materials and shadow maps for native scenes

**Status:** Implementation record, 2026-10-27. M0–M7 deliver and document the native PBR/Dreamhome profile. The continuation delivers native PBR animation (M8), MTL PBR conversion (M9), the original external-model sweep and a qualified snapshot optimization (M10, §11). Focused acceptance passes; required aggregate gates retain the existing failures recorded below. Further whole-page performance work and profile limits remain explicit in §12. Decisions SP1–SP8 (§10) were ruled by the user on 2026-10-26: SP1 option (a), and SP2–SP8 as recommended.

**Scope:** the native `<scene3d>` path that Lambda scenes use (`radiant/scene3d.cpp`, `lmd/package/scene3d/`). This delivers the native material follow-on to Phase III; the current contract is in the [3D and WebGL design](../radiant/Radiant_Design_WebGL.md#5-radiant-scene-renderer-and-shared-native-gl-core) and the [package guide](../../doc/Lambda_Packages.md#27-scene3d--native-3d-scenes). The JS WebGL path is out of scope: it already runs unmodified Three.js `MeshStandardMaterial`, PMREM environments and PCF shadows ([Phase III record](Radiant_WebGL_Phase3.md)).

## 1. Motivation and starting point (before M0, 2026-10-26)

Before this work, Dreamhome (`test/demo/dreamhome/`) exposed the native renderer's material limit: wood, metal, glass and fabric shaded alike, nothing cast a shadow, and sunlight through a window lit nothing on the floor. Imported glTF models lost most of their look. The table below preserves that starting point; it is not the current capability list. Implemented behavior and evidence are in §11.

| Area | Before M0 | Original source anchors |
|---|---|---|
| Materials | `basic` (unlit) and `lambert` only, one color, one base-color texture, opacity, emission | `scene3d_material`, `radiant/scene3d.cpp:459`; validator `lmd/package/scene3d/model.ls` (`check_node`, material branch) |
| Lighting | Ambient plus at most 8 directional, point or spot lights, inverse-square falloff; Lambert irradiance with no 1/π term | fragment shader, `radiant/scene3d.cpp:972`; quota at `:704` |
| Shadows, environment | None | — |
| Color | Linear shading written to a 4× MSAA `SRGB8_ALPHA8` target with hardware sRGB encoding; no exposure or tone mapping, so highlights clip | `native_gl_target`, `radiant/gl_core.cpp:594`; `native_gl_begin` `:623` |
| Passes | One program, one pass into one target, then readback as the D4.5.1v4 snapshot | `scene3d_snapshot`, `radiant/scene3d.cpp:1041` |
| GL waist | One base-color texture (unit 0) and one data texture (unit 1, skinning palette) per draw; every texture uploads as `SRGB8_ALPHA8`; no depth-only targets, compare samplers, cube maps or float color targets | `NativeGlDraw`, `radiant/gl_core.hpp`; upload at `gl_core.cpp:547` |
| Vertices | Position, normal, UV0, color, skin, two morph targets; no tangents | `Scene3dVertex`, `radiant/scene3d.cpp:28` |
| Invalidation | Any document mutation (including a camera attribute write) re-projects the whole scene | `reproject` in `scene3d_snapshot` |
| glTF import | Metallic/roughness "approximated by Lambert base color"; normal, occlusion, emissive and metal-rough maps dropped with a warning; alpha `MASK` rejected | `load_materials`, `lambda/input/input-scene3d-gltf.cpp:180` |
| glTF export | Every material written as metal 0, roughness 1 | `lambda/format/format-gltf.cpp:245` |

## 2. Formal contracts

No formal ruling changes. The design doc already notes that its follow-on decisions "do not change the formal specification" (§13).

| Ruling | How it applies |
|---|---|
| **S12.1.1v3** | Lambda builds pure scene data; shadow maps, environment prefiltering and shading are host effects in Radiant. |
| **D7.2.4v4** | GPU resources (shadow maps, prefiltered environments) belong to the renderer, never to module state. |
| **D7.4.1v2** | New GL kinds (depth targets, cube maps, float textures) cross the waist only as context-branded `NativeGlResource` IDs. |
| **D4.5.1v4 / D4.5.2** | The published image stays the generation-owned snapshot; shadow and environment caches are native-owned and hold no GC pointers. |
| **D7.5.3** | Lambda reaches the renderer only through the DOM host boundary: new features are scene attributes and elements, not new APIs. |
| **D7.3.5** | Every milestone names its conformance gate (§9). |
| **D7.1.4v2** | Headless profile C keeps its null backend; scenes there report "graphics unavailable" as today. |

Thread and coordinate policy follow RC1 and RSC1/RSC6–RSC8, as in the design doc §2.

## 3. Goals and non-goals

**Goals**

- A `standard` metallic-roughness material matching glTF 2.0 and Three.js `MeshStandardMaterial`: base color, metalness, roughness, normal, occlusion and emissive maps, alpha mask.
- Image-based environment lighting, so metals and glossy surfaces reflect something: procedural `room` and `sky` environments first, HDR images later.
- Exposure and tone mapping.
- Shadow maps for directional and spot lights with soft PCF edges, then point lights.
- glTF import and export that keep PBR materials.
- Lambda helpers, validation and animation tracks for the new properties.
- Dreamhome adopts all of it as the showcase.

**Non-goals** (each is a later, separate scope)

- Ray tracing, global illumination, screen-space reflections or ambient occlusion (SSAO is the natural next step after this plan).
- Clearcoat, transmission, sheen and the other `KHR_materials_*` extensions; the `physical` type name is reserved for them.
- Deferred shading, HDR intermediate targets, postprocessing chains.
- Cascaded shadow maps (needed for city-sized scenes; first milestone fits one map to the scene).
- Any change to the WebGL adapter.

## 4. Scene schema additions

All new attributes are optional; omitting them preserves the earlier native output. The validator in `lmd/package/scene3d/model.ls` and the native validator in `scene3d.cpp` both check them (SP8).

| Element | Additions |
|---|---|
| `<material type:'standard'>` | `color`, `texture` (base color, sRGB), `metalness` (default 0), `roughness` (default 1, as glTF), `metal-rough-texture` (glTF packing: G roughness, B metalness, linear), `normal-texture` + `normal-scale`, `occlusion-texture` + `occlusion-strength` (R channel), `emissive-texture`. (A per-material `env-intensity` was dropped in M3: r186 applies only the scene's `environmentIntensity` to scene environments.) Existing `opacity`, `alpha-mode`, `side`, `emissive`, `emissive-intensity` keep their meaning. |
| Any `<material>` | `alpha-mode:'mask'` with `alpha-cutoff` (default 0.5): alpha-tested, opaque depth, casts cutout shadows. |
| `<texture>` | `color-space:'srgb'|'linear'`. Defaults follow the slot (base color and emissive sRGB, data maps linear), so authors rarely set it. |
| `<geometry type:'buffer'>` | Optional `tangents` (xyzw per vertex, w = handedness). Without them, normal maps use a derivative-based frame (§5.2). |
| `<light>` | `cast-shadow` (default false, SP3), `shadow-map-size` (integer 512–4096; point faces at most 1024; default 2048 directional, 1024 spot and point), `shadow-intensity` (0..1, default 1), `shadow-bias`, `shadow-normal-bias`, `shadow-softness` (PCF radius 0..16 texels, default 1.5), `shadow-extent` (directional half-size in scene units; 0/default fits the scene). New `type:'hemisphere'` with `color` and `ground-color`: one per scene/environment, outside the 8-light quota. |
| `<mesh>` | `cast-shadow`, `receive-shadow` (both default true; they only matter once a light casts). |
| `<scene3d>` | `environment` (the id of an `<environment>`), `environment-intensity` (default 1), `tone-mapping` (`'none'` by default, `'neutral'`, `'aces'`; SP4), `exposure` (default 1). |
| `<environment>` (new; in the scene, a group or `<resources>`) | Ordinary scene content (groups, meshes, lights, resources; no cameras, no nested environments), captured from the environment's origin and never drawn in the view. `background` (default black), `near` (0.1), `far` (100). As built in M3, `room` and `sky` are Lambda helpers that fill one, not native kinds. M5 adds `src` (a Radiance `.hdr` equirectangular image) in place of content (user ruling, 2026-10-27). |
| Animation tracks | A mesh's `material.metalness`, `material.roughness` (standard material only), `material.emissive` (linear color), or the same properties addressed directly by material ID; a light's `shadow-softness` (number). Projection and retained pose refresh consume the sampled values; stopping restores authored values (M8). |

Helpers in `lmd/package/scene3d.ls`: `s.environment(kind, options)^` returns a ready `room`, `sky` or `image` environment (id defaults to the kind; image requires `options.src`); `s.sphere(rings, segments, options)`; `s.material('standard', {...})` uses the generic constructor.

## 5. Rendering design

### 5.1 Shading model

Follow the glTF 2.0 reference BRDF as Three.js r186 implements it, so Three.js can serve as the oracle (§9): GGX distribution, height-correlated Smith visibility, Schlick Fresnel with F0 = mix(0.04, base, metalness), Lambert diffuse on (1 − metalness) · base weighted by (1 − F) of the dielectric reflectance. Direct specular is scaled by r186's multiple-scattering compensation, and ambient light by its single- and multiple-scattering terms. Both read a 16 × 16 split-sum DFG table, which Radiant generates natively (M1 record, §11) rather than copying, and which also serves the image-based term in M3. `lambert` and `basic` keep their current equations; Lambert additionally receives shadows.

Light units follow SP1 option (a): a white Lambert surface lit head-on by an intensity-1 native light reaches 1.0. `standard` agrees with `lambert` in the same scene. glTF punctual lights divide intensity by π on import and multiply by π on export, preserving the authored physical units at that boundary.

### 5.2 Normal maps

Use glTF `TANGENT` (or authored `tangents`) when present; `Scene3dVertex` carries a `tangent[4]` attribute. Otherwise build the tangent frame from screen-space derivatives in the fragment shader, as Three.js `getTangentFrame` does. Generating MikkTSpace tangents on import remains deferred.

### 5.3 Environment lighting

An environment is ordinary scene content or a local equirectangular Radiance HDR image, captured into r186's cube-UV RGBA16F atlas and GGX-filtered. Diffuse light reads the roughest atlas level as r186 does; the original cube-map/SH plan was replaced in M3 (§11). The capture is cached by a content key, so camera moves reuse it. Image environments retain decoded pixels by resolved path (M5).

### 5.4 Color and tone mapping

Shade in linear light, apply `exposure` and the tone-map curve in the fragment shader, then write to the existing sRGB framebuffer. This keeps the single MSAA pass and matches Three.js's inline tone mapping when it runs without postprocessing. Transparent surfaces then blend after tone mapping, the same approximation Three.js makes. With `tone-mapping:'none'` and no `standard` materials, output stays bit-identical to today; that is the regression guard for every existing scene.

### 5.5 Shadow maps

- **Depth pass.** For each casting light, render the casters into a depth texture (`DEPTH_COMPONENT24`, compare mode on, linear filtering for hardware 2×2 PCF). Casters draw their opposite side as r186 does; only the light's bias and receiver normal bias supply offsets. PCF uses five Vogel-disk taps and `shadow-softness` (M4).
- **Directional.** An orthographic frustum fitted to the bounds of casters and receivers, which does not depend on the camera. The map is therefore cached across camera-only changes, which matters for Dreamhome's orbit drags. Cascades come later (§3).
- **Spot.** A perspective frustum from the cone angle, with `distance` (or the scene bounds) as its far plane.
- **Point.** A six-face cube map stores perspective depth. Sampling uses the offset's largest component to derive comparison depth, with five PCF taps in the direction's tangent plane (M5).
- **Who casts.** Opaque and mask materials cast (mask with alpha test in the depth pass); blended materials (glass) and `depth_only` materials don't. Skinned, morphed and instanced meshes cast through the same vertex code as the main pass. `basic` materials receive nothing, being unlit.
- **Budget (SP5).** At most four casting lights per scene. The main pass uses at most 12 bindings, including material maps, skin palette, DFG table, environment atlas and four shadow slots. A 2048² depth map accounts for 16 MiB; a 1024² cube for 24 MiB. All count toward the 512 MiB context budget (design doc §13).

### 5.6 Programs and passes

Programs are variants compiled lazily from one source with `#define` feature bits for material model, tone curve, mask, environment, shadows and shadow cube layout. A depth-only variant shares the vertex transforms. `native_gl_program` compiles and links the desktop GLSL.

`scene3d_snapshot` runs: environment build if dirty → shadow passes if dirty (caster, pose, physics or light change) → the main pass (depth-only, opaque, sorted transparent) → snapshot. `Scene3dStats` exposes projection, snapshot, shadow-pass and environment-build counters so tests assert cache reuse.

### 5.7 Invalidation

As built in M6, a source key excludes camera view attributes while tracking all other scene data and topology. Camera-only changes retain the projection, environment and shadow maps and re-render the main pass; animation and physics generations still refresh retained poses. Scene edits and context loss take the full projection/validation path (§11).

## 6. GL core additions (`radiant/gl_core.*`)

These sit below both clients; the WebGL adapter keeps its own command path and is unaffected.

- **Draw bindings.** `NativeGlDraw` has a fixed array of resource/kind bindings; array index selects the texture unit, at most 12. The value-only waist is unchanged (D7.4.1v2).
- **Texture color space.** `NativeGlSampler::srgb` selects `RGBA8` for data maps or `SRGB8_ALPHA8` for color maps; the minification filter controls mipmap generation.
- **Depth targets.** `native_gl_depth_target(context, size, cube)` (a depth texture with compare mode) and a depth-only begin for a target, or one face of it.
- **Float cube targets.** `native_gl_cube_target(context, size, levels)` (RGBA16F), with begin-face-and-level, for environment capture and prefiltering. (M3 uses a float atlas target instead, below; the cube target stays for M5's point shadows and HDR images.)
- **Float atlas targets (M3).** `native_gl_float_target(context, width, height, depth)` (RGBA16F 2D, linear, optional depth renderbuffer) and `native_gl_begin_region` (viewport and scissor on one region, optional clear), sampled through a `FLOAT` binding.
- **Accounting.** Byte formulas for the new kinds, so they fall under the existing quota, generation checks and teardown.

`test_scene3d_unit_gtest` covers allocation, completeness, accounting, release and context loss.

## 7. Assets

- **glTF import.** `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture` (with scale), `occlusionTexture` (with strength), `emissiveTexture`, `TANGENT`, and `alphaMode:MASK` with `alphaCutoff` map onto `standard`. All maps share the first map's `KHR_texture_transform`; differing transforms warn. The old Lambert/omitted-map warnings are removed. `KHR_materials_unlit` stays `basic`; other optional `KHR_materials_*` extensions still warn.
- **glTF export.** Standard factors, maps, masks and tangents are written back. Lambert exports as metal 0, roughness 1. Environment resources, scene tone mapping and shadow settings have no core glTF mapping; direct environments and hemisphere lights are refused. See the design's [export profile](../radiant/Radiant_Design_WebGL.md#gltf-and-glb-export).
- **Light units.** SP1 option (a) applies at the glTF boundary in both directions: import ÷π, export ×π.
- **OBJ/MTL.** `Pr`, `Pm`, `map_Pr`, `map_Pm` or `norm` select standard shading, independently of legacy `illum` ordering. Factors are [0,1]. Scalar maps read R and pack into linear G roughness/B metalness; missing channels are white, and a metallic map without an explicit factor uses 1. The two scalar images must share dimensions and wrapping. `norm` is linear with optional `-bm` normal scale. Native texture limits and clear input errors bound conversion (M9).
- **Design doc.** §15 "Renderable format profiles" and §14 glTF mapping describe the implemented profile (M7).

## 8. Milestones

Each milestone ends with its acceptance cases and gates (§9); broad suites run only at milestone boundaries, per `radiant/README.md`.

| Milestone | Delivers | Acceptance |
|---|---|---|
| **M0 — GL waist** | Draw bindings, texture color space and mipmaps, depth targets, float cube targets (§6) | Existing Scene3D pixel fixtures are unchanged; new unit tests pass |
| **M1 — Standard material, direct light** | Shader variants, BRDF, exposure and tone mapping, `standard` in both validators, the SP1 light units | A 5 × 5 metalness × roughness sphere grid under one directional light, plus a point-light case, matches the Three.js oracle; basic and Lambert scenes are bit-identical with `tone-mapping:'none'` |
| **M2 — Texture maps** | Base, metal-rough, normal (tangent and derivative), occlusion and emissive maps, alpha mask; glTF import and export parity | Generated `pbr.gltf` covers maps, mask, tangents and shared transforms; independent Three.js map pixels and stable import/export. The original DamagedHelmet/NormalTangentMirrorTest/AlphaBlendModeTest/TextureSettingsTest sweep is qualified in M10 |
| **M3 — Environment lighting** | Procedural room and sky, cube-UV capture and GGX prefilter, diffuse irradiance from the roughest level, hemisphere light, `environment-intensity` | Metal and dielectric spheres in `room` match Three.js `RoomEnvironment` + PMREM within tolerance; stats show one environment build across 30 camera moves |
| **M4 — Directional and spot shadows** | Depth passes, PCF, bias, cast and receive flags, skinned and mask casters, camera-independent caching | No acne or peter-panning on a sloped plane (contract fixture), soft edges within a measured penumbra width, a cutout leaf casts a cutout shadow, a skinned character's shadow follows its pose, orbiting re-renders no shadow map (stats) |
| **M5 — Point shadows and HDR images** | Cube shadow maps; `<environment src: ...>` and `s.environment('image', options)^`; a bounded Radiance RGBE decoder in `lib/image_hdr.cpp` | A lamp in a closed room shadows all six directions; an HDR studio environment lights the sphere grid against the oracle |
| **M6 — Adoption and speed** | Dreamhome: `standard` materials (wood and stone floors, metal fixtures, glass), the sun casting through windows, a `room` environment indoors, `neutral` tone mapping; the camera-only invalidation path (§5.7) | Release-build frame times recorded for Dreamhome orbiting with shadows on; a camera drag no longer re-projects (stats); Dreamhome's UI suite passes |
| **M7 — Documentation** | Design doc §4 element table, §5 shading, §14/§15 asset rows; `doc/Lambda_Packages.md` §27; this file becomes the implementation record | New example compiles and validates; local links/formal references/fences checked; package doc gate compared with its pre-edit failures (§11) |
| **M8 — Native property animation** | Bind and sample material metalness/roughness/emissive and light shadow softness through retained poses | Independent pixel changes for each property; authored-value restoration; range errors and seek recovery; retained projection/environment/GPU bytes |
| **M9 — MTL PBR conversion** | Factors, packed scalar maps and tangent-space normal maps using existing scene texture resources | Exact packed channel bytes and map-only defaults; property-order independence; invalid factor/file/image/sampler refusals; existing asset inputs remain covered |
| **M10 — Remaining qualification and snapshot cost** | Pinned original Khronos sample sweep and an opaque readback fast path | 99% RGB samples within 8 levels in all four models; byte-identical before/after images, paired release orbit measurements and retained cache/memory invariants |

## 9. Verification

- **Oracle.** Chromium with ANGLE SwiftShader and the pinned Three.js r186, as in Phase III, renders the same scene the native path renders. The vendored Three.js has no `GLTFLoader`, so instead of a GLB the oracle page builds its scene from the same plain description the Lambda fixture builds its native scene from (`test/scene3d/mod_pbr.ls`, `test/scene3d/oracle/`). Lights are multiplied by π for Three.js physical units (SP1). The glTF light mapping is covered by the `formats.ls` round trip instead. Phase III declared ≥99% of RGB channel samples within 8 levels. Native PBR will differ slightly (cube prefilter versus PMREM atlas, PCF kernels), so each milestone declares and records its own threshold, never below 97%, along with the measured value.
- **Contracts.** Negative and edge fixtures in `test/scene3d/contracts/`: invalid metalness or roughness, a missing map, too many casting lights, an unknown environment kind or tone-map curve, all with clear diagnostics.
- **Package goldens.** `test/lambda/scene3d/`: validator acceptance and rejection, helpers, glTF round trips (`formats.ls`).
- **Gates.** M0–M5: `make test-scene3d` and `test_scene3d_unit_gtest` at every milestone; `make test-radiant-baseline` at M1, M4 and M6; `make test-lambda-baseline` whenever the validator or importers change (M1, M2, M5). M6 adds the Dreamhome model golden and UI suite.
- **Performance.** Release build only (AGENTS rule 10), measured on the Phase III host with `make build-release-compile` (never `make release` in a worktree). Record main-pass, shadow-pass and environment-build times for Dreamhome and for the Phase III observatory-sized scene.

## 10. Decisions for the user

**Ruled by the user, 2026-10-26:** SP1 takes option (a); SP2–SP8 take the recommendations below.

| ID | Question | Options | Recommendation |
|---|---|---|---|
| **SP1** | Light units | **(a)** Keep today's native meaning (a white Lambert surface under an intensity-1 light reaches 1.0) and make `standard` agree with it; convert at the glTF boundary (÷π on import, ×π on export) so imported lights look as authored. **(b)** Adopt glTF/Three.js physical units everywhere, so every existing native scene gets π times darker until its intensities are raised. | (a): existing demos stay as they are and imports become correct. It does change the `formats.ls` golden and the brightness of already-imported glTF lights, which are π too bright today. |
| **SP2** | Material type name | `standard` (Three.js) or `pbr` | `standard`, with `physical` reserved for the extensions. |
| **SP3** | Shadow defaults | Lights opt in, meshes cast and receive by default; or everything opt-in, as in Three.js | Lights opt in (as in glTF and Three.js); meshes default to cast and receive, since a Lambda author who turns on a light's shadows expects everything to take part. |
| **SP4** | Tone-mapping default | `'none'` everywhere (no change to existing output); or `'neutral'` whenever a scene uses `standard` materials | `'none'`, with demos opting in: an implicit switch would make adding one PBR mesh change every other mesh's colours. |
| **SP5** | Shadow budget | Number of casting lights; maximum map sizes | Four casting lights; maps up to 4096² (cube faces up to 1024²). |
| **SP6** | First environments | Procedural `room` and `sky` first, HDR files in M5; or HDR files first | Procedural first: no asset or decoder needed, and they cover interiors and exteriors. |
| **SP7** | Where shading lives | Extend the native program with variants; or route native scenes through the Three.js shader chunks on the WebGL path | Native variants. §5 of the design doc keeps the native core free of any JS realm, and the native path must work without JS. |
| **SP8** | Two validators | Keep the Lambda validator (`model.ls`) and the native one in step, as today; or validate natively only | Keep both: the Lambda validator gives early, pure diagnostics (S12.1.1v2) and tests can run without graphics. |

## 11. Progress

### M0 — GL waist (2026-10-26)

`radiant/gl_core.hpp` / `gl_core.cpp`:

- **Draw bindings.** `NativeGlDraw` carries up to `NATIVE_GL_BINDING_LIMIT` (12) `NativeGlBinding {sampler, resource, kind}` entries in place of its `texture` and `data` fields. Binding *i* takes texture unit *i* and sets the named sampler to it, so a sampler's unit follows its name, not the binding order. Kinds: `TEXTURE` (an image or data texture), `CUBE` (the color cube of a cube target), `SHADOW` (the depth of a depth target, through a comparison sampler). A missing or mismatched resource, a null sampler name, too many bindings, or sampling the target currently being drawn (`context->drawing`, set by every native begin) rejects the draw with a diagnostic.
- **Texture color space.** `NativeGlSampler::srgb` (default true) selects `SRGB8_ALPHA8` or `RGBA8`, so data maps keep their stored values.
- **Depth targets.** `native_gl_depth_target(context, size, cube)`: `DEPTH_COMPONENT24`, compare mode `LEQUAL`, linear filtering (hardware 2×2 PCF). 2D maps clamp to a far-plane border, so anything outside the map reads as lit; cube maps clamp to edge. Up to 4096² (2D) or 2048² per face (cube).
- **Cube targets.** `native_gl_cube_target(context, size, levels)`: RGBA16F with up to 13 mip levels, seamless filtering, and a depth renderbuffer for capturing geometry.
- **Layer drawing.** `native_gl_begin_layer(context, target, face, level, background)` attaches one face and level. A depth target clears depth only; a cube target clears to any finite non-negative color (HDR). `native_gl_begin` and `native_gl_snapshot` reject layered targets: only the color drawing buffer publishes pixels.
- **Accounting.** Depth: `size² × 4 × faces` bytes. Cube: `Σ (size >> level)² × 6 × 8` bytes plus the `size² × 4` depth buffer. Both are released by the existing target teardown.

`radiant/scene3d.cpp` binds `image` (material texture or white) and, for skinned meshes, `palette`, through the new bindings. The shader and pass structure are unchanged.

**Evidence.** Six native scenes (Dreamhome, Ringworld, physics dominoes, Cube pets, `test/scene3d/skinning-palette.ls`, `test/scene3d/mixed.ls`) were rendered headless before and after the change. The renders are deterministic run to run, and all six PNGs are byte-identical across the change. `test_scene3d_unit_gtest`: 32/32 pass, including four new cases: `LinearDataTexturesSkipSrgbDecoding`, `DrawBindingsAssignUnitsByNameAndRejectMismatches`, `DepthTargetsCompareReadLitBeyondTheirEdgesAndStayOutOfSnapshots`, and `CubeTargetsKeepLinearHdrValuesPerFaceAndLevel` (which stores 3.0 in a face to prove the target is not clamped). `make test-scene3d`: 78/78 Radiant View Scene3D and event-sim cases, plus the 32 unit cases.

### M1 — standard material, direct light, tone mapping (2026-10-26)

- **Material and scene.** `<material type:'standard'>` with `metalness` (default 0) and `roughness` (default 1), both 0..1. `<scene3d>` takes `tone-mapping` (`'none'` default, `'neutral'`, `'aces'`) and `exposure` (default 1, greater than 0). As in Three.js `NoToneMapping`, exposure applies only with a curve. The background is never tone mapped. Both validators (`radiant/scene3d.cpp`, `lmd/package/scene3d/model.ls`) accept the new names and reject bad values, and the Lambda validator also rejects metalness or roughness on non-standard materials.
- **Shader variants.** One fragment source; `#define STANDARD` and `#define TONE_MAPPING 1|2` select up to six variants, compiled on first use and cached per entry (`programs[]`). Each variant used in a frame receives the scene uniforms once. `scene3d_uniform` skips uniforms a variant's compiler dropped. The light-falloff code is shared between Lambert and standard shading.
- **Shading.** r186's direct and ambient terms line for line (§5.1), including geometry roughness from view-space normal derivatives (`view` uniform) and the orthographic view direction (`eye.w = 0`). Native units (SP1 a): every light term carries π times the physical response, so the π of the Lambert BRDF cancels. Neutral and ACES curves are copied from r186's `tonemapping_pars_fragment`.
- **DFG table.** `scene3d_dfg_table()` integrates the split-sum table once per process: 16 × 16 texel centres over roughness and n·v, 4096 Hammersley samples each, height-correlated visibility. It matches r186's `DATA` table to a mean of 0.0015 (maximum 0.014 at one grazing texel) and uploads per entry as a filtered RGBA32F data texture (`native_gl_data_texture(..., filtered)`, a small M0 addition).
- **glTF.** Import divides `KHR_lights_punctual` intensities by π and export multiplies by π. Export writes `standard` factors (`lambert` still exports as metal 0, roughness 1). Importing glTF materials as `standard` stays in M2: glTF's default metallic factor of 1 would render imported models dark until M3 adds environment light.
- **Fixtures.** `test/scene3d/mod_pbr.ls` describes four cases (`direct`, `point`, `neutral`, `aces`) over a 5 × 5 sphere grid. `pbr-spheres.ls` renders one (`PBR_CASE`); it has no golden, under the `test/README.md` exemption for mesh-array fixtures. `test/scene3d/oracle/` dumps each description, renders it natively and in Chromium 154 with Three.js r186, and compares.

**Evidence.**

| Case | Channel samples within 8 levels | Mean absolute error (R, G, B levels) |
|---|---:|---|
| direct | 99.904% | 0.069, 0.061, 0.071 |
| point | 99.941% | 0.079, 0.063, 0.064 |
| neutral | 99.912% | 0.065, 0.053, 0.060 |
| aces | 99.934% | 0.067, 0.055, 0.051 |

The declared M1 threshold is 99%, the Phase III bar (`run.sh` default). The first comparison, without r186's multiple-scattering and (1 − F) terms, scored 95.8–96.7%; those terms closed the gap. `StandardMaterialsMatchThreeJsMetalnessRoughnessGrid` pins eight oracle-agreed pixels (native and Three.js within one level) in `make test-scene3d`, alongside the unit test `DfgTableMatchesThreeJsSplitSumIntegration`.

Six existing scenes stay byte-identical (Dreamhome, Ringworld, dominoes, Cube pets, skinning palette, mixed). `make test-scene3d`: 79/79 view cases and 33/33 unit cases.

Aggregate gates in the worktree:

- **`make test-lambda-baseline`.** Every suite passes except three cases:
  - `test_js_test262_prelim_gtest`: the worktree has no `test/js262` data link.
  - `dom_media_playback`: passes once `test/media` is linked absolutely, so it is environmental.
  - `tests_finder_io`: fails identically under the main checkout's own build, so it predates this work.
- **`make test-radiant-baseline`** (jQuery UI and media data linked for the run). 6,235 passed, 141 partially passing (the usual layout and render residue).
  - `test_css_cascade_memory_gtest` fails on the jQuery UI CSS budget, the pre-existing failure the Phase III record also notes.
  - `RadiantViewTest.ModuleFetchFailuresDispatchElementErrorsBeforeLaterModules` failed in the full run and passes alone: a load flake. Package goldens: `invalid.ls` gains six checks, and `formats.ls` shows imported lights at 6/π and 2/π and exported intensities π, 6 and 2, with `standard` factors in the exported material.

### M2 — texture maps, alpha mask, glTF materials (2026-10-26)

- **Material maps.** `standard` materials take `metal-rough-texture` (G roughness, B metalness), `normal-texture` with `normal-scale`, `occlusion-texture` with `occlusion-strength` (R), and `emissive-texture`, each naming a `<texture>`; other material types reject them. Shading follows r186's `roughnessmap`, `metalnessmap`, `aomap` (ambient light only, until M3), `emissivemap` and `normal_fragment_maps` chunks. An absent map binds the white texture, so its factor applies unchanged; only the normal map is gated (`maps.x`).
- **Normal frames.** With `tangents` on the geometry (xyzw, unit and w = ±1, validated), the vertex stage carries tangent and bitangent (skinned like normals). Without them, the fragment stage builds r186's `getTangentFrame` from screen-space derivatives, in world space, which is equivalent because the construction rotates with the frame. Tangents occupy a vertex lane after colors, so meshes without them keep their exact upload.
- **Color spaces.** A `<texture>` uploads once per color space it is read in: sRGB for base color and emission, linear for metal-rough, normal and occlusion maps. `color-space: 'srgb' | 'linear'` on the texture overrides the slot.
- **Alpha mask.** `alpha-mode: 'mask'` with `alpha-cutoff` (default 0.5) on any material type: cut out below the cutoff, opaque where drawn. The first version discarded through a uniform in every program and changed Ringworld's double-sided rings by up to 51 levels in 1,383 pixels, though the cutoff was 0 there: a shader that can `discard` changes multisampled depth even where it never does. Masks therefore compile as their own variant (`#define MASK`). Variants are now material model × tone curve × mask, up to 12, compiled on first use.
- **glTF import.** Metallic-roughness materials arrive as `standard` (unlit ones stay `basic`). They carry `metallicFactor` and `roughnessFactor` (defaults 1, as glTF says), all four maps with scale and strength, MASK with `alphaCutoff`, emissive factor times map, and `TANGENT` (float or quantized signed-normalized, renormalized; ignored without normals, per glTF §3.7.2.1). A material has one set of UVs, so all its maps share the first map's `KHR_texture_transform`; a differing one warns. The "approximated by Lambert" and "secondary maps not rendered" warnings are gone. `lambda.scene3d.asset` now prefixes the map references along with the other resource ids. Of the repository's glTF files, only `lit.gltf`'s `glow` material relied on the old approximation; under glTF's defaults it is now a rough metal.
- **glTF export.** Exports the maps (`metallicRoughnessTexture`, `normalTexture.scale`, `occlusionTexture.strength`, `emissiveTexture`), MASK with its cutoff, and `TANGENT`. Both sides keep the bitangent toward the image's top, so the handedness carries over unchanged through the UV flip.
- **Fixtures.** No glTF sample models were downloaded. `test/scene3d/assets/pbr/_generate.py` draws six 128² textures (checker, metal-rough, dome normals, occlusion, emissive stripes, cut-out leaf; about 10 KB) and `pbr.gltf` (a tangent quad with a fully mapped MASK material, a defaults material, and a mixed-transform material). The oracle's `maps` case shows one map per sphere, with derivative and vertex-tangent normal maps side by side. `pbr-maps.ls` renders it for CI.

**Evidence.** Three.js r186 oracle, `maps` case: 99.938% of channel samples within 8 levels, mean absolute error 0.045, 0.035, 0.039 levels. The four M1 cases are unchanged (99.904–99.941%). `StandardMaterialMapsMatchThreeJs` pins twelve oracle-agreed pixels, including a hole the mask cuts. The six reference scenes stay byte-identical. `make test-scene3d`: 80/80 view cases and 33/33 unit cases. `test_input_model_gtest`: 47/47, with two expectations updated to the new behaviour: the imported directional intensity is now 2/π (SP1), and textured emission is mapped instead of dropped. Package goldens: `invalid.ls` has 35 checks; `formats.ls` now imports `pbr.gltf` (materials, maps, mask, tangents, the shared transform) and exports it back. `make test-lambda-baseline` fails the same three environmental or pre-existing cases as in M1, plus `Tiers/HotwireHttpTest.ClientAndServer/1` (mir), an HTTP timing case that passes when rerun alone.

### M3 — environment lighting (2026-10-27)

- **Schema.** `<environment>` holds ordinary scene content (groups, meshes, lights, resources) and is captured from its origin; it is never drawn in the view, may not hold cameras or other environments, and takes `background`, `near` and `far` for its capture. `<scene3d environment: id>` names the one that lights the scene, scaled by `environment-intensity`; unnamed environments are validated but neither captured nor drawn, so a page can switch environments by changing one attribute. `type:'hemisphere'` lights take `color` (sky), `ground-color` (default white, as Three.js) and `intensity`; the sky lies toward the light's position (straight up at the origin). One hemisphere light per scene and per environment, outside the 8-light quota. Both validators learn all of it.
- **Layers.** Draw records and lights carry a layer: the scene, the lighting environment, or an unused environment. The projection keeps one light set per layer (`Scene3dLights`, which also replaced the loose light arrays), and pose refresh routes animated lights by their nearest `<environment>` ancestor.
- **Capture and prefilter.** The cube-UV atlas is r186's: 768 × 1024 RGBA16F, 256-texel faces, levels halving to 16 texels and six more 16-texel roughness levels (11 in all). The six faces are drawn with Three.js's cube-camera conventions into their atlas regions, with depth, by the ordinary draw path (`scene3d_draw_mesh`, now shared by the view and the capture), with tone mapping off. Each further level GGX-filters the one above through a ping-pong target with 256 Hammersley VNDF samples and r186's incremental roughness (`sqrt(t² − s²) · t · 1.25`), then copies back; the level quads and the shader are ports of `_createPlanes` and `_getGGXShader`. No blur pass (`fromScene` sigma 0).
- **Shading.** A fourth variant bit, `ENVIRONMENT` (24 variants), adds r186's `cube_uv_reflection_fragment`. Standard materials take `getIBLIrradiance` (the roughest level, not SH: the deviation from §5.3's plan) and `getIBLRadiance` along the roughness-bent reflection, weighted by the dielectric and metallic split-sum terms of `RE_IndirectSpecular_Physical`; occlusion maps now darken environment light too, with `computeSpecularOcclusion` on the specular part. Lambert materials add the environment irradiance, as r186 does for scene environments. The hemisphere light joins the ambient path of both models. Material `env-intensity` was dropped: r186 applies only the scene intensity.
- **Caching.** The capture key hashes everything a capture reads and no GPU identity: the origin, background, clip planes, the environment's lights, and each environment mesh's geometry content hash (xxHash3 of the uploaded vertices and indices), instance hash, world matrix, visibility, morph and bone pose, material factors and texture sources. Camera moves and other re-projections keep the atlas; editing the environment, or losing the context, rebuilds it. `Scene3dStats::environment_builds` counts builds and the event simulator reports it.
- **Package.** `lambda.scene3d.environment` builds `room` (a port of r186 `RoomEnvironment`: the point light's 900 becomes 900/π under SP1; the six panels are black Lambert with emission) and `sky` (a vertex-colored dome from horizon to zenith over a flat ground, and an emissive sun disc; options `zenith`, `horizon`, `ground` as sRGB triplets, `sun-direction`, `sun-size`, `sun-intensity`). The unit-diameter sphere generator moved from the PBR fixture into `lambda.scene3d.shape` and backs the new `s.sphere`.
- **Fixtures.** `mod_pbr.ls` gains `hemisphere` (the grid under the sun plus a hemisphere light) and `room` (lit only by the room environment). The oracle page now loads Three.js through an import map so it can use the vendored `RoomEnvironment` and `PMREMGenerator.fromScene(room, 0)` itself, rather than a port. `pbr-room.ls` renders `room` for CI; it has no golden, under the mesh-array exemption.

**Evidence.** Three.js r186 oracle: `room` 99.870% of channel samples within 8 levels (mean absolute error 0.16, 0.13, 0.12 levels), `hemisphere` 99.920%; the five earlier cases are unchanged (99.904–99.941%). `StandardMaterialsMatchThreeJsRoomEnvironment` pins eleven oracle-agreed pixels, then moves the camera 30 times: the projection is rebuilt 30 times and the environment never (`environment_builds` stays 1). The new unit case checks exact values: a hemisphere light seen side-on gives the half-way color, a uniformly glowing environment lights Lambert exactly like ambient light of its radiance (also under `environment-intensity`), camera changes don't capture, editing the environment and losing the context do, and the four invalid structures fail with their diagnostics. `FloatTargetsDrawRegionByRegionAndKeepHdrValues` covers the new waist. The six reference scenes stay byte-identical. `make test-scene3d`: 81/81 view cases and 35/35 unit cases. Package goldens: `invalid.ls` has 42 checks. `make test-lambda-baseline` fails only the same three environmental or pre-existing cases as in M1 (`tests_finder_io`, `dom_media_playback`, `Test262Prelim.RunnerContracts`).

### M4 — directional and spot shadows (2026-10-27)

- **Schema.** `<light>` takes `cast-shadow` (directional and spot only; point shadows are M5), `shadow-map-size` (512–4096; default 2048 directional, 1024 spot), `shadow-intensity` (0..1, default 1, as r186's `shadow.intensity`), `shadow-bias` (depth units), `shadow-normal-bias` (metres), `shadow-softness` (PCF radius in texels, default 1.5) and `shadow-extent` (a directional light's half-size; default fits the scene). `<mesh>` takes `cast-shadow` and `receive-shadow`, both default true (SP3). At most four casting lights (SP5), none inside an environment. Both validators check all of it.
- **Frames.** Each casting light gets r186's camera: at the light, looking at its target, up +y (with `Matrix4.lookAt`'s nudge when the view is vertical). Its frustum is fitted every frame to the casters and receivers (the eight corners of every instance's bounds, and of every bone's reach for skinned meshes) in light space: depth with a 1% margin always, sideways unless `shadow-extent` is given; a spot light uses its cone (fov 2 × angle) and reaches `distance`, or the scene. Geometry bounds and batch instance matrices are now kept on the draw records for this.
- **Depth passes.** One `DEPTH_COMPONENT24` compare-mode map per casting light (M0's depth target), drawn by two small depth programs that share the scene's vertex stage (so skinned, morphed and instanced casters pose as in the view) and cut masked texels out. As in r186, casters draw their opposite side, front-faced materials their far faces, and nothing adds a polygon offset; unlike r186, blended and depth-only materials don't cast (§5.5). A map re-renders only when its key changes: the light's clip matrix and every caster's draw-record key, shared with the environment capture (`scene3d_mesh_key`). Camera moves never redraw it; caster poses and edits do. `Scene3dStats::shadow_passes` counts renders and the event simulator reports it.
- **Receiving.** A fifth variant bit, `SHADOWS` (48 variants), adds per-vertex shadow coordinates from the normal-biased position (`shadowmap_vertex`) and r186's PCF `getShadow`: five Vogel-disk taps turned by interleaved gradient noise, each a hardware 2×2 comparison, lit outside the frustum, mixed by `shadow-intensity`. It scales each casting light's direct term for standard and Lambert materials on meshes that receive. Four `sampler2DShadow` slots bind the maps (a cleared 1-texel map fills unused ones), which brings the draw-binding count to its limit of 12.
- **Fixtures.** `mod_pbr.ls` gains `shadow` (a directional sun) and `spot` (a spot light) over a stage of three spheres and a cut-out leaf on a tilted ground. Its sphere list became a general `objects` list with rotations. The oracle page frames Three.js's shadow cameras with the same fit, using the same bounds. `pbr-shadow.ls` and `pbr-spot.ls` render them for CI; `shadow-skinned.ls` slides a skinned tile under a casting sun.

**Evidence.** Three.js r186 oracle: `shadow` 99.786% of channel samples within 8 levels, `spot` 99.884%; the seven earlier cases are unchanged (99.870–99.941%). The tilted ground shows no acne in either, and the leaf casts its cut-out. `DirectionalShadowsMatchThreeJsAndSurviveCameraMoves` pins nine oracle-agreed pixels (sphere and leaf shadows, lit ground, spheres), then orbits the camera 20 times: 20 re-projections, no shadow pass. `SpotShadowsMatchThreeJs` pins nine more. `SkinnedCasterShadowFollowsItsPose` seeks a clip: the shadow moves with the tile, the map re-renders and the scene isn't re-projected. The new unit case checks exact values: a shadowed point gets no direct light, `shadow-intensity` 0.5 halves it, opting out of casting or receiving restores it, camera changes don't redraw the map and caster moves do, and point casters and a fifth casting light fail. The six reference scenes stay byte-identical. `make test-scene3d`: 84/84 view cases and 36/36 unit cases. Package goldens: `invalid.ls` has 49 checks. `make test-lambda-baseline` fails only the three known cases. `make test-radiant-baseline` passes 6,224 of 6,387: the failures are the pre-existing `css_cascade_memory` case and media cases (11 video tests, 4 media UI fixtures) that all pass once `test/media` is linked absolutely, the usual worktree data gap.

### M5 — point shadows and HDR images (2026-10-27)

- **Point shadows.** `cast-shadow` now works on point lights too (maps up to 1024 texels a face, SP5). Each renders r186's six 90° faces (`_cubeDirections`, `_cubeUps`) into a compare-mode depth cube (M0's cube depth target), reaching `distance` or the farthest caster or receiver, with the near plane at a thousandth of that. Receivers port r186's PCF `getPointShadow`: the face's perspective depth along the offset's largest component, five Vogel taps across the direction's tangent plane. The shadow variant now records which of the four slots hold cubes (16 layouts, `#define SHADOW_CUBES`), so directional, spot and point maps mix freely; programs stay lazily compiled.
- **Back-side normals.** r186 flips the vertex normal of back-side materials (`FLIP_SIDED`), so their normal bias points inward. The native vertex stage now matches; before, a room's walls (back-side) grew their shadows by the bias instead of shrinking them, a one-pixel outline difference that the point-light oracle exposed.
- **HDR images (user ruling, 2026-10-27).** `<environment src: 'file.hdr'>` lights a scene from an equirectangular Radiance image in place of scene content (content and `src` are exclusive), and `s.environment('image', {src})` builds one. This replaces the plan's `kind:'image'`, after M3 dropped `kind`. `lib/image_hdr.{h,cpp}` decodes `-Y H +X W` images with flat or adaptive run-length scanlines, converting RGBE as r186's `RGBELoader` does (mantissa / 255 × 2^(e − 128)) and refusing XYZE, other orientations, truncation and images above 4096 texels. The entry keeps the decoded RGBA32F texture by resolved path across re-projections; local files only, through `document_dependency_admits`. The capture unwraps it into the atlas's top level with a port of `EquirectangularToCubeUV`, then runs the same GGX chain. As in Three.js the cube is 256 texels for a 1024-wide image; native always uses 256. The capture key holds a hash of the decoded pixels.
- **Fixtures.** `mod_pbr.ls` gains `lamp` (a point light inside a closed room, spheres toward the floor, ceiling and three walls) and `hdr` (the sphere grid in a generated studio). `test/scene3d/assets/hdr/_generate.py` writes `studio.hdr` (1024 × 512, run-length encoded, 50 KB, values up to 26): grey walls, a brown floor, a warm key soft box, a cool fill, a window strip and a ring of top light. No HDR loader is vendored with Three.js, so the oracle page decodes the file with a few lines following `RGBELoader` (half floats, flipped rows) and passes it to `PMREMGenerator.fromEquirectangular`, an independent second decoder. The lamp fixture first used a −0.0005 depth bias, which a point light's perspective depth turns into a metre at wall distance and hollow shadows in both renderers; it now uses a normal bias, the better choice for point lights.

**Evidence.** Three.js r186 oracle: `lamp` 99.935% of channel samples within 8 levels, `hdr` 99.854%; the nine earlier cases are unchanged (99.786–99.941%). `PointShadowsMatchThreeJsOnEveryRoomSurface` pins shadows on the left wall, right wall, ceiling, floor and back wall, then moves the camera 10 times without a shadow pass. `StandardMaterialsMatchThreeJsHdrEnvironment` pins diffuse points and soft-box reflections, then 10 camera moves without an environment build. New unit cases: the decoder (flat and run-length scanlines with exact values, six refusals with their reasons), point cube shadows on either side of the plane with exact values and mixed with a directional caster, and image environments (loaded once across camera moves; content beside `src`, a missing file and a PNG all fail with clear diagnostics). The six reference scenes stay byte-identical. `make test-scene3d`: 92/92 view cases and 39/39 unit cases. Package goldens: `invalid.ls` has 53 checks. `make test-lambda-baseline` passes in full (6,197 runtime and 2,112 input tests) in the main checkout, whose test data links resolve.

### M6 — Dreamhome adoption and camera-only rendering (2026-10-27)

- **Invalidation.** `scene3d_snapshot` compares a bounded source key when the document epoch changes. It includes node identities, child order, attributes and text, excluding only the cameras' view/transform attributes. Immutable compound attribute identities keep large mesh arrays out of the per-move walk; only the hash survives (D4.5.2). Camera edits and sibling UI writes retain the projection, refresh cameras through the shared dynamic-object reader, and render the main pass. An animation/physics generation change still refreshes all poses and checks the environment/shadow keys. Mixed edits, changed camera IDs, topology changes, rebuilds and context loss retain the full projection/validation path; invalid camera parameters clear the snapshot and recover after repair (D4.5.1v4).
- **Dreamhome.** `mod_scene.ls` shares standard materials by colour and finish: individual floor roughness for wood, tile, stone and carpet, rough cloth, metallic fixtures and smooth transparent glass. A procedural room environment supplies reflections, neutral tone mapping preserves highlights, and the sun uses a 2048² shadow map with normal bias. Wall geometry already cuts openings; blended glass casts no shadow, allowing sunlight through the windows (SP3). Lamps retain their existing emission and point lighting.
- **Acceptance fixtures.** The Dreamhome model golden checks validation in day, evening, cutaway and walk modes, GLB export, standard finishes, environment selection and the sun settings. `test/ui/dreamhome/pbr_orbit.json` captures eight real pointer moves: pixels change, snapshots advance, and projection, environment, shadow and GPU-memory deltas stay zero. The room/HDR/directional/point oracle pixel fixtures now require zero camera re-projections too. A native regression checks retained allocations, camera-error recovery, mixed material edits, changed camera IDs/children and parent transforms.
- **Measurement.** The existing release-only `measure_frames` operation now accepts `source:"scene3d"`, reporting projection/snapshot/environment/shadow deltas alongside its frame p50/p95 and GPU accounting. The orbit replay optionally measures 120 camera-write plus whole-page-render frames with sun shadows enabled; it requires 120 snapshots and zero projection, environment, shadow or GPU-memory growth. Reproduction commands are in `test/demo/dreamhome/README.md`.

**Release measurement.** Apple M4, 16 GiB RAM; `make build-release-compile`, one isolated 120-frame orbit replay at a 1280 × 800 viewport (native scene raster 732 × 359, 416 mesh records), with the sun's shadows enabled. Whole-page frame **p50 78.205 ms / p95 84.828 ms**; camera writes **p50 0.222 ms / p95 0.258 ms**. Exactly 120 snapshots, zero projection/environment/shadow rebuilds, and zero GPU or native graphics CPU-byte growth. Raw values: `temp/dreamhome-orbit-metrics.json`; replay log: `temp/m6-release-orbit.log`. Further reduction of whole-page frame cost remains follow-on work.

**Verification.** `make test-scene3d` passes all 92 viewer/control cases and 41 native units; the Dreamhome model golden passes interp/auto/jit, and both Dreamhome UI replays pass on the release executable (planner 40/40 assertions, orbit 20/20 with measurement enabled). The initial page capture was inspected for visible furnishings, reflections and cast shadows. Integer-cast lint and `git diff --check` pass. The required Radiant baseline found six WPT layout failures and a camera-diagnostic regression in the new fast path. The diagnostic is repaired and the complete Scene3D gate rerun above passes. The remaining layout classification exactly matches the archived pre-M6 `temp/scene3d-shaders-merge/native-baseline/` run: text 1, inline 1, sizing 4; 3,151 passed and 137 partial. This is the aggregate qualification limit, recorded without changing layout code or references.

### M7 — documentation (2026-10-27)

- **Outcome and scope.** Synchronize the main design's element table, native shading/caching description and raw/renderable glTF profiles with `scene3d_material`, `scene3d_snapshot`, `Gltf::load_materials` and `GltfExport` in the current tree. Add the package guide's Scene3D overview, API, runnable PBR/room/shadow example, tests and export limits. Correct the raw OBJ/MTL examples' content separators and make the asset example return its viewport. Retain the existing M6 performance and aggregate evidence rather than treating documentation as a new engine qualification.
- **Implementation record.** Mark §1's original Lambert-only table as historical; replace superseded SH/linear-point-depth plans with the implemented PMREM/perspective-depth contracts, and distinguish planned gaps from delivered features. No formal ruling changes: pure construction remains **S12.1.1v3**, package placement **D7.2.4v4**, and native retention **D4.5.1v4 / D4.5.2**.
- **Acceptance and budget.** Compile and run the new example through interp/auto/jit, compile the main design's Lambda fences, check local links/anchors and fence balance, compare the affected package-doc gate with its pre-edit result, and run `git diff --check`. No engine build or engine baseline is required for these documentation-only changes (`radiant/README.md`).

**Verification.** The new PBR/room/shadow example constructs and normalizes
successfully in interp, auto and jit. All five Lambda fences in the main
design pass; its asset-loading example runs against the real fixture because
`--dry-run` substitutes synthetic `input()` data and cannot select the model's
clip. Local links/anchors, balanced fences, current formal references and
`git diff --check` pass. Historical links to absent local ANGLE measurement
artifacts now name their original paths and state that the artifacts are
unavailable in this checkout; those measurements were not refreshed.

`make check-doc-code ARGS='--filter Lambda_Packages'` has 32 clean plain
units, one honored expected error, eight skips and five failures. Before the
edit it had 31 clean plain units and the same expected error/skips/failures;
the five failing units have unchanged source and diagnostic sets (E318,
E201, E7801, E7801, E7800). This is the documentation gate's existing
qualification limit, not a green aggregate. Logs:
`temp/dreamhome-m7-doc-before.log`, `temp/dreamhome-m7-doc-after.log`;
focused example and structure results: `temp/dreamhome-m7/`.

### M8 — native PBR and shadow-property animation (2026-10-27)

The original regression failed with `invalid scene animation clip, track or
binding` at `surface.material.metalness`. `scene3d_animation_initial` now
shares a numeric-property table, admitting standard-material metalness and
roughness plus light shadow softness, and its color reader admits emissive.
`scene3d_material_values` reads color, opacity, PBR factors and emission for
both projection and retained pose refresh. Shadow softness uses the existing
animated numeric reader. No authored scene data is mutated (**S12.1.1v3**);
retained state continues to use native ownership (**D4.5.1v4 / D4.5.2**).

`PbrTracksChangePixelsAndRestoreAuthoredValuesWithoutReprojection` separately
drives all four properties, verifies changed pixels and restored image bytes,
and pins projection/environment/GPU-byte reuse. Shadow softness changes PCF
without redrawing the depth map. `PbrTrackRangesRejectInvalidSamplesAndRecoverAfterSeek`
checks out-of-range samples, recovery and refusal of PBR tracks on Lambert.
The native suite passes 43/43 before the additional M10 snapshot case. Logs:
`temp/dreamhome-continuation/animation-before.log`, `animation-after.log`,
`animation-ranges.log`, `scene3d-units-after.log`.

### M9 — OBJ/MTL PBR material conversion (2026-10-27)

`wave_materials` now converts the planned PBR keys from the
[original MTL extension](https://ben3d.ca/blog/extended-wavefront-obj-mtl-for-pbr).
Explicit PBR keys select standard shading regardless of `illum` order.
The existing texture-option/resource path is shared by diffuse, normal and
packed maps; normal maps support `-bm`. Scalar images load through the normal
dependency boundary, decode within 4096-axis/64 MiB limits and stream into a
packed PNG data URI. They read R without color decoding, write roughness to G
and metalness to B, and leave missing channels white. The generated texture
is explicitly linear. Matching dimensions and wrap modes are required;
incompatible inputs raise errors. Values and URIs remain Input-owned
(**D4.1.3**); decoder scratch is released before return.

All three new `SceneAssetMtl*` cases failed before the implementation. The
full model-input suite now passes 50/50, covering factors, normal scale,
ordering, exact packed bytes, map-only defaults, invalid factors, missing or
corrupt images, mismatched dimensions and incompatible wrapping. Both
single-map cases check that the absent channel stays white. Existing OBJ/A3D/glTF/STL/PLY
profiles stay in the same gate. Logs:
`temp/dreamhome-continuation/mtl-before.log`, `model-input-after.log`.

### M10 — external models and bounded snapshot optimization (2026-10-27)

The new `test/scene3d/oracle/gltf_samples.py` downloads the four original
Khronos models at `8f492390ddc7a2ae221a1369ef560589cf8c70fb` into `temp/`,
verifies Git blob identities and records SHA-256 hashes and model licenses.
It takes GLTFLoader and its dependencies from the integrity-checked official
Three.js 0.186.1 package without changing the vendored copy. The existing
browser capture server is reused. Three.js computes one shared framing;
both renderers independently load the original asset with matched lights
(SP1) and a 512 × 384 viewport. All four pass the declared 99% gate (RGB
samples within 8 levels): DamagedHelmet 99.743%, NormalTangentMirrorTest
99.890%, AlphaBlendModeTest 99.951%, TextureSettingsTest 99.925%. Native
images and the Dreamhome page capture were visually inspected. Manifest,
captures and comparisons: `temp/gltf-samples/`; runner log:
`temp/dreamhome-continuation/gltf-models.log`.

Release profiling uses the private `temp/dreamhome-validation` checkout so
unrelated active work cannot replace its executable. The control includes
M8/M9. A four-second sample after the orbit warmup identifies full-page
reflow/SVG work and snapshot conversion. A link map was generated by relinking
the same objects; the `__TEXT,__text` hash is identical, so its addresses
resolve the sampled binary. `native_gl_snapshot` unnecessarily decoded and
re-encoded all opaque sRGB pixels. At alpha 255 that conversion is the
identity; the fast path retains the framebuffer byte and preserves the
translucent conversion. This is a bounded readback optimization, not a claim
that the page's remaining layout/SVG costs are solved. Artifacts:
`temp/dreamhome-continuation/orbit-sample.txt`, `orbit-symbolicated.txt`.

**Paired release measurement.** Apple M3, 24 GiB RAM (verified with `sysctl`);
the historical M6 record names a different M4/16 GiB host. Three alternating control/candidate pairs,
120 camera frames per run, at 1280 × 800 (scene raster 732 × 359, 416 mesh
records, sun shadows enabled). No builds or other test runs overlapped these
measurements. The profiled run is excluded; this compares the two freshly
frozen binaries, not the older M6 recording.

| Pair | Control frame p50 / p95 (ms) | Candidate frame p50 / p95 (ms) |
|---|---|---|
| 1 | 93.458 / 94.551 | 83.799 / 85.999 |
| 2 | 96.265 / 98.597 | 84.971 / 88.001 |
| 3 | 94.776 / 103.181 | 82.805 / 84.509 |

The median run p50 improves **94.776 → 83.799 ms (11.6%)**; median p95
**98.597 → 85.999 ms (12.8%)**. All six replays pass 20/20 assertions,
produce exactly 120 snapshots and zero projection/environment/shadow rebuilds,
and show zero GPU/native-graphics CPU-byte growth. Initial and final PNG files
are byte-identical across all six runs. The four original-model native PNGs
also retain their pre-optimization SHA-256 values. Raw timings, binary hashes
and image hashes: `temp/dreamhome-continuation/orbit-paired.json`.
`SnapshotsPreserveEveryOpaqueSrgbByteAndPremultiplyTranslucentRows` supplies
the independent full-byte-range/partial-alpha native regression.

**Final focused qualification.** Native Scene3D passes 44/44 and model input
50/50. The new MTL package golden passes interp/auto/jit; the frozen release
passes the planner's 40/40 assertions, the six orbit runs above (20/20 each)
and all four original-model comparisons. Integer-cast lint, Python/Node syntax
checks, 156 local documentation links/anchors, fence balance and
`git diff --check` pass. Final focused logs and the planner result are under
`temp/dreamhome-continuation/`.

**Required aggregate qualification.** The private checkout starts at
`3622887641015e5998b34f6c1a81dcd5d32a1bda` and includes only this continuation's
changes. `make test-lambda-baseline` passes 8,310/8,311: input 2,112/2,112,
runtime 6,198/6,199. The sole failure, `tests_greetings_model`, returns errors
for undo/redo. It fails alone and produces byte-identical output with the
unmodified base executable from the same checkout.

`make test-radiant-baseline` reports 6,348 passed, seven failed, 141 partial
and two skipped (6,498 total). All 729 UI tests pass. The layout component
has the same M6 result: 3,151 passed, 137 partial and six failures (text one,
inline one, sizing four). The remaining failure is
`CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`: CSS-role live
bytes exceed the unchanged budgets for jqueryui, linuxmint and bschool,
in both initial and recascade phases. The current and freshly built base
executables reproduce all six diagnostics with identical byte counts;
neither the budgets nor references were changed. Base-control experiments
restore the continuation's sources and qualified executable afterward.

The package documentation gate retains M7's result: 32 clean plain units,
one expected error, eight skips and the same five failures. Aggregate logs,
summaries and base comparisons are in `temp/dreamhome-continuation/`:
`lambda-baseline.log`, `radiant-baseline.log`, `packages-doc-final.log`,
`greetings-base-comparison.json` and `css-memory-base-comparison.json`.

## 12. Remaining work and risks

The delivered native/Dreamhome profile is qualified above. The following
follow-on work and qualification limits remain.

- **Whole-page frame cost.** M10 delivers the qualified opaque readback improvement, with a fresh paired median run p50/p95 of 83.799/85.999 ms. Full-page reflow and SVG work remain substantial in the source-resolved sample; smooth-frame performance is not complete. Keep the 120-snapshot/zero-rebuild and memory-growth invariants when selecting the next bounded optimization. The old M6 recording is historical, not a causal baseline for M10.
- **MTL profile limits.** Scalar roughness/metalness maps currently require equal dimensions and wrapping, and use the R channel. Broader texture transforms/options, sheen and other physical extensions remain outside this conversion profile; the planned factors/maps/normal keys are delivered in M9.
- **Aggregate qualification limits.** The Lambda gate retains the base-confirmed Greetings undo/redo failure; Radiant retains six WPT layout failures and the base-confirmed CSS-memory failure. The package documentation gate retains five existing failing examples. Counts, controls and artifacts are recorded in M10 above.

- **Large scenes.** A shadow pass costs another draw of every caster; cityscape's 1.66M vertices would double its cost. Shadows are opt-in per light, and cascades with caster culling are follow-on work.
- **Oracle drift.** Prefiltering and PCF differences may exceed a tight threshold; thresholds are declared and recorded per fixture, never relaxed silently.
- **Platforms.** Only macOS GL 4.1 is verified (design doc §13). Depth compare samplers, cube maps and RGBA16F render targets are core GL 3.3, but other desktops stay unverified until tested.
- **Shader variants.** More variants mean more compile time and more cache entries; feature bits stay few, and programs cache per context.
- **Light units (SP1).** Option (a) retains authored native brightness and converts glTF lights at the boundary. M1 records the changed import/export goldens.

# Radiant Scene3D — PBR materials and shadow maps for native scenes

**Status:** M0–M3 done, 2026-10-27 (§11); M4 (directional and spot shadows) next. Decisions SP1–SP8 (§10) ruled by the user on 2026-10-26: SP1 option (a), and SP2–SP8 as recommended.

**Scope:** the native `<scene3d>` path that Lambda scenes use (`radiant/scene3d.cpp`, `lmd/package/scene3d/`). This is the "additional native material parity" that the [3D and WebGL design](../radiant/Radiant_Design_WebGL.md) leaves as follow-on scope after Phase III (§5 "Native scene processing", §8 "Further coverage after Phase III"). The JS WebGL path is out of scope: it already runs unmodified Three.js `MeshStandardMaterial`, PMREM environments and PCF shadows ([Phase III record](Radiant_WebGL_Phase3.md)).

## 1. Why, and where things stand

Native scenes look flat. Dreamhome (`test/demo/dreamhome/`) shows the ceiling: wood, metal, glass and fabric all shade alike, nothing casts a shadow, and sunlight through a window lights nothing on the floor. Imported glTF models lose most of their look.

| Area | Today | Evidence |
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
| **S12.1.1v2** | Lambda builds pure scene data; shadow maps, environment prefiltering and shading are host effects in Radiant. |
| **D7.2.4** | GPU resources (shadow maps, prefiltered environments) belong to the renderer, never to module state. |
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

All new attributes are optional; omitting them reproduces today's output exactly. The validator in `lmd/package/scene3d/model.ls` and the native validator in `scene3d.cpp` both learn them (SP8).

| Element | Additions |
|---|---|
| `<material type:'standard'>` | `color`, `texture` (base color, sRGB), `metalness` (default 0), `roughness` (default 1, as glTF), `metal-rough-texture` (glTF packing: G roughness, B metalness, linear), `normal-texture` + `normal-scale`, `occlusion-texture` + `occlusion-strength` (R channel), `emissive-texture`. (A per-material `env-intensity` was dropped in M3: r186 applies only the scene's `environmentIntensity` to scene environments.) Existing `opacity`, `alpha-mode`, `side`, `emissive`, `emissive-intensity` keep their meaning. |
| Any `<material>` | `alpha-mode:'mask'` with `alpha-cutoff` (default 0.5): alpha-tested, opaque depth, casts cutout shadows. |
| `<texture>` | `color-space:'srgb'|'linear'`. Defaults follow the slot (base color and emissive sRGB, data maps linear), so authors rarely set it. |
| `<geometry type:'buffer'>` | Optional `tangents` (xyzw per vertex, w = handedness). Without them, normal maps use a derivative-based frame (§5.2). |
| `<light>` | `cast-shadow` (default false, SP3), `shadow-map-size` (512–4096; default 2048 directional, 1024 spot and point), `shadow-bias`, `shadow-normal-bias`, `shadow-softness` (PCF radius in texels, default 1.5), `shadow-extent` (directional half-size in metres; default fits the scene). New `type:'hemisphere'` with `color` and `ground-color`: an ambient-like light outside the 8-light quota. |
| `<mesh>` | `cast-shadow`, `receive-shadow` (both default true; they only matter once a light casts). |
| `<scene3d>` | `environment` (the id of an `<environment>`), `environment-intensity` (default 1), `tone-mapping` (`'none'`, `'neutral'`, `'aces'`; default per SP4), `exposure` (default 1). |
| `<environment>` (new; in the scene, a group or `<resources>`) | Ordinary scene content (groups, meshes, lights, resources; no cameras, no nested environments), captured from the environment's origin and never drawn in the view. `background` (default black), `near` (0.1), `far` (100). As built in M3, `room` and `sky` are Lambda helpers that fill one, not native kinds; `src` for equirectangular HDR images stays M5. |
| Animation tracks | `material.metalness`, `material.roughness` (number), `material.emissive` (color); `light.shadow-softness` (number). The Phase III binding table already lists metalness and roughness for the JS bridge. |

Helpers in `lmd/package/scene3d.ls`: `s.environment(kind, options)` returns a ready `room` or `sky` environment (id defaults to the kind); `s.sphere(rings, segments, options)`; `s.material('standard', {...})` already works through the generic constructor.

## 5. Rendering design

### 5.1 Shading model

Follow the glTF 2.0 reference BRDF as Three.js r186 implements it, so Three.js can serve as the oracle (§9): GGX distribution, height-correlated Smith visibility, Schlick Fresnel with F0 = mix(0.04, base, metalness), Lambert diffuse on (1 − metalness) · base weighted by (1 − F) of the dielectric reflectance. Direct specular is scaled by r186's multiple-scattering compensation, and ambient light by its single- and multiple-scattering terms. Both read a 16 × 16 split-sum DFG table, which Radiant generates natively (M1 record, §11) rather than copying, and which also serves the image-based term in M3. `lambert` and `basic` keep their current equations; Lambert additionally receives shadows.

Light units are SP1. Today a white Lambert surface lit head-on by an intensity-1 light reaches 1.0, which matches Three.js's pre-r155 "legacy" convention; the glTF and current Three.js physical convention gives 1/π. A `standard` material must agree with `lambert` in the same scene, and imported glTF lights must arrive at the brightness their authors intended.

### 5.2 Normal maps

Use glTF `TANGENT` (or authored `tangents`) when present; `Scene3dVertex` gains a `tangent[4]` attribute. Otherwise build the tangent frame from screen-space derivatives in the fragment shader, as Three.js `perturbNormal2Arb` does. Generating MikkTSpace tangents on import is deferred; the derivative frame is correct for most assets and the glTF sample `NormalTangentMirrorTest` shows where it isn't.

### 5.3 Environment lighting

*Planned:* render the source into an RGBA16F cube map, prefilter a roughness mip chain, project irradiance into nine spherical-harmonic coefficients, and accept the small difference from Three.js's PMREM atlas. *As built in M3 (§11):* the environment is ordinary scene content, captured into r186's own cube-UV atlas layout and GGX-filtered as `PMREMGenerator.fromScene` does, and diffuse light reads the roughest atlas level as r186 does, not SH. Matching r186 exactly cost little more than the plain cube and keeps the oracle tight (99.87% on the first run). The capture is cached by a content key, so it re-renders only when the environment changes, never per frame.

### 5.4 Color and tone mapping

Shade in linear light, apply `exposure` and the tone-map curve in the fragment shader, then write to the existing sRGB framebuffer. This keeps the single MSAA pass and matches Three.js's inline tone mapping when it runs without postprocessing. Transparent surfaces then blend after tone mapping, the same approximation Three.js makes. With `tone-mapping:'none'` and no `standard` materials, output stays bit-identical to today; that is the regression guard for every existing scene.

### 5.5 Shadow maps

- **Depth pass.** For each casting light, render the casters into a depth texture (`DEPTH_COMPONENT24`, compare mode on, linear filtering for hardware 2×2 PCF), with slope-scaled polygon offset. Receivers apply normal-offset bias and a PCF kernel sized by `shadow-softness`.
- **Directional.** An orthographic frustum fitted to the bounds of casters and receivers, which does not depend on the camera. The map is therefore cached across camera-only changes, which matters for Dreamhome's orbit drags. Cascades come later (§3).
- **Spot.** A perspective frustum from the cone angle, with `distance` (or the scene bounds) as its far plane.
- **Point.** A six-face cube depth map storing linear distance (M5).
- **Who casts.** Opaque and mask materials cast (mask with alpha test in the depth pass); blended materials (glass) and `depth_only` materials don't. Skinned, morphed and instanced meshes cast through the same vertex code as the main pass. `basic` materials receive nothing, being unlit.
- **Budget (SP5).** At most four casting lights per scene. GL 3.3 guarantees 16 fragment texture units; this plan uses base, metal-rough, normal, occlusion, emissive, palette, environment cube and up to four shadow maps, eleven in all. Memory stays well inside the 512 MiB context budget (design doc §13): a 2048² depth map is 16 MiB, a 1024² cube 24 MiB.

### 5.6 Programs and passes

The single program becomes a small family of variants compiled from one source with `#define` feature bits (standard or not, which maps are present, shadow count, environment on, tone-map curve), cached per entry. `native_gl_program` already compiles arbitrary source. A depth-only variant shares the vertex stage.

`scene3d_snapshot` then runs: environment build if dirty → shadow passes if dirty (projection, pose, physics or light change) → the main pass with today's ordering (depth-only, opaque, sorted transparent) → snapshot. `Scene3dStats` gains shadow-pass and environment-build counters so tests can assert that caching works.

### 5.7 Invalidation

Today a camera attribute write marks the document mutated and re-projects every mesh and resource, which costs Dreamhome a full scene rebuild on every orbit-drag move. M6 adds a camera-only path: when the only changed nodes since the last projection are camera attributes, keep the projection and shadow maps and re-render the main pass. Animation and physics generations already show the pattern.

## 6. GL core additions (`radiant/gl_core.*`)

These sit below both clients; the WebGL adapter keeps its own command path and is unaffected.

- **Draw bindings.** `NativeGlDraw` replaces its single `texture` and `data` fields with a small fixed array of `{unit, resource, kind}` bindings (2D, cube, shadow), at most 12. The value-only waist is unchanged (D7.4.1v2).
- **Texture color space.** `NativeGlSampler` gains a color-space flag, so data maps upload as `RGBA8` rather than `SRGB8_ALPHA8`, plus an option to generate mipmaps.
- **Depth targets.** `native_gl_depth_target(context, size, cube)` (a depth texture with compare mode) and a depth-only begin for a target, or one face of it.
- **Float cube targets.** `native_gl_cube_target(context, size, levels)` (RGBA16F), with begin-face-and-level, for environment capture and prefiltering. (M3 uses a float atlas target instead, below; the cube target stays for M5's point shadows and HDR images.)
- **Float atlas targets (M3).** `native_gl_float_target(context, width, height, depth)` (RGBA16F 2D, linear, optional depth renderbuffer) and `native_gl_begin_region` (viewport and scissor on one region, optional clear), sampled through a `FLOAT` binding.
- **Accounting.** Byte formulas for the new kinds, so they fall under the existing quota, generation checks and teardown.

Unit tests go in `test_scene3d_unit_gtest` (allocation, completeness, accounting, release, context loss).

## 7. Assets

- **glTF import.** Map `metallicFactor`, `roughnessFactor`, `metallicRoughnessTexture`, `normalTexture` (with scale), `occlusionTexture` (with strength), `emissiveTexture`, `TANGENT`, and `alphaMode:MASK` with `alphaCutoff` onto `standard`. Apply `KHR_texture_transform` to every map. Remove the "approximated by Lambert" and "secondary maps not rendered" warnings. `KHR_materials_unlit` stays `basic`; other `KHR_materials_*` extensions still warn.
- **glTF export.** Write `standard` factors and maps back. `lambert` keeps exporting as metal 0, roughness 1.
- **Light units.** Whatever SP1 decides is applied once, at the glTF boundary in both directions.
- **OBJ/MTL.** The PBR extension keys (`Pr`, `Pm`, `map_Pr`, `map_Pm`, `norm`) map onto `standard` when present.
- **Design doc.** §15 "Renderable format profiles" and §14 glTF mapping are updated to match.

## 8. Milestones

Each milestone ends with its acceptance cases and gates (§9); broad suites run only at milestone boundaries, per `radiant/README.md`.

| Milestone | Delivers | Acceptance |
|---|---|---|
| **M0 — GL waist** | Draw bindings, texture color space and mipmaps, depth targets, float cube targets (§6) | Existing Scene3D pixel fixtures are unchanged; new unit tests pass |
| **M1 — Standard material, direct light** | Shader variants, BRDF, exposure and tone mapping, `standard` in both validators, the SP1 light units | A 5 × 5 metalness × roughness sphere grid under one directional light, plus a point-light case, matches the Three.js oracle; basic and Lambert scenes are bit-identical with `tone-mapping:'none'` |
| **M2 — Texture maps** | Base, metal-rough, normal (tangent and derivative), occlusion and emissive maps, alpha mask; glTF import and export parity | glTF sample models (DamagedHelmet, NormalTangentMirrorTest, AlphaBlendModeTest, TextureSettingsTest) render against the oracle; import, export, import round-trips are stable. The models are not in the repository: a fetch script pulls pinned versions into `temp/` and they are never committed (each has its own license), as with the Mixamo fixtures |
| **M3 — Environment lighting** | Procedural room and sky, cube capture, roughness prefilter, SH irradiance, hemisphere light, `environment-intensity` | Metal and dielectric spheres in `room` match Three.js `RoomEnvironment` + PMREM within tolerance; stats show one environment build across 30 camera moves |
| **M4 — Directional and spot shadows** | Depth passes, PCF, bias, cast and receive flags, skinned and mask casters, camera-independent caching | No acne or peter-panning on a sloped plane (contract fixture), soft edges within a measured penumbra width, a cutout leaf casts a cutout shadow, a skinned character's shadow follows its pose, orbiting re-renders no shadow map (stats) |
| **M5 — Point shadows and HDR images** | Cube shadow maps; `kind:'image'` equirectangular HDR environments. This needs a Radiance RGBE (`.hdr`) decoder: none exists today (libyuv under `lib/image/` has only row conversions), and the format is small enough to decode in Radiant's image input | A lamp in a closed room shadows all six directions; an HDR studio environment lights the sphere grid against the oracle |
| **M6 — Adoption and speed** | Dreamhome: `standard` materials (wood and stone floors, metal fixtures, glass), the sun casting through windows, a `room` environment indoors, `neutral` tone mapping; the camera-only invalidation path (§5.7) | Release-build frame times recorded for Dreamhome orbiting with shadows on; a camera drag no longer re-projects (stats); Dreamhome's UI suite passes |
| **M7 — Documentation** | Design doc §4 element table, §5 shading, §14/§15 asset rows; `doc/Lambda_Packages.md` scene3d section; this file becomes the implementation record | Doc checks pass (`make check-doc-code` for any lambda fences) |

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

## 12. Risks

- **Large scenes.** A shadow pass costs another draw of every caster; cityscape's 1.66M vertices would double its cost. Shadows are opt-in per light, and cascades with caster culling are follow-on work.
- **Oracle drift.** Prefiltering and PCF differences may exceed a tight threshold; thresholds are declared and recorded per fixture, never relaxed silently.
- **Platforms.** Only macOS GL 4.1 is verified (design doc §13). Depth compare samplers, cube maps and RGBA16F render targets are core GL 3.3, but other desktops stay unverified until tested.
- **Shader variants.** More variants mean more compile time and more cache entries; feature bits stay few, and programs cache per context.
- **Light units (SP1).** Whichever option wins changes some existing output; the milestone records which goldens moved and why.

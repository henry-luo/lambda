# Scene3D outdoor profile for Eden

2026-10-28. Baseline `d53f1a043`. **Implementation complete; qualification partial at repository aggregate gates.** Native macOS OpenGL profile. The design lives in
[Radiant_Design_WebGL](../radiant/Radiant_Design_WebGL.md#outdoor-atmosphere-foliage-wind-and-water),
with field-level documentation in [Outdoor Scene3D](../../doc/Lambda_Scene3d_Outdoor.md).

This is a finite application of **S12.1.1v3**, **D2.11.4**,
**D4.5.1v4–D4.5.3v2**, **D7.1.2v2**, **D7.2.4v4** and **D7.3.5**. No formal
language ruling changes. Eden retains terrain generation/editing, species,
surface-water volumes, stream discharge and ecological stepping. Native Scene3D
owns view-dependent shading, fog, shadows, shared motion and selected-frame
captures. No rendering event advances an Eden model.

## Finite milestone and acceptance

| Delivery | Acceptance |
|---|---|
| Atmosphere | Linear/exp2/height fog shades built-in materials; alpha/background remain correct; invalid/sibling/nested fog is diagnosed |
| Foliage | Backlighting is visible on thin leaves and blocked by the same shadow visibility as direct light |
| Wind | One lit vertex bend drives visible and depth passes; animation retains geometry/programs; static envelopes remain conservative |
| Cascades | Camera-depth slices preserve sun-shadow detail; transitions overlap; four-map budget is enforced; camera edits retain geometry/targets |
| Water | Authored flow/ripple normals combine with existing dielectric refraction/absorption; phase changes pixels and rewinds consistently |
| Reference scene | Existing Procart Meadow geometry demonstrates the new features with real controls and desktop/compact layout |
| Regression | Existing native/package/reference gates retain their assertions; required aggregate results are reported without hiding inherited failures |

Scope deliberately uses existing PBR, HDR/PMREM, PCF, instancing, culling,
distance LOD, transmission and shared animation. It does not recreate those
facilities. F7's fog and F6's directional cascades are delivered profiles;
clipping/draw-state controls and area lights remain separate. F2's arbitrary
shader depth contract remains open: the useful lit wind path is built into the
shared native vertex stage instead.

## Implementation and resource ownership

- `scene3d_outdoor_shaders.hpp` holds shared lit wind, analytic ripple normals and
  fog functions. Color, environment and shadow vertex sources use the same bend
  and inverse-transpose normal/Jacobian tangent adjustment.
- `lib/scene3d_material.h` shares outdoor factor defaults/ranges with native
  projection and animation binding. The pure package validator checks the same
  supported factors/models. Shader materials reject unsupported outdoor fields.
- Fog is a singleton leaf under the scene; only the main view receives it.
  It introduces no extra target or texture. Straight linear color is fogged
  before display conversion without changing coverage.
- Cascades reuse the four checked shadow slots, existing depth targets and PCF.
  Uniform/log splits fit expanded overlapping frustum slices. Stable spherical
  radius and texel-snapped centers avoid camera-translation shimmer. All caster
  depth bounds remain included; main-view culling never removes a caster.
- Camera-only changes reevaluate cascaded fits and retain their targets/geometry.
  Single-map shadows retain their existing camera-independent cache. Wind phase
  participates in capture/shadow keys, so matching caster content is refreshed.
- Water adds uniforms and a reusable material helper, rather than a second fluid
  solver or untracked reflection renderer. Existing one-opaque-image transmission
  capture and target accounting remain authoritative.
- Static glTF export diagnoses native outdoor material fields; it cannot silently
  serialize an undeformed/unlit approximation as though those effects survived.

## Verification record

Final focused qualification passes **81/81 native units**, including seven
new outdoor cases, **104/104 Scene3D integration/render-contract cases**, and
**28/28 new package checks on each of interp, AUTO and MIR/JIT**. The garden
reference passes **15/15 assertions across 20 events**, including mist/shadow
controls, phase-dependent pixels, and compact-layout bounds. The documented
example executes successfully; local documentation links and diff checks pass.

Native cases cover analytic linear-fog pixels/alpha/background, every built-in
material, height density, backlit leaf occlusion, matching animated wind shadows,
no geometry/GPU-byte growth during phase changes, cascade camera/cache behavior,
slot-budget rejection, water phase changes, zero biological/GPU-simulation steps,
and exact selected-image recovery after reset/context loss in those fixtures.

| Required gate | Result |
|---|---|
| Final outdoor fixture on interp/AUTO/JIT | 28 checks per tier, all pass |
| `make test-radiant-baseline` | 6,471 pass; 6 layout failures; 140 partial; 2 skipped |
| `make test-lambda-baseline` | 8,372/8,374 pass; known `tests_greetings_model` and `tests_procart_foliage` mismatches |
| `make test-input` for the export diagnostic | Incomplete: 66 input executables absent; script-format round trips 31/43 pass |

The six Radiant failures belong to CSS text/inline/sizing; UI automation passes
743/743 and native Scene3D passes its own gates. No text/layout source or their
assertions changed. The input command does not build every input test executable;
its raw report counts 1,752 passing checks and 78 failures, comprising 66 missing
executables and 12 AsciiDoc/man/Typst prose round-trip mismatches. It therefore
cannot establish full importer/exporter qualification. The new unsupported-export
checks pass across all three tiers. No aggregate failure was hidden or repaired
outside this milestone. If full input qualification is pursued, build the exact
inventory from `node test/test_run.js --target=input --list-build-targets` first.
No required process remains running at this handoff.

Artifacts are under `temp/eden-outdoor/`: build logs, focused/native logs,
per-tier package logs, reference replay result, desktop/mobile captures and
aggregate logs. No release performance claim is made; these are functional
checks. Existing calibrated-scene image gates remain unchanged.

The original Eden Meadow has two inherited byte-identical reset/hide-show replay
failures (40/42), despite identical pure descriptions and camera values. Their
cache owner remains unproven, and this milestone does not claim to fix them.
Its new selected-frame reset/context cases pass independently. See Eden's
`docs/Verification.md` and the root bootstrap diagnostics for the existing issue.

## Limits and next work

- Custom GLSL materials still require their own future depth/deformation contract
  and do not automatically cast shadows or receive built-in fog.
- Thin foliage/backlighting and wind are authored visual models, not measured
  tissue optics or biomechanics. Local attachment coordinates matter.
- Shadow cascades consume existing slots; highest-detail shadow LOD and
  whole-instance-batch culling remain the M7 profile.
- Water retains opaque/masked transmission limitations, one opaque source image,
  bounded rough filtering and environment fallback. No scene-depth shoreline
  reconstruction, planar reflection, SSR, foam, caustics, nested transparent
  interfaces, underwater view or hydrodynamic transport is implemented here.
- Terrain texture-layer blending, procedural asset fidelity, planting density,
  soil wetness/nutrient visual bindings and large-world chunk/LOD generation need
  their own Eden/Procart work. The reference intentionally reuses current
  stylized Meadow geometry; it is not a photorealism benchmark.
- SSAO, temporal antialiasing, depth of field and more complete GI remain separate
  finite milestones, not implied by the current postprocessing support.

Prior art: [NVIDIA vegetation](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis),
[Three.js CSM](https://threejs.org/docs/pages/CSM.html), and
[Three.js Water](https://threejs.org/docs/pages/Water.html). These inform the
bounded profiles; no new parity claim is made against their complete behavior.

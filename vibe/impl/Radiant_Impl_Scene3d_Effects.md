# Scene3D image effects — implementation record

Date: 2026-10-27. Scope: five reusable image materials, periodic animation,
SVG texture acquisition, and the Scene3D “Make waves” gallery.

## Authority and boundary

**S12.1.1v3**, **D7.2.4v4**, **D7.5.3v2**, **D4.5.1v4** and **D4.5.2**
apply. Lambda describes values; Radiant owns rendering and playback. This
extends the implemented **SH1 / SH2 / SH6** shader-material and uniform-track
facilities in [the shader record](Radiant_Impl_Scene3d_Shaders.md). No formal
ruling changes or new rendering backend are needed.

## Delivery and acceptance

| Milestone | Observable behavior | Owning code and verification |
|---|---|---|
| Image materials | Channel split, liquid distortion, rolling distortion, sliding doors and flies sample the same texture interface; amount zero is identity | `lmd/package/scene3d/effects.ls`; package golden and fixed-time native comparisons |
| Animation | Periodic phase clips autoplay; pause/play/restart and source/intensity updates preserve playback | Existing native mixer; gallery replay with scheduler advancement and paused-time invariants |
| SVG sources | SVG files and pure SVG values rasterize and sample alongside raster images; semi-transparent colour composites correctly | `scene3d_texture`, existing `render_media_rasterize_svg_picture` and `render_pixel_unpremultiply_abgr`; quadrant and alpha pixel checks |
| Demo | One page shows all five effects and original source; source/intensity/playback controls work; mobile and index navigation remain reachable | `test/demo/scene3d/effects/`, index tile, desktop/mobile replays |

Implementation checks use the package goldens and three focused graphics
replays. Milestone gates are `make test-lambda-baseline` and
`make test-radiant-baseline`; graphics checks require native macOS graphics
access. The required layout integer-cast lint also checks the bounded SVG pixel
extent conversions. Aggregate failures remain distinct from focused evidence.

## Implementation

The package shares its vertex shader, sampler, transparent out-of-bounds
behavior, tint and alpha convention. Each preset supplies its fragment body
and extra uniform defaults. Flies sample neighbouring lattice cells so moving
dots can cross cell boundaries without being cut off. Common parameters are
phase, amount, aspect, rotation and tint. Unknown presets/parameters and invalid
numeric ranges raise package errors; normal scene validation checks references
and uniform types.

`animate(id, duration, options)` creates an ordinary phase track from zero to
one. All presets are periodic in phase. Existing native scheduling evaluates
the track; no per-frame Lambda/JavaScript loop or new clock is introduced.
`s.shader_material` remains the general GLSL extension point.

The native texture path previously rejected `IMAGE_FORMAT_SVG` outright. It
now parses sampler bounds before acquisition, rasterizes SVGs at the requested
or natural size under `max-size`, then uploads a temporary straight-alpha copy.
The document's shared SVG picture/premultiplied pixel cache retains its normal
representation. Actual texture dimensions join the environment texture key so
changing SVG raster dimensions cannot reuse an incompatible environment capture.
The same path serves built-in and shader materials.

`svg_texture` converts a Lambda SVG element through XML serialization and URI
encoding. It reuses the document loader's SVG image resource path, including its
external-resource policy. It does not add a second inline-SVG vocabulary to
`<texture>`.

The gallery's source and intensity controls are Lambda view state. Its uniforms
remain typed maps; JS DOM `getAttribute` cannot round-trip those maps as JSON.
The playback-only JS adapter follows current scene DOM nodes after a template
rebuild. It uses the existing native host and has no frame callback.

## Validation and limits

Focused cases:

- `scene3d_effects` golden: catalog, all presets, alias constructor, generated
  SVG, phase-track shape, and rejected effect/uniform/duration inputs.
- `EffectsAnimateAllSourcesAndRetainPrograms`: every preset on text, SVG and
  raster sources; moving pixels, loop return, one retained linked program per
  scene, zero-strength identity, playback and pause/restart controls.
- `EffectsTexturesSvgRasterAndAlpha`: exact file/value SVG and raster quadrant
  pixels, built-in material compatibility, and semi-transparent SVG compositing.
- `EffectsMobileControlsAndNavigation`: mobile bounds, source switching and
  breadcrumb return. Existing collection routing cases also visit the new tile.

The gallery's SVG sources are original CC0 artwork. Its raster source reuses
the Photos demo's bundled beach photograph (`test/demo/photo/assets/coast.jpg`);
its [attribution](../../test/demo/photo/assets/ATTRIBUTION.md) applies. Source
dimensions preserve the photograph's proportions in the plane and shader, and
the original preview contains the image in the same frame. Effects are independently
implemented and inspired by [Blotter](https://blotter.js.org/#/materials), with
no Blotter dependency or API/pixel-compatibility claim.

SVG fidelity follows the existing SVG image renderer. The effect animates a
rasterized source image, not its SVG nodes. This delivery adds image materials,
not scene-wide postprocessing or GPU simulation. Native macOS OpenGL remains
the qualified graphics profile; no performance qualification is claimed.

### Qualification results

These results come from the shared worktree, which also contained unrelated
edits. The focused commit includes the effects and SVG texture changes only;
it has not received a separate clean-checkout qualification.

- Scene3D integration and event-render contracts: **96/96 passed** in the final
  stable-executable run (`temp/scene3d-effects/scene3d-integration-final.log`).
- Native Scene3D units: **44/44 passed** in the Radiant gate.
- Scene3D package goldens: **21/21 passed**. The new effects golden also matches
  under explicit interpreter and JIT execution. Its standalone forced-JIT
  negative cases report six container allocations / 240 bytes at shutdown;
  the existing shader negative-case fixture reports fifteen / 600 bytes under
  the same host, while successful catalog and scene construction probes report
  no leaks. Error-path runtime cleanup remains a qualification limitation.
- Final desktop replay with three extra scroll-reset capture steps: **182
  assertions passed, zero failed**; mobile capture replay: **5 passed, zero
  failed**. All three source captures and mobile output were visually inspected.
  Artifacts are `temp/scene3d-effects/gallery-{text,svg,raster,mobile}.png`.
- Integer-cast lint, new documentation links/examples, fixture event inventories
  and `git diff --check`: passed.
- `make test-lambda-baseline`: **8,319 passed / 1 failed** of 8,320. The failure
  is `tests_greetings_model`, a greetings-card golden mismatch outside Scene3D;
  its modules do not import Scene3D. The input prerequisite passed **2,112/2,112**.
- `make test-radiant-baseline`: recorded **6,356 passed / 8 failed / 141 partial /
  2 skipped** of 6,507. One failure was the physics navigation fixture's old
  eleven-tile expectation; it was corrected to twelve, then the final 96/96
  Scene3D run passed. The other failures are the CSS cascade memory budget and
  six layout cases in text, inline and sizing. The full aggregate was not
  repeated after that fixture-only correction, so its recorded result remains
  failed (`temp/scene3d-effects/radiant-baseline.log`).

An earlier broad Scene3D run overlapped the Lambda baseline rebuild, which
temporarily replaced `lambda.exe`; its spawn failures are discarded as
qualification evidence. The final integration run used the stable executable.

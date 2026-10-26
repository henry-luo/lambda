# Scene3D GPU shaders and simulation — implementation plan

**Status:** proposal, 2026-10-26. Decisions SH1–SH7 await the user. No code has landed.

**Driving case:** port the three.js butterfly swarm (`temp/butterflies/index.html`) to a native Lambda Scene3D demo: a large swarm in the centre and a right-hand column of three single butterflies whose wing pattern, flex, curl and flap settings the reader changes live with sliders.

## 1. Division of labour

Lambda plays the role three.js plays in a browser: it describes the scene, the materials, the shader sources and their parameters, and reacts to events. The GPU does the heavy work: shading, per-vertex wing deformation and the per-frame swarm simulation. Lambda never loops over butterflies per frame, and the engine gains no butterfly-specific C++ code — only general, declarative GPU capabilities any scene can use.

This stays inside the existing design (`vibe/radiant/Radiant_Design_WebGL.md`): scenes remain declarative elements updated through the normal Lambda view/DOM path (§7 "Execution and frame scheduling"); time still comes from the shared SVG/3D playback engine (S12.1.1v2, §8 Phase III), so there is no new animation loop; shader text is desktop GLSL compiled by the driver (§6).

## 2. What the native renderer has today

Read from `radiant/scene3d.cpp` and `radiant/gl_core.cpp`:

- One fixed program (`scene3d_vertex_shader` / `scene3d_fragment_shader`) draws every mesh: basic or Lambert shading, one texture on unit 0, skinning palette on unit 1, two morph targets.
- `<mesh instances>` carries only 4×4 matrices (locations 4–7, divisor 1). No other per-instance data.
- Any document mutation (`mutation_epoch`) re-runs `scene3d_compile`, which destroys and rebuilds every GPU resource of the scene. Playback (`Scene3dAnimationState`) and physics survive by structural matching; GPU resources do not.
- Each changed frame renders into an MSAA sRGB target and is read back as an `ImageSurface`.
- The GL core underneath already supports what this plan needs: arbitrary GLSL programs (`native_gl_program`), uniform reflection, instanced attributes (`NativeGlAttribute.divisor`), RGBA32F textures (`native_gl_data_texture`), and the full WebGL2 command surface used by LambdaJS.

## 3. Proposed vocabulary

### 3.1 Shader material

```lambda
<material id: "wing", type: 'shader', side: 'double', 'alpha-to-coverage': true,
    vertex: wing_vertex_glsl, fragment: wing_fragment_glsl,
    uniforms: {time: 0.0, flex: 0.55, curl: 2.6, atlas: "wing-atlas", swarm: "swarm.position"}>
```

- `vertex` / `fragment` are GLSL bodies. The host prepends `#version 330 core`, the standard inputs (`position`, `normal`, `uv`, `color`, `instance_matrix`), and the standard uniforms (`projection_matrix`, `view_matrix`, `model_matrix`, `camera_position`), like three.js `ShaderMaterial` (**SH1**).
- `uniforms` map: number → `float`; 2/3/4/9/16 numbers → `vec2`/`vec3`/`vec4`/`mat3`/`mat4`; a colour string → linear `vec3`; a string naming a `<texture>` or a `<simulation>` variable → `sampler2D`. Up to eight samplers. The program's reflected uniforms must match these declarations; a mismatch is a scene diagnostic, never a silent default.
- Uniform values are animatable with ordinary keyframe tracks (`"wing.uniforms.time"`), so a looping clip drives `time` through the shared playback engine (**SH2**).
- `alpha-to-coverage` turns texture alpha into MSAA coverage for cut-out wings without depth sorting.

### 3.2 Instances without matrices, and per-instance data

```lambda
<mesh geometry: "wing-grid", material: "wing", 'instance-count': 4096,
    'instance-data': {seed: seeds, species: species}>
```

- `instance-count` draws N instances whose matrix is identity; the vertex shader places them, typically from `gl_InstanceID` and a simulation texture.
- `instance-data` adds named per-instance attributes (1–4 floats each, inferred from length ÷ count), bound at locations 8+ for shader materials only (**SH3**). The butterfly demo can also derive everything from a hash of `gl_InstanceID`, keeping Lambda's arrays small.

### 3.3 Textures drawn by Lambda

Lambda paints the 16-species wing atlas as SVG (paths, gradients, eyespots), and Radiant rasterizes it into a GPU texture (**SH4**):

```lambda
<texture id: "wing-atlas", width: 2048, height: 2048, <svg viewBox: "0 0 2048 2048", ...species>>
```

Today `scene3d_texture` rejects SVG images. The change rasterizes inline (or `src`) SVG through the existing SVG renderer at the declared size, with mipmaps.

### 3.4 GPU simulation (GPGPU)

```lambda
<simulation id: "swarm", width: 64, height: 64, uniforms: {time: 0.0, attractors: [...]},
    <variable name: "velocity", initial: velocities, shader: velocity_glsl>
    <variable name: "position", initial: positions, shader: position_glsl>>
```

- Each variable is an RGBA32F ping-pong texture pair (`width × height` texels, one record per butterfly). Each step renders every variable in order with a full-screen pass whose fragment shader reads all variables' current textures (as samplers named after them) and writes the next value: the three.js `GPUComputationRenderer` model.
- The simulation steps when its `time` uniform advances (driven by a clip track like any uniform): `delta` = new − old time, clamped; time moving backwards resets to `initial` (**SH5**).
- Materials sample a variable through a uniform naming `"swarm.position"`.
- State persists across reprojection while the simulation's structure (size, variable names, shader text, initial data) is unchanged, matched like physics and playback are today. Slider-driven uniform changes therefore never reset the swarm (**SH6**).

### 3.5 Retained GPU resources

Compiled programs move to an entry-level cache keyed by shader source, so a template re-render after a slider move reuses them instead of recompiling. Simulation textures live in the entry beside physics and playback. The rest of the projection rebuild stays as today; measure it before optimizing further.

## 4. The demo built on it

`test/demo/scene3d/butterflies/butterflies.ls`, a new tile in the Scene3D collection:

- **Centre:** one `<scene3d>` with the swarm: a wing-grid geometry (12 × 8 cells), the shader material (flap, span-wise curl, chord-wise lag, size-scaled flex — the same model as the three.js page), 4,096 instances, and the `swarm` simulation (curl noise, five wandering attractors, group switching, all in GLSL).
- **Right column:** three small `<scene3d>` viewports, each with one butterfly from a chosen species, circling slowly. Under each, sliders for species, flex, curl, chord lag, flap speed and flap amplitude. The sliders are Lambda view state; each change re-renders the template and only the uniforms change (**SH7**: three single-butterfly viewports plus the swarm makes 4 of the 8 allowed viewports per document).
- The settings start from the three tuned presets (stiff small, medium, large and very flexible).

## 5. Milestones

| # | Outcome | Acceptance |
|---|---|---|
| M1 | Shader material, `instance-count`, `instance-data`, named samplers, `alpha-to-coverage`, animatable uniforms, program cache | A fixture of instanced quads flapping under a `time` track renders at fixed times; malformed GLSL and uniform mismatches produce diagnostics; a uniform-only re-render does not recompile |
| M2 | SVG textures rasterized into GPU textures | A Lambda-generated SVG atlas renders on a textured quad; pixel checks on two cells |
| M3 | `<simulation>` ping-pong passes, persistence, sampling from materials | A deterministic particle advection matches expected positions after N steps; a uniform change keeps state; a shader change resets it |
| M4 | Butterfly demo page, index tile, README | Swarm and three tunable butterflies render; each slider visibly changes its butterfly; capture for the tile preview |
| M5 | Documentation and gates | `Radiant_Design_WebGL.md` §4 vocabulary, scene3d package helpers, `doc/Lambda_Packages.md`; `test-radiant-baseline` and the scene3d tests pass |

Verification budget: scene3d unit and integration tests in the edit loop; release-build frame timing for the swarm (readback of a ~1000 × 600 retina frame is the known Phase I cost, §12); `make test-radiant-baseline` at M3 and M5.

## 6. Decisions for the user

- **SH1** Host-prefixed GLSL bodies (three.js `ShaderMaterial` style) rather than full raw sources.
- **SH2** Shader `time` comes from keyframe tracks on the shared playback engine, not a new host clock.
- **SH3** `instance-count` and named `instance-data` attributes, for shader materials only.
- **SH4** Lambda paints the wing atlas as inline SVG, rasterized by Radiant into a texture.
- **SH5** A `<simulation>` element (ping-pong RGBA32F variables) that steps on its `time` uniform and resets when time goes backwards.
- **SH6** Simulation state and compiled programs survive reprojection while their structure is unchanged.
- **SH7** The page uses four `<scene3d>` viewports: the swarm plus three single butterflies.

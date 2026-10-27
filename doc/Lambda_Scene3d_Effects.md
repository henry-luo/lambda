# Scene3D image effects

`lambda.scene3d.effects` applies animated GLSL materials to image textures:
typography, SVG paths and gradients, and raster images use the same API. Scene
construction is pure (**S12.1.1v3**, **D7.2.4v4**); Radiant owns GPU resources
and playback (**D4.5.1v4**, **D4.5.2**, **D7.5.3v2**).

Open the complete, offline gallery:

```sh
./lambda.exe view test/demo/scene3d/effects/effects.ls
# Also available as “Make waves” in:
./lambda.exe demo scene3d
```

The five independently implemented presets are inspired by
[Blotter's materials](https://blotter.js.org/#/materials). They use Scene3D's
native shader and animation facilities; Blotter is not a dependency.

| Effect ID | Appearance | Additional uniforms |
|---|---|---|
| `'channel-split'` | Animated RGB separation along an axis | None |
| `'liquid-distort'` | Periodic flowing UV distortion | `frequency: 7.0`, `seed: 0.4` |
| `'rolling-distort'` | A travelling, localized sine wave | `frequency: 3.0`, `spread: 0.22` |
| `'sliding-door'` | Alternating strips separate and rejoin | `divisions: 12.0` |
| `'flies'` | Image-coloured dots move independently | `cells: 90.0`, `radius: 0.34` |

## Build an effect

```lambda
import s: lambda.scene3d
import fx: lambda.scene3d.effects

s.scene([
    s.camera({id: "camera", type: 'orthographic', view_height: 2.0}),
    s.resources([s.texture("artwork.svg", {id: "artwork", 'max-size': 1024})]),
    s.mesh(s.plane([4.0, 2.0, 1.0]),
        fx.material('liquid-distort', "artwork", {amount: 0.08, aspect: 2.0}, {id: "ink"})^),
    fx.animate("ink", 8.0)^
], {width: 640, height: 320, camera: "camera", background: "#12232e"})
```

`material(kind, texture_id, uniforms?, options?)^` returns a shader material.
`s.effect_material(...)^` is the same constructor through `lambda.scene3d`.
The texture ID names a texture in the scene. Material options include `id`,
`side` and `transparent`; defaults are double-sided and transparent.

`catalog()` returns the five `{id, title, description}` records. Unknown
effects, unknown uniform names, nonfinite numbers and invalid ranges raise
diagnostics. Normal `s.normalize(scene)^` validates scene references and shader
uniform types; GLSL compilation and reflection happen in the native host.

Common uniforms:

| Name | Default | Meaning |
|---|---|---|
| `phase` | `0.0` | Cycle position; 0 and 1 produce the same image |
| `amount` | `0.06` | Distortion strength; zero returns the original image for all five effects |
| `aspect` | `2.0` | Positive displayed width / height, used for distortion and dot proportions |
| `rotation` | `0.0` | Axis in radians for channel split, rolling distortion and sliding doors |
| `tint` | `"#ffffff"` | Colour multiplier, using the shader material colour convention |

`frequency` and `spread` are positive; `divisions` is 1–512, `cells` is 1–1024,
and `radius` is greater than zero and at most 0.5 cell widths. `amount` is
nonnegative. Sampling outside the source returns transparent pixels. Shaders
emit linear, premultiplied colour for native compositing.

## Choose a source

Raster and SVG files use `s.texture(path, options)`. The normal document image
loader resolves local paths and data URIs. Raster formats follow the host's
image decoder support.

To use a Lambda-generated SVG without writing a file:

```lambda
let artwork = <svg xmlns: "http://www.w3.org/2000/svg", width: 640, height: 320,
    <circle cx: 320, cy: 160, r: 120, fill: "#ff775c">
    <text x: 320, y: 180, 'text-anchor': "middle", 'font-size': 48, "Hello!">>
let texture = fx.svg_texture(artwork, {id: "artwork", width: 1280, height: 640})^
```

`svg_texture(svg, options?)^` serializes an SVG element into a percent-encoded
image data URI, preserving text and XML attribute escaping. Rasterization uses
Radiant's existing SVG image renderer and its supported SVG profile. It handles
the SVG as an image resource, including that renderer's external-resource policy.

SVG textures rasterize at their natural size, or the texture's positive integer
`width` / `height`. `max-size` (default 4096, maximum 4096) scales the requested
dimensions down proportionally before allocating pixels. The shared SVG cache
stays premultiplied; a temporary straight-alpha copy is uploaded to the GPU.
SVG sources work with built-in materials and custom shader materials as well.
These effects animate the rasterized image; they do not edit the SVG tree.

## Animate and customize

`animate(material_id, duration?, options?)^` returns an autoplay clip named
`<material_id>-cycle` with a linear `phase` track from 0 to 1. Duration defaults
to eight seconds; smaller values run faster. Every preset closes the cycle
smoothly. Material IDs cannot contain dots because tracks use dotted paths.
Set `{autoplay: false}` for manual playback. Existing Scene3D pause, play and
seek operations apply to the clip.

Other parameters use ordinary keyframe tracks, for example
`s.track("ink.uniforms.amount", [0.0, 2.0], [0.0, 0.12])`.
For your own GLSL, `s.shader_material(vertex, fragment, uniforms, options)`
remains available. Sources are GLSL 330 bodies under Scene3D's standard attribute
and matrix prefix; see [SH1–SH3](../vibe/impl/Radiant_Impl_Scene3d_Shaders.md).

The gallery updates source and intensity through Lambda view state, preserving
typed uniform maps. Native playback and linked shader programs survive those
updates. Its small JavaScript adapter controls pause, play and restart through
the existing `RadiantAnimationHost`; it runs no animation-frame loop.

The qualified host is native macOS OpenGL. These are image materials on meshes,
not a scene-wide postprocessing stack or a Blotter API compatibility layer.

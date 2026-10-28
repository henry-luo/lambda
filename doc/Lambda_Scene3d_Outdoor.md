# Outdoor Scene3D rendering

2026-10-28. Native macOS OpenGL profile. This extends the existing Scene3D
materials, lights, shared animation and resource ownership; it adds no language
semantics. **S12.1.1v3**, **D2.11.4**, **D4.5.1v4–D4.5.3v2** and **D7.2.4v4**
remain authoritative. See the [design](../vibe/radiant/Radiant_Design_WebGL.md#outdoor-atmosphere-foliage-wind-and-water)
and [qualification record](../vibe/impl/Radiant_Impl_Scene3d_Eden.md).

```lambda
import s: lambda.scene3d
import o: lambda.scene3d.outdoor
let grass=o.foliage({id:"grass",'wind-strength':0.2,'wind-height':1.0})
let water=o.water(0.4,{id:"water"})
s.normalize(s.scene([
    s.camera({position:[5.0,4.0,7.0],target:[0.0,0.5,0.0]}),
    s.light('directional',{position:[8.0,12.0,6.0],intensity:1.2,
        'cast-shadow':true,'shadow-cascades':3,'shadow-distance':120.0}),
    s.light('hemisphere',{color:"#d6e7f1",'ground-color':"#64704b",intensity:0.4}),
    o.fog('height',{color:"#b9cbce",density:0.008,height:0.0,falloff:0.12}),
    s.mesh(s.box([8.0,0.1,8.0]),s.material('standard',{color:"#768653",roughness:0.95})),
    s.mesh(s.geometry([-0.1,0.0,0.0,0.1,0.0,0.0,0.0,1.0,0.0],
        {normals:[0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0]}),grass),
    s.mesh(s.plane([2.0,2.0,1.0]),water,
        {position:[1.8,0.2,0.0],rotation:[0.0-math.pi/2.0,0.0,0.0],'cast-shadow':false}),
    o.wind("grass")^,o.flow("water")^
],{width:800,height:500,background:"#b9cbce",'tone-mapping':'aces'}))^
```

## Fog

`o.fog(kind='exp2', options={})` returns one leaf `<fog>` directly below the
scene. Supported kinds are `linear`, `exp2` and `height`. Geometry outside an
Eden visibility mask is still an Eden concern; fog does not create that mask.

| Option | Default | Meaning |
|---|---|---|
| `color` | `#d5dedb` | Fog color, decoded using the material sRGB convention |
| `near`, `far` | 0, 1000 | Ordered nonnegative linear-fog distance range |
| `density` | 0.002 | Nonnegative inverse-world-unit density |
| `height` | 0 | Reference world Y elevation |
| `falloff` | 0.1 | Nonnegative inverse-world-unit height falloff |

Linear fog interpolates across camera-to-fragment distance. Exponential-squared
fog uses `1-exp(-(density*distance)^2)`. Height fog integrates an exponential
density profile along that ray; zero falloff becomes constant-density exponential
fog. The reference height shares the terrain's elevation datum.

Basic, Lambert, standard and physical materials, including native lines/sprites,
apply fog to straight linear color before their output conversion and preserve
coverage/alpha. The background and lighting-environment captures remain unchanged.
This first profile does not automatically fog arbitrary custom shader output,
provide volumetric shadowing, or synthesize atmospheric scattering/clouds.

## Foliage and lit wind

`o.foliage(options={})` returns an ordinary `standard` material with double-sided
rendering, roughness 0.8 and a thin-leaf backlighting approximation. Existing base,
normal, roughness and occlusion textures remain available. The factors also work
on `physical` materials.

| Material factor | Default | Meaning |
|---|---|---|
| `foliage` | 0.6 in helper; otherwise 0 | Backlighting strength, 0–1 |
| `foliage-thickness` | 0.3 | Nonnegative authored optical thickness |
| `foliage-color` | white | Backlighting tint |
| `wind-strength` | 0 | Maximum bend at the reference height, in local units |
| `wind-height` | 1 | Positive local Y reference height |
| `wind-frequency` | 0.1 | Spatial phase variation per world unit |
| `wind-phase` | 0 | Explicit phase in radians |
| `wind-direction` | 0 | Local X/Z direction in radians; zero is +X |

Backlighting uses opposite-side direct irradiance, authored thickness attenuation
and the same light-shadow visibility as ordinary shading. It is a bounded visual
approximation, not a measured leaf optical model or full subsurface transport.
The [NVIDIA vegetation chapter](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis)
is prior art for thin-leaf shading and procedural motion.

Wind bends above local Y=0 with quadratic height falloff. Put a blade/root origin
at its attachment point. The same bend and transformed normal/tangent run in
visible, environment and shadow passes. Different instance roots receive stable
spatial phase offsets. Wind factors also work with built-in Basic/Lambert models;
they do not alter an arbitrary shader material. General author-provided shader
depth programs remain separate work.

Static geometry bounds expand conservatively for the maximum bend. Skinned or
morphed geometry retains the existing rule: provide an authored conservative
`bounds` envelope or skip frustum rejection. Wind should not be layered onto a
rig unless its attachment coordinates and supplied bounds describe that result.

`o.wind(material_id,duration=8.0,options={})^` constructs a native clip from phase
0 to 2π. Numeric outdoor factors and `foliage-color` can also use ordinary tracks.
Changing phase updates existing buffers/programs and invalidates matching shadow
content. It does not advance biology, consume random seeds, or change source
geometry. Material IDs must be nonempty without dots; duration must be positive.

## Cascaded directional shadows

Directional lights accept these optional fields:

| Field | Default | Meaning |
|---|---|---|
| `shadow-cascades` | 1 | Integer 1–4; greater than one requires directional light |
| `shadow-distance` | 500 | Positive world-space maximum camera-depth coverage |
| `shadow-split` | 0.6 | Logarithmic/uniform split blend, 0–1 |
| `shadow-transition` | 0.1 | Overlap fraction per depth interval, 0–0.25 |

Cascades share the existing **four shadow-map slots**. A three-cascade sun leaves
one slot for another casting light. Shadow-map size and existing PCF/bias options
apply per cascade. Coverage ends at the lesser of camera far and shadow distance;
shadow distance must exceed the camera near plane. Orthographic near=0 uses
uniform splits. `shadow-extent` remains the single-map setting; cascades fit camera
slices instead.

Spherical slice bounds and texel-snapped light-space centers stabilize the fit;
neighboring cascades overlap and blend across transitions. Camera changes can
refresh cascade maps while retaining geometry and targets. Off-camera casters and
the existing highest-detail shadow LOD remain included. This is a bounded native
profile, not every configuration of [Three.js CSM](https://threejs.org/docs/pages/CSM.html).
Area lights remain separate work.

## Water appearance and simulation data

`o.water(depth=0.5,options={})` returns a `physical` material: metalness 0, IOR
1.333, transmission 1, roughness 0.12, finite optical thickness and absorption.
Its default attenuation color is `#438c88` over 3 world units. Existing
environment reflection and opaque-scene refraction supply the view-dependent
response; one opaque capture is shared by transmitting materials.

`water-amplitude` (default 0.025 in helper, otherwise 0), `water-frequency`
(default 2 in helper, otherwise 1), `water-phase` (default 0) and
`water-direction` (default 0 radians toward world +X) control analytic world-X/Z
ripple normals. Amplitude/frequency are nonnegative. They perturb normals without
moving the simulated water surface. `o.flow(id,duration=8.0,options={})^` animates
phase. Direction can follow an Eden flow vector; a physical flow velocity needs
an explicitly chosen phase-rate mapping rather than an implied correspondence.

Supply actual stream/pond geometry and optical depth from the world state. Use
world-unit geometry (or account for the existing thickness/world-scale contract)
and set the water drawable's `cast-shadow:false`: this transmission profile has
opaque/masked coverage and does not implement transmitting shadows. A painted
water material creates no source, volume or routing. Eden still owns its fixed
hydrology step, surface storage, discharge and conservation ledger.

The first profile has the existing transmission limits: one opaque image,
bounded rough filtering and environment fallback; no nested transparent
interfaces or caustics. There is no automatic scene-depth shoreline reconstruction,
planar reflection capture, SSR, foam, or underwater-camera model. These are
follow-ons, not hidden in a water-material promise. [Three.js Water](https://threejs.org/docs/pages/Water.html)
is prior art for a separate reflective-water profile.

Outdoor material effects have no glTF equivalent in this implementation; export
diagnoses them instead of silently dropping them. Save native descriptions as
Mark data. Fog is likewise outside static glTF export. These facilities preserve
the selected-frame capture contract (**D2.11.4**) and keep ecological time
independent from rendering (**S12.1.1v3**, Eden ED2).

# Model-fidelity fixtures

Created in this repository for M3. The embedded quad, UV0/UV1, RGBA colors,
independent map rotations/mirroring, texture-coordinate override and finite/
infinite perspective plus orthographic cameras are deterministic test data.
The two tiny PNGs were generated from explicit RGBA channel values; no external
assets or codecs are required. The glTF images use nearest/clamp sampling.

M4 adds `physical.gltf`: the same quad with clearcoat/sheen extensions,
all five layer maps, separate UV selectors/transforms and coat normal scale.
`layers.png` is a generated 2 × 2 RGBA map with independent R/G/A values.
Both extension names are required so an unsupported importer cannot silently
fall back to standard shading. Numeric and exact-pixel round trips check the
encoded channels rather than accepting a rewritten visual golden.

M5 adds `glass.gltf` with required transmission, IOR and volume extensions.
Transmission and thickness sample different coordinates/transforms of the
embedded R/G layer map. IOR, mesh thickness and world-space attenuation survive
numeric and exact-pixel static round trips. The original clearcoat/sheen data
remain present to exercise combined layers.

M6 adds `drawables.gltf`, derived from the same portable embedded streams.
Modes 0/1/3 reuse four face-free positions, UV0/UV1, RGBA and one material;
the selected adapter retains unlit base color/coverage and source UVs while
reporting omitted PBR maps. Loader/native tests cover counts, absent consumed
UVs, cached material reuse, rendering, context loss, retirement and explicit
styling-export refusal.

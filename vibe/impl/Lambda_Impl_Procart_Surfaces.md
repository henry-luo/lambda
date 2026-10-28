# Procart P9 — reusable surfaces

> **Status:** implemented and qualified, 2026-10-28.
> **Contract:** [PG14.1–PG14.5](../Lambda_Design_Procart.md#11-p9--reusable-surface-patterns-pg14).
> **Spec linkage:** S12.1.1v3 (pure generation), S11.4.6 (runtime admission), S8.1.1v2/S8.1.2v3 (name membership and explicit null), S1.6 (tier transparency), S12.1.5v2/D7.5.3v2 (presentation ownership), D7.2.4v4 (source package reuse). No formal ruling changes.

## Delivered behavior

Version-2 Procart compositions store versioned pattern resources separately from
geometry recipes. `lambda.procart.surface` implements five fixed generators:
noise, stripes, marble, bark and leaf veins. Pattern seed, two opaque palette
colors, frequency, warp and width remain literal data. Unknown versions,
references, fields and invalid parameter values fail admission.

Whole-object `surface` bindings and role-specific `surfaces` bindings inherit
through roots, groups, repetitions and named rule calls. Explicit null clears
whole-object or role bindings; `at` checks presence independently of value.
Binding keys never enter geometry growth or placement random streams. A pinned
pattern seed keeps appearance independent of the composition seed.

P8 leaves now carry normalized base-to-tip/across-blade UVs. Sweeps carry
circumference/length UVs and independent disk UVs on caps. Shared arc-length and
frame helpers preserve original-path coordinates at retained LOD rings. Buffer
merging retains complete UV sets. Geometry fingerprints share one helper in
`procart._common`; canonical SHA-256 inputs preserve existing primitive IDs.

UV surfaces use repeating PNG textures. Nonidentity UV scale/offset creates a
shared transformed buffer. Object-space fields bake into padded triangle atlases,
preserving source positions, normals, optional colors and tangents. Instance
transforms do not enter atlas identity. Texture baking reverses image rows to
match native bottom-up UV coordinates; corner sample oracles verify this boundary.

The baker plans unique jobs before allocating rasters, enforcing total
`max_surface_pixels` and accounting for geometry copies under `max_vertices`.
Textures and mapped buffers include effective seeds in their identities. Parts
retain their usual source IDs and gain a resolved surface description. The
adapter includes surfaces in batch keys and emits existing Scene3D standard
materials; unbound parts keep Lambert materials. No native renderer, runtime,
build-configuration or vendor source changed.

The fifth Form studies tab, **Grain, bark & veins**, loads
[`surfaces.mark`](../../test/demo/procart/samples/surfaces.mark). One scene binds
bark and leaf patterns independently on a tree, maps a mineral pattern over two
stones, and enlarges the leaf pattern on a folded mesh. Desktop and 390px captures
were inspected; the textures are visible on all three surface families.

## Qualification

| Gate | Result |
|---|---|
| `ProcartSurfaceTests.BindingsAndCoordinatesAcrossTiers` | PASS: two package fixtures and the saved study on interp/AUTO/JIT, nine script executions |
| `ProcartDrawingTests.ExpansionAndAdmissionAcrossTiers` | PASS: existing admission, primitive, adjustment, rule and gallery checks on all three tiers |
| `ProcartPlantTests.ArchitectureAndDevelopmentAcrossTiers` | PASS: architecture, 3D turtle, parametric development and Tree studio on all three tiers |
| `ProcartTests.KeyedNoiseIsExactAcrossTiers` | PASS: legacy algorithm and numeric compatibility fixtures on all three tiers |
| Surface study native replay | **19/19 assertions PASS**: texture bindings, real pixels, changed seed, exact replay, retained orbit/reset and mobile |
| Existing Form studies native replay | **50/50 assertions PASS**, including five sample tabs |
| Existing Tree studio native replay | **51/51 assertions PASS**, including LODs and seed replay |
| Saved surface composition | CLI structural schema validation PASS, zero errors/warnings |
| Public package example | Compiles and expands to Scene3D |
| Paired goldens, event JSON, local document links, whitespace | PASS |

The three native replays total **120 passing assertions**. Package checks include
seed pinning, definition-order independence, inherited/null overrides, sharing,
unknown references/versions/fields, invalid coordinates, pixel/vertex budgets,
UV transforms, original-path LOD ring identity and exact atlas corner colors.
Artifacts are under `temp/procart-p9/`: `cross-tier.log`, `final-tests.log`,
`schema.log`, `api.log`, the three `*-events.json` results and the desktop/mobile
PNG captures. The inherited three-container/120-byte shutdown diagnostic also
appears in the committed P7/P8 qualification; it was not suppressed.

This is a source-package change. Focused cross-tier goldens and native consumer
replays were used; neither engine baseline nor performance claims are made.

## Deliberate limits

P9 provides color patterns with scalar roughness and metalness. Normal/roughness
maps, displacement and attached geometry remain later increments. Patterns are
fixed versioned generators, rather than arbitrary drawing grammars or graphs.
Plant coordinate-aware bindings use P8's package generators; legacy Meadow
models without UVs need object mapping or explicit UV resources. Standalone P8
tree document envelopes remain unchanged; compositions hold the surface resources.

Object mapping requires explicit buffer geometry and at most 4,096 triangles.
Nonidentity UV transforms also require explicit buffer UVs. Native boxes/planes
can use their default host UVs for identity mappings. Atlas minimum tile size can
increase dimensions beyond the requested resolution; actual pixels are budgeted.
Tiles use linear filtering without mipmaps to avoid atlas bleed. Authored
nonintegral stripe/rib periods can produce a boundary at repeated UV tiles.

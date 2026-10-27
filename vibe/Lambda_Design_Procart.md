# Procart — semantic procedural scenes

> **Status:** semantic composition accepted by the user on 2026-10-28; the first Meadow profile is implemented. General scene profiles and editor tooling remain later work.
> **Scope:** separately stored recipes and semantic objects compose a seeded landscape; `lambda.procgen` retains its package name.
> **Spec linkage:** S2.6.1v2 (element content), S12.1.1v3 (pure generation), S12.1.5v2 (presentation ownership), S1.6 (execution representation is invisible), S11.4.6 (validator limits), D7.2.4v4 (package paths).
> **Ledger:** PG11.1–PG11.8 extend [the procedural-generation ledger](Lambda_Pkg_Proc_Gen.md#pg11--stored-procedural-art). No formal-spec ruling changes. Qualification and remaining limitations are recorded in [the Meadow implementation record](impl/Lambda_Impl_Procart_Meadow.md).

## 1. Semantic scene vocabulary (PG11.1)

Artworks are authored with domain tags: `flower`, `tree`, `stone` and `grass`.
`group` composes them recursively. `meadow` is the first scene root. Tags identify
what an object is; its recipe selects the algorithm and its parameter defaults.

The first implementation lives under `test/demo/procart/`. Its actual type
schema is [mod_composition_schema.ls](../test/demo/procart/mod_composition_schema.ls);
the semantic resolver is [mod_composition.ls](../test/demo/procart/mod_composition.ls).
The complete saved scene is [meadow.mark](../test/demo/procart/meadow.mark).
The display page loads this literal Mark file; loading does not evaluate a
Lambda script or execute a function stored in the artwork.

| Part | Responsibility |
|---|---|
| Recipe | Registered generator, exact version, semantic kind and reusable parameter defaults |
| Semantic object | Recipe reference, optional seed/key path, settings and placement |
| Group | Nested composition, placement and optional repeated scatter |
| Meadow | Schema version, root seed, terrain dimensions/settings and object hierarchy |
| Presentation | Scene3D resources, materials, light, camera and category visibility |

The generic algorithms remain code. New grammars, supported organ parameters,
colors, patch layouts and placements are data. A new algorithm family still
requires a registered implementation. Generation remains pure under
**S12.1.1v3 / PG4**.

## 2. Stored element schema (PG11.2)

A compact example demonstrates a group containing a patch of flowers, plus
independent trees, stones and grass. The full scene defines every recipe it
references; this structural sketch shows one recipe and its instances:

```mark
<meadow version:1,seed:2026,width:16.0,depth:12.0,relief:0.55,
    terrain:{width:33,height:25,octaves:3,island:0.35},
    <resources
        <recipe id:"cosmos",kind:"flower",generator:"plant",version:1,
            parameters:{rules:{X:"F[+X][-X]FX",F:"FF"},stem:"#54764b",leaf:"#7e995b",
                petals:8,leaf_size:0.55,order:3,angle:25}>
    >;
    <group id:"flower-patch",position:[-2.0,0.0,1.0],count:9,spread:[3.0,2.0],
        <flower id:"pink-flower",recipe:"cosmos",scale:[0.4,0.4,0.4],settings:{order:2}>>
>
```

Common object/group attributes are `id`, optional `seed`, `key_path`, `count`,
`spread`, `position`, `rotation`, `scale` or `matrix`. Leaf objects additionally
name a `recipe` and may supply a `settings` map. Groups contain objects/groups;
leaf objects have no content. Groups do not carry recipe settings.

IDs are nonempty, at most 32 ASCII letters, digits, hyphens or underscores;
authored recipe and object/group IDs are unique within the document.

The root carries one `resources` block and one or more objects/groups. Recipe
fields are `id`, `kind`, `generator`, `version` and `parameters`. Version 1
registers `plant` for flowers/trees, `rock.faceted` for stones and `grass.blades`
for grass patches. A recipe's kind must match the semantic tag that references it.
Unsupported versions and generator/kind combinations fail explicitly.

The schema uses ordinary Lambda element types and recursive type aliases
(**S11.4.12**). The resolver adds reference, range and budget validation; a
structural type check alone does not close arbitrary parameter maps
(**S11.4.6**).

## 3. Defaults, seeds and composition (PG11.3)

Recipe parameters and instance settings share one parameter vocabulary. Merge
by top-level key: a setting replaces the corresponding recipe value, including
a whole nested map or array. There is no implicit deep merge. Resolve and admit
all effective parameter maps before constructing specimen geometry.

The root seed accepts an integer or string and becomes `rng.key(seed)`. Each
named node normally derives its key from its parent and ID. An explicit node
seed starts an independent stream; an explicit `key_path` derives from the
chosen base instead of the ID. A path contains at most eight integer/string
parts. Reordering siblings therefore preserves their streams; renaming a node
rekeys it unless an explicit path or seed preserves its random identity.

`count` repeats a node/group from 1 to 64 times. Each copy has an indexed key,
so increasing the count preserves the existing prefix. `spread:[width,depth]`
scatters those copies over a local rectangle with keyed yaw. Placement and model
random choices use separate derived labels. Child matrices compose beneath
the group matrix; authored transforms are radians and column-major matrices,
using the existing Scene3D transform helpers.

Composition is bounded to eight group levels and 192 expanded semantic objects.
The first profile also bounds grass to 12,000 blades before generating it.
Invalid transforms, references, settings, recipe versions and unknown semantic
tags/parameters produce errors. Generator-specific bounds remain in force
(**PG8**); requests are never silently truncated.

## 4. Reuse of the existing generators (PG11.4)

| Semantic object | Existing implementation reused | Meadow adaptation |
|---|---|---|
| Flower | `mod_plants.grow`, L-system and solid-organ generators | Takes the recipe map directly; no species catalogue lookup |
| Tree | The same L-system branches and `mod_plant_solids` transforms | Replaces specimen leaves with seeded crown clusters at branch tips |
| Stone | Promoted `mod_minerals.rock` faceting helper | One reusable, uniquely named rock geometry per object |
| Grass | New `mod_grass` patch generator | Curved, tapered ribbon blades share one geometry and keyed clump placement |
| Ground | `lambda.procgen.terrain.heightmap`, `field.sample`, `adapt.heightfield` | Grounded objects, terrain colors, a winding path and an earth skirt |

The plant recipe map now records leaf mode and size instead of selecting them
through a species-ID branch. The existing catalogue remains a consumer of the
same public generator; its behavior and golden outputs remain the reference.

The first profile admits branching plants and lavender, trees with L-system
branches/crowns, faceted stones, and grass patches. It does not yet expose every
layered-flower, succulent, fungal or foliage recipe through semantic objects.

Grass recipes control clump count, blades per clump, radius, height, width,
bend and palette. Fine and tall patch presets are stored in `meadow.mark`.
Individual grass blades sample the terrain at their own bases; whole plants
and stones share their object's terrain anchor. Grass leaves the path clear.

## 5. Shared parts with Scene3D (PG11.5)

| Concern | Shared contract |
|---|---|
| Definitions | `resources`, IDs and references; recipe scope stays outside native scene resources |
| Hierarchy | Recursive groups; shared position/rotation/scale/matrix operations |
| Geometry | Existing sphere and botanical surface resources plus indexed terrain/rock buffers |
| Instancing | Organ/shape/color/category batches become ordinary mesh instance arrays |
| Appearance | Existing Scene3D material, color, lighting and shadow descriptions |
| Viewing | Existing camera data, retained orbit, zoom and reset controls |

The pipeline is **literal Mark scene → resolved semantic objects → model parts
and resources → ordinary Scene3D markup**. No native renderer tags were added.
`flower`, `tree`, `stone` and `grass` are resolved before Scene3D normalization;
the renderer receives its existing scene/group/geometry/material/mesh vocabulary.

The first semantic placement profile supports matrix or position/rotation/scale;
it does not yet expose Scene3D's quaternion attribute. Plant branch angle remains
a recipe parameter in degrees. Scene placement rotation is in radians. Terrain
sample width/height are distinct from physical meadow width/depth and viewport
pixels. These are explicit boundaries, not interchangeable dimensions.

The shared viewer initializes its control state from the authored home camera
and derives zoom bounds from that camera's distance. Orbit retains mesh nodes.
Category visibility reuses generated model data, under **S12.1.5v2**; it does not
rerun the generators. New seed/Previous update the document seed and regenerate.

## 6. Replay and versioning (PG11.6)

`meadow.version` is the file/profile schema version; each recipe's version pins
its generator contract. Frozen defaults, key labels, parameter meaning and output
shape belong to that contract. The file contains all recipe definitions used by
the meadow. Saving its Mark value and loading it again retains the description
needed to rebuild the model with compatible generator implementations.

The numerical guarantee remains that of the generator (**PG2**). File versioning
does not eliminate floating-point tolerance or repair an execution-tier defect.
Backend selection and GPU handles are not persisted generation inputs (**S1.6**).
No save dialog, arbitrary module loading, external recipe-library resolver or
archival baked-model format is included in this milestone.

## 7. Validation and implementation boundary (PG11.7)

The first implementation is a Procart application profile. Its resolver and
model functions operate on values; the page owns input loading and viewer state
(**S12.1.1v3**). Shared numeric algorithms remain in `lambda.procgen` and native
scene construction remains in `lambda.scene3d`, preserving **PG4 / D7.2.4v4**.

A future general document package should extract the demonstrated common shape,
with caller-supplied registered generators. It should not depend on demo modules
or import Radiant into the procedural-generation package.

## 8. Milestone and follow-up (PG11.8)

The delivered page composes terrain, nested flower patches, a tree grove, stones
and two grass styles. It has seed replay, independent layer toggles, retained
camera controls, responsive layout and navigation from the existing workshops.

Focused coverage checks Mark replay, model replay, recipe settings precedence,
transform composition, stable sibling/repetition keys, malformed input, grass
surfaces and native Scene3D validation. The UI replay checks real pixels, camera
retention/reset, visibility, seed replay and a narrow window. Current results and
any remaining qualification failures are in the implementation record.

Later increments can add remaining plant families, generic scene roots, richer
scatter/placement operators, additional presentation adapters, a file editor and
external versioned asset libraries.

## Appendix S. Superseded proposal

~~The initial 2026-10-28 draft used a `procart` envelope with generic `generate`
instances, and `generated` placeholders in separate presentations. IDs were
metadata only, with an explicit `key_path` for random identity.~~

The user replaced the generic producer tag with semantic `flower`, `tree`,
`stone` and other domain tags, composed through groups into scenes such as
`meadow`. Named hierarchy keys provide convenient independent streams; explicit
seed/path overrides remain available. Recipes, settings precedence, versioning,
pure generation and reuse of ordinary Scene3D output retain their rationale.

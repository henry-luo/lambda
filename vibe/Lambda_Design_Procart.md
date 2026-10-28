# Procart — semantic procedural scenes

> **Status:** Meadow, P7's drawing grammar/Form studies, P8's plant architecture/Tree studio, P9's reusable surface bindings and P10's environment-aware meadow are implemented (2026-10-28). P9 adds a fifth Form studies sample with stone, bark and leaf patterns. Editor tooling remains later work.
> **Scope:** separately stored recipes and semantic objects compose a seeded landscape; the shared algorithm package is `lambda.procart` (renamed by the user on 2026-10-28).
> **Spec linkage:** S2.6.1v2 (element content), S12.1.1v3 (pure generation), S12.1.5v2 (presentation ownership), S1.6 (execution representation is invisible), S11.4.6 (validator limits), D7.2.4v4 (package paths).
> **Ledger:** PG11 documents Meadow; PG12 covers drawing grammar and rule names; PG13 covers plants; PG14 covers reusable surfaces; PG15 covers environment and flowering species in the [procedural-art package](Lambda_Pkg_Proc_Art.md). No formal-spec ruling changes. [Prior art](Lambda_Pkg_Proc_Art.md#appendix-c-prior-art--procedural-scenes-and-art) is collected with the package design. Qualification: [Meadow](impl/Lambda_Impl_Procart_Meadow.md), [P7](impl/Lambda_Impl_Procart_Grammar.md), [P8](impl/Lambda_Impl_Procart_Plants.md), [P9](impl/Lambda_Impl_Procart_Surfaces.md), [P10](impl/Lambda_Impl_Procart_Environment.md).

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
| Ground | `lambda.procart.terrain.heightmap`, `field.sample`, `adapt.heightfield` | Grounded objects, terrain colors, a winding path and an earth skirt |

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
(**S12.1.1v3**). Shared numeric algorithms remain in `lambda.procart` and native
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

## 9. P7 — composable drawing grammar

### 9.1 Purpose and document shape (PG12.1)

**Requested direction:** the user asked on 2026-10-28 for a new phase inspired by
[Structure Synth / EisenScript](https://structuresynth.sourceforge.net/reference.php)
and [Context Free / CFDG](https://forge.glyphic.com/john/context-free/wiki).
The following spelling, defaults and limits are implemented by
`lambda.procart.scene`. The standalone structural envelope is
[procart/schema.ls](../lmd/package/procart/schema.ls); `scene.normalize` adds
attribute, reference and budget admission. Meadow v1 keeps its own resolver.

The central operation is **invoke a shape under adjusted inherited state**.
A shape can be a drawing primitive, a semantic object or a reusable rule. All
three share the same placement and color adjustments. Rules compose shapes;
recipes continue to parameterize registered generators. The saved artwork
remains data, using Lambda elements (**S2.6.1v2**), with pure expansion
(**S12.1.1v3**) and presentation-owned viewing (**S12.1.5v2**).

The general envelope is `<procart version:2,seed:...,...>`. It has one
`resources` block and one or more drawing nodes. A diagram of its structural
grammar follows; executable admission remains the complete contract:

```text
Document   = procart(version:2, seed, settings?, adjustments?) { Resources Node+ }
Resources  = resources { (Recipe | Geometry | Rule | Pattern)* }
Pattern    = pattern(id, generator, version:1, seed?, parameters?)
Rule       = rule(name) { Variant+ }
Variant    = variant(name, weight:1) { Node+ }
Node       = Group | Repeat | Call | Primitive | Object
Group      = group(adjustments?, identity?, count?, spread?) { Node* }
Repeat     = repeat(count, step?, adjustments?, identity?) { Node+ }
Call       = <rule-name>(adjustments?, identity?, count?, spread?)
Primitive  = box | grid | sphere | line | point | triangle | mesh | cylinder | tube
Object     = tree(recipe, settings?) | flower(recipe, settings?)
           | stone(recipe, settings?) | grass(recipe, settings?)
```

All drawing nodes accept adjustments, optional identity attributes and PG14's
`surface`/`surfaces` bindings. Drawing
leaves and rule calls have no children; definitions do not draw by themselves.
Groups, calls and leaves also accept independent `count`/`spread`; `repeat`
uses its own cumulative count and has no scatter attribute.
Rule names and recipe/geometry IDs share a globally unique definition namespace.
Node IDs are optional and unique within a sibling body; variant names are
required and unique within their rule. A rule's name is its invocation tag:
`<ribbon ...>` calls `<rule name:"ribbon" ...>`. Built-in drawing and structural
tags are reserved, so a rule cannot replace `box`, `tree`, `group` or `rule`.
The distinction between definition names and instance IDs is **PG12.11**.
The first grammar has no embedded scripts, arbitrary module names or evaluated
expression strings. Dynamic authors can construct the same values in Lambda.

### 9.2 Drawing primitives (PG12.2)

Coordinates are right-handed XYZ with Y up. Default primitives are centered at
the local origin, fitting the existing Scene3D placement model. This explicitly
differs from EisenScript's unit-cube pivot at `(0.5,0.5,0.5)`. Local unit sizes
make `scale` a common adjustment instead of a second geometry-dimension system.

| Tag | Canonical geometry | Geometry-specific attributes |
|---|---|---|
| `box` | Solid unit cube, each coordinate from −0.5 to +0.5. | None. |
| `grid` | The 12 edges of that unit cube, forming a wireframe box. | `stroke`, default 0.01 local units. This is distinct from `procart.grid` tile algorithms. |
| `sphere` | Unit-diameter sphere at the origin. Nonuniform `scale` produces an ellipsoid with correct transformed normals. | `segments`, default 24; `rings`, default 12. |
| `line` | Unit segment along X, from `[−0.5,0,0]` to `[0.5,0,0]`. | `stroke`, default 0.01 local units. |
| `point` | A marker centered at `[0,0,0]`. | `diameter`, default 0.02 local units. |
| `triangle` | One triangle from exactly three supplied local 3D points. Vertex order defines its normal. | Required `points:[[x,y,z],[x,y,z],[x,y,z]]`; non-collinear, finite coordinates. |
| `mesh` | An instance of a prototype geometry defined in `resources`. | Required `geometry` reference to an existing buffer-geometry definition; its positions, indices and normals follow Scene3D's contracts. |
| `cylinder` | Closed unit cylinder of length 1 and diameter 1, with its axis along X. | `segments`, default 24. |
| `tube` | Swept circular surface along an explicit local polyline; default path is the unit X segment. | `points`, `radius` (default 0.5), `segments` (default 12), `capped` (default false). |

`grid`, `line` and `point` lower to ordinary triangle geometry with local-unit
thickness, so they remain visible and share the renderer's instancing path.
Their thickness scales with the inherited frame. `grid` emits each box edge
once. A triangle initially uses a double-sided material; solid primitives use
their ordinary outward faces. Indexed meshes retain their defined topology.

Tube rings use continuous transported frames along the supplied path, with
smooth vertex normals. At least two points are required; consecutive duplicate
points and undefined reversal joints fail. Separate tube invocations remain
separate surfaces. Implicit stitching through the history of recursive calls
is a later extension. Cylinder and explicit-path tube are implemented P7 features;
the historical reference's unsupported flags and sphere limitations do not
define Procart's contract.

Examples of the stored notation:

```mark
<box scale:[0.9,0.1],hue:10>
<sphere scale:[1.0,0.6,0.4]>
<triangle points:[[0.0,0.0,0.0],[1.0,0.0,0.0],[0.5,0.5,0.0]]>
<mesh geometry:"leaf-prototype",scale:0.5>
<cylinder scale:[2.0,0.3,0.3]>
<tube points:[[0.0,0.0,0.0],[1.0,0.0,0.0],[1.5,0.6,0.0]],radius:0.04>
```

### 9.3 Common geometric adjustments (PG12.3)

| Attribute | Meaning |
|---|---|
| `scale:s` | Uniform scale `[s,s,s]` relative to the incoming local frame. |
| `scale:[sx,sy]` | Scale `[sx,sy,1]`; Z is unchanged. It does not flatten geometry onto a plane. |
| `scale:[sx,sy,sz]` | Separate relative scale factors on XYZ. |
| `position:[x,y,z]` | Translation in the incoming parent coordinate frame. |
| `rotate:a` | Rotation by `a` degrees about local Z, convenient for planar compositions. |
| `rotate:[ax,ay,az]` | Euler rotations in degrees, converted once to the shared Scene3D transform representation. |
| `rotation:[ax,ay,az]` | Existing Scene3D-compatible rotation in radians. `rotate` with `rotation` is an error. |
| `matrix:[...]` | Explicit affine column-major 4×4 placement, exclusive with the geometric attributes above. Color adjustments may still accompany it. |

`scale` is the canonical name: it multiplies the inherited frame
and matches Scene3D's existing terminology. `size` suggests an absolute extent
and is not a second spelling of this adjustment. Intrinsic dimensions, such as
tube radius, remain geometry parameters. Scalar/two-component convenience is
new in v2; the existing v1 `scale` contract remains a three-component array.

Missing translation/rotation are zero; missing scale is one. Factors must be
finite and nonzero; negative factors express reflection. The resulting affine
transform must satisfy the shared transform contract. Normal and face handling
must remain correct for nonuniform and reflected instances.

An attribute map has no operation order. After alias/unit normalization the
local matrix is the shared Scene3D `T * R * S` matrix; it acts on a point by
scaling, then rotating, then translating. Euler composition uses the existing
`scene3d.transform.matrix` convention. World placement is `parent * local`.
Attribute reordering cannot alter it. Nested groups express different orders:

```mark
<group rotate:90,<box position:[1.0,0.0,0.0]>>
<group position:[1.0,0.0,0.0],<box rotate:90>>
```

The first rotates the translated box around the parent's origin; the second
rotates the box about its own center and then places it. This gives explicit
composition without depending on attribute source order or adding an
EisenScript command parser. [CFDG's ordinary and ordered adjustments](https://forge.glyphic.com/john/context-free/wiki/Shape-Adjustments)
are precedents for making operation order explicit; these semantics
use Lambda's existing map and transform contracts.

### 9.4 Color adjustments and scope (PG12.4)

| Attribute | Meaning; identity default |
|---|---|
| `color` | Replace the incoming base RGB color; omitted means retain it. Use the existing color parsing contract. |
| `hue` | Add degrees to hue and wrap modulo 360; default 0. `hue:10` is a relative shift, not an absolute hue. |
| `saturation` | Multiply HSV saturation, then clamp to [0,1]; default 1. |
| `brightness` | Multiply HSV value, then clamp to [0,1]; default 1. |
| `opacity` | Multiply alpha; default 1, authored multiplier in [0,1]. |

Within a node, set `color` first if present, then apply hue, saturation,
brightness and opacity. Saturation/brightness factors are finite and
nonnegative. Across levels, apply the parent's color operation before the
child's. A later absolute color resets earlier RGB changes while retaining
inherited opacity. Clamping occurs at each level; optimizations must preserve
that order. A white base remains white under a hue shift alone; set a chromatic
color to make the shift visible. The default primitive base is opaque white.

For generated objects, use each organ's recipe color as the intrinsic base and
apply the inherited color operations to it once. A `hue` shift changes the
whole palette while preserving its relationships; an explicit `color` replaces
the RGB bases uniformly. Store or compose these operations as pure values;
never recolor a shared recipe or cached material in place. Material-level
opacity and color must not receive the adjustment a second time.

Each child receives an adjusted copy of its parent's state. Adjustments on a
`box` affect that box; adjustments on a `group` or `use` affect the entire
subtree. A sibling starts from the unchanged parent state. Thus these compose
without state leakage:

```mark
<group color:"#6da35a",hue:20,
    <box id:"warm",hue:10>;
    <sphere id:"base",position:[1.0,0.0,0.0]>>
```

The box receives +30° and the sphere +20° relative to the group's explicit
base color. The adjustment API is shared by `tree`, `flower`, `stone`, `grass`,
primitive leaves, groups, repeat bodies and rule invocations.

### 9.5 Reusable shapes and weighted rules (PG12.5)

A `rule` names a composite shape. It contains one or more named `variant`
bodies; each variant has a finite positive weight, default 1. A call chooses
one variant using its own derived key. A single-variant rule is deterministic
composition. Calls can refer to rules declared later and recurse; validate
all references before expansion. Definitions cannot shadow reserved tags.

```mark
<procart version:2,seed:2026,settings:{max_depth:16,max_objects:2000},
    <resources
        <rule name:"ribbon",
            <variant name:"continue",weight:3,
                <box id:"tile",scale:[0.9,0.1],hue:10>;
                <ribbon id:"next",position:[1.1,0.0,0.0],rotate:12,scale:0.92,hue:10>>;
            <variant name:"finish",weight:1,
                <sphere id:"cap",scale:0.25>>>
    >;
    <ribbon id:"first-ribbon",color:"#cc744f">
>
```

Each nested call shrinks and turns the next local frame and adds another hue
shift. A variant name is part of that rule's random identity. Normalize variant
order by name before weighted selection, so reordering definitions does not
change a fixed seed's choice. Changing variant names, bodies or weights changes the
rule contract. P7 starts with parameter-free rules; recipe `settings` remain
the parameter vocabulary for semantic generators. General numeric rule
parameters, expressions and conditionals can be designed later.

### 9.6 Repetition versus independent copies (PG12.6)

Existing `count`/`spread` make independent copies of a node under the same
parent, preserving the Meadow behavior. A new `repeat` gives cumulative
adjustments, corresponding to the repeated transformation patterns in the
reference languages:

```mark
<repeat id:"arc",count:12,
    step:{position:[0.8,0.0,0.0],rotate:15,scale:0.96,hue:10},
    <grid id:"frame",scale:0.3>>
```

The body at index 0 uses the repeat node's initial adjusted state; index 1 has
one `step`, index 2 has two, and so on. Geometrically the frame is
`parent * repeat_local * step_local^i`. Apply color steps successively with
the same per-step clamping rules. Body-local adjustments follow that frame.
The step map accepts common adjustments only, not seeds, counts or resources.
Copies have independent indexed random keys even when their frames accumulate.
Repeat count is a finite positive integer bounded by the document work budget;
nested repetition cannot bypass that budget.

### 9.7 Keys, recursion and work limits (PG12.7)

State consists of an affine frame, color operations, keyed random identity,
rule depth and expansion path. Keep it explicit and immutable (**S12.1.1v3**).
Use the PG11 rule for explicit seeds and key paths. Named children derive from
their IDs; anonymous children use their sibling index. Reordering named
siblings preserves their streams, while moving anonymous children can rekey
them. Resource declaration order is irrelevant. Expanded IDs combine the
invocation path, variant and repetition indices; recursive copies do not
collide even when their authored local IDs are the same.

Derive separate keys for variant choice, scatter placement and model generation
(**PG3**). Calls derive the selected body's key from the chosen variant name;
repeat/copy indices preserve prefixes when counts grow. Generated mesh batching
and traversal scheduling must not change these keys. Expanded output retains
stable source IDs so picking and retained presentation can refer to objects.

#### Global settings

The document's `settings` map configures the whole expansion. Resolve and
validate it once, before visiting any drawing nodes. `version` and `seed` remain
root attributes; per-object `settings` retain their existing recipe-overlay
meaning. Global settings are not inherited as mutable node state.

```mark
<procart version:2,seed:2026,
    settings:{max_depth:32,max_objects:10000,max_vertices:1000000,max_steps:200000,
        min_size:0.001,max_size:null,
        defaults:{sphere:{segments:24,rings:12},line:{stroke:0.01}}},
    <resources>;
    <group color:"#6da35a",
        <box scale:[0.9,0.1],hue:10>;
        <sphere position:[1.0,0.0,0.0],segments:32>>
>
```

| Global setting | Default | Meaning and admission |
|---|---|---|
| `max_depth` | 32 (host cap 128) | Maximum active rule-call depth; nonnegative integer. Root drawing starts at depth 0, and entering a rule adds 1. Zero permits direct drawing nodes but retires every rule call. |
| `min_size` | 0 | Minimum inherited frame size for continuing a branch; finite nonnegative number. Zero disables this criterion. |
| `max_size` | `null` | Optional maximum inherited frame size; null disables it. Otherwise finite and positive, and at least `min_size`. |
| `max_objects` | 10,000 (host cap 100,000) | Maximum emitted drawing instances; positive integer. Includes generated organs and repeated instances, not just authored semantic nodes. |
| `max_vertices` | 1,000,000 (host cap 2,000,000) | Maximum vertices across distinct generated geometry resources; positive integer. Shared prototype vertices count once. |
| `max_surface_pixels` | 262,144 (host cap 1,048,576) | Maximum pixels across unique baked surface textures; positive integer (PG14.4). |
| `max_steps` | 200,000 (host cap 2,000,000) | Maximum source-node, expanded-copy and repetition visits; positive integer. Includes non-emitting groups, calls and repetition visits. |
| `defaults` | `{}` | Per-primitive geometry/style defaults, e.g. `sphere:{segments:24,rings:12}` or `line:{stroke:0.01}`. Missing fields use the versioned canonical defaults in PG12.2. |

The effective primitive field comes from its explicit attribute, then that
kind's document default, then the canonical default. Resolve `defaults` by
primitive kind and declared field; this does not change shallow recipe/settings
merging. For example, a sphere with `segments:32` overrides the global 24 while
retaining the global ring count. Required triangle points and mesh references
remain required on the invocation; `defaults` cannot define per-instance data,
IDs, seeds, references or transformations. Root/group color and placement use
the common adjustment mechanism rather than being geometry defaults.

Unknown settings/default fields, wrong types and invalid ranges fail before
expansion. Validate requested limits against finite host caps; exceeding a cap
is an error, not a silent reduction. Descendants cannot raise global budgets.
Background, lighting and camera remain presentation inputs under
**S12.1.5v2**. The earlier flat `max_depth`/budget attributes in this P7 draft
are consolidated into `settings`; v2 proposes one canonical spelling.

`max_depth` is an artistic rule-call limit: a call exceeding it emits nothing.
A branch similarly retires below a positive `min_size` or above an enabled
`max_size`. Equality remains eligible. Frame size is the Frobenius norm of the
matrix's linear 3×3 part, measured in scene units (identity has size √3).
Test size at each node's adjusted frame before expanding/emitting it. This is
a frame criterion, not a visibility or geometry-bound test, and is independent
of the camera. Return retirement counts grouped by reason in model statistics.
In contrast, exceeding object, vertex or step budgets raises an error
(**PG8**); it never returns a silently truncated successful artwork.

### 9.8 Semantic objects and Meadow compatibility (PG12.8)

`tree`, `flower`, `stone` and `grass` keep their recipes, generator versions and
shallow settings overlay. Their generated parts enter the same adjustment and
emission pipeline as primitives. Place a semantic object inside a `rule` or
`repeat`, or combine it with boxes and meshes under a group. Recipe parameters
shape the intrinsic object; common adjustments transform its completed result.
For example, `settings:{height:0.8}` changes a grass recipe, while `scale:0.5`
scales its completed patch, its placement offsets and blade thickness together.

Meadow v1 files retain their global IDs, required node IDs, limits, radians,
key derivation and grounding semantics. A future adapter may lower them into
shared intermediate data, but must preserve those contracts and the existing
goldens. General v2 permits anonymous/scoped node IDs and the new attributes;
those rules do not silently reinterpret v1 files. Grounding is an explicit
Meadow-profile operation, not an implicit effect of invoking any primitive.

### 9.9 Package and renderer boundary (PG12.9)

Modules are `lambda.procart.scene` (schema, admission and expansion),
`.adjust` (geometric/color state operations), `.primitive` (canonical drawing
parts) and `.rules` (definition lookup and expansion). The standalone `procart.schema` supplies
the structural envelope. Public entry points are:

```text
scene.normalize(document, registry = {}) -> admitted document or error
scene.expand(document, registry = {})    -> {resources, parts, stats} or error
scene.to_scene3d(model, presentation={})  -> existing Scene3D markup or error
```

The trusted caller supplies versioned generator functions in a registry; the
stored document names registered kinds and versions. It does not load modules.
Registry entries contain `{validate,generate}` pure functions; `validate` must
return true and `generate` returns existing `{resources,parts}` organ data.
An optional per-organ `opacity` multiplies the inherited paint alpha; it defaults
to one. This lets the same plant palette retain transparency both as a standalone
model and inside a composition.
The demo registry in `mod_grammar.ls` calls the existing Meadow specimen helpers,
so the package never imports a demo. Existing catalog consumers retain those
generators. Scene3D sphere buffers, transforms and geometry validation are
shared; the existing specimen viewer now uses the package mesh batcher
(**PG9 / D7.2.4v4**). Color parsing/HSV helpers were extracted once to
`lambda.color` and reused by slides, UI tokens and procedural adjustments.

Expansion emits geometry references, composed matrices, final colors/opacity
and source identities. The adapter batches compatible instances into ordinary
Scene3D `resources`, `geometry`, `material`, `group` and `mesh` elements. Camera,
lighting and viewport settings remain a separate presentation input
(**S12.1.5v2 / D7.5.3v2**). New Procart tags do not become native renderer tags.

Free-angle rotations, sphere/cylinder tessellation and tube frames use
trigonometric arithmetic. Their generated geometry joins PG2's tolerance tier
for cross-platform comparisons. HSV conversion uses fixed arithmetic and byte
quantization; keyed draws,
variant choices, topology, counts and explicit identities remain exact.
Identical input on the same platform must still replay on every execution tier
(**S1.6**); the existing foliage JIT defect is not an accepted exception.

### 9.10 Delivery and acceptance (PG12.10)

The [P7 record](impl/Lambda_Impl_Procart_Grammar.md) tracks adjustment algebra,
all nine primitives, named weighted recursion, cumulative repetition, semantic
adapters and the Form studies page. Its four separately stored samples are
primitive vocabulary, turning ribbons, recursive branchwork and a mixed garden.
The page exposes seed replay, maximum-depth controls and retained orbit.
Qualification covers model contracts, native pixels, mobile layout and
existing consumers of the shared helpers.

General rule parameters, source-language import/transpilation, 2D SVG/bitmap
adapters, automatic tube stitching, booleans and interactive grammar editing
remain later increments. The Mark vocabulary expresses the shared
concepts without claiming syntax or output compatibility with EisenScript/CFDG.

### 9.11 Definition names and direct invocation tags (PG12.11)

**User revision, 2026-10-28.** Rules use `<rule name:"R1">` and alternatives use
`<variant name:"continue">`. A direct `<R1 ...>` invokes that rule under its
adjusted inherited state. Rule/variant `id` and the former `use rule` spelling
are rejected. These are package data-schema changes over ordinary Lambda
elements (**S2.6.1v2**), with no parser or formal language ruling change.

Rule names are case-sensitive ASCII names of 1–32 characters: a letter or
underscore first, then letters, digits, underscores or hyphens. Built-in and
structural tags are reserved. Variant names retain the same 1–32-character
ASCII identifier domain as their former IDs and do not need to be tag names.
Rules may call later definitions and themselves. Every call is resolved during
admission, including a call that would be retired by `max_depth:0`.

| Attribute | Applies to | Meaning |
|---|---|---|
| `name` | `rule` | Definition name; also the tag used for invocation |
| `name` | `variant` | Alternative name; used to sort and key weighted selection |
| `id` | `recipe`, resource `geometry` | Required reference identity, selected by `recipe` / `geometry` attributes |
| `id` | Drawing nodes, groups, repeats and direct rule calls | Optional instance identity for stable hierarchical keys and expanded source paths |

For example, `<R1 id:"left-branch">` names one occurrence of the reusable
`R1` definition. Repeated occurrences can have different IDs. Preserving the old
ID values as new definition/variant names and retaining call-site IDs preserves
the random labels, paths, expansion counts and generated geometry.

The standalone schema checks the fixed root, resources and named variant
envelopes. Its drawing-node type is `element`: the document itself supplies
legal rule tags, so `scene.normalize`/`scene.expand` owns drawing-tag membership,
attributes, child restrictions and references (**S11.4.6**). This is explicit
runtime admission rather than arbitrary code or renderer-tag registration.

## 10. P8 — plant architecture and developmental models (PG13)

**Requested and implemented 2026-10-28.** This phase brings selected
ideas from [EZ-Tree](https://github.com/dgreenheck/ez-tree) and
[L-Py](https://lpy.readthedocs.io/en/latest/user/lsystems.html) to Lambda's own
plant package. It implements their concepts in Lambda; it does not import their
code or promise file-format compatibility. Pure generation and presentation
ownership continue to follow **S12.1.1v3 / S12.1.5v2**; execution tiers must agree
on the same platform (**S1.6**). No formal language ruling changes.

### 10.1 Architecture before tessellation (PG13.1)

`lambda.procart.plant` admits versioned, serializable parameters, creates a
seeded skeleton, and meshes that skeleton separately. Each branch records its
stable path ID, parent ID, botanical level, centerline points and ring radii.
Leaves record stable IDs, attachment positions, orientation and size. Changing
mesh detail never reruns growth or consumes a different random stream.

EZ-Tree's per-level branch controls motivate `levels`, an array from trunk to
terminal twigs. Each entry controls `length`, `radius`, `children`, `start`,
`angle`, `taper`, `sections`, `gnarliness`, `twist` and `length_falloff`. Angles are degrees;
lengths/radii are local units. Child attachment fractions range from `start`
to 0.95, with a seeded initial azimuth and a golden-angle spiral. A parent's
`length_falloff` shortens child lengths toward its tip, producing a conifer crown.
`variation`
controls keyed length variation. `force:{direction,strength}` bends each growth
step toward a fixed direction in specimen coordinates; zero force preserves
unforced growth. Placement transforms then move the complete specimen.
Radii taper along each branch; child roots cannot exceed the supporting radius.
The skeleton is genuinely three-dimensional, rather than a rotated planar tree.

### 10.2 Leaves and explicit detail levels (PG13.2)

Leaves attach to terminal branches after `leaves.start`. Controls include
`count` per terminal branch, `length`, `width`, `angle`, `size_variance`,
`shape` (`broad`, `needle` or pointed palmate `maple`) and a palette. Leaf geometry is shared and
instanced. Bark is a swept, capped, tapered buffer using the same parallel
transport frames as P7 tubes. Taper normals include the longitudinal slope.

`plant.mesh(skeleton, detail)` accepts radial `segments`, `section_stride`,
`leaf_stride` and `leaf_scale`. Every detail preserves branch endpoints;
coarse leaves select a stable subset by attachment index and can enlarge coverage.
`plant.lods(skeleton, details)` explicitly returns several models from the
same skeleton. Camera-distance switching belongs to presentation and is deferred.

Full detail defaults to eight radial segments with every section and leaf.
Detail maps admit 3–32 radial segments, section stride 1–24, leaf stride 1–64
and leaf scale 0.1–4. Tree studio selects ten radial segments for its full view.

### 10.3 Development and interpretation (PG13.3)

L-Py's separation of developmental productions from geometric interpretation
motivates a new contextual `lsystem.derive` API alongside the existing
`expand`. A callback receives `(module, context)` with zero-based generation,
index, immediate left/right modules from the **old** sequence and a separately
derived key. All replacements are simultaneous. Missing productions preserve
modules; empty arrays erase them. Context is deliberately immediate sequence
context, including bracket tokens, rather than L-Py's axial/topological matcher.
Trusted Lambda callbacks can use numeric parameters and conditions; literal
stored recipes contain no executable closures. Existing `expand` is compatible.

`lambda.procart.turtle3d` interprets strings or parametric module arrays into
the same branch/leaf skeleton. Initial heading is +Y, left is −X, up is +Z.
`F(length, tip_radius)` draws and advances; `f(length)` only advances;
`+`/`-` yaw, `&`/`^` pitch, `/`/`\\` roll, `|` turns around, `!(radius)` sets
radius, and `L(length,width)` emits a leaf. `[`/`]` save and restore the full
frame, radius and parent attachment. Default turn angles are degrees. A supplied
tropism bends each forward step; bracketed state never leaks to siblings.
Unsupported module names remain available as developmental symbols and draw
nothing. The existing 2D turtle remains unchanged.

### 10.4 Stored scenes, admission and delivery (PG13.4)

`<recipe generator:"plant.tree" version:1 kind:"tree" parameters:{...}>`
registers through `plant.registry()` in P7 scenes; the semantic `<tree>` tag,
inherited transforms and colors continue unchanged (**PG12.8–9**). Generator
version and document version are independent. The package exports the generator;
it does not import the demo. Legacy `plant@1` Meadow recipes retain their meaning.

The second registered generator, `plant.apex@1`, uses a trusted, parametric bud
production followed by the 3D turtle. Its data controls generation count (0–7),
initial length/radius, length decay, branching angle, phyllotaxis, leaf dimensions,
colors and growth force. Each generation doubles the apices; existing stem and
leaf modules persist. Callback code is part of the versioned package.

Standalone saved specimens use this envelope, with no child content:

~~~mark
<tree version:1 generator:"plant.tree" generator_version:1 seed:2026
    parameters:{levels:[{length:3.5,radius:0.18,children:0}],leaves:{count:24}}>
~~~

`plant.architecture(document)` validates the envelope and selects the pinned
generator with `rng.key(seed)`. `plant.from_document(document,detail={})` also
meshes it. `plant_schema.ls` is the offline structural envelope; runtime admission
checks versions and parameter meaning (**S11.4.6**). To compose a specimen in a
P7 scene, copy its generator/version/parameters into a recipe and place `<tree>`
instances. Those instances then use the scene's hierarchical keys (**PG12.7**);
their seed scope is distinct from standalone `rng.key(seed)`. Neither file
contains executable code, camera state or LOD settings.

Admission rejects unknown fields, nonfinite dimensions, malformed levels,
invalid colors, zero force directions with nonzero force and excessive work.
At most five levels, 512 branches, 8,192 leaves and 250,000 mesh vertices are
allowed; skeleton limits are checked from parameters before growth. L-system
derivation has explicit generation and symbol limits, and the 3D turtle bounds
its symbols and stack depth. Limit errors never return a partial plant.

The Tree studio demonstrates separately stored broadleaf, conifer and
wind-shaped presets, seed replay, branch levels, bending and mesh detail. Tests
cover fixed turtle coordinates, state restoration, simultaneous contextual
rewriting, taper/attachment contracts, mesh winding/normals, deterministic
regeneration, LOD identity, invalid-input rejection and native scene rendering.
Texture assets, animated wind, biological resource simulation, full L-Py
context matching/decomposition syntax, and automatic distance LOD remain later
work. Delivery evidence belongs in the [P8 implementation record](impl/Lambda_Impl_Procart_Plants.md).

### 10.5 Reproductive organs (PG13.5)

User direction, 2026-10-28: trees and garden plants need flowers, cones, nuts
and fruit as well as leaves. Generation stays pure under **S12.1.1v3**;
attachment identities and topology follow **S1.6**. Botanical maturity and
seasonal transitions are application state, not generator side effects.

`plant_organs.decorate(model,key,settings)` requires a nonempty plant skeleton.
It calls the shared public `plant.sample(branch,fraction)` centerline sampler
and allocates `count` keyed anchors on every terminal branch, independent of
leaves and tessellation. Each anchor records ID, parent, position and keyed
azimuth. The returned model retains all original parts and resources and adds
canonical flower and fruit parts marked `organ:"flower"` / `organ:"fruit"`.
Visibility selection belongs to the caller; stem and calyx parts share their
fruit's marker, preventing detached supports after seasonal filtering.

Settings admit `flower` (`none`, `blossom`, `bell`, `catkin`), `fruit` (`none`,
`apple`, `berry`, `raspberry`, `strawberry`, `cone`, `nut`), `count` (0–8 per
terminal branch), `petals` (3–32), `flower_size` and `fruit_size` (0.002–1.0
local units), and `flower_color`, `center_color`, `fruit_color`, `stem_color`.
Unknown fields, invalid colors, dimensions and enum values raise errors.
A request may allocate at most 256 anchors. Geometry is shared by kind;
cone scales and raspberry drupelets merge into one reusable buffer rather
than allocating a scene object for every small component. Flowers reuse
Procart's curved blade. These meshes are authored morphological approximations.

`plant` also accepts `leaves.leaflets:1|3`, with a shared attachment and three
fan orientations for trifoliate plants. Existing one-leaf IDs remain unchanged;
leaf budgets include all leaflets. Neither this profile nor an attached fruit
claims pollination, fruit mass, dispersal, pine cone multi-year development,
compound pinnate walnut foliage or calibrated botanical anatomy.

Acceptance: independent parent/attachment and count oracles; leafless attachment
identity; unchanged original parts; stable unique organ IDs; finite meshes;
invalid input and attachment limits; interpreter/AUTO/JIT agreement. Eden's
native replay separately checks seasonal visibility, pixels and saved recipes.

## 11. P9 — reusable surface patterns (PG14)

### 11.1 Pattern resources (PG14.1)

The user requested reusable patterns for stone, leaves and tree trunks on
2026-10-28. A pattern describes appearance independently of geometry. Surface
bindings select that pattern and its coordinate domain. They remain literal
data, with pure expansion (**S12.1.1v3**), runtime admission (**S11.4.6**) and
the existing Scene3D presentation boundary (**S12.1.5v2 / D7.5.3v2**).
This is an additive extension of `<procart version:2>`; no language ruling changes.

```lambda
<resources
 <pattern id:"bark-ridges",generator:"bark",version:1,seed:17,
     parameters:{colors:["#403529","#dfcba5"],frequency:9,warp:0.2}>
 <pattern id:"veins",generator:"leaf-veins",version:1,
     parameters:{colors:["#e6e9af","#70934e"],frequency:7,width:0.025}>>
```

Pattern IDs share the global recipe/geometry/rule namespace. `pattern` is a
reserved structural tag. Definitions have no children; fields are `id`,
`generator`, `version`, optional `seed` and optional `parameters`. Version 1
supports `noise`, `stripes`, `marble`, `bark` and `leaf-veins`. Unknown generators,
versions or parameters raise errors. These built-in generators cannot load code.

| Parameter | Default | Contract |
|---|---|---|
| `colors` | `["#808080","#ffffff"]` | Exactly two opaque colors accepted by `lambda.color`; first is low/vein color, second high/background color. |
| `frequency` | 6 | Finite number in 0.1–64; controls noise/grain or rib density. |
| `warp` | 0.35 | Finite number in 0–4; noise distortion for stripes, marble and bark. |
| `width` | 0.06 | Finite number in 0.005–0.5; vein width for marble and leaves. |

An omitted pattern seed uses the document seed; an explicit seed pins appearance
while geometry changes. Pattern randomness derives from `["surface",pattern.id]`,
separately from shape placement, rule choice and botanical growth. Shared pattern
instances sample the same field; P9 has no implicit per-leaf seed variation.
Free-angle trigonometry and bark's fractional power belong to PG2's tolerance
tier; identical platform inputs must still agree across execution tiers (**S1.6**).

### 11.2 Surface bindings and inheritance (PG14.2)

Roots and all drawing nodes, including groups, repeats and direct rule calls,
accept `surface` and `surfaces`. `surface:"id"` abbreviates `{pattern:"id"}`.

```lambda
<tree recipe:"oak",surfaces:{
 bark:{pattern:"bark-ridges",mapping:"branch",roughness:0.95},
 leaf:{pattern:"veins",mapping:"leaf",resolution:128,roughness:0.7}}>
<stone recipe:"granite",surface:{pattern:"mineral",mapping:"object",scale:0.2}>
```

| Binding field | Default | Meaning |
|---|---|---|
| `pattern` | Required | Pattern resource ID. |
| `mapping` | `"uv"` | `uv`, `leaf`, `branch` or `object`; coordinate contracts below. |
| `scale` | 1 | Positive scalar or vector (2 components for UV domains, 3 for object); divide coordinates by scale. Larger values make larger features. Components are at most 10,000. |
| `offset` | Zero vector | Finite vector of the same dimension; added after scaling. |
| `resolution` | 64 | Integer 8–256; UV tile edge or requested object-atlas edge. Object atlases may exceed it to retain a four-pixel minimum per triangle. |
| `roughness` | 0.85 | Scalar in [0,1] for Scene3D standard materials. |
| `metalness` | 0 | Scalar in [0,1]. |

`surface` supplies the whole-object fallback. `surfaces` is a map from generated
part role to binding, with at most 32 identifier keys. Package plants expose
`bark` and `leaf`; stones expose `stone`. Primitive roles equal their tag.
Role overrides take precedence over the whole-object binding. Missing roles use
the fallback. Unknown emitted roles are therefore usable by trusted generators.

Bindings inherit as immutable values. A child replaces its whole-object binding
when `surface` is present; role maps merge by key, replacing each whole binding.
`surface:null` clears the fallback; `surfaces:{leaf:null}` explicitly disables
that role, including any fallback. Field presence uses `at` so an explicit null
is distinct from omission (**S8.1.1v2 / S8.1.2v3**, name membership). An
inherited role override still takes precedence over a child's new fallback.
Sibling adjustments never mutate another binding or shared resource.

The pattern's RGB multiplies the part's final intrinsic/adjusted color. Existing
`color`, `hue`, `brightness` and opacity processing is unchanged; it produces
the material tint once, rather than recoloring each texture. Pattern colors
are opaque in P9; part opacity remains independent.

### 11.3 Coordinates and detail (PG14.3)

| Domain | Coordinates and admission |
|---|---|
| `uv` | Existing geometry UVs. Native box/plane defaults may use host UVs. Nonidentity UV scale/offset requires an explicit buffer with UVs. Missing UVs raise an error. |
| `leaf` | UV domain on parts whose role is `leaf`: U 0–1 crosses the leaf, V 0–1 runs from base to tip, midrib at U=0.5. Both broad and needle leaves use normalized width. |
| `branch` | UV domain on `bark` parts: U 0–1 wraps around each branch; V is distance from its base in original skeleton units. UVs use the original centerline arc length and transported frames across LODs. |
| `object` | Three-dimensional buffer positions before the instance matrix. Movement, rotation and instance scaling carry the pattern with the object. Native box/plane resources need conversion to explicit buffers for this domain. |

Sweeps emit seam-duplicated side UVs and disk UVs for independent cap vertices.
Merging buffers retains UVs when every input supplies them. Plants retain the
original path distances and frame directions before selecting LOD sections;
retained rings preserve their orientation and V value. Leaf geometry remains
shared across all attachments and detail settings. This adds no growth changes.

UV patterns bake to a repeating square texture. Their coherent noise closes
the circular U domain and cross-fades V; authored nonintegral stripe/rib periods
can still form a visible boundary at a repeated tile. UV scale/offset transforms
the geometry's UVs, so the texture tile itself remains unchanged. Object patterns
bake the three-dimensional field to a padded triangle atlas with independent UVs;
one local-space sample field therefore crosses faces without requiring an unwrap.
Atlas baking preserves source normals, vertex colors and optional tangents.

### 11.4 Expansion, caching and limits (PG14.4)

`lambda.procart.surface` supplies pattern admission/preparation/sampling, binding
validation/inheritance and `bake(model,definitions,seed,limits)`. `scene.expand`
collects effective part bindings and invokes the baker; ordinary unbound models
retain their existing output shape. Bound parts receive
`surface:{texture,roughness,metalness}`; their geometry reference can name a
mapped copy. Expansion still returns ordinary geometry and part data.

Bake jobs are shared by canonical definition, effective seed and binding;
geometry also participates for object atlases and transformed UV buffers.
Forty leaves with the same binding share one texture and their prototype.
Material tint does not enter the bake key. Instance transforms never enter the
object-atlas key. Texture and mapped-geometry IDs include the effective seed,
preventing changed content from retaining an old native resource identity.

All unique jobs are planned before allocating rasters. `max_surface_pixels`
defaults to 262,144, with a host cap of 1,048,576; it counts all unique baked
texture pixels. Object atlases accept at most 4,096 triangles each, use at least
four pixels per tile edge and linear sampling without mipmaps to avoid cross-tile
bleed. Atlas vertices and UV-transformed geometry copies also consume the
existing `max_vertices` budget; retained source prototypes continue to count.
Overflow raises an error, never silently drops a binding. Stats add
`surface_pixels` and `surface_textures` when a pattern is baked.

The Scene3D adapter batches by geometry, tint, opacity, kind, role **and surface**.
Bound parts use standard materials; unbound parts keep Lambert materials.
Textures are self-contained PNG data URIs produced by the existing image package.
Native Scene3D continues to own lighting, cameras, texture upload and rendering.
No renderer or vendor changes are required (**D7.2.4v4 / D7.5.3v2**).

### 11.5 Milestone and follow-up (PG14.5)

The fifth Form studies sample, [surfaces.mark](../test/demo/procart/samples/surfaces.mark),
composes a tree with separately bound bark/leaves, two patterned stones and an
enlarged leaf study. Acceptance covers resource admission, inherited overrides
and null clearing, stable seeds, shared textures, original-path LOD coordinates,
three-dimensional atlas corner oracles, UV transforms, budgets, native pixels,
seed replay, retained orbit and mobile layout. Qualification is recorded in the
[P9 implementation record](impl/Lambda_Impl_Procart_Surfaces.md).

P9 implements color patterns and scalar roughness/metalness. Normal/roughness
maps, displacement, attached moss/thorn geometry, arbitrary drawing-grammar
textures, layered pattern graphs and per-organ seed variation remain later
increments. Standalone P8 tree documents retain their original envelope; use
a P7 scene to store pattern resources and their bindings together.

## 12. P10 — Environment and a living meadow (PG15)

The user requested global environmental settings, responsive plants, a larger
meadow and a pond on 2026-10-28. These package contracts extend PG11–PG14;
**S12.1.1v3** still requires pure generation, and **S12.1.5v2 / D7.5.3v2** keep
live presentation and camera state with their explicit owners. No language
semantics change.

### PG15.1 — One global environment

An optional literal `<environment>` is a direct child of `<procart version:2>`
or the demo's `<meadow version:1>`. It is metadata, never a drawing rule or a
resource. It is distinct from Scene3D’s image-based lighting capture element;
lowering emits lights, background and motion. Multiple, nested, unknown-field and out-of-range forms are rejected.
Omitting it preserves the existing generic composition lighting and output;
the Meadow demo supplies its own default summer conditions.

```lambda
<environment latitude:45.0,altitude:120.0,day:172,time:14.0,
    weather:"sunny",humidity:0.65,wind_speed:0.0,wind_direction:70.0,
    lighting:{sun:1.0,sky:1.0,ambient:1.0}>
```

| Attribute | Units / admission | Default |
|---|---|---|
| `latitude` | Degrees north, −90..90 | 45 |
| `altitude` | Metres above sea level, −500..9000 | 120 |
| `day` | Integer day of a non-leap year, 1..365 | 172 |
| `time` | Local solar hour, 0..24; 24 equals midnight | 14 |
| `weather` | `sunny`, `cloudy`, `rainy`, `foggy`, `windy` | `sunny` |
| `humidity` | Relative fraction, 0..1 | Weather preset |
| `wind_speed` | Metres per second, 0..30 | Weather preset |
| `wind_direction` | Bearing **toward which** wind blows, 0..360° clockwise from north | 70 |
| `lighting` | Optional `sun`, `sky`, `ambient` multipliers, each 0..4 | Each 1 |

Weather presets specify `(humidity,wind_speed)` as sunny `(0.55,0)`, cloudy
`(0.7,1.5)`, rainy `(0.95,3.5)`, foggy `(0.98,0.3)`, windy `(0.55,7)`.
Explicit humidity/wind override a preset. Selecting weather in the demo resets
those two controls to that preset; subsequent manual edits override them.

`lambda.procart.environment.normalize(raw)` accepts the literal element or an
attribute map and fills defaults. `from_document(doc)` returns its admitted
configuration or null. `description(config)` builds a literal for saving.
`sample(config)` returns `{config,sun,weather,background,sun_color,lighting,wind}`.
Sun contains a unit direction, elevation in degrees and a daylight factor.
World axes are east +X, up +Y, north −Z. A seasonal declination approximation
and solar hour angle determine direction; twilight spans −6°..8° elevation.
Time is explicitly solar time: no longitude, time-zone, clock or network input.
Altitude adjusts illustrative clear-air direct-light intensity; humidity affects
leaf droop and mist opacity. Weather attenuates sun and changes sky/fill light.
`lights(sample,extent)` emits existing native hemisphere, directional and ambient
lights. Explicit `scene.to_scene3d` presentation lights/background still win.

The spherical relation follows the [NOAA solar equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF).
This implementation uses a compact seasonal approximation, not NOAA's complete
calendar/refraction algorithm; its output is for artwork, not precision astronomy.

### PG15.2 — Reusable responsive herbs

`lambda.procart.herb` provides `normalize`, `resources`,
`generate(key,parameters,environment_sample=null,frame=identity)` and `registry`.
Registry key `plant.herb@1` supports `flower` with `sunflower`, `water-lily`,
`daisy`, `poppy`, `cosmos`; `grass` with `reed`. Parameters are `species`,
`height` (0.05..4), `size` (0.03..1), `petals` (integer 0..32), `petal`, `center`,
`leaf`, `stem`. Colors use `lambda.color`. Defaults are per species. Shared
rounded organs, cupped petals and notched pads carry leaf/petal UVs. Organ roles
include `stem`, `leaf`, `petal`, `center`, `seedhead`, compatible with PG14 role
bindings. The existing demo's branching cosmos and lavender remain supported.

```lambda
<resources
    <recipe id:"sun",kind:"flower",generator:"plant.herb",version:1,
        parameters:{species:"sunflower",height:1.7}>
    <recipe id:"lily",kind:"flower",generator:"plant.herb",version:1,
        parameters:{species:"water-lily"}>>
```

`scene.expand` passes registered generators the sampled `environment` and the
instance's world `frame`, in addition to the existing request fields. Admission
still validates parameters before allocation. A missing environment remains null
for existing generators; herbs use default summer conditions when called alone.

`environment.response(species,sample)` yields `openness`, humidity-dependent
`droop`, wind `flexibility` and a head `direction`. Sunflower heads align to the
world sun, converted through the instance frame so scattered yaw does not change
their target; at night they return east. Day-flowering water lilies close below
2° solar elevation, open progressively to 24°, and partly close in rain. Daisies
and poppies fold partly at night. Seeded stature, petal variation, placement and
resource identity stay independent of these poses. Leaf placement and flower
opening can change, so pose sampling never means an immutable final mesh.

The sunflower is an explicit artistic tracker: [UC Davis](https://www.ucdavis.edu/curiosity/news/why-sunflowers-face-east)
distinguishes young tracking plants from mature east-facing heads. The lily
profile follows the day-flowering habit of [Nymphaea alba](https://vicflora.rbg.vic.gov.au/flora/taxon/82074df0-b24f-49f6-a703-b8fc63f32e42);
it does not represent night-blooming water lilies.

### PG15.3 — Rooted wind and presentation

`environment.wind(sample,key,seconds,flexibility)` returns a normalized
quaternion, combining two periodic gusts over an eight-second period. A stable
plant-identity key varies phase without consuming growth randomness. Wind speed
zero yields identity; maximum bending is bounded. `rooted(parts,pivot)` converts
world matrices to a root-relative group. `wind_track` samples that same pure
motion into an existing native quaternion track. Trees bend less than grass;
lilies barely move. This milestone sways the whole rooted specimen rather than
solving individual flexible branches or blade dynamics.

With a global environment, expanded semantic plant parts retain an `organism`
record `{id,pivot,species}`. `scene.to_scene3d` wraps them in rooted native groups
and emits an autoplay clip when wind is nonzero; expansion admits at most
1024 moving plant roots, matching the native clip limit; `presentation.motion:false`
stops autoplay. Stones and drawing primitives remain static. `scene.meshes`
remains the static batching adapter. No custom native renderer vocabulary,
wall-clock access from `fn`, or per-frame recipe regeneration is introduced.

Before a Meadow author update, the page snapshots the live orbit pose into
presentation state (**S12.1.5v2**). The new scene and controller start from that
pose; the reset pose stays the original home camera. This explicitly hands the
lower-tier camera to the higher-tier projection without making it a generation
input. Controls expose solar time (including
morning/noon/evening/night), all five weather states, wind speed and bearing,
humidity, latitude, altitude, day of year and sunlight multiplier. Environment
edits update the literal document; new-seed actions retain the chosen conditions.
Reset restores the original document's conditions. Motion can be stopped.

### PG15.4 — Expanded meadow and water habitat

The saved plot is 28 × 20 (previously 16 × 12), with 104 recipe instances:
48 flowers across six varieties, four trees, six stones and 46 grass/reed
instances. A demo `<pond id,position:[x,z],size:[width,depth],depth,color?>`
carves a basin and sandy shore into the seeded terrain and draws a horizontal
water surface with ripple rings. At most one pond is admitted, fully inside the
plot, with positive terrain relief. Lily roots anchor to its stable water level; terrestrial organs are
excluded from the shore interior. Reeds ring the bank. Pond and environment
are separate metadata children, not recipe scatter nodes.

The pond is a static illustrated water surface; rain is a looping field of
native drop meshes, fog is layered translucent mist, and cloudy conditions
change lighting. There is no fluid solver, volumetric scattering, accumulated
rainfall, long-term plant growth, ecological suitability or seasonal simulation.
UI ranges use practical subsets of the package bounds. Native desktop/mobile
pixels and cross-tier admission/pose fixtures qualify the core milestone; see
[implementation and verification](impl/Lambda_Impl_Procart_Environment.md).

## 13. P11 — Procedural animal specimens and markings (PG16)

User direction, 2026-10-28: editable plant/animal model builders in Eden, with
the Scene3D butterfly demonstration as reference and authored frog skin / bird
feather patterns. This profile follows **S12.1.1v3**, **S11.4.6** and
**D7.2.4v4**; biological state and native playback remain outside pure generation.

### PG16.1 — Stand-alone morphology

`procart.animal.normalize(document)` / `.generate(document)` admit stand-alone
`butterfly`, `bird`, `frog`, `fish`, `bee` and `rabbit` elements with `version:1`,
`generator_version:1`, integer/string `seed`, a `parameters` map and optional
species reference. Unknown fields, versions and invalid ranges are errors.
These tags belong to this profile; P7's drawing vocabulary is not extended.

The +Y-up frame faces −Z. `size` is a nominal scale in metres; girth, head,
appendage and tail are authored ratios. Bird feather count is integral; motion
frequency is Hz and amplitude radians. Defaults are authored approximations,
not measured anatomy. Generation returns `{resources,parts,joints,stats,scale}`;
parts retain stable IDs, roles, local transforms and optional hinge IDs.
Existing spheres/cylinders are shared; feathers/fins reuse `primitive.blade`,
also used by herb petals. Bird primary/tail feathers, frog segmented limbs/toes,
fish fins and bee legs/antennae/wings retain their distinct structures.

### PG16.2 — Shared markings

`surface` adds `spots`, `feathers` and `scales`, reusing PG14's seed, palette,
frequency/width/warp, UV bindings, deduplicated baking and admission. Undersides
are separate organ/color regions. Pattern edits do not change topology/motion.
Animal skins use 96² pixels; butterfly wing art uses 256 × 512, included in
statistics. The first model uses a 262,144-pixel allowance and 100,000-vertex cap.

### PG16.3 — Butterfly reuse

`procart.butterfly_atlas` owns the original sixteen-cell SVG atlas; the old
demo module re-exports its constructor/constants. New `wing_svg(options)` shares
contour, vein, border, eyespot and speckle helpers for a configurable right-wing
pair. Mirrored sides share it. Colors, seed, density, mark width, variation and
supported shape ratios are data. These authored studies do not identify species.

### PG16.4 — Presentation and plant pose

`animal.nodes(model,phase=0,motion=false,prefix="specimen")` emits existing
Scene3D resources/groups/meshes and optional hinge tracks. Geometry and texture
generation remain independent of phase/camera. Unit-axis quaternions share
`scene3d.transform.quaternion_axis_angle`. The application owns playback and
biology; these loops do not implement autonomous locomotion.

`animal.pose_nodes(model,{state,speed},phase=0,motion=false,prefix="specimen")^`
adds an admitted family activity input: resting/flight wing poses, walking/hopping
limb hinges, grazing head motion, swimming tail motion and calling throat scale.
Hop offset and all hinges/scales are selected-phase values or native clip tracks.
The original `nodes` path remains unchanged. Application state supplies activity
and speed; this helper never chooses a path, eats a plant or advances a clock.
Eden's ED9 owns those behaviors and any biological assumptions.

`herb.generate` keeps its four original arguments and adds optional fifth
`pose:{openness?}`. Opening is 0–1 and overrides presentation only; environment
response remains the default. Eden's cached plant adapter filters independent
leaf keys and transforms parts while reusing architecture/textures.

### PG16.5 — Acceptance and boundary

Paired `animal_morphology`, `animal_surfaces` and `animal_poses` goldens join the package baseline
and `ProcartAnimalTests.SpecimensAndSurfacesAcrossTiers`. They cover six families,
scene admission, identities, Mark replay, normals/UVs, patterns and diagnostics.
The pre-promotion atlas SVG hash and original native butterfly replay are pinned;
existing plant/surface gates protect shared helpers. Eden owns catalog/editors,
history/cameras, recipe persistence and native builder replay under ED7.
Terrain placement, every-wiki-taxon models and autonomous behavior are separate.

## Appendix S. Superseded proposal

~~The initial P7 schema declared `<rule id:"R1">` and `<variant id:"main">`,
then invoked the rule with `<use rule:"R1">`.~~

The user replaced those spellings with definition/variant `name` attributes and
direct invocation tags in **PG12.11**. Instance, recipe and geometry IDs retain
their separate identity roles. The document profile remains version 2; all
shipped v2 samples are migrated together.

~~The initial 2026-10-28 draft used a `procart` envelope with generic `generate`
instances, and `generated` placeholders in separate presentations. IDs were
metadata only, with an explicit `key_path` for random identity.~~

The user replaced the generic producer tag with semantic `flower`, `tree`,
`stone` and other domain tags, composed through groups into scenes such as
`meadow`. Named hierarchy keys provide convenient independent streams; explicit
seed/path overrides remain available. Recipes, settings precedence, versioning,
pure generation and reuse of ordinary Scene3D output retain their rationale.

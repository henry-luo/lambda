# Procart P7 — drawing grammar implementation record

> **Status:** P7a–P7d implemented and qualified (2026-10-28). The existing
> foliage AUTO/JIT replay issue remains an explicitly recorded qualification limit. The user requested primitives and inherited
> adjustments inspired by Structure Synth and Context Free. The package and
> four-sample page implement the schema, defaults, limits and all nine primitives.
> **Design:** [PG12.1–PG12.11](../Lambda_Design_Procart.md#9-p7--composable-drawing-grammar),
> summarized in [the package ledger](../Lambda_Pkg_Proc_Art.md#pg12--composable-drawing-grammar-p7).
> **Spec linkage:** S2.6.1v2 (element data), S12.1.1v3 (pure expansion),
> S12.1.5v2 / D7.5.3v2 (presentation ownership), S1.6 (execution transparency),
> D7.2.4v4 (package paths). No formal-spec ruling changes.

**Outcome:** a stored artwork composes all nine drawing primitives and semantic
objects using the same adjustment state, named rules, seeded variant selection
and bounded recursive/repeated expansion. `scale` is the relative
adjustment name; existing geometry dimensions remain separate parameters.
Global controls are stored in root `settings`: recursion depth, minimum/maximum
frame size, object/vertex/step budgets and per-primitive defaults (PG12.7).

## Milestones

| Milestone | State | Deliverables | Acceptance cases |
|---|---|---|---|
| P7a — admission and adjustments | Delivered | Standalone v2 schema, root settings/default resolution, definition/reference validation, immutable geometric/color state, box/sphere/triangle/mesh, explicit package modules. | Scalar/2D/3D `scale`; `[0.9,0.1]` normalizes to `[0.9,0.1,1]`; fixed TRS independent of attribute order; nested transforms differ as documented; degrees/radians boundaries; parent hue accumulation, absolute color reset, per-level clamping and isolated siblings; prototype reuse; explicit primitive fields override per-kind defaults; unknown/global setting errors and host-cap admission. |
| P7b — composition | Delivered | Groups, explicit rule invocation, weighted named variants, recursive expansion, independent count/scatter and cumulative repeat; resource/source identities and work accounting. | Named sibling/resource reordering preserves streams; variant ordering is normalized; count prefixes remain stable; repeat index 0 is unadjusted and index 2 has two steps; recursive paths are unique; zero/depth-boundary calls and minimum/maximum frame-size retirement report counts; object/vertex/step budget overflow fails even for non-emitting recursion. |
| P7c — full primitive and object surface | Delivered | Grid, line, point, cylinder and explicit-path tube; shared geometry helpers; versioned registry adapters for tree/flower/stone/grass; Meadow v1 compatibility path. | Correct unit origins and X axes; grid has 12 unique edges; visible local-unit strokes; ellipsoid/reflection normals; cylinder caps; smooth tube frames and invalid-path errors; organ colors adjusted once; geometry cardinality checked before allocation; unchanged Meadow v1 data and goldens. |
| P7d — mixed gallery | Delivered | A Procart grammar page loading separate literal Mark, containing abstract recursive forms and a botanical composition, with shared native Scene3D presentation. | Save/reload of the data reproduces model/choices; all nine primitives are represented; seed next/previous, retained orbit/reset, responsive layout and navigation work; the completed model uses existing native scene tags. |

Package work delivers `procart.scene`, `.adjust`, `.primitive` and `.rules`.
The trusted demo registry reuses the existing semantic generators without
introducing package-to-demo imports or changing catalog consumers. Reuse the existing
Scene3D matrix, box/sphere, buffer and instancing contracts; promote a missing
shared geometry/color helper once rather than copying variants (PG9). Read
`radiant/README.md` before any implementation that changes native rendering.

## Verification budget

Use focused model goldens during each milestone, with a paired `.txt` for every
new unit `.ls`. At milestone boundaries run affected package/demo goldens and
the relevant cross-tier gate. Integer choices, counts and identities compare
exactly; free-angle geometry, HSV conversion and sweep frames use the
PG12 tolerance rules across platforms. Same-platform replay must agree across
interp/AUTO/JIT under S1.6. The recorded foliage JIT issue remains a defect,
not an exemption or a reason to change expectations.

P7a/P7b need document/model checks, not a native UI matrix. P7c adds focused
geometry and native-image checks for normals, strokes and sweeps. P7d gets one
bounded desktop/mobile replay covering pixels, controls and mesh retention,
plus a visual inspection of the mixed gallery. Run required engine aggregate
gates only for engine source changes, and repeat broad qualification only for
relevant changes or unresolved failures. Performance evidence uses a release
build; no speculative native port is part of this plan (PG7).

## Completion and later work

P7 is complete only when every milestone's behavior is delivered. Track actual
results and remaining defects here as implementation proceeds. Implicit tube
stitching, general rule parameters/expressions, source-language translation,
2D export adapters, booleans and an interactive editor remain later work.


## Delivered behavior and verification — 2026-10-28

The package implements all nine primitives, immutable TRS/color inheritance,
weighted recursive rules, cumulative repetition, named keys, global settings,
per-kind defaults and native instance batching. Tube rings use transported
frames with independent cap normals; mesh prototypes share validated buffers.
Admission checks the entire authored graph and effective registered parameters
before expansion; allocation/instance/step limits fail explicitly. Depth and
frame-size retirement have separate reported statistics.

`grammar.ls` loads four literal files from `samples/`: the nine-form vocabulary,
turning ribbons, branchwork and a mixed garden. It includes seed replay,
maximum-depth controls, source descriptions and retained camera controls.
The trusted demo registry reuses the existing flower/tree/stone/grass models.
Meadow v1 continues through its existing resolver; v2 does not reinterpret it.

Shared code was promoted rather than copied: `lambda.color` owns existing
slide parsing and UI HSV arithmetic, the Scene3D geometry validator is public,
and the specimen viewer calls the package mesh batcher. These remain pure
value operations under **S12.1.1v3 / PG9**; native ownership is unchanged
(**S12.1.5v2 / D7.5.3v2**).

| Qualification | Result |
|---|---|
| Four drawing goldens and mixed-gallery model | Pass on interp/AUTO/JIT through `ProcartDrawingTests.ExpansionAndAdmissionAcrossTiers` |
| Final drawing cross-tier gate plus shared UI palette/slide checks | 31/31 GTests pass; the cross-tier GTest includes 15 script executions |
| All ten Procart demo goldens, interp | 10/10 pass |
| Package/demo/UI shared AUTO selection | 59/60 pass; only the existing foliage replay fixture fails |
| Form studies native UI | 50/50 assertions; all samples, nine primitive categories, real pixels, seed replay, depth, retained orbit/reset, mobile and navigation |
| Existing botanical native UI | 68/68 assertions |
| Existing Meadow native UI | 37/37 assertions |
| Existing mineral/texture/pattern navigation UI | 85/85 assertions; navigation expectation includes the seventh destination |
| Literal samples, public references, document/source checks | All four stored scenes validate; both new public API examples compile; golden pairs, JSON, local links and whitespace pass |

Native UI totals **240 passing assertions**. Desktop/mobile captures were
inspected. Evidence is in `temp/procart-p7/`, including
`final-shared-tests.log`, `final-demo-interp.log`, `qualification-auto.log`,
`*-events.json` and the four sample captures.

The existing foliage replay defect remains [LR07-50](<../Lambda_Issue_Ledger (fixed).md#lr07-50>).
No backend override or assertion/golden change hides it. Generator registration
is caller-owned; the general package intentionally imports no demo modules.
The standalone schema is structural: full semantic validation is
`scene.normalize`/`scene.expand`, per **S11.4.6**. Explicit-path tubes are
implemented; implicit stitching, expression-bearing rules, source-language
translation, 2D export, booleans and editor tooling remain later work.

## Rule naming revision — PG12.11, 2026-10-28

The user replaced definition/variant `id` with `name`, and explicit `use rule`
calls with direct named tags. A `<rule name:"R1">` now exposes `<R1 ...>` as a
shape invocation. Rules/variants accept only the new names; call sites retain
their optional instance IDs. Recipes and geometry prototypes keep required
reference IDs. The [current naming contract](../Lambda_Design_Procart.md#911-definition-names-and-direct-invocation-tags-pg1211)
separates these roles without changing Lambda elements (**S2.6.1v2**) or pure
expansion (**S12.1.1v3**).

Resource lookup, variant sorting and selected-body key derivation now read rule
or variant names. Dispatch resolves every non-built-in drawing tag to a declared
rule, including forward and recursive references; reserved tags cannot be
shadowed. Existing instance IDs and name values preserve seeded labels and
expanded source paths. Both recursive saved samples, the rule fixture and
public/design examples are migrated.

The offline schema keeps fixed root/resource/variant envelopes. Drawing nodes
are `element`, since their permitted names depend on this document's definitions;
complete drawing-tag/attribute/child/reference admission belongs to
`scene.normalize` (**S11.4.6**).

The new paired `drawing_names.ls/.txt` fixture covers mixed-case invocation tags,
known cumulative positions/scales and depth retirement, Mark roundtripping,
forward references, named instance identity, geometry IDs and rejection of bad
names, duplicate variants, reserved tags, obsolete syntax and unknown attributes.
Existing golden expectations are unchanged. The drawing gate now checks five
package fixtures and the gallery on three tiers — **18 script executions**;
the plant gate adds **12** passing executions through the shared scene adapter.
The strengthened native Form studies replay passes **50/50 assertions**, including
the displayed name/direct-tag syntax. All four stored scene envelopes validate,
and the new public recursive example runs. Evidence is under
[`temp/procart-rule-names/`](../../temp/procart-rule-names/): `cross-tier.log`,
`grammar-events.json`, `*-schema.log` and `reference.log`.

The required full baseline for this revision completed **8,341/8,347** checks
(2,112/2,112 input and 6,229/6,235 runtime). Four sandbox socket failures clear
in the focused unrestricted server rerun, **152/152 PASS**. The two remaining
script failures are the same unchanged Greetings undo/redo mismatch and existing
[LR07-50](<../Lambda_Issue_Ledger (fixed).md#lr07-50>) foliage replay defect recorded in P8.
The legacy procedural, updated drawing and plant cross-tier gates all pass in
this baseline; no unrelated golden or runtime code was changed. Evidence:
`temp/procart-rule-names/lambda-baseline.log` and `serve-unrestricted.log`.

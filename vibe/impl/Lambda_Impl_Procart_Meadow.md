# Procart Meadow — semantic composition

**Milestone:** a separately stored meadow composes `group`, `flower`, `tree`,
`stone` and `grass` elements into a native Scene3D page. User direction,
2026-10-28, replaces PG11's generic `generate` instances with semantic tags.
Generation stays pure (**S12.1.1v3**); presentation and retained camera ownership
follow **S12.1.5v2 / D7.5.3v2**. The working design is
[Procart PG11](../Lambda_Design_Procart.md).

**Acceptance:** load literal Mark recipes/composition; inherit and override
recipe parameters; preserve nested transforms and named random streams;
reuse existing flower/rock geometry; add seeded curved grass; render trees,
flower patches, stones and grass together on terrain; replay a seed; retain
meshes while orbiting; toggle categories; navigate from the existing pages;
support desktop and narrow windows. Invalid recipes, versions, references and
excessive repetition must fail before expansion.

**Scope:** the first semantic scene profile lives in the Procart demo. Existing
Scene3D elements remain the native renderer input. General generator graphs,
external asset libraries and editor tooling remain later work.

**Verification budget:** focused composition/grass goldens and existing Procart
goldens after shared-helper edits; scene validation; one native graphics replay
covering controls, pixels, orbit and mobile layout; inspect saved captures.
No engine change is planned; an engine defect would require its own root-cause
investigation and relevant aggregate gates.

**Checkpoint:** the Meadow behavior milestone is implemented. Its new model
checks and native UI replay pass. Full Procart AUTO qualification retains the
foliage seed-replay failure described below; no engine workaround was added.

## Delivered behavior

- `meadow.mark` stores recipes, root seed, terrain settings, semantic objects,
  nested groups, repetition/scatter, placement and parameter overrides.
- `mod_composition_schema.ls` provides the standalone recursive type envelope;
  `mod_composition.ls` validates references, exact versions, attributes,
  transforms and expansion limits, then resolves named random streams.
- Flowers share the existing `mod_plants` generator, now also exposed as
  `grow(key, recipe, order, angle)`. Leaf-mode/size defaults are recipe data.
  Trees reuse its branches with seeded crown clusters. Stones call the
  promoted `mod_minerals.rock` helper. Botanical surface resources and mesh
  instancing were extracted for reuse rather than duplicated.
- `mod_grass.ls` produces curved, tapered blades instanced over keyed clumps.
  Fine and tall presets, color palettes and dimensions are in the Mark file.
  Each blade is grounded at its own sampled terrain elevation; the path is
  kept clear. Ground geometry reuses `procart.terrain`/`field`/`adapt`.
- Meadow has native 3D, independent flower/tree/stone/grass visibility, seed
  replay, retained orbit/zoom/reset, desktop/mobile layout and workshop links.
  Integer and named string seeds can both step forward and replay backward.
- The shared orbit component now starts from the supplied home camera and
  scales zoom limits to its distance. Its former fixed specimen camera would
  jump when controlling a world-sized scene. Native output remains ordinary
  Scene3D elements (**D7.5.3v2 / S12.1.5v2**).

## Verification — 2026-10-28

| Check | Result |
|---|---|
| Standalone schema against `meadow.mark` | Pass; zero errors/warnings |
| New `procart_meadow` and `procart_grass` goldens | Pass on interp, AUTO and forced JIT |
| All nine Procart goldens, interpreter | 9/9 pass |
| All nine Procart goldens, AUTO | 8/9 pass; foliage replay below |
| Meadow native UI | 37/37 assertions, including real pixels, orbit retention/reset, layers, seed replay, mobile and navigation |
| Existing botanical native 3D UI | 68/68 assertions |
| Existing mineral/texture/pattern navigation UI | 85/85 assertions; the navigation expectation now includes Meadow |
| Golden pairs, JSON paths, source/doc checks | Pass |

The new model coverage checks actual full-model replay, Mark round trips,
settings precedence, nested transforms, sibling reordering, repeat-prefix
stability, explicit key paths, invalid inputs/budgets, grass normals/dimensions
and Scene3D validation. Native scene captures were inspected at desktop and
390px width. UI replay compares rendered surfaces for orbit/reset and seeded
replay; DOM `getAttribute` is not a geometry-buffer comparison.

No engine source changed for this milestone. The script/model and native UI
checks cover the shared demo helpers. Logs, source-isolated comparisons and
captures are under `temp/procart-meadow/`; desktop/mobile previews are
`meadow-desktop.png` and `meadow-mobile.png`.

## Qualification limits and follow-up

**Engine repair update, 2026-10-28.** LR07-50 is fixed: dynamic calls borrowed
wide seeds from movable argument-List scalar tails. Explicit caller-owned numeric
homes preserve the seed through GC (**D4.3.1 / D5.2.3 / D5.3.3**). The unchanged
foliage replay now passes; the paired reduced regression fails on the preserved
old executable and passes all three tiers under GC stress. See the
[engine repair record](Lambda_Impl_Eden_Engine_Repairs.md) for current aggregate
qualification. The earlier milestone's evidence below remains historical.

**Foliage replay (S1.6 / S12.1.1v3).** Extracting the shared plant generator
exposes the recorded JIT determinism defect on AUTO: identical fern/palm/berry
inputs can yield different organ scales. The interpreter passes the unchanged
fixture. A saved pre-extraction module passes AUTO but also fails forced JIT
for berry spray, so the underlying class of defect predates Meadow. The first
differences are random blade dimensions, not harmless last-bit tolerance. Its
runtime cause remains unresolved; backend overrides, goldens and assertions
were not altered. The central record is [LR07-50](../Lambda_Issue_Ledger.md#lr07-50).

**CLI schema imports.** Attempting to use the implementation module itself as
the CLI schema exposed a null `Runtime` in the import-cache path. The actual
schema now has its own type-only file, separate from implementation imports;
it validates successfully. The general CLI defect and debugger evidence are
recorded in [LR13-17](../Lambda_Issue_Ledger.md#lr13-17).

The next feature increment can expose more existing plant families and extract
a reusable caller-registered scene resolver from this first application profile.
External recipe libraries, general generator graphs, file-editor/save UI and
archival baked models remain outside the delivered milestone.

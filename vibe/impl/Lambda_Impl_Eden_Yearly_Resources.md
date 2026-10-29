# Eden yearly rendering and seasonal studies

2026-10-29. Preserve **S12.1.1v3 / S12.1.5v2**, semantic admission
**S11.4.6**, and shared provider/application ownership
**D7.1.1–D7.1.3 / D7.3.5**. Eden decisions are ED10–ED13; they add no
Lambda language ruling.

## Finite scope and diagnosis

The user reported repeated `scene3d graphics: resource count quota exceeded`
when entering Eden's yearly ecosystem. The actual app is the sibling
`../eden`; ignored working copies under Lambda's `temp/` identify it. Earlier
searches limited to tracked Lambda sources missed it. Preserve the existing
uncommitted fruiting-plant work in both checkouts.

Reproduce native yearly entry and captures, inspect the generated batches,
repair the shared lowering, and verify unchanged instances/identities and
bounded renderer resources through growth and view switches. Finish the
pending Eden Wiki entry, untranslated world title, deciduous maple presets,
seasonal foliage and close-up sakura petal/maple leaf studies. Verify relevant
package goldens, Eden's three tiers and native application fixtures; run the
required Lambda gate once after the shared package changes settle. Native
renderer quotas are retained.

The paired working tree already contains the **LR07-52** repair documented in
[the engine repair record](Lambda_Impl_Eden_Engine_Repairs.md#structural-map-grouping--lr07-52).
It traced the screenshot's resource-count failure to independently allocated
equal surface maps being grouped by address instead of value, contrary to
**S5.6.1v2 / S5.6.2**. That repair reduces the saved summer projection from
2253 meshes to 58 with unchanged geometry and textures. Preserve its runtime
change, tests and Eden ED12 receipts.

With that repair present, the current yearly entry reproduces
**`instance quota exceeded`**, rather
than the screenshot's resource-slot diagnostic. A generated foliage batch
contains **24192** instances; native Scene3D admits **16384 per mesh**.
`procart.scene.meshes` grouped every equal geometry/material into one node,
with no second partition. Native projection already destroys its previous
projection and releases its owned resources before rebuilding, so this
reproducer provides no evidence of an accumulating resource leak.

## Shared package change

Promote the existing per-mesh bound to `scene3d.model.INSTANCE_LIMIT` and
use it in both validator paths and Procart's lowering. Split each material
batch into consecutive slices within that bound, retaining all matrices,
source IDs, material values, options and order. Nothing is culled to satisfy
admission; the native instance/resource limits are unchanged. Empty, exact
boundary, remainder and multiple-chunk cases have paired goldens on both
interpreter and JIT. The original yearly entry/captures then pass; the existing
98-event ecosystem replay passes all 42 assertions.

Extend the existing folded-leaf fan with a pointed palmate maple outline;
broad/needle geometry retains its original point/triangle order. The same
normal/UV construction serves every outline. New pure geometry and admission
checks accompany a paired golden.

## Eden integration

The staged Eden change uses its existing 150-species catalog and Chinese labels
to expose a Wiki workspace in the rail, with links to the selected language's
existing pages. It retains builder/simulation state and uses existing Markdown
navigation. Localize the displayed world name via the registered `world` label
group; saved names and unregistered custom names stay unchanged (**ED10**).

Literal sugar/red maple presets use the shared palmate leaf mesher. Literal
season profiles also cover existing deciduous trees/shrubs. The yearly scene
samples calendar/hemisphere-dependent leaf retention and pigment; ecological
state and budgets are unchanged. Eden's supported catalog expands from 16 to
24 species to admit the 18 supplied profiles, bounding the largest daily grid
to 1536 cohorts (24 × 8 × 8). This is an explicit application-profile extension;
it does not change a renderer quota.

Seasonal leaf textures carry neutral vein shading, with pigment in the material
color. Tinting an already green texture was visibly producing brown/olive autumn
leaves. Separating these signals preserves veins while permitting the authored
red/gold palette and avoids per-date texture proliferation.

The close-up study detaches a bounded keyed selection from the attached model,
reuses its geometry/materials, and samples delayed descent, lateral drift,
flutter and a ground-plane landing into existing native one-shot clips. An
exact birth/impact key accompanies sampled flight keys. Ground clearance uses
the rotated vertices. Settled poses stay still; replay creates a new clip
identity. There is no world-clock step or per-frame Lambda ecological update
(**S12.1.5v2 / ED13**). The population inspector can open the same study while
pausing daily/animal jobs and silencing active observation audio.

These are authored visual phenology/flight profiles, not calibrated botany,
aerodynamic CFD, cloth deformation, litter decay or an ecological carbon budget.
Botanical references: [sugar maple](https://plants.ces.ncsu.edu/plants/acer-saccharum/),
[red maple](https://plants.ces.ncsu.edu/plants/acer-rubrum/), and
[Japanese flowering cherry](https://plants.ces.ncsu.edu/plants/prunus-serrulata/).
The calendar bands and rates remain explicitly authored inputs.

## Verification receipts

Artifacts are under `temp/eden-yearly-quota/`. The staged project is `eden/`;
`eden-base.json` records source hashes so installation can reject concurrent
changes instead of overwriting them.

- `repro.log`: failed native entry/capture with the oversized foliage batch.
- `probe.log`: generated material batches and their full instance counts.
- `batched.log`: the same entry/captures pass after partitioning.
- `ecosystem-batched.log`: existing ecosystem replay, 98 events / 42 assertions.
- `batches-{interp,jit}.out/.err`: boundary and preservation checks.
- `maple-interp.out`: palmate geometry, normals/UVs and scene admission.
- `seasons-native-first.log`: new UI checks pass through the studies, then
  correctly fail on the old 16-species domain cap with the expanded catalog.
- `eden-tests-first.log`: first diagnostic pass, with the same domain-cap
  rejections retained rather than changing expected outputs.

The next UI attempt measured **230 graphics resources** in the yearly scene.
Its resource assertions used an unsupported `lt` key; the event protocol accepts
`min`/`max`, so the equivalent integer predicate is `max:4095`. Preserve that
failed attempt separately. Final receipts follow after the corrected fixture
and final source complete qualification.


Final staged qualification:

- All 27 Eden fixtures pass interpreter/AUTO/JIT (**81/81**); the final added
  catalog-cap checks pass all three with forced collection every 97 allocations
  and freed-storage poisoning.
- Both new package goldens pass interpreter and JIT with the same GC stress.
- `seasons-native-qualified.log`: **58 events / 36 assertions, all pass**. It
  exercises Wiki/title locales, winter shedding, native maple/sakura flight,
  unchanged animation resource counts, yearly admission/growth, population
  study/return with preserved date, and two Wiki/yearly round trips.
- `seasons-native-unbound-panel.log` retains an intermediate failed replay: the
  new population panel needed its editor/library passed explicitly. The final
  panel takes that state and the study transition passes.
- `tools/check_locales.py`: **604** matched English/Chinese entries, placeholder
  parity and view registration pass. Selected local documentation links,
  launcher syntax, fixture JSON, golden pairs and `git diff --check` pass.
- Final native maple and sakura captures were visually inspected.

Original Eden README and verification files received newer ED12 qualifications
after the staging snapshot. Merge those receipts into the ED13 document update
and hash-guard their current versions during installation; do not overwrite
them with older staged prose.

Required aggregate gate: **8396/8396 pass** for `make test-lambda-baseline`,
with 2112 input and 6284 runtime checks (`core-gate.log`, `core-results/`).
No additional native/layout engine change was made for ED13; the focused actual
application replay supplies native rendering coverage for the new package code.

Installation completed in `../eden`: **34 source/data/document/test files**,
with target hashes checked before any write and every installed file compared
with its qualified source. The newer ED12 README/verification evidence was
merged, and no other Eden source differed from the staging copy.

`installed-entry.log` / `installed-entry-result.json`: final executable run from
the actual Eden directory, **8 events / 6 assertions pass**, including real
yearly rendering/capture and the unchanged resource bound. The first installed
capture attempt used an absolute output path rejected by the capture protocol;
`installed-entry-absolute-capture.log` retains that diagnostic. Repeating with
the required project-relative `temp/ed13-installed-yearly.png` path passes.
The installed locale checker and whitespace checks in both checkouts pass.
Restart an already open Eden window to load the changed source and packages.

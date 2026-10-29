# Procart P10 — Environment and living meadow

**Status:** core delivered and focused/native acceptance passed, 2026-10-28.
The broad baseline retains two pre-existing failures listed below.
**Authority:** PG15 in [Procart design](../Lambda_Design_Procart.md); pure
sampling follows S12.1.1v3, native presentation follows D7.5.3v2.

## Finite milestone

Deliver a larger saved meadow, a recessed pond with lilies and reeds, additional
flower species, and global environment data shared by package generators and
presentation. Latitude, altitude, local solar hour, day of year, humidity,
weather, wind and lighting have bounded, documented units. Seeded architecture
and placement remain independent of environment. Sunflower heads track the
sampled sun; day-flowering lilies close at night; wind animates rooted plant
groups through existing native animation clips. The sidebar changes conditions
without changing the seed or resetting the orbit camera.

Acceptance: saved-data roundtrip and rejection cases; equatorial/polar sun
samples; day/night lily opening; sunrise/sunset head direction; stable seeded
architecture; rooted periodic wind; pond excludes terrestrial plants; weather
and controls change native pixels; calm replay and mobile layout remain valid.

Verification budget: focused package/demo golden fixtures on interpreter,
AUTO and JIT; neighboring drawing/plant/surface gates once at completion;
native Meadow replay for controls, motion, orbit and mobile pixels. No engine
changes planned; broaden qualification only if an engine defect blocks delivery.

## Deliberate bounds

Illustrative plant behavior and weather, not a climate or ecological simulator.
Time is local solar time, with no time-zone/longitude conversion. No growth over
seasons, fluid simulation, physical volumetric fog or accumulated precipitation.
Sunflower tracking is an explicit artistic behavior (mature real sunflowers
usually remain east-facing). Existing surface work is preserved.

## Delivered behavior and ownership

- `procart.environment` admits a global element or map, derives local-solar
  direction/daylight and weather lighting, samples species responses, and
  supplies rooted periodic quaternion tracks. Configuration is separately
  serializable; no wall clock or procedural effects enter generation.
- `procart.herb` exports six reusable species, shared cupped-petal/notched-pad
  geometry and organ UVs. `plant.herb@1` works in generic compositions alongside
  existing registries. Sunflower head orientation converts world light through
  the instance frame; lilies close at night and partly in rain. Keyed stature,
  placement and shared geometry remain stable across conditions.
- Generic scene expansion reserves the environment vocabulary, passes samples
  and frames to generators, retains organism pivots, and admits at most 1024
  moving roots for the native clip. Explicit presentation lights/background
  remain overrides. A structural schema admits environment/resources in either
  order; semantic admission still owns references, bounds and uniqueness.
- Meadow is 28 × 20 with 104 recipe instances, six flower varieties, four trees,
  granite, grass and reeds. A carved pond owns one horizontal water level;
  lilies anchor there and terrestrial organs avoid its interior. Its floor and
  terrain sides include the basin depth. Pond admission rejects flat relief,
  invalid dimensions/colors and multiple ponds.
- The sidebar exposes solar hour, five weather states, wind speed/direction,
  humidity, latitude, altitude, day of year and sunlight. It updates the saved
  literal environment, retains conditions through seed changes, resets the
  original day and can stop motion. Rain and mist use existing native meshes.
- Camera pose is presentation state, handed from the live controller to author
  state before the author replaces the scene (**S12.1.5v2 / D7.5.3v2**). The
  controller receives distinct initial and home/reset poses. Camera attributes
  use full-precision numeric text so DOM readback does not round typed array
  components. The shared DOM serializer handles nested instance rows and both
  parsed and typed numeric arrays; scalar numbers retain JSON precision.

## Verification — 2026-10-28

- `make test-lambda-baseline`: **8,353 / 8,355 passed** (2,112 input-parser
  cases plus 6,241 runtime cases). All Procart drawing, tree, surface and
  environment gates passed on interpreter/AUTO/JIT, as did the Meadow golden.
  The two failures are the already recorded, unchanged
  `tests_greetings_model` popup undo/redo mismatch and
  `tests_procart_foliage` seeded replay mismatch
  ([LR07-50](<../Lambda_Issue_Ledger (fixed).md#lr07-50>)). Their expectations and runtime
  implementations were not changed. A first sweep overlapped a helper edit;
  the figures here are from the subsequent stable full sweep.
- `ProcartEnvironmentTests.SolarPlantsAndWindAcrossTiers` covers solar vectors,
  equatorial/polar day and night, day/night opening responses, humid/dry leaves,
  world/local head orientation, wind periodicity/unit quaternions/root anchoring,
  malformed settings, literal roundtrip, native wind lowering, organ anatomy,
  petal winding/UVs, precise DOM numeric serialization, pond admission and
  Meadow condition replay across all three execution tiers.
- Native replays pass **203 assertions**: Meadow 37, environment 46, Form
  studies 50, Tree studio 51, surfaces 19. Checks include actual scene pixels,
  day/night lily poses, rain/fog, moving wind with retained mesh identity,
  stopped motion, keyboard range controls, location input, unchanged seed,
  camera retention, exact reset/seed pixel replay, layers, navigation and
  desktop/mobile bounds. Desktop, mobile, night, rain and wind captures were
  visually inspected.
- Saved Meadow schema validation: zero errors/warnings. New scripts have paired
  goldens; JSON, local documentation links and `git diff --check` pass.
- No engine/vendor code changed. No performance measurements or claims were
  made from the debug build. The existing P7/P8 three-container shutdown
  diagnostic remains visible in the drawing gate; it is not suppressed.

Artifacts are under `temp/procart-p10/`; stable broad qualification is
`lambda-baseline-final.log`, with native `*-events.json` results and captures.
Earlier interrupted/diagnostic logs are not the qualification result.

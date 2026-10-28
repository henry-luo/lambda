# P8 — Procart plant architecture

> **Status:** P8a–P8d implemented and focused qualification passed, 2026-10-28.
> The full baseline retains two unrelated script failures after sandbox-only server failures were cleared in a focused rerun.
> **Authority:** [PG13.1–PG13.4](../Lambda_Design_Procart.md#10-p8--plant-architecture-and-developmental-models-pg13);
> S12.1.1v3 (pure generation), S12.1.5v2 (presentation), S1.6 (tier transparency).

| Milestone | Acceptance |
|---|---|
| P8a — delivered | Admitted per-level tree architecture, stable branch/leaf skeletons, force and taper |
| P8b — delivered | Shared tapered sweep, leaf instances, explicit skeleton-preserving LODs |
| P8c — delivered | Contextual parametric derivation and stack-based 3D turtle feeding the same mesher |
| P8d — delivered | Stored presets, semantic registry, Tree studio and focused native replay |

Focused verification covers independently known turtle coordinates and winding,
context values, admission errors, skeleton/detail invariants and three execution
tiers. Shared P7/legacy L-system gates run after module changes. Native replay
checks every preset, seed reset, controls, orbit and mobile layout. Run the
required Lambda baseline once at the milestone boundary and record any failures
without weakening unrelated tests or changing their goldens.

## Delivered behavior

`lmd/package/procart/plant.ls` exports parameter admission, seeded architecture,
meshing, explicit LODs, stored specimen loading and the trusted semantic registry.
Per-level controls include length falloff toward the parent tip, supporting a
conifer silhouette. Broad and needle leaves use one folded diamond buffer per
model and native instances. Palettes use shared color admission; intrinsic alpha
survives the standalone adapter and multiplies inherited composition opacity.

`apex.ls` implements a bounded numeric bud model, using contextual
`lsystem.derive` and `turtle3d.skeleton`. Productions see the previous generation,
including immediate bracket tokens; recursive halving admits each replacement
before joining a generation, rather than flattening an oversized result. The 3D
turtle restores frame, position, radius and parent attachment on pop. Errors
from command callbacks propagate out of the accumulator rather than producing an
empty-looking skeleton.

P7's existing parallel-transport sweep is promoted and extended with per-ring
radii and taper normals. Its frame and buffer-merging helpers serve both plant
paths; existing constant-radius primitive output remains unchanged. Rodrigues
rotation is promoted from the existing Scene3D camera module. No engine/native
generator changes or third-party source imports are involved (**PG9 / D7.2.4v4**).

`test/demo/procart/trees.ls` loads four standalone `tree-*.mark` specimens. The
controller caches architecture across detail/visibility changes. Stored seeds
own initial state; changing seed and stepping back exactly replays pixels.
`mod_grammar.ls` exposes the two new package generators alongside the legacy
Meadow handlers. Every workshop now links the Tree studio.

## Qualification

| Gate | Result |
|---|---|
| `ProcartPlantTests.ArchitectureAndDevelopmentAcrossTiers` | PASS: three package goldens plus the Tree studio model, each on interp/AUTO/JIT — 12 script runs |
| Existing `ProcartTests.KeyedNoiseIsExactAcrossTiers` | PASS: legacy algorithms, including deterministic/stochastic/parametric rewriting, on all three tiers |
| Existing `ProcartDrawingTests.ExpansionAndAdmissionAcrossTiers` | PASS: all P7 fixtures and four-sample gallery on all three tiers |
| Tree studio native replay | **51/51 assertions PASS**: four presets, object categories/pixels, exact seed replay, growth/force/detail controls, leaves, retained orbit and mobile |
| Form studies native replay | **50/50 assertions PASS** |
| Shared workshop navigation replay | **85/85 assertions PASS** with eight destinations |
| Four saved specimen envelopes | CLI schema validation PASS, zero errors/warnings |
| Two new public API examples | Compile PASS |
| Golden pairs, event JSON, local document links and `git diff --check` | PASS |

The three native replays total **186 passing assertions**. Captures were inspected
for all four desktop specimens and the mobile layout. Artifacts are in
[`temp/procart-p8/`](../../temp/procart-p8/): `final-tests.log`,
`trees-events.json`, `grammar-events.json`, `workshops-events.json`,
`*-desktop.png`, `trees-mobile.png`, the schema logs and API example logs.

The required `make test-lambda-baseline` completed **8,340/8,346** checks:
2,112/2,112 input checks and 6,228/6,234 runtime checks. Four server failures
reported socket binding denied by the sandbox; the unrestricted focused server
rerun passed **152/152**. The remaining two script failures also reproduce in
isolation: the unchanged Greetings model produces errors for pop-up undo/redo,
and the existing [LR07-50](../Lambda_Issue_Ledger.md#lr07-50) foliage JIT replay
failure remains open. Greetings imports no Procart code and its source/dependencies
were not changed by P8. Their goldens were not changed. Evidence:
`lambda-baseline.log`, `serve-unrestricted.log` and `final-tests.log`.

The inherited three-container/120-byte shutdown diagnostic also occurs in the
earlier P7 qualification logs; it is not introduced or suppressed here. A full
Radiant engine gate was not needed: no Radiant engine source changed. The
selected native consumer replays cover the shared adapters and navigation.

## Deliberate limits

This phase implements the PG13 subset, not either reference package's full
language. Immediate sequence context is not L-Py's axial pattern matcher.
Stored specimens select package generators and numeric parameters; they cannot
load arbitrary code. Low-level meshing expects a skeleton returned by the two
admitted generation paths or the turtle. LOD selection is explicit; texture
assets, animation, physiological simulation and distance switching remain future
phases. No release performance claim is made.

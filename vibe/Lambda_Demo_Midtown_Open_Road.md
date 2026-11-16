# Midtown Open Road — Proposal

**Status:** Proposal and implementation record, 2026-11-16. USER asked to take
the Midtown demo closer to a modern driving game: a more realistic city,
streets and cars; bridges, a tunnel, a roundabout and other road types; and
countryside and coast roads to drive through, at a frame rate that keeps up
with normal-speed driving. It stays a street-driving sandbox, not a race.
This document introduces no language or runtime rulings.

**Scope:** `test/demo/midtown/` only, plus its golden
(`test/lambda/scene3d/midtown.ls`), replay fixtures and preview. Radiant and
the Lambda runtime are used as they are; defects found on the way go to their
owners' ledgers (AGENTS.md "Working across project boundaries").

**Specification linkage:** [Formal Semantics](../doc/Lambda_Formal_Semantics.md)
S12.1.1v3 (scene packages build pure values; the host owns GPU work) and
S12.1.3 (mutation only in `pn` handlers). [Formal Design](../doc/Lambda_Formal_Design.md)
D9.3.4 (shipped `lambda.*` packages) and D9.5.6 (the Lambda↔Radiant
boundary). Existing Midtown design and evidence:
[README](../test/demo/midtown/README.md),
[Radiant_Scene3d_Phase1](impl/Radiant_Scene3d_Phase1.md) "Retina frame pacing",
[GPU compositor](radiant/Radiant_Design_GPU_Compositor.md).

## Current checkpoint

- **Updated:** 2026-11-16, main checkout `master` (base `315915449`), all
  changes uncommitted. Another session builds and tests in the same checkout;
  timing runs wait for a quiet machine (`uptime`, `ps`).
- **Decisions (this proposal, not yet reviewed by USER):** OR1–OR9 below.
- **State:** M1 done except junction signal for the gateway approach. New
  modules `mod_land` (branch-free terrain formula over numbers or arrays),
  `mod_roads` (loop spline, profile, corridor index), `mod_region` (carving,
  driving height, solids, land use), `mod_world`, `mod_land_mesh`,
  `mod_land_scene`; minimap region layer (an SVG image). M2 next.
- **Evidence:** golden passes except `obstacles` 857→858 (barrier gap);
  headless shots `temp/openroad/shots/`; startup (release/lambda of 11-13,
  headless, load only) 1.5 s at base → 2.4 s now.
- **Perf lessons:** per-element Lambda calls cost ~5–10 µs; array maths
  ~0.03 µs per value (`sin`, `exp`, `abs`, `math.sqrt` are element-wise;
  clamp/min/max via `abs`); `flatten(transpose(channels))` interleaves 100k
  values in 1 ms; `a[i][k]` in a loop is ~30 µs per read (bind `a[i]` first);
  `sign()` returns an untyped array that `transpose` rejects;
  `math.transform_buffer` replaces Lambda-side point transforms.
- **Open:** live frame timing is noisy under GPU contention (same build 23 vs
  28 ms mean); measure OR9 with a release build on a quiet machine in M4.

## 1. What changes

Lumen Bay keeps its 6 × 6 city, but the east end of Union Street now leaves
town through a gap in the perimeter barrier. Beyond it a roundabout feeds one
loop road of about 3.5 km through open country and back:

1. **Gateway roundabout (R1)** at (310, 0): a ten-metre ring around a raised
   island with a monument, splitter islands and yield lines.
2. **River truss bridge** over a river that runs south to the sea.
3. **Farmland** of rolling fields, hedgerows, farmhouses, barns, hay bales and
   a line of wind turbines on the ridge.
4. **Hill tunnel** through a forested ridge, lit by ceiling lamps.
5. **Coast road** along cliff tops, down to a beach bay and up past a
   lighthouse headland, with sailboats out at sea.
6. **Estuary cable-stayed bridge** across the river mouth, back to R1.

Traffic drives the loop both ways and circulates through the roundabout. Auto
drive tours the loop when engaged outside the city.

## 2. Rulings for this demo (OR#)

- **OR1 One world function.** `mod_world.ground(x, z)` answers the driving
  height everywhere: city rules inside the barrier, the nearest road's deck
  inside a road corridor, terrain elsewhere. Bridges and tunnels never stack
  roads, so ground stays single-valued; the vehicle model is unchanged.
- **OR2 Terrain is a carved heightfield.** A 10 m near grid over the drivable
  region and a 50 m far grid out to the horizon, both sampled from the same
  noise at shared points. Roads carve their corridors; bridges leave the
  valley; tunnels keep the hill and drop only the triangles at each portal.
- **OR3 Roads follow the land.** Each road's elevation is the natural terrain
  under it, smoothed and grade-limited, with explicit spans: a bridge deck
  keeps a clearance envelope over water, a tunnel runs straight between its
  portals.
- **OR4 One water plane.** The sea, the harbour and the river are a single
  water surface; terrain below it is sea floor or river bed. A car that sinks
  below it is put back on the nearest lane.
- **OR5 Oriented solids.** Guardrails, parapets and tunnel walls are oriented
  boxes in their own cell index; the existing separating-axis test handles
  them with the city's axis-aligned boxes.
- **OR6 Paths for traffic.** Open-road traffic follows precomputed lane paths
  (closed loops, one per direction, through the roundabout) with geometric
  car-following; grid traffic stays as it is.
- **OR7 Chunked static geometry.** The near terrain and vegetation instances
  are split into tiles of about 600 × 500 m so native frustum culling drops
  what the view or a shadow cascade cannot see; structures are one mesh per
  material each. Nothing static is edited per frame.
- **OR8 Realism within the shipped renderer.** Clearcoat physical paint,
  metal-rough and normal maps, vertex-coloured terrain over detail textures,
  height fog, bloom, a colour-grade shader pass and contact-shadow decals. No
  new native features are required.
- **OR9 Budget.** The release build holds a 60 Hz median frame on the Retina
  MacBook with the GPU compositor in the city and on the open road, and the
  page is interactive within a few seconds of launch.

## 3. Milestones

| M | Content | Done when |
|---|---|---|
| M1 | Terrain, coast and river; road network with roundabout, bridges and tunnel; world ground and solids; driving, reset and splash; HUD names and minimap | The car drives the whole loop and back into town |
| M2 | Realism: lofted cars with clearcoat, reflective facades, road textures, terrain shading, vegetation, farms, turbines, lighthouse, boats, clouds, grading | Day, dusk and night look right in all regions |
| M3 | Open-road traffic and Auto drive on the loop | A simulated minute has no overlaps and no car off its lane |
| M4 | Performance pass, golden and replays, README, preview | OR9 measured; goldens and replays pass |

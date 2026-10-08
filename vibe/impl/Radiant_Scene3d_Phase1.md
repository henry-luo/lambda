# Native scene3d — Phase I implementation

**Status:** in progress, 2026-10-08. Scope and acceptance are
[Radiant_Design_WebGL.md §§4–9 and Appendix A](../radiant/Radiant_Design_WebGL.md).

The explicit `lambda.scene3d` module constructs ordinary elements. Pure helpers
follow S12.1.1v2 and D7.2.1/D7.2.4. Radiant owns rendering behind the DOM boundary
(D7.5.3); native projections copy values and use generation-checked DOM identities
(D4.5.1v4/D4.5.2). No borrowed GC value is retained.

Delivery sequence:

1. Native GL waist: explicit core contexts, branded resources, shader services,
   FBOs, linear-light shading, normalized immutable snapshots, quotas and teardown.
2. Native scene projection: hierarchy, camera, geometry, materials, textures,
   lights, shared resources, instancing, validation and invalidation.
3. Shared SVG sizing/fitting and retained page presentation.
4. Source package, Lambda goldens, native pixel/lifecycle/page corpus and gates.

The full host's existing `LAMBDA_HEADLESS_GLFW_WINDOW=1` switch explicitly enables
hidden GLFW infrastructure for rendered tests. Ordinary headless execution and
profile C remain windowless (D7.1.4v2). Runtime-only and WASM acquire no GL dependency.

Validation commands and measured release growth will be recorded here after
the focused tests and aggregate baselines run. The matched pre-change release
measurement is recorded locally in `temp/scene3d/release-baseline.json`.

# React compatibility implementation

Date: 2026-10-25
Status: P0 implemented and verified; follow-on milestones remain open

## Outcome and authority

Make pinned, unmodified React 19.3.1 and React DOM ordinary JavaScript guests
of LambdaJS/Radiant. Add `test/js/react.js`, its HTML document and golden to
the existing auto-discovered JS unit harness. Fix failures in shared runtime,
DOM bindings or event mechanisms, never in React or a fixture-only shim.

The production parser/execution path remains D8.1.3v42; declared host members
follow D7.4.4; native handoffs and retained callbacks follow D5.3.3 and the
Radiant ownership seam D4.5.1v4/D4.5.2. Form policy stays in the shipped DOM
behavior package (DOM Default ES5/ES25/ES26); native code supplies dispatch,
activation staging and canonical state mechanisms. No formal ruling changes
are proposed.

## Evidence before implementation

The analysis probes under `temp/react-analysis/` used official npm packages,
esbuild's automatic JSX transform and the existing executable:

- Production mount, state/effects, click/timer updates, keyed identity, refs,
  portals and controlled text input passed AST and MIR.
- Basic hydration preserved the existing node and updated after explicit GC
  in AST/MIR; a separate HTML-script render exported the updated text to SVG.
- Controlled checkbox activation failed in AST/MIR and through HTML scripts.
  `HTMLInputElement.prototype.checked` lacks React's captured getter/setter.
  The synthetic bridge currently reaches click defaults after author dispatch;
  HTML checkbox/radio pre-activation must be observable during listeners.
- Both setImmediate and explicitly selected MessageChannel scheduling passed
  the basic production probe. MessagePort delivery still uses timers.
- Native bare npm imports and untransformed JSX failed. A development bundle
  passed AST; whole-unit MIR exceeded a bounded 50-second probe, which is not
  a performance measurement or a diagnosed runtime failure.

## P0 — permanent React tests and the failing shared mechanisms

Done criteria:

1. A reproducible production bundle at `test/js/react.js` uses pinned React,
   React DOM and scheduler, preserves upstream licenses, and has readable
   owned test source/build instructions beneath `test/js/react/`.
2. The ordinary JS GTest discovers the fixture and compares meaningful golden
   observations for mounting, state/effects, batching, keyed identity, refs,
   portals, text input, checkbox/radio activation and cancellation, select /
   textarea, hydration, scheduler progress, and unmount/GC lifetime.
3. Tests use normal DOM APIs and deterministic explicit React commit boundaries
   or observed completion; no framework polyfills, arbitrary sleeps, suppressed
   failures or changed expected outputs that bless runtime defects.
4. Missing prototype accessors reuse declared member publication. Diagnose
   activation before editing: package activation and native staging must agree
   for script-created and physical clicks, including canceled activation.
5. Add small DOM regressions where they isolate the shared cause; React tests
   remain the end-to-end acceptance case. AST, MIR and AUTO must agree.

Owner/hypothesis: eager DOM prototype publication is in
`lambda/js/js_dom_realm.cpp`; synthetic/trusted dispatch and behavior selection
are in `radiant/event.cpp`, while checkbox/radio policy is in
`lmd/package/dom/form.ls`. First reproduce failures through the committed test
shape and trace whether package matching, pre-activation or state reflection
first diverges. Reuse existing dispatch/state helpers rather than adding a
React path or duplicating default actions.

Verification budget: focused React and related DOM cases in the edit loop;
focused native/physical-input cases for shared activation changes; integer-cast
lint for Radiant edits; then build-test, Lambda baseline, Radiant baseline and
the existing JS/Test262 preliminary gate at the milestone boundary. Keep
every-allocation GC/poisoning focused on lifetime cases. Record required-gate
failures and diagnose relevant regressions; never mask Test262 failures.

## Follow-on roadmap from the analysis

These remain distinct milestones; P0 does not claim complete browser or
arbitrary React-application compatibility:

- **Live rendering:** physical input and repeated class/style/layout mutations,
  layout-effect measurements, focus/selection, dynamically created controls,
  paint invalidation, long-lived windows and navigation teardown.
- **Scheduling/lifetime:** yielding, transitions, Suspense/lazy loading,
  animation-frame checkpoints, pending work across GC and repeated documents;
  improve MessagePort task-source semantics if a real acceptance case fails.
- **Authoring:** support externally bundled browser output first. Direct JSX/TSX
  and npm imports would need their own parser/lowering and package-resolution /
  conditional-export work. Browser import maps are a separate existing path.
- **Bundle qualification:** exercise development and large production bundles
  within explicit compilation/admission budgets, investigate the MIR diagnostic
  lane, and retain useful errors. React 19 has no upstream UMD distribution.
- **Broader compatibility:** advanced hydration/mismatch recovery, component
  libraries, observers/geometry, React 19.3 host features, SSR and eventually
  server-component/framework support each need explicit acceptance scopes.

## Completion record

Implemented:

- Added the generated React fixture, readable JSX suite, pinned npm lock and
  MIT license; its full golden agrees with headless Chromium 148.
- Published native input.checked and textarea.value accessors through D7.4.4.
- Behavior matching now reads live DOM attributes. The original source value
  had no `type` for a freshly created input, so package activation never matched.
- The shared native click scope roots a package completion through propagation;
  the package owns pre-activation, commit notification and canceled rollback.
  Existing physical/synthetic radio fixtures now assert pre-activated state.
  Interpreted and compiled package completions enter through their respective
  runtime ABIs; both Lambda execution tiers are covered. Completion runs before
  author reconciliation and retains the normal behavior dispatch/emit context.
  The Radiant gate exposed why that context matters: without it, input/change
  notifications used the standalone event path and the chart's reactive input
  update was overwritten by its subsequent change notification. The shared
  `EmitHandlerScope` repairs that boundary; `chart_input_controls` passes all
  seven assertions without changing the fixture. Completion also publishes
  nested emit mutations to the outer event's dirty flag, and the already-handled
  UA path still settles those mutations. The DTNA controls regression exposed
  both missing handoffs; its 25 assertions pass on AUTO and forced Lambda JIT.
- A MIR-only failure remained after every React assertion passed. Native
  Promise property writes were excluded from the shared property membrane and
  attempted to mutate metadata-owned expando storage with MVP's plain-map
  writer. React's lazy thenable `.status` write triggered a sticky allocation
  abort. The membrane now recognizes native Promise carriers; the small
  `promise_expando` regression runs on MIR in the mixed baseline.
- Promise metadata now dispatches extensibility, preserves Symbol own keys,
  and uses property presence rather than prototype Get for `in`. Absent own
  properties delegate Set to the carrier's prototype, preserving inherited
  setters and Receiver instead of walking the expando's unrelated prototype.
  Shared deletion delegates MVP-owned storage to its shape-aware operation
  instead of writing native tombstones. These repairs are covered by the
  Promise regression and retain the class-metadata ownership boundary
  (**D3.4.7**).
- Added a physical UI fixture loading the same React bundle as an HTML script.
  It verifies a counter and both directions of a controlled checkbox update.
  The bundled suite also covers Suspense/lazy resolution and a class error
  boundary, with explicit completion signals rather than sleeps.

Verification:

- React's full golden passes AST, MIR and AUTO. The physical React UI fixture
  passes all seven assertions; existing synthetic and physical radio UI cases
  pass as well.
- Both small regressions pass AST/MIR. Activation and Promise expandos pass
  every-allocation GC plus freed-memory poisoning; activation also passes
  with the Lambda package forced to JIT.
- All 92 neighboring DOM fixtures pass AST.
- `make build-test`, Radiant integer-cast lint, and `git diff --check` pass.
  Regenerating the bundle from the pinned inputs reproduces all 230,594 bytes.
- `make test-lambda-baseline`: 8,220/8,220 pass, including 550 JS tests,
  847 lower-level JS runtime tests and the Test262 preflight. Its first run
  exposed the inherited-Promise-setter defect above; the corrected rerun passes.
- `make test-radiant-baseline`: passes with native media/graphics service access.
  The final inventory has 6,173 passes, 143 recorded partials, two skips and no
  failures, including 680/680 UI automation tests. The first sandboxed run
  blocked native video/audio/GPU services; the final aggregate uses the required
  native service access and retains the existing partial/skip inventory.
- `make test262-baseline`: 40,261/40,261 fully pass, zero regressions and no
  unstable/slow results. The harness reports 2,652 skips under its existing
  configuration; no harness, skip list or baseline was changed.

One adjacent standards edge is kept explicit: when a canceled radio click's
listener moves the old checked peer into another group, the HTML Standard's
legacy-canceled-activation algorithm clears the clicked radio. Installed
Chromium 148 and 152 leave it checked. `dom_click_activation` retains the
specification expectation; ordinary cancellation and the complete React suite
agree with Chromium. This rare browser difference does not expand P0's scope.

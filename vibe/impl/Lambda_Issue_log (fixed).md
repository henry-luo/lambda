# Lambda Language and Runtime Issue Log

This log records confirmed issues encountered while implementing Lambda-facing
features. Each entry must distinguish a Lambda language/runtime defect from a
DOM, Radiant, package-policy, test-fixture, or toolchain defect. Do not infer
a language/runtime issue from an application-level failure.

## Entry format

- **ID / date** — short title
  - **Area:** parser, evaluator, MIR, GC/rooting, module system, or standard library.
  - **Reproduction:** smallest command or test.
  - **Observed / expected:** concrete divergence.
  - **Status:** open, fixed, or not-a-Lambda-issue.
  - **Resolution / ruling:** implementation link and relevant `S#` or `D#` ruling.

## 2026-09-07 — computed-style catalog row was not realm-neutral

- **Area:** GC/rooting.
- **Reproduction:** `document.execCommand('backColor', false, '#FF0000')` on
  selected content in `temp/api_exec_probe.js`.
- **Observed / expected:** the color mutation completed, but resolving
  `dom.computed_style()` from the Lambda command package returned `ItemError`.
  The command result record was lost, so no required `input` event was sent.
  - **Status:** fixed; verified by the contenteditable command matrix.
- **Resolution / ruling:** the catalog row now carries `DOM_F_NEUTRAL`, making
  the core operation callable from the realm-neutral Lambda DOM package. The
  implementation also roots its node, property, and computed-style temporary
  across nested calls, satisfying D5.3.3's precise-rooting requirement.

## 2026-09-07 — pending scalar resolution requires intrinsic NO_GC audit handling

- **Area:** MIR / GC metadata.
- **Reproduction:** `make lint` (`structural:gc-effects`).
- **Observed / expected:** the static audit expanded the MIR pending-pair
  materialization intrinsic through its number-stack fallback and reported it
  as an ordinary MAY_GC import. Reclassifying it MAY_GC made the emitter
  recursively materialize the pending argument it is specifically meant to
  resolve, causing stack overflow.
- **Status:** fixed; verified by `make test-lambda-baseline`,
  `make test-radiant-baseline`, and the full lint gate.
- **Resolution / ruling:** retain the intentional `JIT_EFFECT_NO_GC` contract
  protected by `AutoAssertNoGC`; teach the audit that this is a dynamically
  guarded MIR intrinsic rather than a general unchecked no-GC leaf. This
  preserves the exact-rooting contract in D5.3.3.

## 2026-09-07 — dense-array fast-store import omitted a required safepoint

- **Area:** MIR / GC metadata.
- **Reproduction:** `make lint` (`structural:gc-effects`).
- **Observed / expected:** the dense-array store's final write is correctly
  guarded by `AutoAssertNoGC`, but its preceding prototype/shape guards can
  lazily resolve JavaScript state. The exported whole operation was incorrectly
  classified `JIT_EFFECT_NO_GC`, allowing MIR to omit roots across those guards.
- **Status:** fixed; verified by `make test-lambda-baseline`,
  `make test-radiant-baseline`, and the full lint gate.
- **Resolution / ruling:** classify the import `JIT_EFFECT_MAY_GC`; the
  localized final write remains no-GC while the native boundary now preserves
  caller roots under D5.3.3.

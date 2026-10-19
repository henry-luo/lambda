# Automatic package layout connection

**Ruling:** user decision, 2026-10-16; **D7.5.3v2**.
**Status:** implemented and focused verification complete. Inventories regenerated; aggregate qualification has the failures/limits below.

Graph and TikZ HTML selects its existing layout name; Radiant resolves that name
to a pure package `layout(parent, children, ctx)` export. This reuses MathML's
package bridge, retained module state and precise document paint roots
(**D7.2.4v3 / D5.3.3**). The public `radiant.register_layout`, graph
`transform.install()` and conformance `install()` entry points are removed.
No application or pure document transform performs callback installation.

The mutable Lambda callback table, separately rooted function slots and heap
cleanup hook are removed. The private native callback table remains for engine
extensions and isolated tests; clearing it cannot disconnect shipped layouts.
Static HTML lazily acquires a package evaluator, while nested Lambda rendering
uses its live evaluator. Creating a second static document now transfers the
thread at the quiescent package-loading boundary before initializing its module
(**D5.4.1**); previously initialization tried to use the first document's owner.

Acceptance covers direct graph rendering and serialized HTML with no setup;
measured graph/TikZ paint across reflow; two independent static documents and
closing one before reflowing the other; absent public registration; and unchanged
unknown-name resolution. Existing BFC, negative-placement/constraint and Velmt
attribute-to-paint checks move from scripts using the removed API to native
package-boundary tests. Their geometry assertions remain unchanged and execute
on interpreter, automatic and JIT tiers. The accessor-only smoke fixture is
renamed `radiant_velmt_helpers`.

Focused validation is recorded under `temp/automatic-layouts/`:

- `custom-layout-unit.log`: 13 native custom-layout cases pass.
- `api-surface.log`: catalogue colour and absent registration checks pass.
- `native-{auto,interp,jit}.log`: automatic graph/TikZ and package boundary cases
  pass in all tiers; the auto run also covers native MathML.
- `goldens-{auto,interp,jit}-final.log`: 21 graph/TikZ/accessor goldens pass per tier.
- `generated-clip-final.log`: all three ancestor-overflow pixel assertions pass
  with generated TikZ paint; the former custom registration fixture is migrated.
- `boundary.log`: module boundary check passes.
- `lint-final.log`: Radiant integer-cast rule passes.
- `formal-index-check.log`: generated index is current; existing duplicate DO25
  warning remains.

Native qualification used a separate macOS clipboard transport linkage repair
in the shared workspace. The CLI already compiled that work's `.mm` source;
native runners needed the same Objective-C++ wrapper scheme as IME. The
clipboard work and its build wiring are outside this change.

The page fixture `test/input/graph_transform_render.ls` also needs no install
call. A repository-wide tracked-script search finds no remaining registration
or graph installation calls. A Lambda-generated document with no Radiant import
renders its declarative graph layout successfully (`no-import.*`).

An invalid `200px` TikZ dimension exposed a separate error-propagation fault;
its preserved reproducer is recorded as **LR10-20** in the central issue ledger.
The migrated clipping fixture uses supported TeX dimensions and keeps its pixel
assertions unchanged.

Aggregate qualification:

- `lambda-baseline.log`: 7,300/7,509 checks pass. The 1,605/1,607 script results
  include the passing graph/TikZ cases; failures are `binary_js_bridge` and the
  concurrently changed `tests_note_images_test`. Other failures are JavaScript
  emission/backend/regex checks, three JS reference cases in MIR GC stress, and
  the JS optimization idle timeout. This gate is not green.
- Radiant's full in-process runner passes 134/134 cases, and its full UI run
  passes the migrated generated-layer clipping fixture. The broader aggregate
  reports CSS/WPT/layout failures. `radiant-baseline.log` is a partial failed run:
  stopped after more than ten minutes of repeated unrelated graphics/physics
  case timeouts. Completed results include native text/layout 134/134, UI
  580/631, page loading 108/109, and layout baseline 3,103/3,294. No full-suite
  pass is claimed. `stopped-processes.txt` records the run's terminated subtree;
  a separate concurrent baseline was left running.

Inventory regeneration uses `make interp-sweep ARGS='--jobs 3'` in the frozen
CLI/package workspace. The Make target now forwards `ARGS` to the existing
sweeper, allowing a bounded worker count without changing its corpus, timeout
policy or comparison rules. The first default-worker attempt was stopped under
shared-checkout contention (`interp-sweep-first.log`); it did not publish lists.

Final inventory result: **1,277 scripts; 1,229 matches, 24 exclusions, 24
inconclusive timeouts, zero mismatches and zero launcher errors**. All three
lists were regenerated from that one report. The committed partition omits the
concurrent, uncommitted `proc/image_view.ls` fixture: 1,276 scripts, with 1,229
matches, 23 exclusions and 24 inconclusive results. `inventory-check.txt`
confirms a disjoint, exhaustive partition, existing paths only, matching new
helper/accessor rows, and no references to the removed registration fixtures.
`formal-index-check-final.log` and the final whitespace check pass (the existing
duplicate DO25 index warning remains). No required application work is pending;
full aggregate green qualification remains limited by the recorded failures.

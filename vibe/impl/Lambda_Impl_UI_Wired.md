# lambda.ui.wired implementation

**Status:** initial native subset, 2026-10-13.

The package follows shipped source packaging (**D7.2.4**), logical element
identity (**S2.4.3v3**), pure presentation/effectful handlers (**S12.1.3**), and
native control ownership (**D7.2.5**, **S8.2.2v5**). It takes visual reference
from [Wired Elements](https://wiredjs.com/) without a browser web-component or
RoughJS dependency. No semantics or design ruling changes are required.

Implemented 21 constructors plus the page view/facade, family-scoped tokens/CSS,
and deterministic SVG quadratic pencil strokes. Two strokes per edge give the
rough outline; seeds keep it stable during state changes. Native inputs sit
inside decorative wrappers; SVG overlays are noninteractive and accessibility
hidden. Progress uses a diagonal hatch. Elevation adds lower/right strokes.

Shared prop contracts, token validation/serialization, and semantic HTML shapes
were extracted from bold into `lambda.ui.core`. Native input event dispatch
uses one policy for dtna, bold, and wired. Existing namespaces, public props,
and ownership behavior remain unchanged.

Visual verification exposed Radiant defects. Absolute percentage heights must
use the containing block's used padding box even when its authored height is
auto (CSS 2.1 §10.5). The automatic-size query previously discarded a resolved
SVG height and derived a square viewport from the viewBox. Flex content sizing
also stretched item heights after laying out their absolute descendants,
leaving stale overlays. The engine preserves resolved absolute percentage
heights and republishes final flex item heights to positioned descendants.

Verification lives in `test/lambda/ui_wired` (four golden scripts),
`test/ui/wired` (native ownership/forms and retained geometry), and
`test/ui/wired_gallery.ls` plus its event fixture. The package guide is
[`doc/Lambda_UI_Wired.md`](../../doc/Lambda_UI_Wired.md).

Validation on 2026-10-13:

- `make test-ui-wired`: four source goldens and four native fixtures in each
  execution tier; 52 native assertions per tier, 156 total.
- All 35 dtna/bold/wired source goldens pass in interpreter, automatic, and JIT
  modes (105 runs). Existing focused dtna ownership/form fixtures and the bold
  gallery also pass.
- Chromium independently agrees with all seven plain HTML geometry checks.
  Without the final flex-height publication, the native textarea regression's
  SVG remains 71.2px high inside a 120px card; its correct padding-box height is
  116px. This test isolates the engine defect from the theme.
- The broader Radiant run passes 555 native UI tests, all 51 page snapshots,
  and the render baseline gate. It is not fully green: eight CSS sizing failures
  also reproduce in an isolated binary with only this work's three engine
  changes removed; the previously observed Markdown broken-image placeholder
  failure remains. One retained-display-list reader scheduling test failed in
  the full run and passed on isolated retry.
- The existing uncontrolled-select issue **UI-2** reproduces identically with
  the original HEAD UI package and the shared helpers. Tests remain unchanged.
- `make lint ARGS='--rule ^no-int-cast-radiant$'` and `git diff --check` pass.

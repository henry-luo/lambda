# Pinned Ant Design reference

Ant Design **6.6.5**, upstream commit
`4a39f54842eade4e565ab336ef6097cd7e723cdd`, is the comparison target.
`app/package-lock.json` locks the reference application and capture tooling.
The package integrity is also recorded in `catalog.manifest`.

From the repository root:

```sh
mkdir -p temp/ui_dtna/npm-tmp
PUPPETEER_SKIP_DOWNLOAD=true TMPDIR="$PWD/temp/ui_dtna/npm-tmp" \
    npm ci --prefix test/ui/dtna_reference/app --cache "$PWD/temp/npm-cache"
TMPDIR="$PWD/temp/ui_dtna/npm-tmp" \
    CHROME_HEADLESS_SHELL=/absolute/path/to/chrome-headless-shell \
    node test/ui/dtna_reference/app/capture.mjs
```

The default fonts are the test corpus's Liberation Sans regular, bold, italic
and bold italic faces. Their weight/style mappings and hashes are recorded. Set
`DTNA_REFERENCE_FONT` to the exact font file used in native captures when
comparing another font. The capture writes source/lock/font
hashes, browser identity, viewport, geometry and PNG hashes to
`temp/ui_dtna/reference/provenance.json`. Locale is en-US, timezone UTC,
scale 1, and motion disabled. Generated output stays under `temp/`.

Foundation, composition, Progress, Timeline/Statistic, Badge/Descriptions,
Skeleton, Card/Empty/Result, Typography, scoped typography tokens, line/conic gradients, Pagination, Grid/Flex, Segmented, Avatar, Tag, Rate, Alert, Spin and Button modes
cases establish reference input, geometry and interaction evidence. Avatar's
pixel comparison is described below; other cases do not establish native pixel
correspondence.
Native comparisons and the full feature matrix remain milestone gates in
`vibe/Lambda_Pkg_UI.md` (execution tier parity: **D8.1.1v17**).

`.ui-assets` marks a subtree of supporting assets rather than native JSON
interaction fixtures. Both phases of UI harness discovery honor this marker;
fixture directories must remain outside it so manifest ownership and nonzero
assertion checks still apply.

Regenerate the licensed icon data from the locked dependencies with
`node test/ui/dtna_reference/app/freeze_icons.mjs`. `icons.manifest` records
all 848 upstream definition hashes and the generated catalog hash. No upstream
package code is edited.

`node test/ui/dtna_reference/app/freeze_illustrations.mjs` regenerates the five
default-light SVG illustrations. `illustrations.manifest` records their
upstream and generated hashes; the adjacent assets retain AntD's MIT license.
Each reference capture records its own viewport so comparisons use matching
native fixture dimensions.

Grid records its initial 500px viewport and live 800px, 1100px, then 500px
resizes. `python3 test/ui/dtna_reference/check_grid_geometry.py` verifies all
32 native geometry oracles against that capture, including explicit RTL,
nested gutters, push/pull reset and flexible columns. This check establishes
those geometry comparisons; it does not certify broader component parity.

`python3 test/ui/dtna_reference/check_typography_tokens.py` compares all 30
native font-size, line-box-height, value, focus and caret assertions with the
scoped typography capture and its live edits. Heading margins, glyph pixels
and input widths remain separate correspondence work.

`python3 test/ui/dtna_reference/check_segmented_geometry.py` verifies 14 size,
block, vertical and RTL geometry oracles. Browser interactions cover repeated
selection, disabled choices and controlled requests. The pinned browser radios
omit their native `value` attribute and submit `choice:on`; dtna's named hidden
field deliberately submits the selected scalar's text (`choice:3`). Its change
notification retains the original typed scalar. Repeated ordinary SVG values
currently expose open engine issue **UI-1**; the native fixture remains failing.

`python3 test/ui/dtna_reference/check_avatar_geometry.py` compares 23 size,
corner and font oracles across six live viewport states. Missing responsive
breakpoint entries revert to the default Avatar metrics, matching the pinned
reference. Text auto-fitting, image failure handling and groups/overflow remain
open.

After capturing the reference, run
`python3 test/ui/dtna_reference/check_native_pixels.py` to compare Avatar on
both native execution tiers using the existing `assert_snapshot` harness.
The checker verifies source, lockfile, font and screenshot hashes before
running. It permits at most 0.1% mismatched pixels at the harness's fixed YIQ
threshold, with identical 500×600 dimensions and no masks. Generated fixtures,
native screenshots and logs remain under `temp/ui_dtna/pixel_checks/`. This
records correspondence for this fixture on the current macOS renderer, not
all Avatar features or platforms.

`python3 test/ui/dtna_reference/check_tag_geometry.py` compares 16 Tag geometry
and color oracles and verifies the captured selection/close observations.
Upstream close cancellation maps to dtna's controlled visibility request;
native checkable buttons also support Enter alongside Space. The standalone
flex strut fixture now passes all eight unchanged assertions; **UI-2-R**
records the root-cause fix in the archived issue ledger.

`python3 test/ui/dtna_reference/check_rate_geometry.py` compares 22 Rate size
and font oracles and verifies 13 committed reference requests across full/half,
clear, controlled, disabled, readonly, keyboard and RTL modes. Native buttons
supply Space/Enter activation; upstream star spans supply Enter. This is the
sixteenth reference case. The independent `inline_baseline_contracts` and
`hover_boundary_contracts` fixtures isolate fractional line boxes, flow-button
sizing, ancestor boundary order, `relatedTarget` and non-bubbling event flags.

`python3 test/ui/dtna_reference/check_alert_geometry.py` compares 21 Alert
geometry/color oracles and verifies six reference action/close observations,
including nested roots without HTML IDs. Native controlled visibility retains
an alert after its close request; upstream Alert closes it, so that translation
is recorded separately. Native Enter/Space and semantic-part overrides have
additional assertions. The seventeenth reference case does not establish full
pixel or close-motion correspondence. The independent
`border_color_cssom_contracts` fixture covers shorthand compression, inheritance
and synchronous `currentColor` refresh after a style mutation.

`python3 test/ui/dtna_reference/check_spin_geometry.py` compares ten initial
geometry/style oracles, including indicator-only sizes, description fonts and
nested content. Real reference interactions cover delayed activation, blocked
content clicks, pending-owner removal and fullscreen pointer/keyboard behavior.
The eighteenth case explicitly pauses CSS Web Animations at 0ms because Spin's
constant keyframes ignore the global motion token. Automatic percentages remain
a live observation, so this PNG carries no pixel-parity gate. Native timing uses
document-owned named frames (**D7.5.3**). The independent
`pointer_events_cssom_contracts` fixture checks inheritance, handler-time reads,
active-state recascade and generated overlays after an ownership swap.

`python3 test/ui/dtna_reference/check_button_geometry.py` compares 34 initial
geometry/style oracles, three semantic-part observations and real keyboard,
delayed-loading, controlled-loading, removal and submit/reset interactions.
This nineteenth case pauses keyframes at 0ms. It establishes these observations;
full Button pixels, motion and component-token/provider defaults remain open.
The independent `form_activation_rebind` fixture checks native submit/reset
continuations when click and form handlers redraw their containing template.

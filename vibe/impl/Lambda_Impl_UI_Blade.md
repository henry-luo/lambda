# Lambda UI Blade implementation

## Architecture

Blade is an ordinary shipped source package (**D7.2.4**), with 64 qualified
component tags (**S2.4.3v3**). It shares 58 Ant-inspired component constructors
and a scoped configuration provider with Clay; five additional widgets supply
RPG panels, meters, item slots, ability buttons and vector icons.

The shared constructor registry, family diagnostic mapping, nested slot
lowering and fallible renderer now live in `ui/core/catalog.ls`. Theme modules
keep static tag factories so descriptors retain their authored family.
The existing source adapter caches each lowered descriptor in view state;
retained native collection state continues to belong to that descriptor.
Providers remain explicit scope boundaries. RPG widgets participate in the
same recursive validation pass while retaining their own presentation tags;
panels also cache the lowered tree so their nested collections keep state.
Array and page rendering propagate diagnostics (**S7.6.3v2**).

The document shell is shared by Bold, Wired, Clay and Blade. Theme validation,
derived token aliases and scoped inheritance share the core theme helper.
The native delegation helper takes an optional family, retaining `dtna` as
its default. Blade's slot and ability views use that helper for event scoping;
they do not copy native behavior state machines. Their native buttons provide
keyboard activation, and only event procedures emit application effects
(**S12.1.3**). Explicit null/false control ownership remains based on key
membership (**S8.2.2v6**). The initial theme implementation changed no language
semantics or C++ engine code; the select correction below updates Radiant.

## Appearance and artwork

References: [RPGUI](https://github.com/RonenNess/RPGUI) and the user's supplied
fantasy inventory/equipment image. RPGUI's framed containers, resource bars,
inventory icons and native form concept inform the theme. Blade ships no
RPGUI code or assets and no pixels from the supplied reference.

The theme uses scoped CSS gradients, multiple inset bevels, serif typography,
gold/iron borders, dark wood and parchment. Eighteen original equipment/spell
icons use SVG paths, bevel facets and highlights in a 96×96 viewBox. Engraved
corner pieces and meter frames use separate scalable SVG geometry. Artwork
is constructed by ordinary source functions, without external resources or
pixelated image scaling. Blade-specific selectors remain under `.blade-root`.

Panels emit sections with headings. Meters expose textual ARIA min/max/current
values and proportional fills. Inventory selection is controlled application
state; slots emit `activate` requests without changing that selection.
Abilities use the same action channel, with native disabled semantics for
positive cooldown percentages. Hotkey labels and cooldown values are supplied
by the application; no game timer or global key dispatcher is installed.
Any positive cooldown shades the full circular clipped face, while the hotkey
is its sibling outside that clip. A lighter shade keeps the disabled spell icon
readable alongside the disabled skull; activation remains guarded by the
existing native disabled state (**D7.2.4**, **S12.1.3**).

## Validation

Source goldens: `test/lambda/ui_blade/`. Native fixtures: `test/ui/blade/`,
`test/ui/blade_gallery.json` and `test/ui/blade_components.json`.
`make test-ui-blade` runs all three execution tiers (**D8.1.1v18**).

The source checks cover all 58 shared constructors, five RPG widgets,
qualified descriptors, the document shell, native roles/attributes, errors,
finite dimensions, token derivation/inheritance, and all 18 SVG icons at larger
sizes. Native checks cover selection, accepted and rejected controlled
requests, Enter/Space activation, disabled and cooldown guards, retained tab
edits, nested RPG actions, table sorting/selection/paging, form submit/reset,
scoped tokens, parchment text, large vector dimensions, sibling theme roots
and narrow/wide gallery geometry. Native PNG captures are under
`temp/ui_blade/`.

Shared source/helper changes also require the Clay, Bold and Wired gates.
The theme inherits the shared component feature limits documented in
[the public guide](../../doc/Lambda_UI_Blade.md); it adds no placeholder widget
for unsupported catalog families.

Validated on 2026-10-13:

- `make test-ui-blade ARGS='--jobs 2'`: four source goldens (79 assertions)
  and five native fixtures (64 assertions) pass on interpreter, auto and JIT.
- Clay and Wired source/native gates pass on all three tiers after the shared
  helper extraction. Bold's source contracts and gallery pass, but its native
  controls fixture reports 20/21: an uncontrolled select returns to its default
  after an application redraw. The same failure reproduces with all original
  UI source helpers restored; no select ownership code was changed here.
- Native wide/narrow captures and static Chromium exports were inspected.
  The native component collection reserves more vertical space in some
  wrapped panels than the Chromium export. No compensating panel heights
  were introduced. Interactive browser distribution remains deferred.
- `git diff --check` passes. No C++ engine or vendor files are part of this
  initial theme change.

## Native select correction (2026-10-13)

The popup painter used fixed white/gray/blue colors, and combo-box options
never resolved view styles because their normal boxes are hidden. Form layout
now resolves option and optgroup styles without exposing those boxes. Popup
and listbox painters share computed option paint settings; the popup surface,
border and closed arrow also consume the control's computed style. Blade
supplies scoped normal, checked/hovered and disabled option colors.

Option `:hover` is derived from the existing select hover index and dropdown
owner. Transitions request the ordinary pseudo-state recascade, preserving
one owner per state (**S12.1.5v2**). A disabled-row test also exposed an
unguarded user commit; the source procedure now reuses `option_enabled`
before calling the native setter (**S12.1.3**, **D7.2.4**).

`test/ui/blade/dropdown.ls` and its JSON replay check dark/parchment computed
styles, painted normal/hover/disabled colors, selection, dismissal, and
disabled pointer/Enter commits. PNG captures remain under `temp/ui_blade/`.

Validation: `make test-ui-blade ARGS='--jobs 2'` passes four source
goldens (79 assertions) and six native fixtures (80 assertions) on
interpreter, auto and JIT. The Radiant dimension lint reports no violations.

`make test-radiant-baseline` passes all native/UI/DOM suites, page snapshots,
form layout and render baseline checks. It retains one Markdown regression:
`md_nuxt-readme` element matching is 99.3% against a recorded 100.0%. Earlier
baseline logs from 12:49 and 13:25, before this correction, already report the
same Markdown totals (42 passes, 15 partial, one failure). The failing HTML
contains no form controls. Isolated Markdown reruns reproduce the failure.

## Empty artwork and scroll paint correction (2026-10-13)

Blade's empty presets inherited the shared Ant-style inbox illustration.
The Blade lowering hook now validates the existing shared contract, then
substitutes original SVG chest artwork for `default` and `simple` only.
Image height, custom images, explicit absence and decorative semantics remain
unchanged. This is source presentation under **D7.2.4** and **S12.1.3**.

A release-build wheel replay of the component forge reproduced about half a
second per frame. `render.stats` records and macOS CPU sampling identified
`render_box_shadow_inset` calling the shared software Gaussian convolution as
the dominant recording cost. The blur now traverses rows/columns as runs:
when the complete kernel lies within a flat run and the sample region, one
convolution supplies all identical results. Boundaries still use the original
kernel and edge mapping. Masks with four identical channels accumulate once
and replicate that value before the original packing/rounding step. There
is no theme-specific renderer path, shadow reduction, new cache or vendor edit.

Three paired release replays (1280×800, 24 moving wheel events each) measured
median frame times of 487.8/404.1/382.9 ms before and 111.3/112.5/112.5 ms after:
3.4–4.4× faster. Top/middle/bottom captures have identical PNG bytes across
the old/new renderer with the same current source artwork. Raw timings,
event logs, CPU sample and captures are under `temp/ui_blade/scroll/`.

`make test-ui-blade ARGS='--jobs 2'` passes all four source goldens and all
six native fixtures in interpreter, auto and JIT modes. The source composition
fixture now checks 23 cases, and the component fixture checks nine. The
Gaussian unit passes in the 142-case Radiant vector suite; the Radiant
dimension lint and `git diff --check` also pass.

The full Radiant baseline rerun with graphics access and four workers passes
the 569 UI fixtures, 51 page snapshots, 142 vector cases and render baseline.
Its remaining failures are the previously recorded `md_nuxt-readme` Markdown
fidelity mismatch and one CellScope Scene3D interaction case; the CellScope
fixture files were changing in the shared checkout during this run. Neither
failure concerns Blade artwork or the software Gaussian path.

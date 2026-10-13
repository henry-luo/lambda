# Lambda UI Clay implementation

## Architecture

`lambda.ui.clay` is an ordinary source package (**D7.2.4**) with 59 qualified
component tags (**S2.4.3v3**). Its facade and catalog preserve authored Clay
descriptors. The presentation view lowers component children and nested data
slots into validated Ant-inspired descriptors, then applies their existing
views. Stateful collection views and handlers remain in their defining module;
the adapter does not copy their state machines (**S12.1.3**).

Each adapter retains its lowered descriptor in view state. Reapplying the same
immutable Clay descriptor therefore preserves the native view's source identity
and uncontrolled state across parent redraws. A direct view return uses the
runtime's existing wrapper ownership chain without adding a layout element.

The single catalog pairs each shared constructor with its static Clay tag
factory. It dispatches constructor validation for both API calls and direct
element presentation. Nested slot lowering propagates diagnostics instead of
embedding errors in items. Configuration providers remain explicit Clay
boundaries so scoped tokens inherit through their native fieldsets.
The public renderer propagates child diagnostics through arrays and page shells
(**S7.6.3v2**), so invalid components cannot become inert error children.

Native input ownership, frame synchronization and radio exclusivity remain in
the shared control layer (**D7.2.5**, **S8.2.2v5**). Application effects stay in
event procedures (**S12.1.1v2–S12.1.3**). No language semantics were changed.

## Appearance

The reference is [Clay Dashboard by Muhammad Farhan Juna](https://dribbble.com/shots/27508244-Clay-Dashboard-Claymorphism-Dashboard-UI).
The theme uses CSS surfaces, pastel colors, inset fields, gradient buttons,
large radii and multiple soft shadows. The dashboard’s illustrative chart is
ordinary SVG; no downloaded reference asset is shipped.

The canonical Ant-inspired base stylesheet is reused unchanged. All additional
appearance selectors are scoped beneath `.clay-root`; shared behavior classes
and delegation attributes retain their spelling. Tokens generate both Clay
variables and local behavior aliases. The existing scoped-variable resolver
owns derived color and heading dependencies, including their pixel units.

## Radiant shadow correction

Visual review found that `resolve_shadow_value` classified all function values
as colors. A blur or offset expressed with `calc`, `min`, `max` or `clamp` was
therefore omitted from the length sequence, producing hard shadows and shifted
slots. The resolver now recognizes the shared CSS math length type before
dispatching color functions. Box and text shadows share this correction.
The regression fixture checks multiple shadows, inset/spread, variable
substitution, math in offsets and blur, and functional RGB colors against
the [CSS Backgrounds §6.1 grammar](https://www.w3.org/TR/css-backgrounds-3/#box-shadow).

## Retained collection ownership correction

The retained Tabs fixture exposed a shared runtime defect. An external panel
array copied into the result Input also cloned template results that already
belonged to that Input. Those new addresses had no native handler binding.
UI content copying now reuses owned descendants while adopting external values
(**D4.5.2**); the separate DOM occurrence copy still creates distinct nodes
(**D4.5.3**). The MarkBuilder regression checks both ownership boundaries, and
the native fixture clicks a button after activating its retained panel.

## Validation

Source contracts live in `test/lambda/ui_clay/`, each with a checked-in golden.
Native checks live in `test/ui/clay/`, plus the dashboard and component gallery.
`make test-ui-clay` runs interpreter, automatic and MIR JIT paths
(**D8.1.1v17**). `RadiantTextLayout.ShadowMathKeepsLengthsDistinctFromColors`
checks the underlying shadow values directly.

Validated on 2026-10-13:

- `make test-ui-clay ARGS='--jobs 2'`: four source goldens (56 assertions) and
  five native fixtures (59 assertions) pass on interpreter, automatic and JIT.
- `make test-lambda-baseline`: 6,809/6,809 runtime and input tests pass.
- MarkBuilder deep-copy tests: 42/42; Radiant text/shadow tests: 3/3.
- The Radiant gate passes all 563 UI automation cases, 156 DOM UI cases,
  171 native view cases, and 514 view-reuse cases. Its remaining failure is
  the Markdown layout baseline (one regression); known partials
  and the existing skipped case remain in that gate.
- The Radiant integer-cast lint rule and `git diff --check` pass.

The implementation inherits the current shared Ant-inspired feature limits;
it does not introduce placeholder widgets for unsupported catalog families.

Visual review covered native PNG captures and a Chromium HTML export. The
native dashboard currently exposes two sizing limitations: its Space/Avatar
branding can overlap, and a Card containing only a Table can clip lower rows.
A plain HTML reduction reproduces the first when a nested inline-flex child's
width uses `var()`; a literal width passes. These remain Radiant layout issues;
the theme does not add compensating widths or heights.

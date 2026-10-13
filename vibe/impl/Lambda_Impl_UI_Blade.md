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
membership (**S8.2.2v6**). No language semantics or C++ engine code changed.

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
  theme change.

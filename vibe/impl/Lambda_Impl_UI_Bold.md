# Lambda UI bold — initial implementation

> **Date:** 2026-10-10.
> **Status:** initial native subset implemented; native dependency failures remain open.
> **User-selected package/theme name:** `lambda.ui.bold` / `bold`.
> **Authority:** **D7.2.4**, **D7.2.5**, **D8.1.1v17**,
> **S2.4.3v3**, **S8.2.2v5**, **S12.1.1v2–S12.1.3**. No language or engine ruling changes.

## Scope and reference

The package follows the requested
[Neobrutalism reference](https://neobrutalism.com/), consulted 2026-10-10.
The official [Button](https://neobrutalism.com/docs/components/button) and
[Card](https://neobrutalism.com/docs/components/card) pages informed the thick
borders, hard offset shadows, flat colors and pressed states. This is an
idiomatic Lambda implementation; no React, Tailwind or upstream runtime is
introduced. A pinned pixel-equivalence target and full upstream catalog are
not claimed.

The initial surface includes 21 component constructors plus the page shell:
Button; six native control kinds; four typography kinds; Card, Badge, Alert,
Progress; Flex, Space, Divider; Form and Form Item; Config Provider. Helpers
expose resolved tokens, scoped CSS and explicit rendering. Unknown features
are diagnosed, rather than represented by placeholder exports.

## Sources and ownership

| Source | Responsibility |
|---|---|
| `lmd/package/ui/bold.ls` | Public facade, page shell and imported view registration (**D7.2.4**) |
| `ui/bold/contract.ls` | Shared validation for constructors and direct logical attributes |
| `ui/bold/tokens.ls` | Immutable defaults, validated overrides, full/scoped `--bold-` variables |
| `ui/bold/style.ls` | Independent family selectors, native states, responsive spacing and hard shadows |
| `ui/bold/presentation.ls` | Button, form, layout, typography, surfaces, feedback and configuration views |
| `ui/bold/input.ls` | Native-control view/event bindings using shared behavior |
| `ui/bold/elements.ls` | Static qualified component tags and constructor registry (**S2.4.3v3**) |
| `ui/core/control.ls` | Extracted native markup, typed changes and controlled settlement used by dtna and bold |
| `ui/core/component.ls` | Shared naming, diagnostics, select-value validation, CSS colors and heading emission |
| `ui/core/element.ls` | Shared invocation of family tag registries |

Application data stays in logical qualified elements such as
`<bold.button id:"save","Save">`. Constructors return that same shape;
imported templates present HTML/SVG (**S2.4.3v3**, **S12.1.3**). Tag suffixes
use underscores for multiple words, while shared kind normalization retains
the internal hyphenated names. Shared normalization also accepts legacy
kind/props descriptors for the native-control bindings. Configuration uses
inherited CSS variables and a native fieldset; no global mutable theme or
ambient context is introduced.

The native controls share one implementation rather than copying dtna's
procedures. Frame event names are family-specific; native radio groups use
the shared `data-ui-controlled-checked` marker so controlled peers in either
family can settle. DOM remains the sole owner of default actions
(**D7.2.5**). Present empty/false controlled values are preserved by key
membership (**S8.2.2v5**).

## Verification and remaining work

Four new golden fixtures cover default/custom tokens and invalid CSS values;
native HTML semantics and content slots; invalid props and diagnostics; direct
elements, source preservation and nested/family coexistence. They are registered
in the existing auto-discovery harness. An isolated source snapshot of the
staged package and existing dtna API passed all **30** combined bold/dtna
golden fixtures on each of `interp`, `auto` and `jit` (**D8.1.1v17**).

`test/ui/bold_gallery.ls` is a direct-element component explorer. Its native
fixture checks 11 style/content/control assertions, including inherited pink
accents and a nested square corner. `test/ui/bold/controls.ls` exercises native
editing across parent updates, controlled rejection/acceptance, checkbox and
switch state, radio exclusivity, mixed-family controlled radio peers, typed
select requests, form entries and reset.

The isolated snapshot's native sweep selected **54** dtna/bold fixtures on
each tier: **52 passed, 2 failed**. `bold_controls` retains one selection assertion
failure (**20/21**); the existing `dtna_segmented_modes` fixture retains its
known repeated-native-node failure (**41/42**). See central ledger
[UI-2](../Lambda_Issue_Ledger.md#ui-2) and
[UI-1](../Lambda_Issue_Ledger.md#ui-1). Captured gallery PNGs also expose
[UI-3](../Lambda_Issue_Ledger.md#ui-3), a native overflow clipping defect.
These are open dependencies, not passing checks. No runtime/layout workarounds
are introduced inside the package. Logs and captures are under `temp/ui_bold/`.

`make test-ui-bold` runs the package goldens and native gallery/control fixtures
across execution tiers, returning failure while UI-2 remains open. Browser
hydration, dark presets, motion timing, advanced collections and the remaining
reference catalog are unimplemented. The sibling dtna roadmap remains intact.

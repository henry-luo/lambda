# Shared editor coordination and control bindings

Verified against the tree: 2026-10-16.

`lambda.edit.controller` coordinates a model editor with its native surface.
`lambda.ui.controls` provides style-neutral native controls with declarative
change actions. The Word demo and the general edit shell use the controller;
the style-neutral, dtna, bold, and wired controls share synchronization policy.

Template state remains the application-owned source of truth. Projection is
pure, and handlers assign the next state (**S12.1.3**, **S1.4**). Editing policy
uses the existing Lambda editor model and DOM mechanisms (**D7.2.5**).

## Editor controller

```lambda
import controller: lambda.edit.controller
```

| Operation | Colour | Behavior |
|---|---|---|
| `surface(node, id="edit-surface")` | `fn` | Find the configured native surface. |
| `mount(editor, node, preset="html5_subset", id="edit-surface")` | `pn` | Bind an existing surface once; an absent surface leaves the editor unmounted. A null preset disables native binding. |
| `adopt(editor, event, preset, id)` | `pn` | Return `{editor, changed}` after validating and adopting a document-selection snapshot. The optional preset and ID have the same defaults as `mount`. |
| `handle(editor, event, read_only=false, preset, id)` | `pn` | Mount and route native editing input through the existing editor adapter. Read-only input is declined; copy claims a no-op without changing history or state. |
| `request(editor, input_type, payload={})` | `pn` | Execute a toolbar request and publish its structured completion to a bound surface. |
| `replace(previous, next)` | `fn` | Preserve the logical surface handle and native-selection revision while advancing the model revision for a replacement document. |
| `restore(editor, enabled=true)` | `pn` | Publish the model selection through the completion bridge; callers disable it while a dialog owns focus or the surface is read-only. |
| `focus(node, id="edit-surface")` | `pn` | Explicitly return focus to the document. |
| `is_control(node)` | `fn` | Recognize an input, textarea, select, or its descendant. |
| `selection_snapshot(event, from_surface, from_control, document_selection=null)` | `fn` | Select eligible event position/selection data without reading native DOM state. |
| `accept_selection(editor, snapshot)` | `fn` | Apply the snapshot to the editor value and report whether it changed. |

`surface` and `is_control` only query the DOM and are `fn`, following native
getters and queries under **D7.4.6**. Mounting, selection publication and focus
remain `pn`; callers obey each wrapper's published colour (**S12.1.1v3**).
See [DOM reads and procedural calls](../Lambda_Func.md#dom-reads-and-procedural-calls).

Applications retain command definitions, file policy, dialogs, and surface
projection. Their handlers assign the returned editor and decide when focus
should move. The controller maintains no second document or hidden global state.

Toolbar hit positions are never document positions. A toolbar control can carry
a document-selection snapshot, but the controller verifies both live selection
boundaries belong to the configured surface before accepting it. Private
form-control selections remain separate. The pure `selection_snapshot` and
`accept_selection` helpers preserve the editor adapter's stale-revision and
model-completion-echo checks (**D7.2.5**, **D7.5.3**).

## Controlled inputs

```lambda
import controls: lambda.ui.controls

view <search_panel> state query:"" {
    <div apply(<control.input id:"query", value:query,
        placeholder:"Find", on_change:"find.query">)>
}
on control_change(req) {
    if (req.action == "find.query") { query = req.value }
}
```

The package also exports `input(props)`, `text_area(props)`, `select(props)`,
`checkbox(props, child)`, `radio(props, child)`, and `switch(props)` constructors.
Constructors return `element^`; direct `control.*` elements receive the same
validation when projected. Apply the descriptor to render it.

`value` and `checked` declare application-controlled state. `default_value` and
`default_checked` initialize native-owned state; each pair is mutually exclusive.
Controls reuse the native property contracts in `lambda.ui.core.contract`.
The `on_change` action is also supported by dtna, bold, and wired constructors.

An action can be a string or a descriptor:

```lambda
<control.select id:"font-size", value:18,
    options:[{value:11,label:"11"},{value:18,label:"18"}],
    on_change:{action:"format.size", param:"size", params:{source:"ribbon"}}>
```

This emits `control_change` with `id`, `action`, `value`, `params`, `node`, and
the originating `event`. Here `value` and `params.size` are the integer `18`.
Select and radio values retain their authored types; checkbox/switch changes
carry Booleans. Select options accept strings, symbols, Booleans, and finite
numbers, including fractional line spacings; serialized values must be unique.
The default parameter name is `value`. The latest value replaces
any same-named entry in the descriptor's fixed `params` map. Action descriptors
reject unknown fields and invalid action, parameter, or parameter-map types.

The parent accepts, transforms, or rejects the request by updating its own state.
Synchronization runs after projection and after a native edit, so programmatic
updates and rejected edits settle without application frame callbacks. It reads
live native properties, writes only mismatches, and emits no change action while
synchronizing. Accepted text edits preserve their native caret and selection.
Active IME preedit is left intact; only the committed input requests a change.
Uncontrolled controls retain native edits across parent updates.

## Verification

Model and contract goldens are `test/lambda/edit_controller.ls`,
`test/lambda/ui_controls.ls`, and `test/lambda/proc/edit_controller_gate.ls`.
The baseline UI manifest discovers `test/ui/controlled_bindings.json` automatically.
It covers caret retention, IME through a parent redraw, typed actions, checkbox
and radio synchronization, uncontrolled fields, and rejected changes.

`python3 test/demo/word/run_tests.py` includes the Word binding regression and
the existing typing, history, table, scroll, file, layout, and review workflows.
The general edit-shell fixtures and each styled family's control fixture verify
the other consumers of the shared packages.

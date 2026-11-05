# Lambda Workspace UI

`lambda.ui.workspace` provides an initial set of desktop application controls
and chrome. It builds on the shared `lambda.ui.core` behavior and has its own
desktop tokens and styles. Its API is experimental and currently covers a
small subset of the planned workspace components.

```lambda
import ui: lambda.ui.workspace

ui.page(ui.toolbar({label:"Document"}, [
    ui.button({id:"save", variant:"primary"}, "Save")^,
    ui.input({id:"search", placeholder:"Search"})^
])^, {title:"Workspace"})^
```

The facade exposes `page`, `render`, `tokens`, `stylesheet`, `button`, `input`,
`text_area`, `select`, `checkbox`, `radio`, `switch`, `toolbar`, `panes`,
`sidebar`, `content_area`, `status` and `modal`. Constructors accept a property map;
content-bearing constructors also accept a child or array of children.
Logical elements such as `<workspace.button>` can also be used with `apply`.
Controls use the same `on_change` request and controlled-value policy as
`lambda.ui.controls`; applications own the accepted state and handle requests
in their templates (S12.1.3v2, D10.2.1).

`page(root, options)` supplies a full document and accepts `title`, `locale`,
`direction` and `tokens`. The `tokens` map overrides the defaults returned by
`tokens()`: primary, text, secondary text, border, background, surface,
disabled colors, font family/size, control height, radius, spacing and line
height. Component selectors and CSS variables are scoped to the workspace root;
document content inherits ordinary typography and can supply its own styles.
Mark an authored-content subtree with `class:"workspace-document"` to keep
workspace, office, ribbon and font-picker selectors from matching its elements.
Word applies this boundary to its editing and preview surfaces.

Existing applications that own their native document can use
`lambda.ui.workspace.shell` without changing their event or state ownership.
Put `shell.variables(overrides)` in the shell's inline `style`, add the
`workspace-shell` class, and include `shell.css` in the document stylesheet.
Mark toolbars or other application chrome with `workspace-chrome` to give
their controls the shared keyboard focus treatment. Content under
`workspace-document` keeps its own focus styles. Mail, Note, Finder, Photo
Album, Book, Archive, Draw, Sketch and Chat use this integration with their
own accents and application layouts (D9.3.4, D10.2.1; S12.1.3v2). Book's
separately owned ebook reader keeps its own document and styles.

Menu and splitter behavior is available through `lambda.ui.core.menu` and
`lambda.ui.core.splitter`. `lambda.ui.core.focus.trap(dialog, event)` cycles
through the dialog's eligible focus targets in native Tab order, including
Shift+Tab; the caller handles opening, dismissal and restoring the prior focus.
`lambda.ui.core.focus.dialog_key(dialog, event)` combines that traversal with
an Escape dismissal request, returning `handled`, `dismiss` or `pass`.
`modal({id, label}, children)` produces a named dialog and backdrop; pass
`labelled_by` instead of `label` when its heading supplies the name. Optional
`class` and `backdrop_class` let an app style its panel and backdrop. Sheet and
Note use this composition with their own dialog commands and focus lifecycle.
Chat, Draw and Sketch also use the core focus policy for their native dialogs.
Existing office demos share ribbon layout and icons
through `lambda.ui.workspace.ribbon`, `.ribbon_overflow`, `.ribbon_icons` and
`.icons`. The office chrome stylesheet in `lambda.ui.workspace.office` is
scoped to `.workspace-office`; its `document_css` reset is an explicit option
for standalone application documents. Word and Sheet use workspace token
variables with their own accents. A complete desktop widget catalog and
automatic restyling of all demos remain future work.

`lambda.ui.workspace.font_picker` supplies the shared searchable, windowed
font picker. Mount `widget(host_id, picker_id)` once for each picker and pass
the same `picker_id` to `wrap(control, control_id, current, disabled, picker_id)`
or to the lower-level `field` and `show` functions. IDs must be unique within
the document; the control IDs must also be unique. The default picker ID is
`word-font` for existing Word and Slides integrations. A host supplies its own
select options and handles the resulting native `change` event.

`lambda.ui.workspace.file_browser` supplies file-list ordering, selection and
navigation history as values. Applications provide the entries and retain
their own filtering, commands and navigation policy; `lambda.io.file_info`
provides a filesystem directory source and entry metadata. Finder extends the
shared state with column and archive behavior, while Word uses it in its file
picker. `lambda.ui.workspace.file_icons` provides their common artwork.

The package's model checks are in `test/lambda/ui_workspace.ls`; native
controlled-input integration and independent workspace instances are covered
by `test/ui/controlled_bindings.json`, `test/ui/workspace_instances.json` and
`test/ui/workspace_font_instances.json`. `test/ui/workspace_shell.json` checks
independent shell tokens, keyboard focus, and authored-content isolation.
File-browser state is checked in
`test/lambda/ui_workspace_files.ls`. Dialog and menu ownership checks live
in `test/ui/workspace_dialog_focus.json` and `test/ui/workspace_menu_owners.json`.
`make check-ui-boundaries` checks the neutral and workspace import closures;
`make check-package-structure` checks shipped script names and package imports.

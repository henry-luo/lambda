# Lambda UI — dtna

`lambda.ui.dtna` is an experimental Ant Design-inspired package for native
Radiant documents. Its ordinary shipped module is
`lmd/package/ui/dtna.ls` (**D7.2.4**). Interactive browser distribution and
additional style families are deferred.

The initial implementation covers portions of 34 of the 73 AntD 6.6.5 catalog
entries. No entry is certified complete. The [feature inventory](../test/ui/dtna_reference/catalog.manifest)
records implemented behavior and remaining features; the
[proposal](../vibe/Lambda_Pkg_UI.md) retains the full Phase 1 scope.

## Getting started

```lambda
import ui: lambda.ui.dtna

ui.page(ui.card({title:"Project"}, [
    ui.form_item({label:"Name",for:"project-name"},
        ui.input({id:"project-name",name:"project",placeholder:"Project name"})^)^,
    ui.space({}, [
        ui.button({id:"save",variant:'primary'},"Save")^,
        ui.button({disabled:true},"Archive")^
    ])^
])^, {title:"My project"})^
```

Save this as a `.ls` document and open it with `./lambda.exe view path/to/page.ls`.
The shipped explorer is `./lambda.exe view test/ui/dtna_gallery.ls`.
Set `LAMBDA_HOME` to the absolute `lmd` path when launching outside the checkout.

Constructors return logical `<dtna kind:...>` elements and use explicit error
propagation (`^`) for invalid props. `ui.render(source)` applies the imported
view templates; `ui.page(source, options)` adds HTML structure, scoped styles,
and root tokens. Ordinary HTML/SVG elements and arrays are valid child slots.
Inside ordinary HTML, apply `ui.render` to logical component children explicitly,
as the event example below does.
Rendering is functional; DOM access and event effects belong in procedures
(**S12.1.1v2–S12.1.3**).

## Current surface

Each constructor accepts a props map and, where applicable, a child argument.
Unknown props raise a package error. This is an idiomatic Lambda API; React
props and AntD subcomponent APIs are not interchangeable with it.

| Area | Exports |
|---|---|
| General | `button`, `icon`, `title`, `text`, `paragraph`, `link` |
| Layout | `space`, `space_compact`, `flex`, `divider`, `row`, `col`, `layout`, `layout_header`, `layout_content`, `layout_footer`, `layout_sider` |
| Data entry | `input`, `text_area`, `password`, `search`, `select`, `checkbox`, `radio`, `switch` |
| Forms | `form`, `form_item` |
| Navigation | `tabs`, `segmented`, `menu`, `pagination`, `breadcrumb`, `steps` |
| Display | `avatar`, `badge`, `card`, `empty`, `statistic`, `progress`, `timeline`, `descriptions` |
| Feedback | `alert`, `tag`, `skeleton`, `spin`, `result` |
| Configuration | `config_provider`, `tokens`, `palette`, `stylesheet`, `render`, `page` |

Common props are `id`, `class`, `style`, `label`, `title`, `role`, `tabindex`,
plus `aria-*` and `data-*` attributes. Some props have a component-specific
meaning: `title` is a Card heading or Result title; `label` names a native
control or Form Item. Size accepts `small`, `middle`, or `large` on supported
controls. Feedback status accepts `success`, `warning`, `error`, or `info`.

Button supports `variant` (`primary`, `default`, `dashed`, `text`, `link`),
`size`, `danger`, `shape`, `block`, `icon`, `loading`, `disabled`, and native
`type` (`button`, `submit`, `reset`). Loading disables activation.

Input and Text Area support native `name`, `value`/`default_value`, `required`,
`readonly`, `maxlength`, and `placeholder`; Input also accepts `type` and
`autocomplete`, and Text Area accepts `rows`. Select accepts
`options:[{value:10,label:"Low"},{value:20,label:"High",disabled:true}]`.
Option values may be strings, symbols, integers, or booleans, and must have
distinct string representations. Select change actions preserve the supplied
value type. Checkbox, Radio, and Switch use `checked`/`default_checked`.
Radio groups use the native shared `name` attribute.

Tabs, Segmented, and flat Menu require a nonempty `id` and unique item keys:
`{id:"sections",items:[{key:'home',label:"Home",children:"Home panel"}]}`.
Arrow keys and Home/End skip disabled items and update focus. Tabs mounts only
the active panel. Pagination accepts `total`, `page_size`,
`current`/`default_current`, and emits a page record.

## Events and state

Components emit `ui_change` or `ui_action` to their enclosing author view.
The envelope is `{component, id, action, value}`. For example:

```lambda
import ui: lambda.ui.dtna

// Retain a source item so parent updates reuse its component instance.
let save = ui.button({id:"save",variant:'primary'},"Save")^
view project: <project> state saves:0 {
    <div *[ui.render(save), <p "Saves: " ++ string(saves)>]>
}
on ui_action(action) {
    if (action.id == "save") { saves = saves + 1 }
}
ui.page(<project>)^
```

| Component | Event | Action and value |
|---|---|---|
| Button | `ui_action` | `'click'`, null |
| Input / Text Area | `ui_change` | `'input'`, native text |
| Input committed change | `ui_action` | `'change'`, native text |
| Select / Radio / choices | `ui_change` | `'select'`, typed value/key |
| Checkbox / Switch | `ui_change` | `'check'`, boolean |
| Pagination | `ui_change` | `'page'`, `{current,page_size}` |
| Form | `ui_action` | `'submit'`, native successful-control entries; `'reset'`, null |
| Closable Alert / Tag | `ui_action` | `'close'`, null |

Form submission runs native constraint validation before emitting the action.
The package prevents native navigation after submission. It does not yet
provide an asynchronous validation schema or a Form field store.

Native text controls own edits, selection and undo history. Retained control
sources preserve edits when an unrelated parent updates. A changed authored
value is applied, but an unchanged `value` is not reapplied to undo user edits;
full controlled-text semantics remain open. Avoid treating it as React's
controlled input contract. Do not supply both `value` and `default_value`, or
both `checked` and `default_checked`.

View state follows source-item and template identity. HTML `id` is not a
general keyed reconciliation API. Reconstructing or reordering descriptors,
or unmounting a tab panel, can reset instance/control state. Keep reusable
sources in bindings; general collection identity remains an M0 gate.

## Tokens and configuration

`ui.tokens(overrides)` returns the resolved default-light token map.
`ui.palette("#1677ff")` returns the ten reference blue shades. Color inputs
currently require six-digit hex strings. Numeric token values must be finite
and positive; radius and spacing may be zero.

The supported seed names are `primary`, `success`, `warning`, `error`,
`text`, `text_secondary`, `border`, `background`, `surface`, `font_family`,
`disabled_background`, `disabled_text`, `font_size`, `line_height`,
`control_height`, `radius`, and `spacing`.
The source of truth is `lmd/package/ui/dtna/tokens.ls`.

```lambda
import ui: lambda.ui.dtna

ui.config_provider({tokens:{primary:"#722ed1"}},
    ui.space({}, [
        ui.button({variant:'primary'},"Purple")^,
        ui.config_provider({tokens:{radius:0}},
            ui.button({variant:'primary'},"Inherited purple, square")^)^
    ])^
)^
```

Nested token overrides inherit unmentioned CSS variables. Providers support
`direction` and native fieldset `disabled` inheritance. They do not yet supply
ambient locale, component size, or detached-overlay services. Page `locale`
sets HTML `lang`; it does not translate component strings. `page` accepts
`title`, `tokens`, `locale`, and `direction` options.

## Validation and remaining scope

```bash
make test-ui-dtna ARGS='--jobs 1'
make test-lambda-baseline
make test-radiant-baseline
make test262-baseline
```

The focused target checks the catalog inventory, package goldens on all three
execution tiers (**D8.1.1v17**), native-state regressions, and real native
pointer/type/keyboard fixtures on forced interpreter and JIT paths. The shared
Radiant baseline includes `dtna`.
The native gallery capture is an inspection artifact; reference-app pixel
comparison, accessibility auditing, and performance measurement remain open.

Tables, trees, pickers, uploads, overlays, virtualization, motion, localization,
and other catalog features remain in Phase 1. See the
[implementation record](../vibe/impl/Lambda_Impl_UI_Dtna.md) for exact gates and
the catalog for per-component limitations. Static rendering uses the normal
Radiant export path; exported snapshots do not carry live Lambda handlers.

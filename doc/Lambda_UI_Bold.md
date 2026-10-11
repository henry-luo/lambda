# Lambda UI — bold

`lambda.ui.bold` is an experimental neobrutalist UI package for native Radiant
documents. Its appearance takes inspiration from
[Neobrutalism](https://neobrutalism.com/): flat yellow and lavender accents,
strong ink borders, hard offset shadows, bold typography and pressed buttons.
The source entry point is `lmd/package/ui/bold.ls` (**D7.2.4**).

## Getting started

```lambda
import ui: lambda.ui.bold

apply(<bold.page title:"My project",
    <bold.card title:"Make something bold.",
        <bold.form_item label:"Project",for:"project-name",
            <bold.input id:"project-name",placeholder:"Your next big idea">>
        <bold.button id:"save","Start building →">>>)
```

Open your `.ls` document with `./lambda.exe view path/to/page.ls`. The component
explorer is `./lambda.exe view test/ui/bold_gallery.ls`.

Logical `<bold.button>`, `<bold.card>` and other qualified elements retain
application data while imported view templates present ordinary HTML
(**S2.4.3v3**, **S12.1.3**). Author props as direct attributes. Component names
with multiple words use underscores, such as `<bold.text_area>` and
`<bold.config_provider>`. The optional constructor helpers accept a props map
and return the same logical elements;
`ui.render(source)` presents them and `ui.page(source, options)` adds the full
document shell. Constructors and recognized direct components share validation.
Unknown props, invalid enums and conflicting ownership props raise an error
prefixed with `bold: <component>:`. Explicit `ui.render(source)` also rejects
unimplemented bold roots.

## Current components

| Area | Constructors |
|---|---|
| Actions | `button` |
| Native controls | `input`, `text_area`, `select`, `checkbox`, `radio`, `switch` |
| Typography | `title`, `text`, `paragraph`, `link` |
| Content and feedback | `card`, `badge`, `alert`, `progress` |
| Layout and forms | `flex`, `space`, `divider`, `form`, `form_item` |
| Configuration | `config_provider`, `tokens`, `stylesheet`, `render`, `page` |

Constructors take a props map and, where applicable, a child value. Child slots
accept ordinary HTML/SVG, logical components, arrays and text. Common props are
`id`, `class`, `style`, `label`, `title`, `role`, `tabindex`, `aria-*` and `data-*`.
For example, `ui.button({variant:'secondary'}, "Save")^` constructs a button.

- **Button:** `variant` is `default`, `secondary`, `outline`, `destructive`,
  `ghost` or `link`; `size` is `small`, `middle` or `large`. `disabled` and
  `block` are booleans. `type` is `button`, `submit` or `reset`. `href`, `target`
  and `rel` produce an anchor; disabled anchors omit `href` and leave the tab
  sequence. External `_blank` links default to `noopener noreferrer`.
- **Input and Text Area:** `value` or `default_value`, `name`, `placeholder`,
  `disabled`, `readonly`, `required`, `maxlength`, `size` and `status`. Input
  also accepts `type` (`text`, `password`, `search`, `email`, `url`, `tel`) and
  `autocomplete`; Text Area accepts positive integer `rows`.
- **Select:** `options` is an array of `{value,label,disabled?}` maps.
  Values may be strings, symbols, integers or booleans, with distinct string
  representations. `value` or `default_value` selects the initial option;
  `name`, `required`, `disabled`, `size` and `status` are supported.
- **Checkbox and Radio:** `checked` or `default_checked`, `name`, `value` and
  `disabled`. Switch accepts `checked`, `default_checked` and `disabled`;
  `label` supplies its accessible name.
- **Typography:** Title accepts integer `level` from 1 to 6. Link accepts
  `href`, `target` and `rel`.
- **Card:** `title`, `description` and `footer` are rendered slots; children
  form the body. Badge supports `default`, `secondary`, `outline` and
  `destructive` variants. Alert uses `title` and child content; warning/error
  status uses an alert role, while other statuses use a status role.
- **Progress:** `percent` is a finite number in `[0,100]`; `show_info` controls
  the percentage label. `label` supplies an accessible name. `status` is
  `success`, `warning`, `error` or `info`, also supported by native fields,
  Alert and Form Item.
- **Flex and Space:** `direction` is `horizontal` or `vertical`; `gap` is a
  nonnegative number of pixels or a single CSS length. `wrap` is boolean.
  `align` accepts `stretch`, `flex-start`, `center`, `flex-end`, `baseline`;
  `justify` accepts `flex-start`, `center`, `flex-end`, `space-between`,
  `space-around`, `space-evenly`. Both lay out child slots directly.
- **Form Item:** `label`, `for`, `required`, `help` and `status` provide native
  label association, a required marker and help text. Set `required` on the
  child control to enable native validation.

## Tokens and scope

Page options are `title`, `locale`, `direction` (`ltr` or `rtl`) and `tokens`.
Config Provider accepts `tokens`, `direction` and `disabled`; it renders a native
fieldset. Nested providers emit supplied token overrides only, so other values
inherit from the enclosing scope. Disabled fieldsets retain native legend
exemptions (**D7.2.5**).

```lambda
import ui: lambda.ui.bold

apply(<bold.page tokens:{primary:"#fb7185"},
    <bold.config_provider tokens:{radius:0,shadow_offset:6},
        <bold.button "Pink, square, and bold">>>)
```

`ui.tokens(overrides)^` returns an immutable resolved map. Colors use `#rrggbb`.
Color keys are `primary`, `secondary`, `accent`, `success`, `warning`, `error`,
`info`, `text`, `text_secondary`, `border`, `shadow`, `background`, `surface`,
`disabled_background` and `disabled_text`. Dimensions are `font_size`,
`control_height`, `radius`, `border_width`, `shadow_offset` and `spacing`, in
pixels, plus unitless `line_height`. `font_family` is a CSS font-family string.
Dimensions must be finite and positive; radius, shadow offset and spacing may
be zero. Selectors and variables use the independent `bold-` / `--bold-`
namespace. `ui.stylesheet()` returns the family CSS for custom document shells.

## Events and native behavior

Rendering is pure. Procedures handle effects (**S12.1.1v2–S12.1.3**), while the
shipped DOM package owns native editing, validation, radio exclusivity and
activation (**D7.2.5**). Native control behavior is shared with `dtna`.

- Buttons emit `ui_action` with `{component:"button",id,action:'click',value:null}`.
- Fields emit `ui_change` with `input`, `check` or `select` actions. Select and
  Radio retain the original typed value; labels are presentation.
- Form emits `ui_action` with `submit` or `reset`. Submission values are native
  `[name,value]` pairs. The submit handler prevents document navigation.
- A present `value` or `checked` prop makes a control application-owned, even
  when its value is empty or false (**S8.2.2v5**). Its `ui_change` event is a
  request; the application must update the model to accept it. These props
  cannot be combined with their `default_*` equivalents.

The executable [control fixture](../test/ui/bold/controls.ls) demonstrates
application handlers, retained edits, typed selection, submission and reset.

## Current limits and verification

This is an initial native subset, not the complete reference catalog. Dialogs,
menus, tabs, pickers and advanced collections are not exported. Browser
hydration, dark presets, animation timing and pixel correspondence with the
reference site are not implemented.

The four golden fixtures in `test/lambda/ui_bold/` cover tokens, constructors,
diagnostics, direct elements, nesting and coexistence with `dtna`.
`make test-ui-bold` runs them and the native fixtures on each execution tier
(**D8.1.1v17**). The native control fixture currently exposes an existing
[select-retention defect](../vibe/Lambda_Issue_Ledger.md#ui-2): an uncontrolled
selection reverts when its parent presents again. The gallery also exposes
[stale native overflow clipping](../vibe/Lambda_Issue_Ledger.md#ui-3) after flex
sizing. These failures remain visible; no package-specific workaround is used.

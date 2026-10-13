# Lambda UI · wired

`lambda.ui.wired` is an experimental native UI theme inspired by
[Wired Elements](https://wiredjs.com/): paper surfaces, handwritten font
fallbacks, irregular double pencil strokes, and hatched progress fills.
It is an ordinary shipped Lambda source package (**D7.2.4**).

```lambda
import ui: lambda.ui.wired

apply(<wired.page title:"My notebook",
    <wired.card title:"An idea worth exploring.",elevation:2,
        <wired.form_item label:"Working title",for:"project",
            <wired.input id:"project",placeholder:"Start here">>
        <wired.button variant:'primary',"Make a first sketch →">>>)
```

Open the complete gallery with:

```bash
./lambda.exe view test/ui/wired_gallery.ls
```

Qualified elements retain source identity (**S2.4.3v3**). Imported views produce
native HTML; handlers own effects (**S12.1.1v2–S12.1.3**). Constructors accept a
props map and optional children: `ui.button({variant:'primary'},"Save")^`.
Use underscores for multiword tags/functions: `<wired.text_area>`,
`ui.form_item(...)`.

Rendering helpers are `ui.render(root)`, `ui.page(root,options)^`,
`ui.tokens(overrides)^`, and `ui.stylesheet()`. Application views returning
logical components should call `ui.render(...)^` to present them as HTML.

| Component | Supported props and behavior |
| --- | --- |
| `button` | `variant`: `default`, `primary`, `filled`, `text`; `size`: `small`, `middle`, `large`; `disabled`, `block`; native `type`: `button`, `submit`, `reset`; anchor `href`, `target`, `rel`; `elevation`, `seed` |
| `input`, `text_area` | `value` or `default_value`, `name`, `placeholder`, `disabled`, `readonly`, `required`, `maxlength`, `size`, `status`, `seed`; input `type`: `text`, `password`, `search`, `email`, `url`, `tel`, plus `autocomplete`; textarea positive integer `rows` |
| `select` | `options` array of `{value,label,disabled?}`, `value` or `default_value`, `name`, `required`, `disabled`, `size`, `status`, `seed` |
| `checkbox`, `radio` | `checked` or `default_checked`, `name`, `value`, `disabled`; children provide the label |
| `switch` | `checked` or `default_checked`, `disabled`, accessible `label` |
| `title`, `text`, `paragraph`, `link` | Title `level` from 1–6; link `href`, `target`, `rel` |
| `card` | `title`, `description`, `footer`, `elevation`, `seed`; children form the body |
| `badge` | `variant`: `default`, `filled`, `primary`; `seed` |
| `alert` | `title`, `status`: `success`, `warning`, `error`, `info`; `seed`; warning/error use `role=alert` |
| `progress` | Finite `percent` in `[0,100]`, `show_info`, accessible `label`, `status`, `seed`; `progressbar` semantics |
| `divider` | `seed`; sketch separator with `role=separator` |
| `flex`, `space` | `direction`: `horizontal` or `vertical`; nonnegative numeric pixel or single CSS length `gap`; `align`, `justify`, `wrap` |
| `form`, `form_item` | Native submit/reset; form item `label`, `for`, `required`, `help`, `status` |
| `config_provider` | Nested `tokens`, `direction`: `ltr` or `rtl`, `disabled` fieldset |

All components accept `id`, `class`, `style`, `label`, `title`, `role`, `tabindex`,
`aria-*`, and `data-*`. Unsupported props, invalid enums, invalid dimensions,
and conflicting controlled/default props produce `wired: <component>: ...`
diagnostics. Select values may be strings, symbols, integers, or booleans, with
distinct string representations. Change requests preserve their authored types.
Flex `align` accepts `stretch`, `flex-start`, `center`, `flex-end`, `baseline`;
`justify` accepts `flex-start`, `center`, `flex-end`, `space-between`,
`space-around`, `space-evenly`.

`page` accepts `title`, `locale` (default `en-US`), `direction`, and `tokens`.
CSS selectors/custom properties use `wired-` / `--wired-` namespaces. Nested
providers emit only supplied overrides, inheriting the other tokens.

| Tokens | Values |
| --- | --- |
| `ink`, `primary`, `secondary`, `accent`, `success`, `warning`, `error`, `info`, `text`, `text_secondary`, `background`, `surface`, `disabled_background`, `disabled_text` | `#rrggbb` colors |
| `font_size`, `control_height`, `stroke_width` | Finite positive pixel numbers |
| `spacing` | Finite nonnegative pixel number |
| `line_height` | Finite positive unitless number |
| `font_family` | CSS font-family string; default `Chalkboard SE, Comic Sans MS, cursive` |

Sketch outlines use a deterministic `seed` (integer `0`–`2147483646`). An omitted
seed derives from source metadata and stays stable across updates. `elevation`
(integer `1`–`5`) adds pencil strokes on the lower/right edges of buttons/cards.
SVG strokes retain their screen width when the owner resizes. Decorations are
accessibility hidden and ignore pointer events. Field `style` applies to the
decorative wrapper; the native input keeps its `id`, attributes, focus, and
editing state. Installed fonts determine the handwritten appearance; no font
or JavaScript download is required.

Native DOM controls own editing, radio exclusivity, submission, and reset
(**D7.2.5**). `value` / `checked` presence selects application ownership,
including empty strings and `false` (**S8.2.2v5**). Handlers emit `ui_action` or
`ui_change` with `{component,id,action,value}` requests. Controlled values are
reasserted after the application handles the request. Behavior is shared with
bold and dtna, including radio groups containing more than one theme.

The initial catalog does not implement the full Wired Elements web-component
API. Sliders, calendars, tabs, dialogs, and animated spinners are not included.
The existing uncontrolled-select retention issue after parent re-presentation
remains tracked as **UI-2** in [the issue ledger](../vibe/Lambda_Issue_Ledger.md).

Run `make test-ui-wired` for source contracts, sketch geometry, native
interactions, and the gallery across interpreter, automatic, and JIT execution
(**D8.1.1v17**).

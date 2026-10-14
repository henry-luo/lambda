# Lambda UI Clay

`lambda.ui.clay` is a claymorphism theme for native Radiant documents. It uses
lavender, mint and peach surfaces, rounded corners, raised buttons, inset fields,
and layered soft shadows. The visual reference is Muhammad Farhan Juna’s
[Clay Dashboard](https://dribbble.com/shots/27508244-Clay-Dashboard-Claymorphism-Dashboard-UI).
The implementation uses CSS and SVG; it does not bundle the reference artwork.

The package is ordinary shipped Lambda source (**D7.2.4**). Authored
`<clay.*>` elements retain qualified identity (**S2.4.3v3**); presentation and
event procedures follow **S12.1.1v2–S12.1.3**. Native fields retain the control
ownership policy in **D7.2.5**, including explicit null and false ownership
through key membership (**S8.2.2v5**).

## Start here

```lambda
import ui: lambda.ui.clay

apply(<clay.page title:"My workspace",
    <clay.card title:"Welcome back",
        <clay.form_item label:"Email",for:"email",
            <clay.input id:"email",type:'email',placeholder:"you@example.com">>
        <clay.button variant:'primary',"Continue →">>>)
```

Open the dashboard or the larger interactive component collection:

```bash
./lambda.exe view test/ui/clay_gallery.ls
./lambda.exe view test/ui/clay_components.ls
```

The optional constructors return the same logical elements:
`ui.button({variant:'primary'},"Continue")^`. `ui.render(source)^` presents a
component, and `ui.page(source,options)^` adds the HTML shell, stylesheet and
tokens. Page options are `title`, `locale`, `direction` (`ltr`/`rtl`) and `tokens`.
Render logical children explicitly when placing them in ordinary HTML.
Constructors and `render` report invalid props through the raised error channel;
propagate with `^` or handle the diagnostic (**S7.4.2**, **S7.6.3v2**).

## Component surface

Clay provides **59 components and subcomponents**, plus password/search aliases
and a description-item builder. It shares the existing Ant-inspired behavior
layer, including collection state, keyboard navigation, validation, typed keys,
controlled requests, and native form handling. The full prop contracts and
`ui_change` / `ui_action` payloads are those in the [Lambda UI guide](Lambda_UI.md).
For example, Tabs require an `id` and keyed `items`; Table requires `id`, keyed
`rows`, and keyed `columns`. Alert uses `message` and `description`; Card uses
`title`, `extra`, `actions`, `cover`, and its ordinary child content.

| Area | Components |
|---|---|
| Actions and typography | `button`, `button_group`, `icon`, `title`, `text`, `paragraph`, `link` |
| Layout | `space`, `space_compact`, `flex`, `divider`, `row`, `col`, `layout`, `layout_header`, `layout_content`, `layout_footer`, `layout_sider` |
| Entry and forms | `input`, `text_area`, `select`, `checkbox`, `radio`, `switch`, `rate`, `form`, `form_item`; aliases `password`, `search` |
| Navigation | `tabs`, `segmented`, `menu`, `pagination`, `breadcrumb`, `steps` |
| Display | `avatar`, `badge`, `badge_ribbon`, `card`, `card_grid`, `card_meta`, `collapse`, `empty`, `statistic`, `progress`, `timeline`, `descriptions`, `table`, `tree` |
| Feedback | `alert`, `tag`, `checkable_tag`, `skeleton`, `skeleton_avatar`, `skeleton_button`, `skeleton_input`, `skeleton_image`, `skeleton_node`, `spin`, `result` |
| Configuration | `config_provider` |

`ui.component_names()` lists the 59 tag kinds. `ui.icon_names()` lists the shared
icon catalog. Multiword element names use underscores, such as
`<clay.form_item>`; catalog kind names use hyphens. `ui.description_item(props,child)^`
builds a Descriptions item, and `ui.palette("#6b51db")^` exposes the shared palette.

## Theme tokens

```lambda
<clay.config_provider tokens:{primary:"#287b63",radius:12},
    <clay.button variant:'primary',"Mint">
    <clay.config_provider tokens:{radius:24},
        <clay.button variant:'primary',"Inherited mint">>>
```

Nested providers emit supplied token overrides and their derived values only.
Unspecified colors and dimensions inherit from the parent. A provider is a
native fieldset, so `disabled:true` also applies native descendant disabling.

| Tokens | Defaults |
|---|---|
| `primary`, `text`, `text_secondary` | `#6b51db`, `#202335`, `#6e6888` |
| `background`, `surface`, `border` | `#f4f0f9`, `#fcfaff`, `#ded6ed` |
| `lavender`, `mint`, `peach`, `rose` | `#e3d8fa`, `#d7eee5`, `#fbe0d5`, `#f8dfe6` |
| `highlight`, `shadow` | `#ffffff`, `#c6badb` |
| `success`, `warning`, `error` | `#287b63`, `#a56624`, `#bd4561` |
| `disabled_background`, `disabled_text` | `#e9e3f1`, `#9790a7` |
| `font_family` | system sans-serif stack |
| `font_size`, `line_height` | `14`, `1.6` |
| `control_height`, `radius`, `spacing` | `42`, `16`, `12` |
| `surface_radius`, `shadow_size` | `26`, `12` |

Colors accept six-digit hex strings. Dimensions are finite numbers in pixels;
`radius`, `surface_radius`, `shadow_size`, and `spacing` may be zero.
`ui.tokens(overrides)^` resolves defaults, palette shades and heading dimensions.
CSS exposes `--clay-*` properties alongside local `--dtna-*` behavior aliases.
Styles for Clay’s appearance are scoped beneath `.clay-root`.

The rendered behavior DOM intentionally keeps shared `dtna-*` classes and data
attributes. Author `<clay.*>` tags and theme tokens; inspect native ids and ARIA
roles for application interaction. Clay and other themes can occupy separate
sibling roots in one document. Keep Clay components inside a Clay page root.

## Scope and checks

This is a broad native theme over the current Ant-inspired implementation,
covering parts of 37 Ant Design catalog families. It does **not** claim full
AntD parity. Modal, Drawer, Popover, Tooltip, Dropdown, Date/Time Picker,
Slider, Upload, Transfer, Cascader and virtualization remain outside the shared
implementation; see the [feature inventory](../test/ui/dtna_reference/catalog.manifest).
Interactive browser distribution remains deferred.

`make test-ui-clay ARGS='--jobs 2'` checks source/API contracts and native fixtures
on interpreter, automatic and JIT tiers (**D8.1.1v17**). The fixtures exercise
editing, controlled requests, typed selection, forms, retained tabs, table
sorting/selection/paging, tree checks, disclosure, ratings, tags, alerts, theme
inheritance, and the two galleries.

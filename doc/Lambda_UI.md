# Lambda UI — dtna

`lambda.ui.dtna` is an experimental Ant Design-inspired package for native
Radiant documents. Its ordinary shipped module is
`lmd/package/ui/dtna.ls` (**D7.2.4**). Interactive browser distribution and
additional style families are deferred.

The implementation covers portions of 37 of the 73 AntD 6.6.5 catalog
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
The shipped explorers are `./lambda.exe view test/ui/dtna_gallery.ls` and
`./lambda.exe view test/ui/dtna_data_gallery.ls`.
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
| General | `button`, `button_group`, `icon`, `icon_names`, `title`, `text`, `paragraph`, `link` |
| Layout | `space`, `space_compact`, `flex`, `divider`, `row`, `col`, `layout`, `layout_header`, `layout_content`, `layout_footer`, `layout_sider` |
| Data entry | `input`, `text_area`, `password`, `search`, `select`, `checkbox`, `radio`, `switch`, `rate` |
| Forms | `form`, `form_item` |
| Navigation | `tabs`, `segmented`, `menu`, `pagination`, `breadcrumb`, `steps` |
| Display | `avatar`, `badge`, `badge_ribbon`, `card`, `collapse`, `empty`, `statistic`, `progress`, `timeline`, `descriptions`, `description_item`, `table`, `tree` |
| Feedback | `alert`, `tag`, `skeleton`, `spin`, `result` |
| Configuration | `config_provider`, `tokens`, `palette`, `stylesheet`, `render`, `page` |

Common props are `id`, `class`, `style`, `label`, `title`, `role`, `tabindex`,
plus `aria-*` and `data-*` attributes. Some props have a component-specific
meaning: `title` is a Card heading or Result title; `label` names a native
control or Form Item. Size accepts `small`, `middle`, or `large` on supported
controls. Feedback status accepts `success`, `warning`, `error`, or `info`.

Button supports `variant` (`primary`, `default`, `dashed`, `text`, `link`),
`size`, `danger`, `shape` (`default`, `circle`, `round`, `square`), `block`,
`icon`, `icon_placement` (`start`, `end`), `ghost`, `disabled`, and native
`type` (`button`, `submit`, `reset`). `loading` accepts a boolean or
`{delay:milliseconds,icon:content}`; activation remains available during the
delay and is blocked once the indicator is shown. Pending frames belong to
the document and stop when the owner disappears (**D7.5.3**).
Use `href`, `target`, `rel` and `download` for an anchor button, `auto_focus`
for native autofocus, and `class_names`/`styles` maps for `root`, `icon` and
`content` parts. `auto_insert_space` defaults to true for a literal two-character
Chinese label without an icon, except text/link appearances. Nested Chinese
content, provider defaults, motion and full reference correspondence remain open.
`button_group` and `space_compact` accept `direction` (`horizontal` or
`vertical`), `block`, `size`, and `disabled`. Groups join control borders;
native fieldset rules disable descendants while preserving the first-legend
exemption.

Space wraps each non-null child slot and optionally inserts a `separator`.
Flex lays out children directly. Both accept `direction`, `wrap`, `align`,
`justify`, and a nonnegative pixel/CSS-length `gap` or `[horizontal,vertical]` gap.
Flex also accepts `flex`: a nonnegative numeric factor, `auto`/`none`/`initial`,
a fixed CSS length, or a grow/shrink/basis shorthand.
Divider accepts `direction`, `placement` (`start`, `center`, `end`),
`dashed`, and `plain`; vertical dividers cannot have labels.

Grid uses 24 columns. Col accepts integer `span`/`offset`/`push`/`pull` from 0
to 24 and integer `order`; span zero hides a column. Push/pull use logical
insets and follow RTL direction; zero resets the inset. `flex` accepts the
same values as Flex. Breakpoint props accept a span or a map of these fields. The minimum widths are `xs:0`, `sm:576`,
`md:768`, `lg:992`, `xl:1200`, `xxl:1600`, `xxxl:1920` pixels.
Row accepts `align` (`top`, `middle`, `bottom`, `stretch`), `justify`,
`wrap`, and pixel/CSS-length `gutter` or `[horizontal,vertical]` gutters. Each
gutter, `align` and `justify` can also be a breakpoint map. Rules respond to
live viewport changes. Length strings accept nonnegative dimensions in
`px`, `em`, `rem`, `%`, `vw`, `vh`, `vmin`, `vmax`, `ch`, `ex`, `cm`, `mm`,
`in`, `pt` or `pc`, and unitless `"0"`; complex `calc()`/`var()` expressions
are diagnosed as unsupported. The same dimension validation applies to
Skeleton widths/heights and Icon dimensions. A base Col `flex` is inline
and takes precedence over responsive flex, matching the pinned reference.

```lambda
import ui: lambda.ui.dtna
ui.page(ui.row({gutter:[{xs:8,md:24},16]}, [
    ui.col({xs:24,md:12},"First")^,
    ui.col({xs:24,md:{span:8,offset:4}},"Second")^
])^)^
```

Input and Text Area support native `name`, `value`/`default_value`, `required`,
`readonly`, `maxlength`, and `placeholder`; Input also accepts `type` and
`autocomplete`, and Text Area accepts `rows`. Select accepts
`options:[{value:10,label:"Low"},{value:20,label:"High",disabled:true}]`.
Option values may be strings, symbols, integers, or booleans, and must have
distinct string representations. Select change actions preserve the supplied
value type. Checkbox, Radio, and Switch use `checked`/`default_checked`.
Radio groups use the native shared `name` attribute.

A supplied `value` or `checked` is controlled: native input first requests a
change through `ui_change`, then the next document frame restores the latest
committed prop if the application declines it. Accepted edits retain native
selection and caret state. Uncontrolled fields keep their native edits.
Radio restoration uses the native named group, including its controlled peers;
Select requests retain the original option value type. Input and Text Area
fixtures cover composition commit/cancel as well as ordinary typing.

Tabs, Segmented, and flat Menu require a nonempty `id` and unique item keys:
`{id:"sections",items:[{key:'home',label:"Home",children:"Home panel"}]}`.
Arrow keys and Home/End skip disabled items and update focus. Tabs mounts only
the active panel by default; `keep_mounted:true` eagerly retains all panels,
including native input edits when hidden. Panel input arrows keep their native
editing behavior.

Segmented also accepts `size`, `block`, `orientation`, `shape:'round'`, and a
native form `name`. Items may supply `icon` and `title`. Its root is a radio
group; selected buttons form a roving focus stop. Selecting the committed
choice emits no change. Named groups submit the selected scalar's text while
`ui_change` preserves its type. Reusing one ordinary SVG value across multiple
slots currently hits engine issue **UI-1**; those native slots are not certified.

Pagination accepts `total`, `current`/`default_current`, and
`page_size`/`default_page_size`. `show_size_changer` displays a native select;
`page_size_options` defaults to `[10,20,50,100]`. A size change requests the
page containing the old page's first item. `show_quick_jumper` accepts a
positive page number on Enter and clamps it to the last page. Large totals
render a bounded page window with jump ellipses. `locale` explicitly selects
`en-US` or `zh-CN` labels; it does not create ambient provider state
(**S9.1.7**). Controlled page and size remain authoritative while actions
request changes. Controlled/default pairs are mutually exclusive.
`simple:true` uses an editable current-page input; `simple:{read_only:true}`
uses text. Enter, blur and Up/Down commit the simple draft and clamp its bounds.
`size:'small'` uses 24px controls. `show_total:true` supplies localized total
text; a pure `show_total(total,bounds)` callback receives the inclusive item
bounds (`[0,0]` when empty). `hide_on_single_page:true` omits the presentation
when there is at most one page.

Steps accepts `items:[{title,description,subtitle,icon,status,disabled}]`,
zero-based `current`/`default_current`, `status` (`wait`, `process`, `finish`,
`error`), `direction`, `size`, and `disabled`. `clickable:true` requires an
`id` and renders native buttons supporting Enter/Space; disabled items are
skipped by sequential focus. Horizontal steps become vertical below 576px
unless `responsive:false`. Controlled current remains authoritative.

Icon exposes 498 names and 848 outlined/filled/two-tone variants from the pinned
licensed upstream catalog. `icon_names()` lists names; unavailable name/theme
pairs raise an error. Props include `theme`, `width`, `height`, `rotate`, `spin`,
`primary_color`, and `secondary_color`. A `label` gives the SVG image semantics;
otherwise it is decorative. Automatic secondary color derivation accepts
`#rrggbb`; other CSS primary colors require an explicit secondary color.

Avatar accepts `shape:'circle'|'square'`, `size:'small'|'middle'|'large'`, a
positive numeric `size`, or a breakpoint map of positive sizes. A missing entry
at the current breakpoint restores default metrics. `dimension` retains the
earlier numeric-size surface. `src` may be a URL or custom element; `srcset`,
`alt` and `draggable` reach the native image. Without a source, `icon` takes
precedence over child text. Text auto-fitting, failed-image fallback and
group/overflow presentation remain outstanding.

Tag accepts `color` (thirteen named presets, status names or a custom color),
`variant:'filled'|'outlined'|'solid'`, `icon`, `closable`, `close_icon`,
`disabled`, `href` and `target`. Filled/outlined custom colors currently
require `#rrggbb`; other CSS colors are accepted for solid tags. `bordered:true`
selects outlined styling when no variant is supplied. `status` remains a
compatibility spelling for a semantic color. Uncontrolled tags hide after close;
`visible` makes visibility application-owned and `default_visible` supplies the
initial uncontrolled state. A close emits `ui_action` with `action:'close'`
and `value:false`. Retain `visible:true` to decline the request. This uses
procedural notifications, whose return value does not synchronously cancel an
enclosing handler (**S12.1.1v2–S12.1.3**).

`checkable_tag(props,child)` accepts `checked` or `default_checked`, `disabled`
and `icon`. Its native button has checkbox semantics and emits `ui_change`
with a boolean requested value. Enter/Space and inherited disabled state use
the DOM behavior package (**D7.2.5**). Controlled/default pairs are exclusive.
Group selection, motion and semantic-part overrides remain outstanding.

Badge supports `count`, `overflow_count`, `show_zero`, `dot`, `status`, `text`,
`offset:[horizontal,vertical]`, `color`, and `size`. Overflow text retains the
full count as its accessible name. `badge_ribbon` accepts `text`, `color`, and
logical `placement:'start'|'end'`. Descriptions accepts responsive `column`,
`layout`, `bordered`, `colon`, `size`, `title`, `extra`, and `items`. Items carry
`label`, `children`, responsive `span` (positive integer or `'filled'`), and
`label_style`/`content_style`; `description_item(props,child)` constructs one.

Progress supports `type:'line'|'circle'|'dashboard'`, `percent`, `show_info`,
`status:'normal'|'active'|'success'|'exception'` (`error` is an alias), `size`,
`dimension`, `stroke_width`, `stroke_linecap`, `stroke_color`, `rail_color`,
`success:{percent,stroke_color}`, `gap_degree`, and `gap_placement`. `steps`
is a count or `{count,gap}`; a color array colors its discrete segments.
Pure Lambda callbacks `rounding(step)` and `format(percent,success_percent)`
customize rounding and the indicator. `stroke_color:{from,to,direction}` or a
map of percentage stops (`{["0%"]:"red",["100%"]:"blue"}`) supplies a gradient;
it requires a unique component `id` for its SVG mask resource. Lines use linear gradients and rings use conic gradients. Directions are
`to right`, `to left`, `to top`, and `to bottom`. Ring colors have native pixel checks. Percent label positioning is
not implemented and is diagnosed. Bare system-function references lack
the runtime's boxed dynamic-call ABI; callbacks must be Lambda functions
(**D6.2.1/D6.2.2v2**).

Timeline accepts `items`, `orientation`, `mode:'start'|'end'|'alternate'`,
`reverse`, `pending`, and `pending_icon`. Items accept `title`, `content`,
`color`, `icon`, `placement`, `loading`, `class`, and `style`; legacy
`label`/`children`/`dot`/`position` names remain accepted. Vertical/horizontal
connectors have native pixel checks; reference correspondence remains partial.

Statistic accepts `value`, `precision`, `prefix`, `suffix`, `loading`,
`group_separator`, `decimal_separator`, `value_style`, `locale`, and pure
`formatter(value)`. Precision truncates or pads fractional digits, matching
the reference formatter. Numeric strings preserve all digits without a
floating-point conversion. Countdown remains in M6.

Skeleton accepts `loading`, `active`, `round`, `avatar`, `title`, and
`paragraph`. Avatar options include `size` and `shape`; title accepts `width`;
paragraph accepts `rows` and one width or per-row widths. Zero rows are valid.
`loading:false` renders the supplied child content. `skeleton_avatar`,
`skeleton_button`, `skeleton_input`, `skeleton_image`, and `skeleton_node`
provide standalone parts with dimensions, shape and optional motion.
Motion and reduced-motion CSS still need native verification.

Card supports `cover`, `extra`, `actions`, `bordered`, `hoverable`, `loading`,
`size`, `type:'inner'`, `head_style`, and `body_style`; `card_meta` provides
avatar/title/description slots and `card_grid` provides grid cells. `tab_list`
reuses Tabs and requires an `id`; `active_tab`/`default_active_tab` map to its
controlled/default selection. Tab actions carry the nested `<id>-tabs` id.

Empty accepts preset `image:'default'|'simple'`, a URL or an element,
`image_height`, `description`, explicit `locale:'en-US'|'zh-CN'`, and child
actions. `false` hides the image or description. Result accepts
`status:'success'|'error'|'warning'|'info'|'403'|'404'|'500'`, `title`,
`subtitle` (`description` alias), custom `icon`, `extra` actions and child
detail content. Its HTTP illustrations and Empty presets are frozen from
AntD 6.6.5 with MIT notices and source/generated hashes. Native geometry and
semantic-part parity remain partial.

Typography emits semantic `h1`–`h5` headings (`level`, default 1), `strong`, `em`, `u`,
`del`, `code`, `mark`, and `kbd` decorations through the corresponding boolean
props (`delete` and `keyboard` name the last two non-obvious flags). `type`
selects secondary/success/warning/danger text. Disabled links omit `href` and
leave the Tab sequence; external `_blank` links default to
`rel:"noopener noreferrer"`. Copy/edit/ellipsis remain outstanding for M3.

## Disclosure and collections

Collapse, Tree, and Table require a nonempty `id`. Item/row keys are strings,
symbols, or integers with distinct string representations. Controlled and
default versions of the same prop are mutually exclusive. Controlled props
remain authoritative; actions request a change for the author to accept.
An explicitly supplied null is distinguished from an omitted key by key
membership (**S8.2.2v5**), so `sort:null` keeps a Table unsorted.

```lambda
import ui: lambda.ui.dtna
ui.page(ui.table({id:"people",page_size:10,selection:'multiple',
    columns:[{key:'name',title:"Name",sortable:true},{key:'age',title:"Age",sortable:true}],
    rows:[{key:1,name:"Ada",age:36},{key:2,name:"Grace",age:85}]
})^)^
```

Collapse accepts `items:[{key,label,children,extra,disabled}]`, `accordion`,
`active_keys`/`default_active_keys`, and `disabled`. All panels mount eagerly
and retain their content while closed. Header buttons support Enter/Space;
Up/Down and Home/End move focus over enabled headers. Nested Collapse events
stay with the component that owns the clicked header.

Tree accepts nested `items:[{key,label,children,disabled,disable_checkbox}]`,
`expanded_keys`, `selected_keys`, and `checked_keys`, each with a `default_`
form, plus `multiple`, `checkable`, `check_strictly`, and `disabled`. Default
checking conducts through eligible descendants and derives checked/mixed
ancestors. Disabled nodes and `disable_checkbox` stop conduction; their
descendants can be checked independently. Strict checking is independent.
Up/Down and Home/End navigate visible enabled nodes; Right expands or enters
a branch, Left collapses or returns to its parent, Enter selects, and Space
checks (or selects when not checkable). First-letter search wraps through
visible enabled labels. Full timed typeahead, lazy loading, drag/drop,
directory mode and virtualization remain open.

Table accepts `rows` (maps), `columns`, and optional `row_key` (default `'key'`).
Columns accept `{key,title,data_index,sortable,filters,render,width,align}`;
`data_index` defaults to the column key, `width` is positive pixels, and
`align` is `left`, `center`, or `right`. A pure `render(value,row,source_index)`
function may return text, HTML or another logical component. Rows may carry
`disabled` and a `detail` slot. Caption and summary use `caption`/`summary`.
`empty_text`, `bordered`, `size`, `loading`, and `disabled` are supported.

Local operations apply filters, then a stable single-column sort, then
pagination. `sort`/`default_sort` is null or `{key,direction:'asc'|'desc'}`;
repeated sort activation cycles ascending, descending, unsorted. Comparisons
use Lambda's total order (**S6.2.1–S6.2.3**). `filters`/`default_filters` maps column
keys to a single typed option value (null clears it); column filter options
are `{value,label,disabled}`. Filters use inline native buttons. Sorting and
filtering request page 1. `current`/`default_current`, positive `page_size`,
and `pagination:false` control paging; pages clamp to the filtered result.

`selection` is `none`, `single`, or `multiple`, with
`selected_keys`/`default_selected_keys`. Selection survives sorting, filtering
and page changes by data key. Select-page affects only visible enabled rows
and preserves other pages' selection. Table selection uses keyboard-activated
buttons with checkbox/radio semantics, separate from form submission controls.
`expanded_keys`/`default_expanded_keys` opens row detail slots; closing or paging
away unmounts those slots. Tree rows, grouped/fixed headers, editing, popup
filters, remote data workflows and virtualization remain open.

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
| Steps | `ui_change` | `'step'`, zero-based index |
| Collapse | `ui_change` | `'expand'`, `{key,active_keys,expanded}` |
| Tree | `ui_change` | `'expand'`, `{key,expanded_keys,expanded}`; `'select'`, `{key,selected_keys,selected}`; `'check'`, `{key,checked_keys,half_checked_keys,checked}` |
| Table | `ui_change` | `'sort'`/`'filter'`/`'page'`, `{sort,filters,current,page_size}`; `'select'`, `{selected_keys}`; `'expand'`, `{expanded_keys}` |
| Form | `ui_action` | `'submit'`, native successful-control entries; `'reset'`, null |
| Closable Alert / Tag | `ui_action` | `'close'`, false |

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

`font_size` derives the five heading sizes and line-box heights from the pinned
reference scale. The resolved map exposes `font_size_heading_1` through
`font_size_heading_5` and corresponding `font_height_heading_*` values; these
are outputs, not override seeds. Body line height derives from font size unless
`line_height` is supplied explicitly. Scoped font changes update these values;
a provider overriding only radius inherits its parent's font scale. Native
editing, focus and caret survive the tested input-driven font changes.

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
sets HTML `lang`; Pagination's explicit `locale` translates its own strings.
`page` accepts
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

Rate accepts `value`/`default_value`, positive integer `count`, `allow_half`,
`allow_clear`, `keyboard`, `disabled`, `readonly`, `size`, `character`,
`tooltips` and `name`. Values must be finite and within `[0,count]`.
`character` accepts text, an element or a pure function receiving
`{index,count,value,disabled}`; repeated authored elements are subject to the
open presentation-identity issue UI-1. Small/middle/large sizes use 15/20/25px.
Pointer motion previews without committing; pointer exit restores the selection.
Repeated selection clears when `allow_clear` is enabled. Left/Right adjust by
one or half, follow RTL, and stop at the bounds; `keyboard:false` disables these
arrow adjustments. Native buttons provide Enter/Space activation (**D7.2.5**).
`ui_change` and `ui_hover` use typed action values; hover exit carries `null`.
A named hidden field submits the selected value as exact text. Controlled
values request changes while retaining application-owned props
(**S12.1.1v2–S12.1.3; S8.2.2v5**). Tooltips currently use native `title`;
shared overlays, semantic parts, motion and native accessibility remain open.

Alert accepts `title` (or the compatibility `message`), `description`, `action`,
`icon`, `show_icon`, `status` (`info`, `success`, `warning`, `error`),
`variant` (`outlined`, `filled`), and `banner`. Banners default to warning with
an icon; ordinary alerts default to info without an icon. Child content stays
inside the alert's content section. A supplied `title`, including null, takes
precedence over `message` (**S8.2.2v5**).

Use `closable`, `close_icon` and `close_label` for the native close button.
A custom close icon implies closability unless `closable:false` is supplied.
`visible` controls committed visibility; `default_visible` initializes local
visibility. They are mutually exclusive. A close emits `ui_action` with
`action:'close', value:false`; retaining `visible:true` declines the request
(**S12.1.1v2–S12.1.3**). Closing a nested alert leaves its parent open, including
when both omit HTML IDs. Native Enter/Space activate the close button
(**D7.2.5**). Close motion and after-close notification remain outstanding.

Alert's `class_names` and `styles` maps address `root`, `icon`, `section`,
`title`, `description`, `actions` and `close`. Class values and CSS declaration
strings are validated; unknown parts are errors. The root's ordinary `style`
follows `styles.root`. These maps affect presentation rather than application
state. Alert error boundaries remain deferred pending the failure-isolation
contract in the proposal.

Spin accepts `size:'small'|'middle'|'large'`, controlled `spinning` (default
true), nonnegative millisecond `delay`, `description` (or compatibility `tip`),
`indicator`, `fullscreen`, and `percent` (finite number or `'auto'`). Indicator
sizes are 14/20/32px; size changes leave the description font unchanged. Manual
percentages clamp to 0–100. Automatic progress follows the reference's staged
200ms updates and parks above 96 until its owner stops spinning. Delays and
automatic progress use document-owned named frames (**D7.5.3**); removing the
owner prevents later delivery. There is no mutable global default indicator.

`indicator` may be authored content or a pure Lambda function receiving
`{size,percent,spinning}`. Child content stays mounted; active loading dims it
and blocks pointer input. Fullscreen uses a fixed mask and preserves the
reference's keyboard policy. It does not trap focus or add dismissal behavior.
`role:'status'`, `aria-live` and `aria-busy` describe the committed loading state
(**S12.1.1v2–S12.1.3**). These attributes do not establish native screen-reader
support. `class_names` and `styles` address `root`, `section`, `indicator`,
`description` and `container`; `style` follows the root/standalone-section styles.
Provider defaults, repeated authored indicator presentation, native accessibility
and full pixel/motion correspondence remain open in the feature manifest.

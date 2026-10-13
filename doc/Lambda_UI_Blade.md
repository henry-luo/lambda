# Lambda UI Blade

`lambda.ui.blade` is a high-resolution fantasy theme for native Radiant
documents. Gold and iron bevels, dark wood, parchment, serif typography,
engraved corners and luminous resource meters reference
[RPGUI](https://github.com/RonenNess/RPGUI) and the supplied fantasy inventory
image. Its 18 equipment/spell illustrations and ornaments are original SVG;
no pixel sprites, downloaded RPGUI assets, external fonts or image services
are required at runtime.

Blade is an ordinary shipped source package (**D7.2.4**). Qualified
`<blade.*>` descriptors retain their authored names (**S2.4.3v3**); pure views
present them and event procedures update application state (**S12.1.3**).
Constructors, `render` and `page` report invalid props through the raised
error channel; propagate with `^` or handle the diagnostic (**S7.6.3v2**).

## Open the galleries

```bash
./lambda.exe view test/ui/blade_gallery.ls
./lambda.exe view test/ui/blade_components.ls
```

The first is an interactive character inventory: select equipment, cast spells,
or use Heal to update vitality. The second presents forms, collections,
feedback, resource meters, materials, rarities and the full RPG icon set.
The character gallery adapts to narrow and wide windows.

```lambda
import ui: lambda.ui.blade

apply(<blade.page title:"The Armory",
    <blade.panel title:"Inventory",material:'wood',
        <blade.space gap:16,
            <blade.item_slot id:"sword",icon:'sword',rarity:'legendary',selected:true,label:"Iron Oath">
            <blade.item_slot icon:'potion-red',quantity:8,label:"Health potions">>
        <blade.meter resource:'health',value:840,max:1000>
        <blade.button variant:'primary',"Accept quest">>>)
```

Optional constructors produce the same logical descriptors, such as
`ui.item_slot({icon:'sword',label:"Sword"})^`.
`ui.render(source)^` presents a descriptor; `ui.page(source,options)^` adds the
HTML shell, stylesheet and tokens. Page options are `title`, `locale`,
`direction` (`ltr`/`rtl`) and `tokens`. When putting logical components inside
ordinary HTML, render them explicitly: `<div ui.render(<blade.button "Save">)^>`.

## Shared components

Blade exposes **64 components/subcomponents**: the 59 shared components listed
in the [Clay component inventory](Lambda_UI_Clay.md#component-surface), plus
five RPG components below. Inputs, forms, tabs, menu, segmented controls,
pagination, table, tree, collapse, ratings, tags and alerts use the same native
behavior and prop contracts as [Lambda UI](Lambda_UI.md). Password/search
aliases and `description_item` remain available. Shared components keep their
`dtna-*` behavior classes and data attributes in the rendered DOM.

`ui.component_names()` lists all 64 kinds; multiword tag names use underscores.
`ui.icon_names()` lists the shared UI icons; `<blade.icon name:"setting">`
continues to work. `ui.rpg_icon_names()` lists the separate fantasy artwork.

## RPG components

Common props include `id`, `class`, `style`, `title`, `label`, `role`,
`tabindex`, `aria-*` and `data-*` attributes. Only `panel` accepts children.

| Component | Additional props and behavior |
|---|---|
| `panel(props,child)` / `<blade.panel>` | `material`: `wood` (default), `iron`, `parchment`; `title` and `extra` accept presentation content. Emits a semantic section with a heading and engraved corners. Parchment supplies dark text to its descendants. |
| `meter(props)` / `<blade.meter>` | `resource`: `health` (default), `mana`, `stamina`, `experience`; `value` defaults to 0, `max` to 100; `show_value` defaults to true. Values must be finite, with `max > 0` and `0 <= value <= max`. Emits an accessible progressbar and proportional fill. |
| `item_slot(props)` / `<blade.item_slot>` | `icon` from the RPG catalog, or omit it for an empty slot; `rarity`: `common`, `uncommon`, `rare`, `epic`, `legendary`; controlled `selected` (default false), positive integer `quantity`, `disabled`, numeric pixel `size`. Emits a native button with `aria-pressed`. |
| `ability(props)` / `<blade.ability>` | Required RPG `icon`; optional `hotkey`, `disabled`, numeric pixel `size`, `cooldown` from 0 to 100. Cooldown is the remaining percentage; a positive value dims the button, draws the overlay and disables native activation. |
| `rpg_icon(props)` / `<blade.rpg_icon>` | Required `name`; numeric pixel `size` defaults to 64; optional accessible `label`. Emits a scalable SVG with a 96×96 viewBox. Without a label it is decorative. |

Slots and abilities emit `ui_action` with
`{component:"item-slot" or "ability", id, action:'activate', value:icon}`.
Mouse clicks and native Enter/Space activation share this event. Selection is
controlled by the application; the slot does not mutate `selected` itself.
Native disabled providers also disable descendant slots and abilities.
Hotkey badges display the application's bindings; they do not install global
key handlers. Applications update cooldown values as their game state changes.
This theme does not implement a game clock, drag/drop inventory or item rules.

```lambda
view armory: <armory> state equipped:false {
    ui.render(<blade.item_slot id:"weapon",icon:'sword',label:"Equip sword",selected:equipped>)^
}
on ui_action(action) {
    if (action.id == "weapon") { equipped = not equipped }
}
apply(<blade.page <armory>>)
```

The RPG icon names are `sword`, `shield`, `helmet`, `armor`, `boots`, `bow`,
`scroll`, `ring`, `fire`, `ice`, `heal`, `hammer`, `dash`, `skull`, `gem`,
`potion-red`, `potion-blue`, `potion-green`. Bevel facets and engraved outlines
scale with the SVG, including sizes larger than the default inventory slots.

## Tokens and materials

```lambda
<blade.config_provider tokens:{primary:"#78b4d4",gold:"#78b4d4",slot_size:96},
    <blade.button variant:'primary',"Frost covenant">
    <blade.config_provider tokens:{radius:10},
        <blade.button variant:'primary',"Inherits the covenant color">>>
```

Nested providers emit supplied overrides and their derived dependencies only.
Unspecified values inherit. Providers are native fieldsets and accept
`disabled` and `direction`. Blade and other themes can occupy sibling roots;
keep Blade components inside a Blade page root.

| Tokens | Defaults |
|---|---|
| `primary`, `text`, `text_secondary` | `#d7ad62`, `#f4e4c3`, `#c0a782` |
| `background`, `surface`, `border` | `#171313`, `#2d2420`, `#806345` |
| `gold`, `gold_light`, `gold_dark`, `iron` | `#cda464`, `#f9e1a2`, `#634326`, `#869196` |
| `wood`, `parchment`, `ink`, `shadow` | `#39251c`, `#e8d2a6`, `#382a20`, `#090909` |
| `health`, `mana`, `stamina`, `epic` | `#bd3c49`, `#3a96c7`, `#699b58`, `#ae78d7` |
| `success`, `warning`, `error` | `#82b580`, `#e4aa4f`, `#e3746d` |
| `disabled_background`, `disabled_text` | `#292425`, `#84786b` |
| `font_family` | `Georgia, 'Times New Roman', serif` |
| `font_size`, `line_height`, `control_height` | `16`, `1.5`, `42` |
| `radius`, `spacing`, `frame_width`, `slot_size` | `5`, `12`, `3`, `76` |

Colors accept six-digit hex strings. Dimensions are finite pixel numbers;
`radius` and `spacing` may be zero, while `frame_width` and `slot_size` must be
positive. `ui.tokens(overrides)^` resolves defaults, palette shades and heading
sizes. CSS emits `--blade-*` variables and local `--dtna-*` behavior aliases.
Decorative icon facets use their own original illustration palette.

## Scope and validation

Blade shares the current Ant-inspired implementation's feature limits; it
does not add Modal, Drawer, Date Picker, Upload, Slider or virtualization.
The HTML export can be inspected in a browser; interactive browser
distribution remains deferred.

`make test-ui-blade ARGS='--jobs 2'` runs source contracts and native fixtures
across interpreter, automatic and MIR JIT tiers (**D8.1.1v18**). Checks include
all shared constructors, vector scaling, input ownership, keyboard activation,
controlled slot requests, cooldown/disabled controls, retained tabs, table
sorting/selection/paging, forms, token inheritance, mixed theme scopes and
responsive gallery geometry.

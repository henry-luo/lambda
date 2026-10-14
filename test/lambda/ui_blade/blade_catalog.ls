import ui: lambda.ui.blade
import catalog: lambda.ui.blade.catalog
import c: lambda.ui.core.component

fn sample(kind) => if (kind == "tree") {id:"tree",items:[{key:1,label:"One"}]}
    else if (contains(["tabs","menu","segmented","collapse"],kind)) {id:"sample",items:[{key:1,label:"One",children:"Body"}]}
    else if (kind == "table") {id:"table",rows:[{key:1,name:"Knight"}],columns:[{key:'name',title:"Name"}]}
    else if (kind == "select") {options:[{value:1,label:"One"}]}
    else if (kind == "icon") {name:"home"}
    else if (contains(["steps","breadcrumb","descriptions","timeline"],kind)) {items:[]}
    else {}
let all_native = [for (kind in catalog.kinds()) (
    let source = catalog.descriptor(kind,sample(kind))^,
    starts_with(string(name(source)),"blade.") and ui.render(source)^ == c.render(catalog.native(kind,sample(kind))^))]
let icons = [for (icon in ui.rpg_icon_names()) ui.render(ui.rpg_icon({name:icon,size:192,label:icon})^)^];
[
    ["64 unique components",len(ui.component_names()) == 64 and len(unique(ui.component_names())) == 64],
    ["all 58 shared contracts rendered",len(all_native) == 58 and all(all_native)],
    ["RPG widgets available",all([for (kind in ["panel","meter","item-slot","ability","rpg-icon"]) contains(ui.component_names(),kind)])],
    ["18 original vector icons",len(icons) == 18 and len(unique(ui.rpg_icon_names())) == 18],
    ["vectors scale without sprites",all([for (icon in icons) name(icon) == 'svg' and icon.viewBox == "0 0 96 96" and icon.width == 192])],
    ["icons expose accessible labels",all([for (icon in icons) icon.role == "img" and icon["aria-label"] != null])],
    ["shared UI icon set retained",len(ui.icon_names()) > 50],
    ["password and search aliases",c.props(ui.password()^).type == "password" and c.props(ui.search()^).type == "search"]
]

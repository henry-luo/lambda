import ui: lambda.ui.clay
import catalog: lambda.ui.clay.catalog
import c: lambda.ui.core.component

let collections = ["tabs","segmented","menu","collapse","tree"]
fn props(kind) => if (kind == "tree") {id:"tree",items:[{key:1,label:"One"}]}
    else if (contains(collections,kind)) {id:"example-" ++ kind,items:[{key:1,label:"One",children:"Body"}]}
    else if (kind == "table") {id:"table",rows:[{key:1,name:"Ada"}],columns:[{key:'name',title:"Name"}]}
    else if (kind == "select") {options:[{value:1,label:"One"}]}
    else if (kind == "icon") {name:"home"}
    else if (contains(["breadcrumb","steps","timeline","descriptions"],kind)) {items:[]}
    else {}
let rendered = [for (kind in catalog.kinds()) (
    let descriptor = catalog.descriptor(kind,props(kind))^,
    let native = catalog.native(kind,props(kind))^,
    {kind:kind,qualified:starts_with(string(name(descriptor)),"clay."),same:ui.render(descriptor)^ == c.render(native)}
)];
[
    ["full catalog size",len(ui.component_names()) == 59 and len(unique(ui.component_names())) == 59],
    ["every constructor qualified",all([for (item in rendered) item.qualified])],
    ["every renderer shares behavior",all([for (item in rendered) item.same])],
    ["broad collection surface",all([for (kind in ["table","tree","collapse","tabs","menu","rate","spin","skeleton","descriptions","timeline"]) contains(ui.component_names(),kind)])],
    ["icon catalog",len(ui.icon_names()) > 50],
    ["description item shared shape",ui.description_item({label:"Owner"},"Ada")^ == {label:"Owner",children:"Ada"}]
]

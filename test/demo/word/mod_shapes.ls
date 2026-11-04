// Word owns shape metadata and insertion transactions.
import objects: .mod_objects
import shared: lambda.editor.graphics.shapes
import dimensions: lambda.editor.graphics.dimensions

pub let catalog = shared.catalog
pub let defaults = shared.defaults
pub fn settings(n) any^ {
    let raw = objects.settings(n,"data-word-shape")
    if (not (raw is map)) null else {
        let p = {*:defaults,*:raw}
        if (shared.valid(p,dimensions.office)^) p else null
    }
}
pub fn graphic(p,prefix="word-shape-") => shared.graphic(p,prefix)
pub fn parameters(params) map^ => shared.parameters(params,dimensions.office,"Word")^
pub fn transaction(editor,params,selected=null) map^ {
    let p = parameters(params)^;
    if (selected!=null and settings(selected.node)==null) raise error("Word: select a shape first")
    else {
        let label = [for (entry in catalog where entry.id==p.kind) entry.label][0]
        objects.transaction(editor,"data-word-shape",p,graphic(p),p.width,p.height,if (p.alt!="") p.alt else if (p.text=="") label else p.text,selected)^
    }
}

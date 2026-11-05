// Word owns diagram metadata and insertion transactions.
import objects: .mod_objects
import shared: lambda.editor.graphics.diagram
import dimensions: lambda.editor.graphics.dimensions

pub let layouts = shared.layouts
pub let defaults = shared.defaults
pub let labels = shared.labels
pub fn settings(n) any^ {
    let p = objects.settings(n,"data-word-smartart",defaults);
    if (p!=null and shared.valid(p,dimensions.office)) p else null
}
pub let graphic = shared.graphic
pub fn parameters(params) map^ => shared.parameters(params,dimensions.office,"Word")^
pub fn transaction(editor,params,selected=null) map^ {
    let p = parameters(params)^;
    objects.transaction(editor,"data-word-smartart",p,graphic(p),p.width,p.height,"Diagram: " ++ join(labels(p)," → "),selected)^
}

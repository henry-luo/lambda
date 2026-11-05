// Word owns object metadata and the transaction that inserts shared chart artwork.
import objects: .mod_objects
import shared: lambda.editor.graphics.chart
import dimensions: lambda.editor.graphics.dimensions

pub let types = shared.types
pub let defaults = shared.defaults
pub fn rows(source) array^ => shared.rows(source,"Word")^
pub fn graphic(p) element^ => shared.graphic(p,dimensions.office,"Word")^
pub fn settings(n) {
    let p = objects.settings(n,"data-word-chart",defaults)
    let data = if (p==null) null else shared.checked(p,dimensions.office,"Word") ^ { null };
    if (data==null) null else p
}
pub fn transaction(editor,params,selected=null) map^ {
    let p = {*:defaults,*:params}
    let graphic = graphic(p)^;
    objects.transaction(editor,"data-word-chart",p,graphic,p.width,p.height,"Chart: " ++ p.title,selected)^
}

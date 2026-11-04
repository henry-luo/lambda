// Word owns object metadata and insertion transactions for shared Word Art.
import objects: .mod_objects
import shared: lambda.editor.graphics.wordart

pub let styles = shared.styles
pub let transforms = shared.transforms
pub let defaults = shared.defaults
pub fn settings(n) {
    let stored = objects.settings(n,"data-word-wordart")
    if (stored==null) null else {*:defaults,*:stored}
}
pub fn graphic(p,prefix="wordart-") => shared.graphic(p,prefix)
pub fn parameters(params) map^ => shared.parameters(params,shared.office_limits,"Word")^
pub fn transaction(editor,params,selected=null) map^ {
    let p = parameters(params)^;
    objects.transaction(editor,"data-word-wordart",p,graphic(p),p.width,p.height,if (p.alt=="") p.text else p.alt,selected)^
}

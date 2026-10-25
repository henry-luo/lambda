// Partial formatting preserves the selected source atom and its layout (S1.4, D7.2.5).
import lambda.editor.mod_doc
import image: lambda.editor.mod_image
import shapes: .mod_shapes
import art: .mod_wordart
import presets: ~~.mod_shape_presets

pub let styles = presets.styles
pub let colors = presets.colors
pub fn describe(n) {
    let p = shapes.settings(n)
    if (p!=null) {kind:"shape",values:p} else {
        let a = art.settings(n)
        if (a==null) null else {kind:"wordart",values:a}
    }
}
pub fn draft_values(editor,draft) => if (draft.revision==editor.model_revision and draft.path==image.selected(editor).path)
    draft.values or {} else {}
pub fn transaction(editor,params) map^ {
    let selected = image.selected(editor)
    let item = describe(selected.node)
    let preset = [for (s in styles where s.id==params.preset) s][0]
    let values = if (params.preset!=null and preset!=null) {*:preset.values,dash:"solid",effect:"none",fill_style:"solid",opacity:100} else params.values
    if (item==null) raise error("Word: select a shape or Word Art first")
    else if (params.preset!=null and (preset==null or item.kind!="shape")) raise error("Word: choose a supported shape style")
    else if (not (values is map) or any([for (key,value in values) not contains([for (k,v in item.values) string(k)],string(key))]))
        raise error("Word: choose a supported formatting property")
    else {
        let p = {*:item.values,*:values}
        if (item.kind=="shape") shapes.transaction(editor,p,selected)^ else art.transaction(editor,p,selected)^
    }
}

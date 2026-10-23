// Partial formatting preserves the selected source atom and its layout (S1.4, D7.2.5).
import lambda.editor.mod_doc
import image: lambda.editor.mod_image
import shapes: .mod_shapes
import art: .mod_wordart

pub let styles = [
    {id:"outline",label:"Blue outline",values:{fill:"#ffffff",stroke:"#3478cc",text_color:"#245a9b",weight:1.5}},
    {id:"blue",label:"Blue",values:{fill:"#3478cc",stroke:"#245a9b",text_color:"#ffffff",weight:1.5}},
    {id:"ink",label:"Ink",values:{fill:"#24334b",stroke:"#24334b",text_color:"#ffffff",weight:1}},
    {id:"violet",label:"Violet",values:{fill:"#805bc5",stroke:"#63439e",text_color:"#ffffff",weight:1.5}},
    {id:"amber",label:"Amber",values:{fill:"#fff0d5",stroke:"#be8a31",text_color:"#76541c",weight:1.5}},
    {id:"teal",label:"Teal",values:{fill:"#e0f2f3",stroke:"#278993",text_color:"#206970",weight:1.5}}]
pub let colors = [{id:"white",label:"White",value:"#ffffff"},{id:"ink",label:"Ink",value:"#24334b"},
    {id:"blue",label:"Blue",value:"#3478cc"},{id:"violet",label:"Violet",value:"#805bc5"},
    {id:"teal",label:"Teal",value:"#278993"},{id:"amber",label:"Amber",value:"#be8a31"},
    {id:"rose",label:"Rose",value:"#c26073"},{id:"gray",label:"Gray",value:"#94a0b2"}]
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
    if (item==null) raise error("Word: select a shape or WordArt first")
    else if (params.preset!=null and (preset==null or item.kind!="shape")) raise error("Word: choose a supported shape style")
    else if (not (values is map) or any([for (key,value in values) not contains([for (k,v in item.values) string(k)],string(key))]))
        raise error("Word: choose a supported formatting property")
    else {
        let p = {*:item.values,*:values}
        if (item.kind=="shape") shapes.transaction(editor,p,selected)^ else art.transaction(editor,p,selected)^
    }
}

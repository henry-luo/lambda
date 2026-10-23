// Equation chrome uses the shared Word controls; thumbnails use the document's math renderer.
import lambda.editor.mod_step
import image: lambda.editor.mod_image
import lambda.ui.controls
import equation: .mod_equation
import authoring: .mod_equation_edit
import catalog: .mod_equation_catalog
import objects: .mod_objects

fn structure_icon(s) => <svg class:"app-icon color-icon",width:"34",height:"34",viewBox:"0 0 40 38",
    ["aria-hidden"]:"true",["data-icon"]:"equation-" ++ s.id,["data-icon-tone"]:"violet",
    <rect x:"2",y:"3",width:"36",height:"31",rx:"5",fill:"#f2edfb">
    <text x:"20",y:"25",'text-anchor':"middle",'font-family':"Georgia, serif",'font-size':if (len(s.glyph)>2) 20 else 27,fill:"#66518e",s.glyph>>
fn thumbnail(source) {
    let p = equation.render({*:equation.defaults,source:source,size:20}) ^ {null};
    if (p==null) null else <img src:objects.source(p.element)^,alt:source>
}
pub fn ribbon(editor,read_only,opened,ui) {
    let p = equation.settings(image.selected(editor).node)
    let disabled = read_only or p==null;
    <div class:"ribbon-groups equation-ribbon",
        ui.group("Tools",[<div class:"ribbon-command-row",
            ui.menu("equation-presets-menu","Equation","equation","equation-presets",opened,"ribbon-large",disabled);
            <div class:"ribbon-command-stack",
                ui.button("equation-edit","ui.equationEdit","LaTeX","code",{},true,disabled,"","Edit LaTeX source")
                ui.button("equation-normal-text","ui.equationInsert","Normal Text","text_box",{category:"normal"},false,disabled)
                ui.menu("equation-convert-menu","Convert","refresh","equation-convert",opened,"",disabled)>>])
        ui.group("Symbols",[<div class:"equation-symbol-tools",
            <div class:"equation-symbol-grid equation-symbol-quick",*[for (s in catalog.symbol_groups[0].entries)
                ui.button("equation-symbol-" ++ s.id,"ui.equationInsert",s.glyph,null,{category:"symbols",id:s.id},false,disabled,"equation-symbol",s.label)]>
            ui.menu("equation-symbols-menu","More Symbols","symbol","equation-symbols",opened,"ribbon-large",disabled)>])
        ui.group("Structures",[<div class:"ribbon-command-row",*[for (s in catalog.structures)
            ui.menu("equation-structure-" ++ s.id,s.label,structure_icon(s),"equation-structure-" ++ s.id,opened,"ribbon-large",disabled)]>])
        ui.group("Equation Options",[<div class:"ribbon-command-row",
            <div class:"ribbon-command-stack",
                ui.button("equation-display","ui.equationFormat","Display","wrap_top_bottom",{display:true},p.display,disabled)
                ui.button("equation-inline","ui.equationFormat","Inline","wrap_inline",{display:false},not p.display,disabled)
                ui.button("equation-number","ui.equationEdit","Number…","numbering",{focus:"equation-editor-number"},p.number!="",disabled)>
            <div class:"ribbon-command-stack",
                ui.menu("equation-justify-menu","Justification","align_" ++ p.justify,"equation-justify",opened,"",disabled)
                ui.button("equation-close","ui.equationClose","Close Equation","close",{},false,disabled)>>])>
}
pub fn menu(st,ui) {
    let menu = st.context.menu
    let disabled = st.read_only or equation.settings(image.selected(st.editor).node)==null;
    if (menu=="equation-convert") <div
        ui.menu_item("equation-professional","ui.equationFormat","Professional","equation",disabled,null,{linear:false})
        ui.menu_item("equation-linear","ui.equationFormat","Linear (LaTeX)","code",disabled,null,{linear:true})>
    else if (menu=="equation-justify") <div *[for (v in ["left","center","right"])
        ui.menu_item("equation-justify-" ++ v,"ui.equationFormat",v ++ " aligned","align_" ++ v,disabled,null,{justify:v})]>
    else if (menu=="equation-symbols") <div class:"equation-symbol-categories",*[for (g in catalog.symbol_groups) <section
        <div class:"context-caption",g.label>
        <div class:"equation-symbol-grid",*[for (s in g.entries)
            ui.button("equation-more-" ++ g.id ++ "-" ++ s.id,"ui.equationInsert",s.glyph,null,{category:"symbols",id:s.id},false,disabled,"equation-symbol",s.label)]>>]>
    else {
        let category = catalog.structure(slice(menu,19))
        let entries = if (menu=="equation-presets") catalog.examples else category.entries;
        <div <div class:"context-caption",if (menu=="equation-presets") "Built-in Equations" else category.label>
            <div class:"equation-template-grid",*[for (entry in entries)
                ui.button("equation-choice-" ++ entry.id,if (menu=="equation-presets") "ui.equationUse" else "ui.equationInsert",entry.label,
                    thumbnail(entry.source or replace(entry.template,"#",entry.body or "x")),
                    {id:entry.id,category:category.id},false,disabled,"equation-template")]> >
    }
}
fn field(id,label,value,property,disabled=false,area=false) => <label class:"shape-field",<span label>
    if (area) apply(<control.text_area id:id,class:"equation-editor-source",label:label,value:value,disabled:disabled,maxlength:4000,
        on_change:{action:"ui.equationDraft",params:{property:property},param:"value"}>)
    else apply(<control.input id:id,class:"ribbon-select",type:"text",label:label,value:value,disabled:disabled,
        on_change:{action:"ui.equationDraft",params:{property:property},param:"value"}>)>
pub fn pane(st,ui) {
    let draft = authoring.draft(st)
    let p = draft.values
    let n = image.selected(st.editor).node;
    <aside id:"equation-editor-pane",class:"selection-pane equation-editor-pane",["aria-label"]:"Equation editor",
        <div class:"navigation-heading",<strong class:"pane-heading","Equation">
            ui.button("close-equation-pane","ui.equationClose","Close Equation","close",{},false,false,"icon-only")>;
        <div class:"equation-current-preview",<img src:attrs_get(n.attrs,'src'),alt:p.source>>;
        <p class:"pane-hint","Edit LaTeX, then Apply. Select source text before choosing a structure to wrap it.">;
        if (draft.error==null) null else <p id:"equation-editor-error",class:"equation-editor-error",role:"alert",draft.error>;
        field("equation-editor-source","LaTeX source",p.source,"source",st.read_only,true);
        <div class:"shape-pane-grid",
            field("equation-editor-size","Size (px)",p.size,"size",st.read_only)
            field("equation-editor-color","Color (hex)",p.color,"color",st.read_only)>;
        field("equation-editor-number","Equation label",p.number,"number",st.read_only);
        <p class:"pane-hint","Optional label, for example (1). Ctrl/⌘ Enter applies your changes.">;
        <div class:"equation-editor-actions",
            ui.button("equation-apply","ui.equationApply","Apply","check",{},false,st.read_only,"equation-apply")
            ui.button("equation-discard","ui.equationCancel","Discard Draft","undo",{},false,st.read_only)>;
        <p id:"equation-draft-status",class:"pane-hint",if (authoring.parameters(p)==equation.settings(n)) "All changes applied" else "Unapplied changes"> >
}

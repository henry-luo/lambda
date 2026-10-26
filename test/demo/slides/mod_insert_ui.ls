// D7.2.5: Insert shares the Home/Word ribbon widgets and typed dialog descriptors.
import ribbon: ~~.mod_ribbon
import data: .mod_insert_data
import shapes: ~~.word.mod_shapes
import ins: .mod_insert
import c: lambda.slide.common

fn large(id,label,icon,action=null) => ribbon.tool(id,label,icon,"home-large",action)
fn menu(id,label,icon) => ribbon.menu(id,label,icon,"home-large")
pub fn tree() => <div class:"insert-ribbon editor-ribbon",
    ribbon.group("Slides",[large("insert-new-slide","New Slide","blank_page","add-slide")]);
    ribbon.group("Tables",[large("insert-table","Table","table","insert-open-table")]);
    ribbon.group("Images",[ribbon.row([large("insert-picture","Pictures","album","insert-open-picture"),large("insert-screenshot","Screenshot","screenshot","insert-open-screenshot")])]);
    ribbon.group("Illustrations",[ribbon.row([menu("insert-shapes","Shapes","shapes"),large("insert-smartart","SmartArt","smartart","insert-open-smartart"),large("insert-chart","Chart","chart","insert-open-chart")])]);
    ribbon.group("Links",[ribbon.row([large("insert-zoom","Zoom","object_position","insert-open-zoom"),large("insert-link","Link","link","insert-open-link"),large("insert-action","Action","star","insert-open-action")])]);
    ribbon.group("Comments",[large("insert-comment","Comment","comment_add","insert-open-comment")]);
    ribbon.group("Text",[ribbon.row([large("insert-text","Text Box","text_box"),large("insert-footer","Header & Footer","footer","insert-open-footer"),
        large("insert-wordart","WordArt","wordart","insert-open-wordart"),large("insert-date","Date & Time","calendar","insert-open-date"),large("insert-number","Slide Number","page_number","insert-open-number"),large("insert-object","Object","document","insert-open-object")])]);
    ribbon.group("Symbols",[ribbon.row([large("insert-equation","Equation","equation","insert-open-equation"),large("insert-symbol","Symbol","symbol","insert-open-symbol")])]);
    ribbon.group("Media",[ribbon.row([large("insert-video","Video","film","insert-open-video"),large("insert-audio","Audio","sound","insert-open-audio")])])>
pub fn menu_tree(key) => if (key != "insert-shapes") [] else [
    *[for (kind in ["rect","ellipse","circle","line"]) ribbon.item("insert-" ++ kind,if (kind == "rect") "Rectangle" else if (kind == "ellipse") "Oval" else kind,"shapes")],
    *[for (shape in shapes.catalog where not (shape.id in ["rect","ellipse","line"])) ribbon.item("insert-open-shape:" ++ shape.id,shape.label,"shapes")]]
fn options(st,field) => if (field.kind != "slide") field.options else [for (scene in st.plan.slides)
    {value:scene.id,label:string(scene.index+1) ++ " · " ++ c.value(scene.source.title,scene.id)}]
fn control(st,field) {
    let value = st.insert_form.values[field.id]
    let id = "insert-field-" ++ field.id;
    if (field.kind == "checkbox") <label class:"insert-check",<input id:id,type:"checkbox",*:if (value) {checked:""} else {}>;<span field.label>>
    else <label class:"field",<span field.label>;
        if (field.kind in ["select","slide"]) <select id:id,for (option in options(st,field))
            <option value:option.value,*:if (option.value == value) {selected:""} else {},option.label>>
        else if (field.kind == "textarea") <textarea id:id,rows:"5",string(value)>
        else <input id:id,type:if (field.kind == "number") "number" else "text",value:c.as_text(value)>>
}
pub fn dialog(st) {
    let form = st.insert_form
    let spec = data.spec(form.kind);
    <div class:"insert-card",
        <header class:"insert-dialog-heading",<h2 id:"insert-title",(if (form.target == "") "Insert " else "Edit ") ++ spec.label>;
            <button type:"button",'data-action':"insert-cancel",'aria-label':"Close dialog","×">>
        <p class:"insert-help",c.value(spec.help,"Insert settings are saved with the object and can be edited later.")>
        <div class:"insert-fields",for (field in spec.fields) control(st,field)>
        if (form.kind == "equation") <label class:"field",<span "Examples"><select id:"insert-example",<option value:"","Choose an equation…">;
            for (example in data.equations) <option value:example.id,example.label>>> else null;
        if (form.kind == "symbol") <div class:"insert-symbols",for (i,group in data.symbols)
            <section <h3 group.name>; <div class:"insert-symbol-grid",for (j,char in group.chars)
                <button type:"button",class:"insert-symbol-choice",'data-action':"insert-symbol:" ++ string(i) ++ ":" ++ string(j),'aria-label':char,char>>>> else null;
        if (form.kind == "comment") <div class:"insert-comments",for (comment in ins.comments(st))
            <div class:"insert-comment-row",<strong comment.author>;<p comment.text>;
                <div class:"insert-comment-actions",<button type:"button",'data-action':"insert-edit-comment:" ++ comment.id,"Edit">;
                <button type:"button",'data-action':"insert-remove-comment:" ++ comment.id,"Delete">>>> else null;
        <p id:"insert-error",class:"insert-error",role:"alert">
        <footer class:"dialog-actions",<button id:"insert-cancel",type:"button",'data-action':"insert-cancel","Cancel">;
            <button id:"insert-confirm",type:"button",class:"primary",'data-action':"insert-confirm",if (form.target == "") "Insert" else "Apply">>
    >
}

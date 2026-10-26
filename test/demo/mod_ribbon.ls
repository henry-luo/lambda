// D7.2.5: stateless ribbon layout shared with Word and Slides.
import icons: .word.mod_ribbon_icons
import outline: .mod_icons
// choose one word boundary; intrinsic button width keeps both lines fully visible.
pub fn label_lines(label) {
    let words = split(label," ")
    let lines = if (len(words)<2) [label] else {
        let at = sort(1 to (len(words)-1),(i)=>abs(len(join(slice(words,0,i)," "))-len(join(slice(words,i)," "))))[0];
        [join(slice(words,0,at)," "),join(slice(words,at)," ")]
    };
    [for (line in lines) <span class:"ribbon-label-line",line>]
}

fn group_graphic(label) => {
    undo:"undo",clipboard:"paste",slides:"blank_page",font:"grow_font",paragraph:"paragraph",drawing:"shapes",
    tables:"table",images:"album",illustrations:"shapes",links:"link",comments:"comment_add",text:"text_box",
    symbols:"equation",media:"film",themes:"format_shape",'slide layout':"columns",canvas:"object_position",
    preview:"play",'transition to this slide':"repeat_header",options:"object_position",timing:"repeat_header",
    entrance:"star",emphasis:"star",effects:"shapes",'advanced animation':"star"
}[lower(label)] or "document"
fn compact_trigger(label) => <button type:"button",class:"ribbon-group-trigger",title:(if label=="" "More" else label) ++ " commands",
    ["aria-label"]:(if label=="" "More" else label) ++ " commands",["aria-expanded"]:"false",
    <span class:"tool-icon",icons.icon(group_graphic(label),22)>
    <span class:"tool-label",if label=="" "More" else label>
    <span class:"group-chevron",outline.icon("down",10)>>
pub fn group(label,items,trigger=null) => <div class:"ribbon-group group-" ++ replace(lower(label)," ","-"),role:"group",["aria-label"]:label,
    if (trigger!=null) trigger else compact_trigger(label);
    <div class:"group-tools", *items> <div class:"group-label", label>>

pub fn tool(id,label,graphic,cls="",action=null) => <button id:id,type:"button",class:"home-tool " ++ cls,
    'data-action':if (action == null) id else action,title:label,'aria-label':label,
    <span class:"tool-icon",icons.icon(graphic,if (cls == "home-large") 28 else 17)>
    <span class:"tool-label",*if (cls == "home-large") label_lines(label) else [label]>>
fn chevron() => <span class:"home-chevron",'aria-hidden':"true",outline.icon("down",10)>
pub fn menu(id,label,graphic,cls="",preview=false) => <button id:id,type:"button",class:"home-tool home-menu-trigger " ++ cls,
    'data-action':"menu-" ++ id,title:label,'aria-label':label,'aria-haspopup':"menu",'aria-controls':"home-menu",'aria-expanded':"false",
    <span class:"tool-icon",icons.icon(graphic,if (cls == "home-large") 28 else 17);
        if (preview) <span class:"home-color-indicator",id:id ++ "-preview"> else null;
        if (cls == "home-large") chevron() else null>
    <span class:"tool-label",*if (cls == "home-large") label_lines(label) else [label]>
    if (cls != "home-large") chevron() else null>
pub fn row(children) => <div class:"home-row",*children>
pub fn stack(children) => <div class:"home-stack",*children>
pub fn history_tools(button) => [for (actions in [["undo","redo"],["save","print"]])
    <div class:"history-tools",for (action in actions) button(action,upper(slice(action,0,1)) ++ slice(action,1),action)>]

// popup actions can share commands with ribbon buttons without duplicating their DOM IDs.
pub fn item(action,label,graphic=null,disabled=false,prefix="") => <button id:prefix ++ (if (contains(action,":")) "home-option-" ++ replace(action,":","-") else action),
    type:"button",class:"home-menu-item",role:"menuitem",'data-action':action,
    *:if (disabled) {disabled:""} else {},
    if (graphic is string) icons.icon(graphic,17) else graphic;
    <span label>>

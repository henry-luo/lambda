// Native controls and the shared model surface retain their own input policy.
import dom
import lambda.editor.mod_editor
import icons: lambda.ui.dtna.icons
import controls: lambda.ui.controls
import rich: lambda.edit.rich_text
import model: .mod_model
import images: .mod_images

pub fn icon(name) => icons.render({name:name,width:19,height:19})^
pub pn task_hit(st,evt) {
    if (st.dialog!=null or st.selected=="" or model.current(st).deleted) { return null }
    let surface = dom.get_element_by_id(dom.root_node(evt.target),"edit-surface")
    let rendered = dom.query_selector_all(surface,"li.edit-task")
    // generated circles have no DOM target; use the same marker bounds for press and activation.
    let index = [for (i,item in rendered,let box=dom.bounding_box(item)
        where evt.x>=box.x and evt.x<=box.x+23 and evt.y>=box.y+4 and evt.y<=box.y+23) i][0]
    // rendered task order matches model preorder, including nested lists.
    return if (index==null) null else model.tasks(st.editor.doc)[index].path
}
view <note_button> {
    <button id:~.id,type:"button",class:"tool " ++ (~.class or ""),title:~.label,
        role:if (contains(~.class or "","context-item")) "menuitem" else null,
        style:if (~.tint==null or ~.tint=="") null else "--note-color:" ++ ~.tint,
        ["aria-label"]:~.label,["aria-pressed"]:string(~.pressed or false),
        *:if (~.disabled) {disabled:""} else {},
        if (~.graphic!=null) icon(~.graphic) else <span ~.label>;
        if (~.graphic!=null and (contains(~.class or "","menu-item") or contains(~.class or "","with-label"))) <span ~.label> else null;
        if (~.shortcut!=null and ~.shortcut!="") <span class:"context-key",~.shortcut> else null>
}
on mousedown() { return 'prevent-default' }
on click(evt) { emit("note_command",{id:~.action,params:~.params or {},node:evt.target,event:evt}) }
pub fn button(id,action,label,graphic=null,params={},pressed=false,disabled=false,css="",tint="",shortcut="") =>
    apply(<note_button id:id,action:action,label:label,graphic:graphic,params:params,pressed:pressed,disabled:disabled,class:css,tint:tint,shortcut:shortcut>)

let marks = [{id:"bold",label:"Bold",graphic:"bold",mark:'strong'},
    {id:"italic",label:"Italic",graphic:"italic",mark:'em'},{id:"underline",label:"Underline",graphic:"underline",mark:'u'}]
let text_colors = [{id:"graphite",label:"Graphite",value:"#38352f"},{id:"gray",label:"Gray",value:"#8a8175"},
    {id:"red",label:"Red",value:"#c4483f"},{id:"orange",label:"Orange",value:"#d27625"},{id:"gold",label:"Gold",value:"#aa800e"},
    {id:"green",label:"Green",value:"#33785b"},{id:"teal",label:"Teal",value:"#277e87"},{id:"blue",label:"Blue",value:"#356eb5"},
    {id:"purple",label:"Purple",value:"#835ba8"},{id:"pink",label:"Pink",value:"#bb527c"}]
let highlight_colors = [{id:"yellow",label:"Yellow",value:"#fff09b"},{id:"peach",label:"Peach",value:"#ffd5ad"},
    {id:"pink",label:"Pink",value:"#ffd1df"},{id:"lavender",label:"Lavender",value:"#e3d6fa"},
    {id:"blue",label:"Blue",value:"#cce7fa"},{id:"green",label:"Green",value:"#ccebd6"}]

fn mark_buttons(st,prefix="",css="",disabled=false) => [for (entry in marks)
    button(prefix ++ entry.id,"edit." ++ entry.id,entry.label,entry.graphic,{},edit_mark_active(st.editor,entry.mark),disabled,css)]
fn menu_item(id,action,label,graphic=null,shortcut="",disabled=false,params={}) =>
    button("context-" ++ id,action,label,graphic,params,false,disabled,"context-item menu-item","",shortcut)

view <note_folder> {
    <button id:"folder-" ++ ~.folder,type:"button",class:"folder-row",title:~.label,
        ["aria-current"]:if (~.active) "page" else "false",
        <span class:"folder-icon",icon(~.graphic)><span class:"folder-name",~.label><span class:"folder-count",string(~.count)>>
}
on click(evt) { emit("note_command",{id:"folder.select",params:{folder:~.folder},node:evt.target,event:evt}) }
fn folder(st,id,label,graphic="folder") => apply(<note_folder folder:id,label:label,graphic:graphic,
    count:model.count(st,id),active:st.folder==id>)
pub fn sidebar(st) => <aside id:"sidebar",class:"sidebar",["aria-label"]:"Folders",
    <div class:"account-label","On My Mac">
    <nav folder(st,"all","All Notes","folder-open");
        *[for (f in st.folders) folder(st,f.id,f.name)];
        <div class:"sidebar-separator"> folder(st,"trash","Recently Deleted","delete")>
    <div class:"sidebar-space">
    <div class:"sidebar-bottom",button("new-folder","folder.new","New Folder","folder-add",{},false,false,"with-label")>
>

view <note_row> {
    <button id:"row-" ++ ~.note.id,class:"note-row",type:"button",role:"option",tabindex:"-1",
        ["data-note"]:~.note.id,["aria-selected"]:string(~.selected),title:model.title(~.note),
        <span class:"note-title",model.title(~.note)>
        <span class:"note-excerpt",<span class:"note-time",slice(~.note.updated,5,10)> model.preview(~.note)>
        <span class:"note-location",icon("folder"); ~.folder>
    >
}
on mousedown() { return 'prevent-default' }
on click(evt) { emit("note_command",{id:"note.select",params:{id:~.note.id},node:evt.target,event:evt}) }
pub fn note_list(st) {
    let rows = model.visible(st)
    let pinned = [for (n in rows where n.pinned) n]
    let ordinary = [for (n in rows where not n.pinned) n];
    <section class:"notes-column",["aria-label"]:"Note list",
        <div class:"search-wrap",<div class:"search-field",icon("search");
            apply(<control.input id:"search",label:"Search notes",placeholder:"Search",value:st.query,
                on_change:{action:"search"}>)>>
        <div class:"list-topline",<span id:"list-heading",if (st.query!="") "Search Results" else model.scope_name(st)>
            button("sort-notes","list.sort",if (st.sort=="title") "Sort by date edited" else "Sort by title","menu")>
        <div id:"note-list",class:"note-list",role:"listbox",tabindex:"0",["aria-label"]:"Notes",
            ["aria-activedescendant"]:if (st.selected!="") "row-" ++ st.selected else null,
            if (len(rows)==0) <div class:"list-empty",icon(if (st.query!="") "search" else "file-text");
                <strong if (st.query!="") "No matching notes" else "No Notes">
                <p if (st.query!="") "Try a different search." else "Your next idea starts here.">> else null;
            *[for (group in [{label:"Pinned",notes:pinned},{label:if (len(pinned)>0) "Notes" else "",notes:ordinary}] where len(group.notes)>0)
                <div class:"note-group",
                    if (group.label!="") <div class:"group-label",if (group.label=="Pinned") icon("pushpin") else null;group.label> else null;
                    *[for (n in group.notes) apply(<note_row note:n,selected:st.selected==n.id,folder:model.folder_name(st,n.folder)>) ]>]
        >
        <footer id:"note-count",class:"list-footer",string(len(rows)) ++ (if (len(rows)==1) " note" else " notes")>
    >
}
pub fn toolbar(st) {
    let n = model.current(st)
    let disabled = n==null or n.deleted;
    <header class:"titlebar",
        <div class:"window-controls",["aria-hidden"]:"true",<span class:"traffic red"><span class:"traffic yellow"><span class:"traffic green">>
        <div class:"list-tools",button("toggle-sidebar","sidebar.toggle","Show or hide folders","menu-fold",{},not st.sidebar);
            <div class:"tool-spacer"> button("delete-note","note.delete",if (n.deleted) "Delete permanently" else "Delete note","delete",{},false,n==null)>
        <div class:"editor-tools",
            button("new-note","note.new","New note (⌘N)","edit");<span class:"toolbar-divider">
            button("format-note","menu.format","Text formatting",null,{},st.menu=="format",disabled,"format-tool");
            *mark_buttons(st,"","",disabled);
            button("text-color","menu.color","Text color","font-colors",{},st.menu=="color",disabled,"color-tool",st.last_color);
            button("highlight","menu.highlight","Highlight","highlight",{},st.menu=="highlight",disabled,"color-tool",st.last_highlight);
            <span class:"toolbar-divider">
            button("checklist","edit.checklist","Checklist","check-circle",{},false,disabled);
            button("insert-table","edit.table","Insert table","table",{},false,disabled);
            <span class:"toolbar-divider">
            button("pin-note","note.pin",if (n.pinned) "Unpin note" else "Pin note","pushpin",{},n.pinned or false,disabled);
            <div class:"tool-spacer">
            if (n.deleted) button("restore-note","note.restore","Restore note",null,{},false,false,"text-tool") else null;
            button("undo","edit.undo","Undo","undo",{},false,disabled or not edit_can_undo(st.editor));
            button("redo","edit.redo","Redo","redo",{},false,disabled or not edit_can_redo(st.editor));
            button("more","menu.more","Notebook actions","ellipsis",{},st.menu=="more")>
    >
}

fn color_picker(st) => <div id:"color-picker",class:"popover color-popover",role:"group",["aria-label"]:if (st.menu=="color") "Text color" else "Highlight color",
    <div class:"popover-title",if (st.menu=="color") "Text color" else "Highlight color">
    <div class:"color-swatches",*[for (color in if (st.menu=="color") text_colors else highlight_colors)
        button(st.menu ++ "-" ++ color.id,"edit." ++ st.menu,color.label,null,{color:color.value},
            model.color_value(st.editor,st.menu)==color.value,false,"color-swatch",color.value)]>
    button("color-clear","edit." ++ st.menu,if (st.menu=="color") "Automatic" else "Remove highlight",null,{color:""},false,false,"color-clear");
    <div class:"popover-rule"><label for:"color-value",class:"color-label","Custom color">
    <div class:"custom-color",apply(<control.input id:"color-value",label:"Hex color",value:st.picker_value,
        placeholder:"#RRGGBB",on_change:{action:"picker.value"}>);
        button("color-apply","picker.apply","Apply",null,{},false,false,"color-apply")>
    if (st.picker_error!="") <p class:"color-error",role:"alert",st.picker_error> else null
>

view <note_context> { ~.rendered }
on render(evt) {
    if (evt.event_phase==2) {
        let root = dom.root_node(evt.target)
        let menu = dom.get_element_by_id(root,"note-context-menu")
        let box = dom.bounding_box(menu)
        let viewport = dom.bounding_box(dom.get_element_by_id(root,"notes"))
        // measure the actual menu so its last row remains reachable near any window edge.
        dom.style_set_property(menu,"left",string(max(8,min(box.x,viewport.width-box.width-8))) ++ "px")
        dom.style_set_property(menu,"top",string(max(8,min(box.y,viewport.height-box.height-8))) ++ "px")
        dom.focus_set(dom.query_selector(menu,"button:not([disabled])"),false)
    }
    return 'pass'
}
fn context_menu(st) {
    let n = model.current(st)
    let selected = st.editor.selection.kind=='node' or (st.editor.selection.kind=='text' and st.editor.selection.anchor!=st.editor.selection.head)
    let disabled = n==null or n.deleted;
    apply(<note_context rendered:<div id:"note-context-menu",class:"popover context-popover",role:"menu",["aria-label"]:if (st.context.kind=="text") "Text actions" else "Note actions",
        style:"left:" ++ string(st.context.x) ++ "px;top:" ++ string(st.context.y) ++ "px",
        if (st.context.kind=="text") [
            menu_item("undo","edit.undo","Undo","undo","⌘Z",not edit_can_undo(st.editor) or disabled),
            menu_item("redo","edit.redo","Redo","redo","⇧⌘Z",not edit_can_redo(st.editor) or disabled),<div class:"popover-rule">,
            menu_item("cut","clipboard.cut","Cut","scissor","⌘X",not selected or disabled),
            menu_item("copy","clipboard.copy","Copy","copy","⌘C",not selected),
            menu_item("paste","clipboard.paste","Paste","snippets","⌘V",disabled),
            menu_item("select-all","clipboard.selectAll","Select All","select","⌘A"),<div class:"popover-rule">,
            <div class:"context-format",*mark_buttons(st,"context-","",disabled)>,
            menu_item("color","menu.color","Text Color…","font-colors","",disabled),
            menu_item("highlight","menu.highlight","Highlight…","highlight","",disabled)
        ] else [
            if (n.deleted) menu_item("restore","note.restore","Restore Note","undo")
                else menu_item("pin","note.pin",if (n.pinned) "Unpin Note" else "Pin Note","pushpin"),
            if (not n.deleted) <div class:"context-label","Move to"> else null,
            *if (n.deleted) [] else [for (f in st.folders where f.id!=n.folder)
                menu_item("move-" ++ f.id,"note.move",f.name,"folder","",false,{folder:f.id})],
            <div class:"popover-rule">,
            menu_item("delete","note.delete",if (n.deleted) "Delete Permanently…" else "Delete Note","delete")
        ]
    >>)
}

pub fn menus(st) => if (st.menu=="format") <div class:"popover format-popover",role:"group",["aria-label"]:"Text formatting",
    <div class:"popover-title","Text style">
    <div class:"style-choices",*[for (choice in [{tag:'h1',label:"Title"},{tag:'h2',label:"Heading"},{tag:'p',label:"Body"}])
        button("style-" ++ string(choice.tag),"edit.style",choice.label,null,{tag:choice.tag},false,false,"style-choice") ]>
    <div class:"format-choices",button("bullets","edit.bullets","Bulleted list","menu",{},false,false,"with-label")>
> else if (st.menu=="color" or st.menu=="highlight") color_picker(st)
else if (st.menu=="context") context_menu(st)
else if (st.menu=="more") <div class:"popover more-popover",role:"group",["aria-label"]:"Notebook actions",
    button("save-notebook","file.save","Save Notebook…","save",{},false,false,"menu-item");
    button("save-as","file.saveAs","Save Notebook As…",null,{},false,false,"menu-item");
    button("open-notebook","file.open","Open Notebook…","folder-open",{},false,false,"menu-item");
    button("close-notebook","file.close","Close Notebook…",null,{},false,false,"menu-item");
    <div class:"popover-rule">
    <label for:"move-note","Move note to">
    apply(<control.select id:"move-note",label:"Move note to folder",value:model.current(st).folder or "notes",
        disabled:st.selected=="" or st.folder=="trash",options:[for (f in st.folders) {value:f.id,label:f.name}],on_change:{action:"note.move"}>)
> else null

pub fn editor(st) {
    let n = model.current(st);
    <section class:"editor-column",["aria-label"]:"Note",
        if (n==null) <div class:"editor-empty",icon("file-text");<h1 "No Note Selected"><p "Choose a note, or start a new thought.">
            button("empty-new","note.new","New Note",null,{},false,false,"primary")> else <div class:"editor-scroll",
            <div class:"note-date",n.updated ++ " · " ++ model.folder_name(st,n.folder)>
            if (n.deleted) <div class:"deleted-banner","This note is in Recently Deleted. Restore it to make changes."> else null;
            <div id:"note-paper",class:"note-paper",*:if (n.deleted) {inert:""} else {},
                rich.surface(st.editor,"note-content");if (not n.deleted) images.tools(st.editor) else null>
        >;
        <footer class:"editor-footer",<span id:"status",role:"status",st.status>
            <span if (n!=null) string(len(join(model.lines(n)," "))) ++ " characters" else "">
            <span id:"save-status",if (model.dirty(st)) "Edited · ⌘S to save" else "All changes saved">>
    >
}

view <note_dialog> { ~.rendered }
on render(evt) {
    if (evt.event_phase==2) {
        let input_node = dom.query_selector(evt.target,"input")
        if (input_node!=null) dom.focus_set(input_node,false)
    }
    return 'pass'
}
pub fn dialog(st) {
    let d = st.dialog;
    if (d==null) null else apply(<note_dialog rendered:<div class:"dialog-layer",
        <div class:"dialog-card",role:"dialog",["aria-modal"]:"true",["aria-label"]:d.title,
            <div class:"dialog-icon",icon(if (d.kind=="folder") "folder-add" else "file-text")>
            <h2 d.title><p d.message or "">
            if (contains(["folder","save","open"],d.kind)) apply(<control.input id:"dialog-value",label:if (d.kind=="folder") "Folder name" else "Notebook path",
                value:d.value or "",placeholder:if (d.kind=="folder") "Folder name" else "path/to/Notebook.note.json",on_change:{action:"dialog.value"}>) else null;
            if (d.error!="" and not (d.error is null)) <p class:"dialog-error",role:"alert",d.error> else null;
            <div class:"dialog-buttons",button("dialog-cancel","dialog.cancel","Cancel",null,{},false,false,"dialog-button");
                if (d.kind=="unsaved") button("dialog-discard","dialog.discard","Don't Save",null,{},false,false,"dialog-button") else null;
                button("dialog-confirm","dialog.confirm",d.confirm or "Save",null,{},false,false,"dialog-button primary")>
        >
    >>)
}

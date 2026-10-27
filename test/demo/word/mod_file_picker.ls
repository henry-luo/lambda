// Finder owns filesystem snapshots, paths, ordering and selection (S12.1.3 / D7.2.5).
import dom
import tree: lambda.dom.tree
import browser: ~~.finder.mod_model
import filesystem: ~~.finder.mod_io
import finder: ~~.finder.mod_ui
import storage: .mod_io

pub fn places() => [{id:"home",name:"Home",icon:"home",path:sys.home#},
    *[for (id,caption in {desktop:"Desktop",documents:"Documents",downloads:"Downloads"})
        {id:string(id),name:caption,icon:string(id),path:browser.absolute(caption,sys.home#)}],
    {id:"project",name:"Project",icon:"folder",path:sys.proc.self.cwd#},
    {id:"computer",name:"Computer",icon:"computer",path:"/"}]
pub fn selectable(entry) => entry!=null and (entry.dir or storage.supported_path(entry.path))
pub fn rows(d) => browser.visible(d.browser)
pub fn selected(d) => browser.focused(d.browser)
// selected paths come from Finder's snapshot; trimming their display names would change leading spaces.
pub fn target(d) => selected(d).path or (if (trim(d.path)!="") browser.absolute(trim(d.path),d.browser.path,sys.home#) else null)
pub fn can_open(d) => d.loading!=true and (if (trim(d.path)!="") storage.supported_path(d.path) else selectable(selected(d)))

pub fn navigate(d,path,cursor=-1) {
    let absolute = browser.absolute(path,d.browser.path,sys.home#)
    let loaded = filesystem.read_folder(absolute) ^ { {error:^.message} } ~ { {entries:~} };
    if (loaded.error!=null) {*:d,error:loaded.error,open_path:null}
    else {*:d,browser:browser.listing(d.browser,absolute,loaded.entries,cursor),path:"",error:null,go:false,open_path:null}
}
pub fn initial(path="",last_folder=null) {
    let documents = browser.absolute("Documents",sys.home#)
    let start = last_folder or (if (path!="") browser.parent(browser.absolute(path,sys.proc.self.cwd#))
        else if (exists(documents)) documents else sys.proc.self.cwd#)
    let d = {kind:"open",browser:browser.initial(start),path:"",error:null,go:false,open_path:null};
    navigate(d,start)
}
pub fn choose(d,path) => if (not selectable([for (e in d.browser.entries where e.path==path) e][0])) d else (
    let next = browser.choose(d.browser,path), let e = browser.focused(next),
    {*:d,browser:next,path:if (e.dir) "" else e.name,error:null,open_path:null})
pub fn command(d,action,params={}) {
    if (action=="navigate") navigate(d,params.path)
    else if (action=="back" or action=="forward") {
        let cursor = d.browser.cursor+(if (action=="back") -1 else 1);
        if (cursor<0 or cursor>=len(d.browser.history)) d else navigate(d,d.browser.history[cursor],cursor)
    }
    else if (action=="up") navigate(d,browser.parent(d.browser.path))
    else if (action=="refresh") navigate(d,d.browser.path,d.browser.cursor)
    else if (action=="go") {*:d,go:not d.go,error:null}
    else if (action=="location") navigate(d,params.path)
    else if (action=="select") choose(d,params.path)
    else if (action=="move") {
        let eligible = {*:d.browser,entries:[for (e in d.browser.entries where selectable(e)) e]}
        let ordered = browser.visible(eligible)
        let path = if (params.edge=="first") ordered[0].path else if (params.edge=="last") ordered[last].path
            else browser.move(eligible,params.delta).focus;
        choose(d,path)
    }
    else if (action=="search") {*:d,browser:browser.reconcile({*:d.browser,query:params.value}),path:"",error:null}
    else if (action=="mode") {*:d,browser:{*:d.browser,mode:params.value}}
    else if (action=="sort") {*:d,browser:{*:d.browser,sort:params.value}}
    else if (action=="hidden") {*:d,browser:browser.reconcile({*:d.browser,hidden:not d.browser.hidden}),path:""}
    else if (action=="filename") {*:d,path:params.value,browser:{*:d.browser,selected:[],focus:""},error:null}
    else if (action=="open" or action=="activate") {
        let activated = [for (e in d.browser.entries where e.path==params.path) e][0]
        let picked = if (action=="activate") choose(d,params.path) else d
        let path = target(picked)
        let entry = [for (e in picked.browser.entries where e.path==path) e][0];
        if (action=="activate" and not selectable(activated)) {*:d,open_path:null,error:"This file type cannot be opened in Word."}
        else if (entry.dir==true) navigate(picked,path)
        else if (path!=null and storage.supported_path(path)) {*:picked,open_path:path,error:null}
        else {*:picked,error:"Select a Word document, HTML, Markdown, or text file."}
    }
    else d
}

fn tool(id,label,graphic,disabled=false,active=false) => <button id:"word-picker-" ++ id,type:"button",class:"picker-tool",
    'data-picker-action':id,'aria-label':label,title:label,'aria-pressed':string(active),
    *:(if (disabled) {disabled:""} else {}),finder.icon(graphic)>
fn contents(d) {
    let entries = rows(d);
    if (d.loading==true) <div id:"word-picker-loading",class:"picker-empty",role:"status",finder.icon("documents");
        <strong "Opening document…"><span d.path><span "You can cancel while the document is being imported.">>
    else if (len(entries)==0) <div class:"picker-empty",finder.icon("search");<strong if (trim(d.browser.query)!="") "No matching files" else "This folder is empty">
        <span if (trim(d.browser.query)!="") "Try a different name." else "Choose another folder to find a document.">>
    else <div class:if (d.browser.mode=="icons") "picker-icons" else "picker-list",
        *[for (e in entries) <button type:"button",class:"picker-file",role:"option",tabindex:"-1",
            'data-picker-path':e.path,'data-dir':string(e.dir),'aria-selected':string(contains(d.browser.selected,e.path)),
            'aria-disabled':string(not selectable(e)),title:e.name,
            *:(if (not selectable(e)) {disabled:""} else {}),
            finder.file_icon(e);<span class:"picker-name",e.name><span class:"picker-date",e.modified>
            <span class:"picker-kind",if (e.dir) "Folder" else if (storage.supported_path(e.path)) "Document" else e.kind>
            <span class:"picker-size",if (e.dir) "—" else browser.size_label(e.size)>>]>
}
fn tree(d) => <div class:"edit-dialog-layer",
    <div class:"edit-dialog-backdrop">;
    <section id:"word-file-picker",class:"edit-dialog edit-dialog-open",role:"dialog",'aria-modal':"true",'aria-label':"Open document",
        'data-path':d.browser.path,'data-mode':d.browser.mode,'aria-busy':string(d.loading==true),
        <aside class:"picker-sidebar",<div class:"picker-brand",finder.icon("documents");<strong "Open document">>
            <h2 "Favorites"><nav 'aria-label':"Favorite folders",*[for (p in places()) <button id:"word-picker-place-" ++ p.id,type:"button",
                class:"picker-place",'data-picker-location':p.path,'aria-current':if (p.path==d.browser.path) "page" else "false",title:p.path,
                finder.icon(p.icon);<span p.name>>]>
            <div class:"picker-sidebar-note","On your computer">>
        <div class:"picker-workspace",
            <header class:"picker-toolbar",
                <div class:"picker-navigation",tool("back","Back","back",d.browser.cursor==0)
                    tool("forward","Forward","forward",d.browser.cursor>=len(d.browser.history)-1)
                    tool("up","Enclosing folder","up",d.browser.path=="/")>
                <div class:"picker-view-switch",tool("icons","Icon view","icons",false,d.browser.mode=="icons")
                    tool("list","List view","list",false,d.browser.mode=="list")>
                <div class:"picker-folder",finder.icon("folder");<strong browser.basename(d.browser.path)>>
                <label class:"picker-search",finder.icon("search");<input id:"word-picker-search",type:"search",placeholder:"Search",value:d.browser.query,'aria-label':"Search this folder">>>
            <div class:"picker-location-bar",<nav class:"picker-crumbs",'aria-label':"Folder path",*[for (i,c in browser.crumbs(d.browser.path))
                <span (if (i>0) <span class:"picker-chevron","›"> else null);
                    <button type:"button",'data-picker-location':c.path,title:c.path,c.name>>]>
                tool("go","Go to folder…","go");tool("refresh","Refresh folder","refresh")>;
            (if (d.go) <div class:"picker-go",<label 'for':"word-picker-location","Go to folder:">
                <input id:"word-picker-location",type:"text",value:d.browser.path,'aria-label':"Folder location">
                <button type:"button",'data-picker-action':"location","Go">> else null);
            (if (d.error!=null) <div id:"word-picker-error",role:"alert",d.error> else null);
            (if (d.browser.mode=="list") <div class:"picker-list-heading",<span "Name"><span "Date modified"><span "Kind"><span "Size">> else null);
            <div id:"word-picker-files",role:"listbox",tabindex:"0",'aria-label':"Files and folders",contents(d)>
            // captions are explicit flex items so text and native controls share one line box.
            <div class:"picker-options",<span id:"word-picker-count",string(len(rows(d))) ++ " items">
                <label <input id:"word-picker-hidden",type:"checkbox",*:(if (d.browser.hidden) {checked:""} else {})>;<span "Show hidden files">>
                <label <span "Sort by"> <select id:"word-picker-sort",'aria-label':"Sort files",*[for (key,label in {name:"Name",modified:"Date modified",kind:"Kind",size:"Size"})
                    <option value:string(key),*:(if (d.browser.sort==string(key)) {selected:""} else {}),label>]>>>
            <footer class:"picker-footer",
                <p "Word documents, HTML, Markdown, text, and DOCX. Imports create an editable copy.">
                <div class:"picker-open-row",<label class:"picker-filename",<span "File name:">
                    <input id:"word-file-path",type:"text",value:d.path,placeholder:"Select a document",autocomplete:"off",*:(if (d.loading==true) {disabled:""} else {})>>
                    <button id:"word-picker-cancel",type:"button",class:"edit-dialog-btn edit-dialog-cancel",'data-picker-action':"cancel","Cancel">
                    <button id:"word-picker-open",type:"button",class:"edit-dialog-btn edit-dialog-confirm primary",'data-picker-action':"open",
                        *:(if (not can_open(d)) {disabled:""} else {}),if (d.loading==true) "Opening…" else "Open">>>>
    >>

pn send(evt,action,params={}) { emit("word_command",{id:"picker." ++ action,params:params,node:evt.target,event:evt}) }
view <word_file_picker> { tree(~.dialog) }
on click(evt) {
    let row = dom.closest(evt.target,"[data-picker-path]")
    let location = dom.closest(evt.target,"[data-picker-location]")
    let action = dom.get_attribute(dom.closest(evt.target,"[data-picker-action]"),"data-picker-action")
    if (row!=null) send(evt,"select",{path:dom.get_attribute(row,"data-picker-path")})
    else if (location!=null) send(evt,"navigate",{path:dom.get_attribute(location,"data-picker-location")})
    else if (action=="location") send(evt,"location",{path:dom.get_state(dom.get_element_by_id(dom.root_node(evt.target),"word-picker-location"),"value")})
    else if (action=="icons" or action=="list") send(evt,"mode",{value:action})
    else if (action!=null) send(evt,action)
    return 'handled'
}
on dblclick(evt) {
    let row = dom.closest(evt.target,"[data-picker-path]")
    if (row!=null) { send(evt,"activate",{path:dom.get_attribute(row,"data-picker-path")}); return 'prevent-default' }
    return 'pass'
}
on input(evt) {
    let id = dom.get_attribute(evt.target,"id")
    if (id=="word-picker-search" or id=="word-file-path")
        send(evt,if (id=="word-picker-search") "search" else "filename",{value:dom.get_state(evt.target,"value")})
    return 'handled'
}
on change(evt) {
    let id = dom.get_attribute(evt.target,"id")
    if (id=="word-picker-sort") send(evt,"sort",{value:dom.get_state(evt.target,"value")})
    else if (id=="word-picker-hidden") send(evt,"hidden")
    return 'handled'
}
on keydown(evt) {
    let id = dom.get_attribute(evt.target,"id")
    let control = dom.closest(evt.target,"input,select")!=null
    let primary = evt.metaKey==true or evt.ctrlKey==true
    if (evt.key=="Tab") {
        // keep focus in the modal while the document editor remains mounted behind it.
        let root = dom.get_element_by_id(dom.root_node(evt.target),"word-file-picker")
        let controls = dom.query_selector_all(root,"button:not([disabled]):not([tabindex='-1']),input,select,[tabindex='0']")
        let index = tree.index_in(controls,evt.target) or 0
        let next = (index+(if (evt.shiftKey) -1 else 1)+len(controls))%len(controls)
        dom.focus_set(controls[next],false)
        return 'prevent-default'
    }
    if (evt.key=="Escape") { send(evt,"cancel"); return 'prevent-default' }
    if (primary and evt.shiftKey and lower(evt.key)=="g") { send(evt,"go"); return 'prevent-default' }
    if ((evt.altKey or primary) and evt.key=="ArrowUp") { send(evt,"up"); return 'prevent-default' }
    if (evt.altKey and contains(["ArrowLeft","ArrowRight"],evt.key)) {
        send(evt,if (evt.key=="ArrowLeft") "back" else "forward"); return 'prevent-default'
    }
    if (evt.key=="Enter" and id=="word-picker-location") {
        send(evt,"location",{path:dom.get_state(evt.target,"value")}); return 'prevent-default'
    }
    let listing = dom.closest(evt.target,"#word-picker-files")!=null
    if (evt.key=="Enter" and (id=="word-file-path" or listing)) { send(evt,"open"); return 'prevent-default' }
    if (not control and listing and contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Home","End"],evt.key)) {
        let files = dom.get_element_by_id(dom.root_node(evt.target),"word-picker-files")
        let rows = dom.query_selector_all(files,".picker-file")
        let y = if (len(rows)>0) dom.bounding_box(rows[0]).y else 0
        let columns = max(1,len([for (r in rows where abs(dom.bounding_box(r).y-y)<1) r]))
        send(evt,"move",{delta:if (evt.key=="ArrowUp") -columns else if (evt.key=="ArrowDown") columns else if (evt.key=="ArrowLeft") -1 else 1,
            edge:if (evt.key=="Home") "first" else if (evt.key=="End") "last" else null})
        return 'prevent-default'
    }
    return 'pass'
}
pub fn dialog(d) => apply(<word_file_picker dialog:d>)
pub pn request(node,event="word_picker_ready") { dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),event) }
pub pn focus(d,node) {
    let root = dom.root_node(node)
    dom.focus_set(dom.get_element_by_id(root,if (d.loading==true) "word-picker-cancel" else if (d.go) "word-picker-location" else "word-picker-files"),false)
    let row = dom.query_selector(root,".picker-file[aria-selected=true]")
    if (row!=null) dom.scroll_into_view_options(row,{block:"nearest",inline:"nearest"})
}

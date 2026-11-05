import model: .mod_model
import dom
import icons: lambda.ui.workspace.file_icons

// native scroll does not bubble; notify the retained Finder owner directly.
view <finder_scrollport> { ~.rendered }
on scroll(evt) {
    dom.request_frame(dom.get_element_by_id(dom.root_node(evt.target),"finder"),"finder_measure")
    return 'handled'
}

pub let icon = icons.icon
pub let file_icon = icons.file_icon
pub fn button(id,title,graphic) => <button id:id,type:"button",class:"tool",'data-action':id,title:title,'aria-label':title,(icon(graphic))>
// filesystem thumbnail jobs cannot consume captured archive members; decode only the selected preview.
fn raster(entry) => entry.kind == "Image" and entry.extension != "svg" and entry.readonly != true
fn graphic(entry,role="tile") => <span class:if (raster(entry)) "file-graphic thumbnail-slot" else "file-graphic",
    *:(if (raster(entry))
        {'data-thumb-path':entry.path,'data-thumb-role':role,'data-thumb-state':"pending"} else {}),
    (if (entry.kind == "Image" and entry.extension == "svg" and entry.readonly != true) <img class:"vector-thumbnail",src:entry.path,alt:"">
    else (file_icon(entry)));
    (if (raster(entry)) <canvas class:"thumbnail-canvas",width:"1",height:"1",'aria-hidden':"true"> else null)>
fn file_rows(st,prefix="file-",column=-1) any^ {
    let rows = model.visible(st)
    let window = model.window(st);
    if (len(rows)==0) <div class:"empty",(icon("search"));<h2 if (trim(st.query)!="") "No matching files" else "This folder is empty">
        <p if (trim(st.query)!="") "Try another name or clear your search." else "There’s room for something new.">>
    else <div class:"listing-window",'data-total':string(len(rows)),
        <div class:"listing-spacer",'aria-hidden':"true",style:"height:" ++ string(window.before) ++ "px">;
        <div class:(if (st.mode == "icons") "icon-grid" else if (st.mode == "columns") "column-rows" else "file-list") ++ " listing-rows",
        *[for (offset,e in ((slice(rows,window.first,window.last)) ^ { [] })) <button id:prefix ++ string(window.first+offset),class:"file",type:"button",role:"option",tabindex:"-1",
            *:(if (column>=0) {'data-column':string(column),'data-trail':string(column<len(st.columns)-1 and contains(st.selected,e.path))} else {}),
            'data-path':e.path,'data-name':e.name,'data-dir':string(e.dir),'data-readonly':string(e.readonly == true),draggable:string(e.readonly != true),
            'aria-selected':string(contains(st.selected,e.path)),'data-focused':string(e.path == st.focus),title:e.name,
            'aria-posinset':string(window.first+offset+1),'aria-setsize':string(len(rows)),
            'data-even':string((window.first+offset)%2 == 1),
            graphic(e);<span class:"file-name",e.name><span class:"file-date",e.modified>
            <span class:"file-size",if (e.dir) "—" else model.size_label(e.size)><span class:"file-kind",e.kind>;
            (if (st.mode == "columns" and model.browsable(e)) <span class:"folder-chevron",'aria-hidden':"true","›"> else null)>]>;
        <div class:"listing-spacer",'aria-hidden':"true",style:"height:" ++ string(window.after) ++ "px">>
}
pub fn column_files(st,index) any^ => file_rows(model.column(st,index),"file-" ++ string(index) ++ "-",index)
pub fn column(st,index) => <section id:"column-" ++ string(index),class:"file-column",'data-folder':st.columns[index].path,'data-column':string(index),
    'aria-label':model.basename(st.columns[index].path),'data-readonly':string(model.read_only(model.column(st,index))),
    <h2 class:"column-heading",title:st.columns[index].path,model.basename(st.columns[index].path)>;
    <div id:"column-files-" ++ string(index),class:"column-list",role:"group",'aria-label':st.columns[index].path,
        column_files(st,index)>>
pub fn files(st) any^ => if (st.mode == "columns") <div class:"columns",*[for (i,c in st.columns) column(st,i)]> else file_rows(st)
pub fn breadcrumb(st,destination=false) any^ => <nav 'aria-label':if (destination) "Destination folder path" else "Folder path",class:"crumbs",
    *[for (i,c in ((model.crumbs(st.path)) ^ { [] })) <span
        (if (i>0) <span class:"crumb-separator","›"> else null);
        <button type:"button",*:(if (destination) {'data-destination':c.path} else
            {'data-location':c.path,'data-readonly':string(model.location_readonly(st,c.path))}),title:c.path,c.name>>]>
pub fn destination_folders(picker) any^ => if (picker == null) <div class:"destination-empty","Enter a folder path and choose Browse.">
    else if (len(picker.entries)==0) <div class:"destination-empty","This folder has no subfolders.">
    else [for (entry in picker.entries) <button type:"button",class:"destination-folder",'data-destination':entry.path,
        title:if (entry.blocked) "An item cannot be moved into itself or its contents." else entry.path,
        *:(if (entry.blocked) {disabled:""} else {}),
        (icon("folder"));<span entry.name><span class:"folder-chevron",'aria-hidden':"true","›">>]
fn metadata(entry) => <dl class:"metadata",
    *[for (key,value in {Kind:entry.kind,Size:if (entry.dir) "—" else model.size_label(entry.size),Modified:entry.modified,Where:model.parent(entry.path)})
        <div <dt string(key)><dd value>>]>
pub fn menu(st) => [for (action,caption in {open:"Open",look:"Quick Look",rename:"Rename…",move:"Move to…",delete:"Delete…",
    'new-file':"New File…",'new-folder':"New Folder…",refresh:"Refresh"})
    <button id:"menu-" ++ string(action),type:"button",role:"menuitem",'data-action':string(action),
        class:if (action == 'delete') "menu-item danger-text" else "menu-item",
        *:(if ((contains(['open','look','move','delete'],action) and len(st.selected)==0) or
            (action == 'rename' and len(st.selected)!=1)) {disabled:""} else {}),caption>]
pub fn preview(st,large=false) any^ {
    let e = model.preview_entry(st)
    let data = st.preview_data;
    if (e == null) <div class:"preview-empty",(icon("preview"));<h3 "A closer look"><p "Select a file to see its preview and details.">>
    else <div class:if (large) "preview-content large" else "preview-content",
        <div class:if (data.kind == "document") "preview-media document-preview" else "preview-media",
            (if (data.kind == "image" and (e.extension == "svg" or data.inline == true)) <img src:data.src,alt:e.name>
            else if (data.kind == "image") (graphic(e,if (large) "look" else "preview"))
            else if (data.kind == "document") <iframe id:if (large) "look-document" else "preview-document",
                class:"document-frame",*:(if (data.html != null) {srcdoc:data.html} else {src:data.src}),title:e.name,frameborder:"0">
            else if (data.kind == "text") <pre data.text>
            else <div class:"generic-preview",(file_icon(e));<p if (data.kind == "folder") "Folder" else data.message or "Preview unavailable.">>)>;
        <div class:"preview-description",<h2 e.name><p class:"preview-subtitle",e.kind>;
            (if (data.truncated == true) <p class:"preview-note","Showing the first 6,000 characters."> else null);
            (if (data.kind == "error") <p class:"preview-note",data.message> else null);
            (metadata(e));
            (if (model.browsable(e) and e.path != st.path) <button type:"button",class:"primary",'data-action':"open",
                if (model.archive(e)) "Open archive" else "Open folder"> else null)>
    >
}
pub fn tree(st,places) => <main id:"finder",'data-mode':st.mode,'data-path':st.path,'data-selected':"0",'data-hidden':"false",
    <aside class:"sidebar",
        <div class:"window-controls",'aria-hidden':"true",<span class:"red"><span class:"yellow"><span class:"green">>;
        <div class:"sidebar-brand",(icon("folder"));<strong "Finder"><small "LAMBDA">>;
        <h2 class:"sidebar-heading","Favorites">;
        <nav 'aria-label':"Favorite folders",*[for (p in places) <button id:"place-" ++ p.id,class:"place",type:"button",
            'data-location':p.path,title:p.path,'aria-current':if (p.path == st.path) "page" else "false",
            (icon(p.icon));<span p.name>>]>;
        <div class:"sidebar-bottom",<span class:"connection-dot"><span "On your computer"><small "Browse · Discover · Preview">>
    >;
    <section class:"workspace",
        <header class:"toolbar workspace-chrome",
            <div class:"navigation",(button("back","Back (⌘[)","back"));(button("forward","Forward (⌘])","forward"))>;
            <div class:"folder-heading",<h1 id:"folder-title",if (st.path == places[0].path) "Demo Files" else model.basename(st.path)><p id:"folder-subtitle","Your files, a little more familiar.">>;
            <div class:"toolbar-actions",
                <div class:"segmented",(button("icons","Icon view (⌘1)","icons"));(button("list","List view (⌘2)","list"));
                    (button("columns","Column view (⌘3)","columns"))>
                ;(button("refresh","Refresh (⌘R)","refresh"));(button("go","Go to Folder (⌘⇧G)","go"));
                (button("preview","Toggle preview pane","preview"));(button("new-folder","New Folder (⌘⇧N)","newfolder"));
                <button id:"actions",type:"button",class:"tool",'data-action':"actions",title:"File actions",'aria-label':"File actions",
                    'aria-haspopup':"menu",'aria-expanded':"false",(icon("actions"))>>;
            <label class:"search",(icon("search"));<input id:"search",type:"search",placeholder:"Search this folder",'aria-label':"Search this folder">>
        >;
        <div class:"location-bar",<div id:"breadcrumbs",(breadcrumb(st))>;
            <div class:"view-options",<label 'for':"sort","Sort by">
                <select id:"sort",'aria-label':"Sort files",*[for (key,caption in {name:"Name",kind:"Kind",modified:"Date modified",size:"Size"}) <option value:string(key),caption>]>;
                <button id:"direction",class:"text-tool",type:"button",'data-action':"direction",title:"Reverse sort order",'aria-label':"Reverse sort order","↑">>
        >;
        <div id:"error",class:"error",role:"alert",style:"display:none",<span id:"error-text">
            <button type:"button",'data-action':"dismiss",'aria-label':"Dismiss error",(icon("close"))>>;
        <div class:"content",id:"content",
            <section class:"browser",'aria-label':"Folder contents",
                <div class:"list-heading",id:"list-heading",style:"display:none",<span "Name"><span "Date modified"><span "Size"><span "Kind">>;
                apply(<finder_scrollport rendered:<div id:"files",tabindex:"0",role:"listbox",'aria-label':"Files",'aria-multiselectable':"true",(files(st))>>)
            >;
            <div id:"preview-divider",class:"preview-divider",role:"separator",tabindex:"0",'aria-label':"Resize preview panel",
                'aria-controls':"preview-pane",'aria-orientation':"vertical",'aria-valuemin':"180",'aria-valuemax':"600",'aria-valuenow':"260">;
            <aside id:"preview-pane",class:"preview-pane",'aria-label':"File preview",<div class:"preview-heading",<span "PREVIEW">
                <button type:"button",class:"text-tool",'data-action':"look","Quick Look ↗">>;
                <div id:"preview-body",(preview(st))>>
        >;
        <footer class:"statusbar",<span id:"status",role:"status",'aria-live':"polite",model.status(st)>
            <div <button id:"hidden",type:"button",'data-action':"hidden",'aria-pressed':"false","Show hidden files">
                <span class:"status-hint","Space for Quick Look">>>
    >;
    <div id:"context-menu",class:"context-menu",style:"display:none",role:"menu",'aria-label':"File actions",(menu(st))>;
    <div id:"operation-dialog",class:"overlay operation-overlay",style:"display:none",role:"dialog",'aria-modal':"true",'aria-labelledby':"operation-title",
        <div class:"go-card",<h2 id:"operation-title">;<p id:"operation-description">;
            <div id:"operation-field",<label 'for':"operation-value",id:"operation-label","Name">
                <input id:"operation-value",type:"text",autocomplete:"off">>;
            <div id:"move-picker",class:"folder-picker",style:"display:none",role:"group",'aria-label':"Destination folder browser",
                <div class:"destination-toolbar",
                    <button id:"destination-parent",type:"button",class:"text-tool",'data-action':"destination-parent",'aria-label':"Parent destination folder",(icon("up"))>;
                    <button type:"button",class:"text-tool",'data-action':"destination-home","Home">;
                    <button id:"destination-current",type:"button",class:"text-tool",'data-action':"destination-current","Current folder">;
                    <button id:"destination-browse",type:"button",class:"text-tool",'data-action':"destination-browse","Browse path">>;
                <div id:"destination-breadcrumbs",class:"destination-breadcrumbs">;
                <div id:"destination-folders",class:"destination-folders",tabindex:"0",'aria-label':"Subfolders">>;
            <p id:"operation-error",class:"dialog-error",role:"alert">;
            <div class:"dialog-actions",<button id:"operation-cancel",type:"button",'data-action':"operation-cancel","Cancel">
                <button id:"operation-confirm",type:"button",class:"primary",'data-action':"operation-confirm","Create">>>>;
    <div id:"go-dialog",class:"overlay",style:"display:none",role:"dialog",'aria-modal':"true",'aria-labelledby':"go-title",
        <div class:"go-card",<div class:"dialog-heading",(icon("go"));<h2 id:"go-title","Go to Folder">>;
            <p "Where would you like to go?">;
            <label 'for':"folder-path","Folder path">;<input id:"folder-path",type:"text",placeholder:"~/Documents",autocomplete:"off">;
            <p class:"path-help","Use / for an absolute path, ~ for home, or a relative path.">;
            <p id:"go-error",class:"dialog-error",role:"alert">;
            <div class:"dialog-actions",<button id:"go-cancel",type:"button",'data-action':"cancel","Cancel">
                <button id:"go-confirm",class:"primary",type:"button",'data-action':"go-confirm","Go">>>>;
    <div id:"quick-look",class:"overlay",style:"display:none",role:"dialog",'aria-modal':"true",'aria-labelledby':"look-title",
        <div class:"look-card",<header <h2 id:"look-title","Quick Look">
            <button id:"look-close",type:"button",class:"tool",'data-action':"close-look",'aria-label':"Close Quick Look",(icon("close"))>>;
            <div id:"look-body">>>
>

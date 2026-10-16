import model: .mod_model
import dom

// native scroll does not bubble; notify the retained Finder owner directly.
view <finder_scrollport> { ~.rendered }
on scroll(evt) {
    dom.request_frame(dom.get_element_by_id(dom.root_node(evt.target),"finder"),"finder_measure")
    return 'handled'
}

let strokes = {
    back:"M15 5l-7 7 7 7",forward:"M9 5l7 7-7 7",up:"M5 14l7-7 7 7",
    icons:"M3 3h6v6H3z M15 3h6v6h-6z M3 15h6v6H3z M15 15h6v6h-6z",
    list:"M8 5h13 M8 12h13 M8 19h13 M3 5h1 M3 12h1 M3 19h1",
    columns:"M3 4h18v16H3z M9 4v16 M15 4v16",
    actions:"M5 12h1 M11 12h1 M17 12h1",newfolder:"M3 7V4h7l3 3h8v13H3z M12 10v7 M8.5 13.5h7",
    preview:"M3 4h18v16H3z M15 4v16",search:"M10 3a7 7 0 1 0 0 14 7 7 0 0 0 0-14 M15 15l6 6",
    refresh:"M20 9a8 8 0 1 0 0 6 M20 3v6h-6",go:"M3 8h18v12H3z M3 8V5h7l3 3 M12 11v6 M9 14l3 3 3-3",
    home:"M3 10l9-7 9 7 M5 9v12h14V9 M9 21v-8h6v8",
    desktop:"M3 4h18v13H3z M12 17v4 M8 21h8",
    downloads:"M12 3v12 M7 10l5 5 5-5 M4 17v4h16v-4",
    documents:"M6 3h8l4 4v14H6z M14 3v5h4 M9 12h6 M9 16h6",
    pictures:"M3 4h18v16H3z M3 17l6-7 5 5 3-3 4 4 M15 8h1",
    folder:"M3 7V4h7l3 3h8v13H3z",computer:"M3 4h18v13H3z M8 21h8 M12 17v4",
    eye:"M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0",
    close:"M6 6l12 12 M18 6L6 18"
}
pub fn icon(graphic) => <svg width:"20",height:"20",viewBox:"0 0 24 24",'aria-hidden':"true",
    <path d:strokes[graphic] or strokes.folder,fill:"none",stroke:"currentColor",'stroke-width':1.6,
        'stroke-linecap':"round",'stroke-linejoin':"round">>
pub fn file_icon(entry) => <svg class:"file-icon",width:"76",height:"68",viewBox:"0 0 80 72",'aria-hidden':"true",
    (if (entry.dir) <g
        <path d:"M5 16Q5 11 10 11H30L37 18H70Q75 18 75 23V61Q75 65 70 65H10Q5 65 5 60Z",fill:"#48a6df">
        <path d:"M5 26Q5 22 10 22H70Q75 22 75 27V60Q75 65 70 65H10Q5 65 5 60Z",fill:"#80cef5">
        <path d:"M10 24H70",stroke:"#b7e5fc",'stroke-width':1.5>
    > else <g
        <path d:"M19 6H49L63 20V63Q63 67 59 67H21Q17 67 17 63V10Q17 6 19 6Z",fill:"#ffffff",stroke:"#d4d9df",'stroke-width':1.2>
        <path d:"M49 6V20H63",fill:"#edf0f4",stroke:"#d4d9df",'stroke-width':1.2>
        <path d:"M26 30H53 M26 36H53 M26 42H47",stroke:"#dce1e7",'stroke-width':2.5>
        <rect x:"23",y:"48",width:"35",height:"13",rx:"3",fill:if (entry.kind == "Image") "#82aa98" else if (entry.kind == "Source code") "#9b90c5" else "#819bb9">
        <text x:"40",y:"57",'text-anchor':"middle",'font-family':"Arial, sans-serif",'font-size':"7",'font-weight':"bold",fill:"#ffffff",
            upper(slice(entry.extension or "FILE",0,5))>
    >)>
pub fn button(id,title,graphic) => <button id:id,type:"button",class:"tool",'data-action':id,title:title,'aria-label':title,(icon(graphic))>
// filesystem thumbnail jobs cannot consume captured archive members; decode only the selected preview.
fn raster(entry) => entry.kind == "Image" and entry.extension != "svg" and entry.readonly != true
fn graphic(entry,role="tile") => <span class:if (raster(entry)) "file-graphic thumbnail-slot" else "file-graphic",
    *:(if (raster(entry))
        {'data-thumb-path':entry.path,'data-thumb-role':role,'data-thumb-state':"pending"} else {}),
    (if (entry.kind == "Image" and entry.extension == "svg" and entry.readonly != true) <img class:"vector-thumbnail",src:entry.path,alt:"">
    else (file_icon(entry)));
    (if (raster(entry)) <canvas class:"thumbnail-canvas",width:"1",height:"1",'aria-hidden':"true"> else null)>
fn file_rows(st,prefix="file-",column=-1) {
    let rows = model.visible(st)
    let window = model.window(st);
    if (len(rows)==0) <div class:"empty",(icon("search"));<h2 if (trim(st.query)!="") "No matching files" else "This folder is empty">
        <p if (trim(st.query)!="") "Try another name or clear your search." else "There’s room for something new.">>
    else <div class:"listing-window",'data-total':string(len(rows)),
        <div class:"listing-spacer",'aria-hidden':"true",style:"height:" ++ string(window.before) ++ "px">;
        <div class:(if (st.mode == "icons") "icon-grid" else if (st.mode == "columns") "column-rows" else "file-list") ++ " listing-rows",
        *[for (offset,e in slice(rows,window.first,window.last)) <button id:prefix ++ string(window.first+offset),class:"file",type:"button",role:"option",tabindex:"-1",
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
pub fn column_files(st,index) => file_rows(model.column(st,index),"file-" ++ string(index) ++ "-",index)
pub fn column(st,index) => <section id:"column-" ++ string(index),class:"file-column",'data-folder':st.columns[index].path,'data-column':string(index),
    'aria-label':model.basename(st.columns[index].path),'data-readonly':string(model.read_only(model.column(st,index))),
    <h2 class:"column-heading",title:st.columns[index].path,model.basename(st.columns[index].path)>;
    <div id:"column-files-" ++ string(index),class:"column-list",role:"group",'aria-label':st.columns[index].path,
        column_files(st,index)>>
pub fn files(st) => if (st.mode == "columns") <div class:"columns",*[for (i,c in st.columns) column(st,i)]> else file_rows(st)
pub fn breadcrumb(st,destination=false) => <nav 'aria-label':if (destination) "Destination folder path" else "Folder path",class:"crumbs",
    *[for (i,c in model.crumbs(st.path)) <span
        (if (i>0) <span class:"crumb-separator","›"> else null);
        <button type:"button",*:(if (destination) {'data-destination':c.path} else
            {'data-location':c.path,'data-readonly':string(model.location_readonly(st,c.path))}),title:c.path,c.name>>]>
pub fn destination_folders(picker) => if (picker == null) <div class:"destination-empty","Enter a folder path and choose Browse.">
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
pub fn preview(st,large=false) {
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
        <header class:"toolbar",
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

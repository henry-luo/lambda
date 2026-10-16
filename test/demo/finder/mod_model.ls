// S1.4 / S12.1.3: filesystem snapshots and session transitions are immutable values.
fn path_parts(parts, index, kept) => if (index >= len(parts)) kept else (
    let part = parts[index],
    path_parts(parts,index+1,if (part == "" or part == ".") kept
        else if (part == "..") slice(kept,0,max(0,len(kept)-1)) else [*kept,part]))
pub fn absolute(path, base, home="/") {
    let expanded = if (path == "~") home else if (starts_with(path,"~/")) home ++ slice(path,1) else path
    let rooted = if (starts_with(expanded,"/")) expanded else base ++ "/" ++ expanded;
    "/" ++ join(path_parts(split(rooted,"/"),0,[]),"/")
}
pub fn basename(path) => if (path == "/") "Computer" else split(path,"/")[last]
pub fn parent(path) => absolute("..",path)
pub fn child_path(folder,filename) => (if (folder == "/") "/" else folder ++ "/") ++ filename
pub fn crumbs(path) {
    let parts = [for (p in split(path,"/") where p != "") p];
    [{name:"Computer",path:"/"},*[for (i,p in parts) {name:p,path:"/" ++ join(slice(parts,0,i+1),"/")}]]
}
pub fn kind(extension, dir=false) => if (dir) "Folder"
    else if (contains(["png","jpg","jpeg","gif","webp","svg","avif","bmp","tif","tiff","ico"],extension)) "Image"
    else if (contains(["md","markdown","txt","log","csv"],extension)) "Text document"
    else if (contains(["ls","js","ts","cpp","c","h","hpp","py","css","html","sh"],extension)) "Source code"
    else if (contains(["json","yaml","yml","toml","xml"],extension)) "Data file"
    else if (extension == "pdf") "PDF document"
    else if (contains(["zip","gz","tar","jar"],extension)) "Archive"
    else if (contains(["mp3","wav","flac","m4a"],extension)) "Audio"
    else if (contains(["mp4","mov","webm"],extension)) "Video" else "Document"
pub fn size_label(bytes) => if (bytes == null) "—" else if (bytes < 1024) string(bytes) ++ " bytes"
    else if (bytes < 1048576) string(round(bytes/102.4)/10) ++ " KB"
    else if (bytes < 1073741824) string(round(bytes/104857.6)/10) ++ " MB"
    else string(round(bytes/107374182.4)/10) ++ " GB"
pub fn initial(path) => {path:path,entries:[],columns:[{path:path,entries:[],query:""}],history:[path],cursor:0,query:"",sort:"name",descending:false,
    mode:"icons",hidden:false,preview:true,selected:[],focus:"",anchor:"",look:false,dialog:false,
    error:"",preview_data:null,menu:null,operation:null,drag:null,thumbnails:{frame:0,job:0,request:null,generation:0}}
fn sort_key(entry, key) => if (key == "size") entry.size else if (key == "modified") entry.modified_raw or entry.modified
    else if (key == "kind") entry.kind else lower(entry.name)
pub fn visible(st) {
    let matching = [for (e in st.entries where (st.hidden or not starts_with(e.name,".")) and
        contains(lower(e.name),lower(trim(st.query))) order by lower(e.name)) e]
    let ordered = sort(matching,{dir:if (st.descending) 'desc' else 'asc',by:(e)=>sort_key(e,st.sort)});
    [*[for (e in ordered where e.dir) e],*[for (e in ordered where not e.dir) e]]
}
pub fn reconcile(st) {
    let paths = [for (e in visible(st)) e.path]
    let selected = [for (p in st.selected where contains(paths,p)) p];
    {*:st,selected:selected,focus:if (contains(selected,st.focus)) st.focus else if (len(selected)>0) selected[last] else "",
        anchor:if (contains(paths,st.anchor)) st.anchor else "",preview_data:null,look:false}
}
pub fn listing(st,path,entries,cursor=-1) {
    let record = cursor < 0 and path != st.path
    let history = if (record) [*slice(st.history,0,st.cursor+1),path] else st.history
    // keep snapshots only along the visited branch; unrelated destinations start a new column root.
    let columns = [for (c in st.columns) if (c.path == st.path) {*:c,entries:st.entries,query:st.query} else c]
    let paths = [for (c in columns) c.path]
    let at = position(paths,path)
    let ancestor = position(paths,parent(path))
    let kept = if (at>=0) slice(columns,0,at) else if (ancestor>=0) slice(columns,0,ancestor+1) else [];
    {*:st,path:path,entries:entries,history:history,cursor:if (record) len(history)-1 else if (cursor>=0) cursor else st.cursor,
        columns:[*kept,{path:path,entries:entries,query:""}],
        query:"",selected:[],focus:"",anchor:"",preview_data:null,look:false,dialog:false,menu:null,operation:null,drag:null,error:""}
}
pub fn column(st,index) {
    let snapshot = st.columns[index];
    if (index == len(st.columns)-1) st else {*:st,path:snapshot.path,entries:snapshot.entries,query:snapshot.query,
        selected:[st.columns[index+1].path],focus:"",anchor:""}
}
pub fn activate_column(st,index) {
    if (index<0 or index>=len(st.columns) or index == len(st.columns)-1) st else (
        let snapshot = st.columns[index],
        {*:listing(st,snapshot.path,snapshot.entries),query:snapshot.query})
}
pub fn previous_column(st) {
    let path = st.path
    let previous = activate_column(st,len(st.columns)-2);
    if (previous.path == path) st else choose(previous,path)
}
pub fn choose(st,path,toggle=false,extend=false) {
    let paths = [for (e in visible(st)) e.path]
    let at = position(paths,path)
    let anchor = position(paths,st.anchor)
    let selected = if (at < 0) st.selected
        else if (extend and anchor >= 0) slice(paths,min(at,anchor),max(at,anchor)+1)
        else if (toggle) (if (contains(st.selected,path)) [for (p in st.selected where p != path) p] else [*st.selected,path])
        else [path];
    {*:st,selected:selected,focus:if (contains(selected,path)) path else if (len(selected)>0) selected[last] else "",
        anchor:if (extend and anchor >= 0) st.anchor else path,preview_data:null,error:""}
}
pub fn move(st,delta,extend=false) {
    let rows = visible(st)
    let at = position([for (e in rows) e.path],st.focus)
    let next = if (at < 0) (if (delta < 0) len(rows)-1 else 0) else max(0,min(len(rows)-1,at+delta));
    if (len(rows)==0) st else choose(st,rows[next].path,false,extend)
}
pub fn focused(st) => [for (e in st.entries where e.path == st.focus) e][0]
pub fn valid_name(value) => value is string and trim(value) != "" and value != "." and value != ".." and
    not contains(value,"/") and not contains(value,"\\") and not contains(value,"\u0000")
pub fn inside(path:string,folder:string) => if (folder == "/") starts_with(path,"/")
    else path == folder or slice(path,0,len(folder)+1) == folder ++ "/"
pub fn remap(path,changes,index=0) => if (index>=len(changes)) path else (
    let change = changes[index],
    remap(if (inside(path,change.from)) change.to ++ slice(path,len(change.from)) else path,changes,index+1))
pub fn relocated(st,changes) {
    let columns = [for (c in st.columns) {*:c,path:remap(c.path,changes)}]
    // moving an open ancestor can break the displayed chain; retain the contiguous suffix containing the current folder.
    let breaks = [for (i,c in columns where i>0 and parent(c.path) != columns[i-1].path) i]
    let first_column = if (len(breaks)>0) breaks[last] else 0;
    {*:st,path:remap(st.path,changes),columns:slice(columns,first_column),history:[for (p in st.history) remap(p,changes)],
        selected:[for (p in st.selected) remap(p,changes)],focus:remap(st.focus,changes),anchor:remap(st.anchor,changes),preview_data:null}
}
pub fn position(values,value) {
    let found = index_of(values,value);
    if (found == null) -1 else found
}
pub fn status(st) {
    let total = len(visible(st));
    string(total) ++ (if (total==1) " item" else " items") ++
        (if (len(st.selected)>0) " · " ++ string(len(st.selected)) ++ " selected" else "") ++
        (if (trim(st.query)!="") " · filtered by “" ++ st.query ++ "”" else "")
}

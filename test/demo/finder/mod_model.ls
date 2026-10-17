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
// navigation capability is separate from physical file kind and write capability.
pub fn archive(entry) => entry != null and not entry.dir and (entry.archive == true or lower(entry.extension or "") == "zip")
pub fn browsable(entry) => entry != null and (entry.dir or archive(entry))
pub fn read_only(st) => st.container.locator != null
pub fn location_readonly(st,path) => any([for (location in st.locations or []) location.path == path])
pub fn writable(st,action) => if (contains(["new-file","new-folder"],action)) not read_only(st)
    else not any([for (e in st.entries where contains(st.selected,e.path)) e.readonly == true])
// the native document loader owns parsing and presentation for these viewer formats.
pub fn viewable(extension) => contains(["html","htm","xhtml","xht","md","markdown","tex","latex","pgf",
    "pdf","docx","epub","psd","diff","patch","wiki","xml","fo","xslfo","rpd","ls","slides",
    "dot","gv","d2","mmd","dsl","structurizr","eps","epsi","json","yaml","yml","toml",
    "txt","csv","ini","conf","cfg","log"],lower(extension or "")) ^ { false }
pub fn preview_limits(available) {
    // reserve 160px for the file list and 6px for the divider.
    let maximum = max(0,min(600,available-166));
    {min:min(180,maximum),max:maximum}
}
pub fn preview_size(width,available) {
    let limits = preview_limits(available);
    max(limits.min,min(width,limits.max))
}
pub fn size_label(bytes) => if (bytes == null) "—" else if (bytes < 1024) string(bytes) ++ " bytes"
    else if (bytes < 1048576) string(round(bytes/102.4)/10) ++ " KB"
    else if (bytes < 1073741824) string(round(bytes/104857.6)/10) ++ " MB"
    else string(round(bytes/107374182.4)/10) ++ " GB"
pub fn initial(path) => {path:path,entries:[],revision:0,serial:0,container:null,archive_cache:null,locations:[],viewports:null,row_cache:[],reveal:false,measure_frame:0,
    columns:[{path:path,entries:[],query:"",container:null,revision:0}],history:[path],cursor:0,query:"",sort:"name",descending:false,
    mode:"icons",hidden:false,preview:true,preview_width:null,preview_drag:null,selected:[],focus:"",anchor:"",look:false,dialog:false,
    error:"",preview_data:null,menu:null,operation:null,drag:null,thumbnails:{frame:0,job:0,request:null,generation:0}}
fn sort_key(entry, key) => if (key == "size") entry.size else if (key == "modified") entry.modified_raw or entry.modified
    else if (key == "kind") entry.kind else lower(entry.name)
// snapshot revisions avoid comparing native archive nodes, whose equality can force member content.
fn visible_key(st) => {revision:st.revision,query:st.query,sort:st.sort,descending:st.descending,hidden:st.hidden}
fn ordered_rows(st) {
    let matching = [for (e in st.entries where (st.hidden or not starts_with(e.name,".")) and
        contains(lower(e.name),lower(trim(st.query))) order by lower(e.name)) e]
    let ordered = sort(matching,{dir:if (st.descending) 'desc' else 'asc',by:(e)=>sort_key(e,st.sort)});
    [*[for (e in ordered where browsable(e)) e],*[for (e in ordered where not browsable(e)) e]]
}
pub fn visible(st) {
    let cached = [for (c in st.row_cache or [] where c.path == st.path and c.key == visible_key(st)) c][0];
    if (cached == null) ordered_rows(st) else cached.rows
}
pub fn prepare(st) => {*:st,row_cache:[for (i,c in st.columns) (
    let part = column(st,i), let key = visible_key(part),
    {path:part.path,key:key,rows:visible(part)})]}
pub fn window(st) {
    let rows = visible(st)
    let count = len(rows)
    let exact = [for (v in st.viewports or [] where v.path == st.path and v.mode == st.mode) v][0]
    let metrics = exact or [for (v in st.viewports or [] where v.mode == st.mode) v][0];
    if (st.viewports == null) {first:0,last:count,before:0,after:0}
    else if (metrics == null or metrics.row_height<=0) {first:0,last:min(1,count),before:0,after:0}
    else {
        let columns = metrics.columns
        let stride = metrics.row_height+metrics.gap
        let total = int(ceil(count/columns))
        let focus = position([for (e in rows) e.path],st.focus)
        let focus_top = floor(focus/columns)*stride+metrics.padding
        let old_top = if (exact == null) 0 else metrics.top
        let top = if (st.reveal and focus>=0 and focus_top<old_top) focus_top
            else if (st.reveal and focus>=0 and focus_top+metrics.row_height>old_top+metrics.height)
                focus_top+metrics.row_height-metrics.height else old_top
        // one surrounding row keeps ordinary wheel motion within the rendered window.
        let first_row = max(0,min(total-1,int(floor((top-metrics.padding)/stride))-1))
        let end_row = max(first_row,min(total,int(ceil((top+metrics.height-metrics.padding)/stride))+1));
        {first:first_row*columns,last:min(count,end_row*columns),before:first_row*stride,after:max(0,total-end_row)*stride}
    }
}
pub fn reconcile(st) {
    let paths = [for (e in visible(st)) e.path]
    let selected = [for (p in st.selected where contains(paths,p)) p];
    {*:st,selected:selected,focus:if (contains(selected,st.focus)) st.focus else if (len(selected)>0) selected[last] else "",
        anchor:if (contains(paths,st.anchor)) st.anchor else "",preview_data:null,look:false}
}
pub fn listing(st,path,entries,cursor=-1,container=null,revision=-1) {
    let version = if (revision>=0) revision else st.serial+1
    let record = cursor < 0 and path != st.path
    let history = if (record) [*slice(st.history,0,st.cursor+1),path] else st.history
    // keep snapshots only along the visited branch; unrelated destinations start a new column root.
    let columns = [for (c in st.columns) if (c.path == st.path) {*:c,entries:st.entries,query:st.query,container:st.container,revision:st.revision} else c]
    let paths = [for (c in columns) c.path]
    let at = position(paths,path)
    let ancestor = position(paths,parent(path))
    let kept = if (at>=0) slice(columns,0,at) else if (ancestor>=0) slice(columns,0,ancestor+1) else [];
    // history keeps small locators; only the active archive/branch retains captured member nodes.
    let locations = [for (location in st.locations where location.path != path and contains(history,location.path)) location];
    {*:st,path:path,entries:entries,container:container,revision:version,serial:max(st.serial,version),
        archive_cache:if (container.locator == null) null else st.archive_cache,
        locations:[*locations,*if (container.locator != null) [{path:path,locator:container.locator}] else []],
        history:history,cursor:if (record) len(history)-1 else if (cursor>=0) cursor else st.cursor,
        columns:[*kept,{path:path,entries:entries,query:"",container:container,revision:version}],
        query:"",selected:[],focus:"",anchor:"",reveal:false,preview_data:null,look:false,dialog:false,menu:null,operation:null,drag:null,error:""}
}
pub fn column(st,index) {
    let snapshot = st.columns[index];
    if (index == len(st.columns)-1) st else {*:st,path:snapshot.path,entries:snapshot.entries,query:snapshot.query,container:snapshot.container,revision:snapshot.revision,
        selected:[st.columns[index+1].path],focus:"",anchor:""}
}
pub fn activate_column(st,index) {
    if (index<0 or index>=len(st.columns) or index == len(st.columns)-1) st else (
        let snapshot = st.columns[index],
        {*:listing(st,snapshot.path,snapshot.entries,-1,snapshot.container,snapshot.revision),query:snapshot.query})
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
        anchor:if (extend and anchor >= 0) st.anchor else path,reveal:true,preview_data:null,error:""}
}
pub fn move(st,delta,extend=false) {
    let rows = visible(st)
    let at = position([for (e in rows) e.path],st.focus)
    let next = if (at < 0) (if (delta < 0) len(rows)-1 else 0) else max(0,min(len(rows)-1,at+delta));
    if (len(rows)==0) st else choose(st,rows[next].path,false,extend)
}
pub fn focused(st) => [for (e in st.entries where e.path == st.focus) e][0]
pub fn preview_entry(st) => if (st.focus != "") focused(st) else if (archive(st.container)) st.container else null
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
        archive_cache:null,locations:[for (location in st.locations) {*:location,path:remap(location.path,changes),
            locator:{*:location.locator,outer:remap(location.locator.outer,changes)}}],
        selected:[for (p in st.selected) remap(p,changes)],focus:remap(st.focus,changes),anchor:remap(st.anchor,changes),preview_data:null}
}
pub fn position(values,value) {
    let found = index_of(values,value);
    if (found == null) -1 else found
}
pub fn status(st) {
    let total = len(visible(st));
    string(total) ++ (if (total==1) " item" else " items") ++
        (if (read_only(st)) " · Archive contents (read-only)" else "") ++
        (if (len(st.selected)>0) " · " ++ string(len(st.selected)) ++ " selected" else "") ++
        (if (trim(st.query)!="") " · filtered by “" ++ st.query ++ "”" else "")
}

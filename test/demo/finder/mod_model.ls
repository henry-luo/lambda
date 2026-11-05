// S1.4 / S12.1.3: filesystem snapshots and session transitions are immutable values.
import paths: lambda.io.path
import file_info: lambda.io.file_info
import browser: lambda.ui.workspace.file_browser
pub fn absolute(path,base,home="/") any^ => paths.absolute(path,base,home)^
pub fn basename(path) any^ => browser.basename(path)^
pub fn parent(path) any^ => absolute("..",path)
pub fn child_path(folder,filename) => (if (folder == "/") "/" else folder ++ "/") ++ filename
pub fn crumbs(path) any^ => browser.crumbs(path)^
pub fn kind(extension,dir=false) => file_info.kind(extension,dir)
// navigation capability is separate from physical file kind and write capability.
pub let archive = file_info.archive
pub let browsable = file_info.browsable
pub fn read_only(st) => st.container.locator != null
pub fn location_readonly(st,path) => any([for (location in st.locations or []) location.path == path])
pub fn writable(st,action) any^ => if (contains(["new-file","new-folder"],action)) not read_only(st)
    else not any([for (e in st.entries where ((contains(st.selected,e.path)) ^ { false })) e.readonly == true])
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
pub fn size_label(bytes) any^ => browser.size_label(bytes)^
pub fn initial(path) => {*:browser.initial(path),container:null,archive_cache:null,locations:[],viewports:null,measure_frame:0,
    columns:[{path:path,entries:[],query:"",container:null,revision:0}],preview:true,preview_width:null,preview_drag:null,look:false,dialog:false,
    menu:null,operation:null,drag:null,thumbnails:{frame:0,job:0,request:null,generation:0}}
fn visible_key(st) => browser.visible_key(st)
pub fn visible(st) any^ => browser.visible(st)^
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
pub fn reconcile(st) any^ => browser.reconcile(st)^
pub fn listing(st,path,entries,cursor=-1,container=null,revision=-1) any^ {
    let base = browser.listing(st,path,entries,cursor,revision)^
    let version = base.revision
    let history = base.history
    // keep snapshots only along the visited branch; unrelated destinations start a new column root.
    let columns = [for (c in st.columns) if (c.path == st.path) {*:c,entries:st.entries,query:st.query,container:st.container,revision:st.revision} else c]
    let paths = [for (c in columns) c.path]
    let at = position(paths,path)
    let ancestor = position(paths,parent(path))
    let kept = if (at>=0) slice(columns,0,at) else if (ancestor>=0) slice(columns,0,ancestor+1) else [];
    // history keeps small locators; only the active archive/branch retains captured member nodes.
    let locations = [for (location in st.locations where ((location.path != path and contains(history,location.path)) ^ { false })) location];
    {*:base,container:container,
        archive_cache:if (container.locator == null) null else st.archive_cache,
        locations:[*locations,*if (container.locator != null) [{path:path,locator:container.locator}] else []],
        columns:[*kept,{path:path,entries:entries,query:"",container:container,revision:version}],
        query:"",selected:[],focus:"",anchor:"",reveal:false,preview_data:null,look:false,dialog:false,menu:null,operation:null,drag:null,error:""}
}
pub fn column(st,index) any^ {
    let snapshot = st.columns[index];
    if (index == len(st.columns)-1) st else {*:st,path:snapshot.path,entries:snapshot.entries,query:snapshot.query,container:snapshot.container,revision:snapshot.revision,
        selected:[st.columns[index+1].path],focus:"",anchor:""}
}
pub fn activate_column(st,index) any^ {
    if (index<0 or index>=len(st.columns) or index == len(st.columns)-1) st else (
        let snapshot = st.columns[index],
        {*:listing(st,snapshot.path,snapshot.entries,-1,snapshot.container,snapshot.revision),query:snapshot.query})
}
pub fn previous_column(st) any^ {
    let path = st.path
    let previous = activate_column(st,len(st.columns)-2);
    if (previous.path == path) st else choose(previous,path)
}
pub fn choose(st,path,toggle=false,extend=false) => browser.choose(st,path,toggle,extend)
pub fn move(st,delta,extend=false) any^ => browser.move(st,delta,extend)^
pub fn focused(st) => browser.focused(st)
pub fn preview_entry(st) any^ => if (st.focus != "") focused(st) else if (archive(st.container)) st.container else null
pub fn valid_name(value) any^ => value is string and trim(value) != "" and value != "." and value != ".." and
    not contains(value,"/") and not contains(value,"\\") and not contains(value,"\u0000")
pub fn inside(path:string,folder:string) any^ => if (folder == "/") starts_with(path,"/")
    else path == folder or slice(path,0,len(folder)+1) == folder ++ "/"
pub fn remap(path,changes,index=0) any^ => if (index>=len(changes)) path else (
    let change = changes[index],
    remap(if (inside(path,change.from)) change.to ++ slice(path,len(change.from)) else path,changes,index+1))
pub fn relocated(st,changes) any^ {
    let columns = [for (c in st.columns) {*:c,path:remap(c.path,changes)}]
    // moving an open ancestor can break the displayed chain; retain the contiguous suffix containing the current folder.
    let breaks = [for (i,c in columns where ((i>0 and parent(c.path) != columns[i-1].path) ^ { false })) i]
    let first_column = if (len(breaks)>0) breaks[last] else 0;
    {*:st,path:remap(st.path,changes),columns:slice(columns,first_column),history:[for (p in st.history) remap(p,changes)],
        archive_cache:null,locations:[for (location in st.locations) {*:location,path:remap(location.path,changes),
            locator:{*:location.locator,outer:remap(location.locator.outer,changes)}}],
        selected:[for (p in st.selected) remap(p,changes)],focus:remap(st.focus,changes),anchor:remap(st.anchor,changes),preview_data:null}
}
pub fn position(values,value) => browser.position(values,value)
pub fn status(st) any^ {
    let total = len(visible(st));
    string(total) ++ (if (total==1) " item" else " items") ++
        (if (read_only(st)) " · Archive contents (read-only)" else "") ++
        (if (len(st.selected)>0) " · " ++ string(len(st.selected)) ++ " selected" else "") ++
        (if (trim(st.query)!="") " · filtered by “" ++ st.query ++ "”" else "")
}

// Shared file-list ordering, selection and history; callers own providers and app chrome.
import file_info: lambda.io.file_info
import paths: lambda.io.path

pub fn absolute(path,base,home="/") any^ => paths.absolute(path,base,home)^
pub fn parent(path) any^ => absolute("..",path)^
pub fn basename(path) any^ => if (path == "/") "Computer" else split(path,"/")[last]
pub fn crumbs(path) any^ {
    let parts = [for (p in ((split(path,"/")) ^ { [] }) where p != "") p];
    [{name:"Computer",path:"/"},*[for (i,p in parts) {name:p,path:"/" ++ join(slice(parts,0,i+1),"/")}]]
}
pub fn size_label(bytes) any^ => if (bytes == null) "—" else if (bytes < 1024) string(bytes) ++ " bytes"
    else if (bytes < 1048576) string(round(bytes/102.4)/10) ++ " KB"
    else if (bytes < 1073741824) string(round(bytes/104857.6)/10) ++ " MB"
    else string(round(bytes/107374182.4)/10) ++ " GB"
pub fn initial(path) => {path:path,entries:[],revision:0,serial:0,row_cache:[],history:[path],cursor:0,
    query:"",sort:"name",descending:false,mode:"icons",hidden:false,selected:[],focus:"",anchor:"",reveal:false,preview_data:null,error:""}
fn sort_key(entry, key) => if (key == "size") entry.size else if (key == "modified") entry.modified_raw or entry.modified
    else if (key == "kind") entry.kind else lower(entry.name)
// snapshot revisions avoid comparing native archive nodes, whose equality can force member content.
pub fn visible_key(st) => {revision:st.revision,query:st.query,sort:st.sort,descending:st.descending,hidden:st.hidden}
fn ordered_rows(st) any^ {
    let matching = [for (e in st.entries where (((st.hidden or not starts_with(e.name,".")) and
        contains(lower(e.name),lower(trim(st.query)))) ^ { false }) order by lower(e.name)) e]
    let ordered = sort(matching,{dir:if (st.descending) 'desc' else 'asc',by:(e)any^ =>sort_key(e,st.sort)});
    [*[for (e in ordered where ((file_info.browsable(e)) ^ { false })) e],*[for (e in ordered where ((not file_info.browsable(e)) ^ { false })) e]]
}
pub fn visible(st) any^ {
    let cached = [for (c in st.row_cache or [] where c.path == st.path and c.key == visible_key(st)) c][0];
    if (cached == null) ordered_rows(st) else cached.rows
}
pub fn reconcile(st) any^ {
    let paths = [for (e in ((visible(st)) ^ { [] })) e.path]
    let selected = [for (p in st.selected where ((contains(paths,p)) ^ { false })) p];
    {*:st,selected:selected,focus:if (contains(selected,st.focus)) st.focus else if (len(selected)>0) selected[last] else "",
        anchor:if (contains(paths,st.anchor)) st.anchor else "",preview_data:null,look:false}
}
pub fn listing(st,path,entries,cursor=-1,revision=-1) any^ {
    let version = if (revision>=0) revision else st.serial+1
    let record = cursor<0 and path!=st.path
    let history = if (record) [*slice(st.history,0,st.cursor+1),path] else st.history;
    {*:st,path:path,entries:entries,revision:version,serial:max(st.serial,version),history:history,
        cursor:if (record) len(history)-1 else if (cursor>=0) cursor else st.cursor,
        query:"",selected:[],focus:"",anchor:"",reveal:false,preview_data:null,error:""}
}
pub fn choose(st,path,toggle=false,extend=false) {
    let paths = [for (e in ((visible(st)) ^ { [] })) e.path]
    let at = position(paths,path)
    let anchor = position(paths,st.anchor)
    let selected = if (at < 0) st.selected
        else if (extend and anchor >= 0) slice(paths,min(at,anchor),max(at,anchor)+1)
        else if (toggle) (if (contains(st.selected,path)) [for (p in st.selected where p != path) p] else [*st.selected,path])
        else [path];
    {*:st,selected:selected,focus:if (contains(selected,path)) path else if (len(selected)>0) selected[last] else "",
        anchor:if (extend and anchor >= 0) st.anchor else path,reveal:true,preview_data:null,error:""}
}
pub fn move(st,delta,extend=false) any^ {
    let rows = visible(st)
    let at = position([for (e in rows) e.path],st.focus)
    let next = if (at < 0) (if (delta < 0) len(rows)-1 else 0) else max(0,min(len(rows)-1,at+delta));
    if (len(rows)==0) st else choose(st,rows[next].path,false,extend)
}
pub fn focused(st) => [for (e in st.entries where e.path == st.focus) e][0]
pub fn position(values,value) {
    let found = index_of(values,value);
    if (found == null) -1 else found
}

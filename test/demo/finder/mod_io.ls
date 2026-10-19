import model: .mod_model
import uri: lambda.doc.uri
import media: lambda.doc.media

// S12.4.1v3: capture metadata now; views never re-read live Path properties.
fn entry_info(p,path,locator=null) {
    let dir = p.is_dir
    let ext = lower(p.extension or "");
    {name:p.name,path:path,dir:dir,extension:ext,size:p.size or 0,
        modified_raw:p.modified,
        modified:if (p.modified == null) "" else format(p.modified,"YYYY-MM-DD HH:mm"),kind:model.kind(ext,dir),
        *:(if (locator == null) {} else {source:p,locator:locator,readonly:true})}
}
pub fn read_folder(path) array^ {
    let paths = input(path,'dir')^;
    if (not (paths is array)) raise error("Choose a filesystem folder. Archive contents are read-only.")
    else [for (p in paths) entry_info(p,p.path)]
}
fn known_entry(st,path) => [for (e in [*(st.entries or []),
    *[for (c in st.columns or []) *c.entries],st.container] where e != null and e.path == path) e][0]
fn archive_member(node,members,index=0) element^ {
    if (index>=len(members)) node else {
        let folder = if (node.is_dir or node.entry_path == "") node else input(node,'zip')^
        let child = [for (e in content(folder)^ where e.name == members[index]) e][0];
        if (child == null) raise error("Archive entry no longer exists: " ++ members[index])
        else archive_member(child,members,index+1)^
    }
}
pub fn read_location(path,st=null) map^ {
    let target = model.absolute(path,sys.proc.self.cwd#)
    let known = known_entry(st,target)
    let saved = [for (location in st.locations or [] where location.path == target) location.locator][0]
    let entry = if (known != null or saved != null or target == "/") known
        else [for (e in read_folder(model.parent(target))^ where e.path == target) e][0]
    let locator = saved or entry.locator or (if (model.archive(entry)) {outer:target,members:[]} else null);
    if (locator == null) {
        if (entry != null and not entry.dir) raise error("Choose a folder or ZIP archive.")
        else {path:target,entries:read_folder(target)^,container:null,archive_cache:null}
    } else {
        let cached = st.archive_cache
        let cache = if (cached.path == locator.outer) cached else (
            let root = input(locator.outer,'zip')^,
            let physical = [for (e in read_folder(model.parent(locator.outer))^ where e.path == locator.outer) e][0],
            {path:locator.outer,root:root,entry:{*:physical,archive:true,kind:"Archive"}})
        let node = archive_member(cache.root,locator.members)^
        let container = if (len(locator.members)==0) {*:cache.entry,locator:locator}
            else entry_info(node,target,locator)
        let folder = if (node.is_dir or len(locator.members)==0) node
            else if (model.archive(container)) input(node,'zip')^ else raise error("Choose a folder or ZIP archive.");
        // UI keys are labels only: native member reads always use their captured fs nodes (S14.3.1v3).
        {path:target,container:container,archive_cache:cache,
            entries:[for (p in content(folder)^) entry_info(p,model.child_path(target,p.name),
                {outer:locator.outer,members:[*locator.members,p.name]})]}
    }
}
pub fn navigate(st,path,cursor=-1) map^ {
    let location = read_location(path,st)^;
    model.listing({*:st,archive_cache:location.archive_cache},location.path,location.entries,cursor,location.container)
}
pub fn read_destination(path,excluded=[]) map^ {
    let entries = read_folder(path)^;
    {path:path,entries:[for (e in entries where e.dir order by lower(e.name))
        {*:e,blocked:len([for (source in excluded where model.inside(e.path,source)) source])>0}]}
}
pub fn preview(entry) map^ {
    if (entry == null) {kind:"empty"}
    else if (entry.dir) {kind:"folder"}
    else if (model.archive(entry)) {kind:"archive",message:"Open this ZIP archive to browse its contents."}
    else if (entry.readonly == true) member_preview(entry)^
    else if (entry.kind == "Image") {kind:"image",src:entry.path}
    else if (model.viewable(entry.extension)) {kind:"document",src:uri.url(entry.path)}
    else if (contains(["Text document","Source code","Data file"],entry.kind) and entry.size <= 131072) text_preview(entry.path)^
    else {kind:"generic",message:if (entry.size>131072) "This file is too large for a text preview." else "No preview available for this file type."}
}
fn text_preview(source) map^ {
    let text = input(source,'text')^;
    {kind:"text",text:slice(text or "",0,6000),truncated:len(text)>6000}
}
fn member_preview(entry) map^ {
    let ext = entry.extension;
    if (entry.kind == "Image") {
        let mime = ({svg:"image/svg+xml",jpg:"image/jpeg",jpeg:"image/jpeg",tif:"image/tiff",tiff:"image/tiff",ico:"image/x-icon"})[ext] or "image/" ++ ext;
        {kind:"image",src:media.data_uri(input(entry.source,'binary')^,mime)^,inline:true}
    } else if (contains(["html","htm","xhtml","xht"],ext)) {
        {kind:"document",html:input(entry.source,'text')^}
    } else if (contains(["md","markdown"],ext)) {
        let parsed = input(entry.source,'markdown')^;
        {kind:"document",html:format(<html lang:"en",<head <meta charset:"UTF-8">
            <link rel:"stylesheet",href:uri.url(sys.lambda.home# ++ "/package/doc/markdown.css")>>;
            <body *content(parsed)^>>,'html')^}
    } else if (contains(["Text document","Source code","Data file"],entry.kind) and entry.size <= 131072) text_preview(entry.source)^
    else {kind:"generic",message:"No preview available for this archive member."}
}

pub fn named_path(folder,value) string^ {
    if (not model.valid_name(value)) raise error("Enter a name without / or \\, other than . or .. .")
    else model.child_path(folder,value)
}
fn existing(path) map^ {
    let entry = [for (e in read_folder(model.parent(path))^ where e.path == path) e][0];
    if (entry == null) raise error("The item no longer exists: " ++ model.basename(path)) else entry
}
pub fn plan(request) array^ {
    let action = request.action
    if (not contains(["new-file","new-folder","rename","move","delete"],action)) raise error("Unknown file operation.")
    else if (action == "new-file" or action == "new-folder") {
        let target = named_path(request.folder,request.value)^
        let entries = read_folder(request.folder)^;
        if (exists(target) or contains([for (e in entries) e.path],target)) raise error("An item with that name already exists.")
        else [{action:action,from:"",to:target}]
    } else if (len(request.paths)==0 or (action == "rename" and len(request.paths)!=1)) raise error("Select " ++ (if (action == "rename") "one item" else "an item") ++ " first.")
    else {
        let sources = [for (p in request.paths) existing(p)^]
        let folder = if (action == "move") request.value else request.folder
        let entries = if (action == "delete") [] else read_folder(folder)^
        let occupied = [for (e in entries) e.path];
        [for (entry in sources) (
            let target = if (action == "delete") "" else if (action == "rename") named_path(folder,request.value)^ else model.child_path(folder,entry.name),
            if (entry.path == "/") raise error("The filesystem root cannot be changed.")
            else if (action == "move" and (target == entry.path or (entry.dir and model.inside(folder,entry.path))))
                raise error("Choose a different folder outside the item being moved.")
            else if (action != "delete" and target != entry.path and (exists(target) or contains(occupied,target)))
                raise error("The destination already contains “" ++ model.basename(target) ++ "”.")
            else {action:action,from:entry.path,to:target})]
    }
}

// S12.1.1v3: filesystem effects run only in procedures, after the whole selection passes preflight.
pub pn perform(request) map^ {
    let steps = plan(request)^
    var changes = []
    var deleted = []
    var created = []
    var problem = ""
    for (step in steps) if (problem == "") {
        if (step.action == "new-file") { io.touch(step.to) ^ { problem = ^.message or "Could not create the file." } }
        else if (step.action == "new-folder") { io.mkdir(step.to) ^ { problem = ^.message or "Could not create the folder." } }
        else if (step.action == "delete") { io.delete(step.from) ^ { problem = ^.message or "Could not delete the item." } }
        // rename keeps directory moves atomic; a cross-device failure leaves the source intact.
        else { io.rename(step.from,step.to) ^ { problem = ^.message or "Could not move the item. Check permissions and use a destination on the same filesystem." } }
        if (problem == "") {
            if (step.action == "delete") { deleted = [*deleted,step.from] }
            else if (step.from == "") { created = [*created,step.to] }
            else { changes = [*changes,{from:step.from,to:step.to}] }
        }
    }
    return {changes:changes,deleted:deleted,created:created,error:problem}
}
pub pn reload(st) {
    var columns = []
    var problem = ""
    var current = {*:st,archive_cache:null}
    var revision = st.serial
    for (c in st.columns) if (problem == "") {
        read_location(c.path,{*:current,columns:columns}) ^ { problem = "Could not refresh “" ++ model.basename(c.path) ++ "”: " ++ ^.message }
            ~ {
                revision = revision+1
                columns = [*columns,{*:c,entries:~.entries,container:~.container,revision:revision}]
                current = {*:current,archive_cache:~.archive_cache}
            }
    }
    if (len(columns)==0) { return {*:st,error:problem} }
    let active = columns[last]
    return model.reconcile({*:st,path:active.path,entries:active.entries,columns:columns,container:active.container,revision:active.revision,serial:revision,
        archive_cache:current.archive_cache,
        query:if (active.path == st.path) st.query else active.query,error:problem})
}

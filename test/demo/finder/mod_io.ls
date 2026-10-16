import model: .mod_model

// S12.4.1v3: capture metadata now; views never re-read live Path properties.
pub fn read_folder(path) array^ {
    let paths = input(path,'dir')^;
    [for (p in paths) (let dir = p.is_dir, let ext = lower(p.extension or ""),
        {name:p.name,path:p.path,dir:dir,extension:ext,size:p.size or 0,
            modified_raw:p.modified,
            modified:if (p.modified == null) "" else format(p.modified,"YYYY-MM-DD HH:mm"),kind:model.kind(ext,dir)})]
}
pub fn preview(entry) map^ {
    if (entry == null) {kind:"empty"}
    else if (entry.dir) {kind:"folder"}
    else if (entry.kind == "Image") {kind:"image",src:entry.path}
    else if (contains(["Text document","Source code","Data file"],entry.kind) and entry.size <= 131072) {
        let text = input(entry.path,'text')^;
        {kind:"text",text:slice(text or "",0,6000),truncated:len(text)>6000}
    }
    else {kind:"generic",message:if (entry.size>131072) "This file is too large for a text preview." else "No preview available for this file type."}
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
    for (c in st.columns) if (problem == "") {
        read_folder(c.path) ^ { problem = "Could not refresh “" ++ model.basename(c.path) ++ "”: " ++ ^.message }
            ~ { columns = [*columns,{*:c,entries:~}] }
    }
    if (len(columns)==0) { return {*:st,error:problem} }
    let active = columns[last]
    return model.reconcile({*:st,path:active.path,entries:active.entries,columns:columns,
        query:if (active.path == st.path) st.query else active.query,error:problem})
}

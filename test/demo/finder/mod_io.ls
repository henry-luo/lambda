import model: .mod_model

// S12.4.1v2: capture metadata now; views never re-read live Path properties.
pub fn read_folder(path) array^ {
    let paths = input(path,'dir')^;
    [for (p in paths) (let dir = p.is_dir, let ext = lower(p.extension or ""),
        {name:p.name,path:p.path,dir:dir,extension:ext,size:p.size or 0,
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

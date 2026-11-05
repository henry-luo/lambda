// Shared file-browser snapshots and lexical file-kind labels.
pub fn kind(extension, dir=false) => if (dir) "Folder"
    else if (contains(["png","jpg","jpeg","gif","webp","svg","avif","bmp","tif","tiff","ico"],extension)) "Image"
    else if (contains(["md","markdown","txt","log","csv"],extension)) "Text document"
    else if (contains(["ls","js","ts","cpp","c","h","hpp","py","css","html","sh"],extension)) "Source code"
    else if (contains(["json","yaml","yml","toml","xml"],extension)) "Data file"
    else if (extension == "pdf") "PDF document"
    else if (contains(["zip","gz","tar","jar"],extension)) "Archive"
    else if (contains(["mp3","wav","flac","m4a"],extension)) "Audio"
    else if (contains(["mp4","mov","webm"],extension)) "Video" else "Document"
// Archives can be browsed even though their physical entry is a file.
pub fn archive(entry) => entry != null and not entry.dir and (entry.archive == true or lower(entry.extension or "") == "zip")
pub fn browsable(entry) => entry != null and (entry.dir or archive(entry))
// S12.4.1v3: capture metadata now; views never re-read live Path properties.
pub fn entry(p,path,locator=null) {
    let dir = p.is_dir
    let ext = lower(p.extension or "");
    {name:p.name,path:path,dir:dir,extension:ext,size:p.size or 0,
        modified_raw:p.modified,
        modified:if (p.modified == null) "" else format(p.modified,"YYYY-MM-DD HH:mm"),kind:kind(ext,dir),
        *:(if (locator == null) {} else {source:p,locator:locator,readonly:true})}
}
pub fn read_folder(path,invalid_message="Choose a filesystem folder.") array^ {
    let paths = input(path,'dir')^;
    if (not (paths is array)) raise error(invalid_message)
    else [for (p in paths) entry(p,p.path)]
}

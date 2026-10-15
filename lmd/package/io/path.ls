// Lexical path folding is pure and shared by document loaders and editor sessions.
pub fn basename(path) {
    let parts = split(path,"/");
    parts[len(parts)-1]
}
pub fn dirname(path) {
    let parts = split(path,"/")
    let folder = join(take(parts,len(parts)-1),"/");
    if (len(parts) <= 1) "." else if (folder == "") "/" else folder
}
fn folded(segments,index,result,rooted) {
    if (index >= len(segments)) result else {
        let segment = segments[index]
        let can_pop = len(result) > 0 and result[len(result)-1] != ".."
        let next = if (segment == "" or segment == ".") result
            else if (segment == ".." and can_pop) take(result,len(result)-1)
            else if (segment == ".." and rooted) result else [*result,segment];
        folded(segments,index+1,next,rooted)
    }
}
pub fn normal_path(path) {
    let rooted = starts_with(path,"/")
    let normalized = join(folded(split(path,"/"),0,[],rooted),"/");
    if (rooted) "/" ++ normalized else if (normalized == "") "." else normalized
}
pub fn resolve_path(base_dir,path) =>
    normal_path(if (starts_with(path,"/")) path else base_dir ++ "/" ++ path)

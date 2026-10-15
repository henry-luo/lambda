import c: .common
import paths: lambda.io.path
import locations: .location

pub let defaults = {font_size:20,font_family:"publisher",line_height:1.6,margin:24,theme:"light",publisher:true,mode:"scroll",zoom:1.0}
pub fn preferences(prefs) {
    {font_size:if (prefs.font_size is int) max(12,min(40,prefs.font_size)) else 20,
        font_family:if (prefs.font_family in ["publisher","serif","sans-serif","monospace"]) prefs.font_family else "publisher",
        line_height:if (prefs.line_height in [1.4,1.6,1.8,2.0]) prefs.line_height else 1.6,
        margin:if (prefs.margin in [16,24,40,64]) prefs.margin else 24,
        theme:if (prefs.theme in ["light","sepia","dark"]) prefs.theme else "light",publisher:prefs.publisher != false,
        mode:if (prefs.mode == "paged") "paged" else "scroll",zoom:if (prefs.zoom is number) max(0.5,min(3.0,prefs.zoom)) else 1.0}
}
pub pn restore(book,options) map {
    if (options.persist == false) { return {path:null,location:null,bookmarks:[],preferences:defaults,message:""} }
    let env = sys.proc.self.env#
    let base = c.value(env.LAMBDA_EBOOK_STATE_DIR,c.value(env.XDG_STATE_HOME,
        c.value(env.LOCALAPPDATA,c.value(env.HOME,".") ++ "/.local/state")) ++ "/lambda/ebook")
    let path = c.value(options.state_path,base ++ "/" ++ book.source_identity ++ ".json")
    let saved = if (exists(path)) input(path,"json") ^ { null } else null
    let valid = saved is map and saved.version == 1 and saved.publication_fingerprint == book.source_identity
    let location = if (valid and saved.location != null) locations.resolve(book,saved.location) ^ { null } else null
    let prefs = if (valid and saved.preferences is map) saved.preferences else defaults
    let checked = preferences(prefs)
    let bookmarks = if (valid and saved.bookmarks is array) [for (item in take(saved.bookmarks,1000),
        let verified = locations.resolve(book,item) ^ { null } where verified != null) verified] else []
    return {path:path,location:location,bookmarks:bookmarks,
        preferences:checked,message:if (valid and saved.location != null and location == null)
            "The saved reading position could not be verified." else ""}
}
pub pn save(book,session) any^ {
    if (session.path == null) { return null }
    io.mkdir(paths.dirname(session.path))^
    return output({version:1,publication_fingerprint:book.source_identity,location:session.location,
        bookmarks:session.bookmarks,preferences:session.preferences},session.path,{format:"json",atomic:true})^
}

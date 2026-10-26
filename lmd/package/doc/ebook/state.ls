import c: .common
import paths: lambda.io.path
import locations: .location

pub let colors = ["yellow","green","blue","pink","purple"]
// reflowable layouts: scrolling, one page fitted to the window, or a two-page spread
pub let layouts = ["scroll","paged","spread"]
pub let textures = ["none","antique","parchment","linen","newsprint","ivory"]
pub let turns = ["none","slide-left","slide-up","turn","flip","dissolve"]
pub let defaults = {font_size:20,font_family:"publisher",line_height:1.6,margin:24,theme:"light",publisher:true,mode:"scroll",zoom:1.0,
    texture:"none",turn:"slide-left"}
pub fn preferences(prefs) {
    {font_size:if (prefs.font_size is int) max(12,min(40,prefs.font_size)) else 20,
        font_family:if (prefs.font_family in ["publisher","serif","sans-serif","monospace"]) prefs.font_family else "publisher",
        line_height:if (prefs.line_height in [1.4,1.6,1.8,2.0]) prefs.line_height else 1.6,
        margin:if (prefs.margin in [16,24,40,64]) prefs.margin else 24,
        theme:if (prefs.theme in ["light","sepia","dark"]) prefs.theme else "light",publisher:prefs.publisher != false,
        mode:if (prefs.mode in layouts) prefs.mode else "scroll",zoom:if (prefs.zoom is number) max(0.5,min(3.0,prefs.zoom)) else 1.0,
        texture:if (prefs.texture in textures) prefs.texture else "none",turn:if (prefs.turn in turns) prefs.turn else defaults.turn}
}
// The per-user reading-state directory; library hosts keep their own records beside it.
pub fn directory() => (let env = sys.proc.self.env#,
    c.value(env.LAMBDA_EBOOK_STATE_DIR,c.value(env.XDG_STATE_HOME,
        c.value(env.LOCALAPPDATA,c.value(env.HOME,".") ++ "/.local/state")) ++ "/lambda/ebook"))
pub pn restore(book,options) map {
    if (options.persist == false) { return {path:null,location:null,bookmarks:[],highlights:[],preferences:defaults,message:""} }
    let path = c.value(options.state_path,directory() ++ "/" ++ book.source_identity ++ ".json")
    let saved = if (exists(path)) input(path,"json") ^ { null } else null
    let valid = saved is map and saved.version == 1 and saved.publication_fingerprint == book.source_identity
    let location = if (valid and saved.location != null) locations.resolve(book,saved.location) ^ { null } else null
    let prefs = if (valid and saved.preferences is map) saved.preferences else defaults
    let checked = preferences(prefs)
    let bookmarks = if (valid and saved.bookmarks is array) [for (item in take(saved.bookmarks,1000),
        let verified = locations.resolve(book,item) ^ { null } where verified != null) verified] else []
    // shape only: each chapter load keeps the highlights whose text still matches
    let highlights = if (valid and saved.highlights is array) [for (item in take(saved.highlights,5000)
        where item is map and item.id is string and item.spine_occurrence is int and item.start is int and
            item.end is int and item.text is string and item.color in colors) item] else []
    return {path:path,location:location,bookmarks:bookmarks,highlights:highlights,
        preferences:checked,message:if (valid and saved.location != null and location == null)
            "The saved reading position could not be verified." else ""}
}
pub pn save(book,session) any^ {
    if (session.path == null) { return null }
    io.mkdir(paths.dirname(session.path))^
    return output({version:1,publication_fingerprint:book.source_identity,location:session.location,
        bookmarks:session.bookmarks,highlights:session.highlights,preferences:session.preferences},session.path,{format:"json",atomic:true})^
}

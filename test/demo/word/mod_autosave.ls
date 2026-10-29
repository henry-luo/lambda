// D7.2.5 / S12.1.3: recovery policy stays in the app; the document owns its sleeping task.
import dom
import files: .mod_io
import session: lambda.edit.session

pub let interval_ms = 120000
let suffix = ".autosave.word.json"
pub fn initial() => {started:false,root:sys.home# ++ "/.lambda/word",document_id:"",task:null}

pub pn clock(root) {
    while (true) {
        sleep(interval_ms)^
        let owner = dom.get_element_by_id(root,"word")
        if (owner==null) { return null }
        dom.dispatch(owner,{type:"word_autosave",bubbles:false})
    }
}

pub fn read_snapshot(path) map^ {
    let value = input(path,'json')^;
    if (not (value is map) or value.format!="lambda-word-autosave" or value.version!=1 or
        not (value.document_id is string) or value.document_id=="" or
        not starts_with(session.basename(path),value.document_id ++ "--") or
        not (value.original_path is string) or not (value.disk_text==null or value.disk_text is string) or
        not (value.saved_at is string)) raise error("Word: invalid recovery snapshot")
    else {
        let loaded = files.restore(value.snapshot)^;
        // a recovered document remains unsaved, and retains the original external-change checkpoint.
        {document_id:value.document_id,doc:loaded.doc,title:loaded.title,saved_at:value.saved_at,
            session:{*:files.new_session(loaded.doc,loaded.title),path:value.original_path,
                name:session.basename(value.original_path),disk_text:value.disk_text,saved_doc:null}}
    }
}

pub fn versions(root,document_id=null) array^ {
    if (not exists(root)) [] else [for (entry in input(root,'dir')^
        where not entry.is_dir and ends_with(entry.name,suffix) and
            (document_id==null or starts_with(entry.name,document_id ++ "--"))
        order by entry.name desc) {path:entry.path,name:entry.name}]
}

pub fn recoveries(root) array^ => [for (entry in versions(root)^,
    let loaded = read_snapshot(entry.path) ^ { null } order by loaded.saved_at desc,entry.name desc)
    {*:entry,valid:loaded!=null,label:if (loaded==null) "Unreadable snapshot: " ++ entry.name
        else loaded.title ++ " — " ++ loaded.saved_at ++
            (if (loaded.session.path=="") " (unsaved document)" else " — " ++ session.basename(loaded.session.path))}]

pub pn write_snapshot(root,document_id,file_state,doc) map^ {
    // enforce the editable reader's limits before creating any recovery file.
    files.export_text(doc,file_state.envelope)^
    let stamp = now().utc.format("YYYY-MM-DDThh:mm:ss") ++ "Z"
    let path = root ++ "/" ++ document_id ++ "--" ++ replace(stamp,":","-") ++ "--" ++ uuid()^ ++ suffix
    let value = {format:"lambda-word-autosave",version:1,document_id:document_id,
        // recovery can run from another working directory; keep the source file's absolute identity.
        original_path:if (file_state.path=="") "" else session.resolve_path(sys.proc.self.cwd#,file_state.path),
        disk_text:file_state.disk_text,saved_at:stamp,
        snapshot:files.archive(doc,file_state.envelope.title)}
    io.mkdir(root)^
    if (exists(path)) { raise error("Word: recovery filename already exists; earlier versions were kept") }
    output(value,path,{format:'json',atomic:true})^
    let checked = read_snapshot(path)^;
    if (checked.doc!=doc or checked.title!=file_state.envelope.title or checked.document_id!=document_id or
        checked.session.path!=value.original_path or checked.session.disk_text!=file_state.disk_text)
        raise error("Word: auto-save verification failed; earlier versions were kept")
    else {path:path,saved_at:stamp}
}

pub pn clear_versions(root,document_id,saved_path) {
    // retire this document's entire chain, including bad copies; keep other documents' versions.
    var failed = false
    let entries = versions(root,document_id) ^ { return false }
    let verified_path = session.resolve_path(sys.proc.self.cwd#,saved_path)
    for (entry in entries) {
        // a Save As target can name a recovery file through either a relative or absolute path.
        if (session.resolve_path(sys.proc.self.cwd#,entry.path)!=verified_path) {
            io.delete(entry.path) ^ { failed=true }
        }
    }
    return not failed
}

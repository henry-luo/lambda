import autosave: ~~.~~.mod_autosave
import app: ~~.~~.mod_app
import files: ~~.~~.mod_io
import model: ~~.~~.mod_model
import document: ~~.~~.mod_document
import session: lambda.edit.session
import lambda.editor.mod_doc

pn main() {
    let root = "temp/word-autosave-" ++ uuid()^
    let backups = root ++ "/recovery"
    let target = root ++ "/document.word.json"
    let absolute_target = session.resolve_path(sys.proc.self.cwd#,target)
    let base = document.blank()
    let doc = node('doc',[node('p',[text("Recovered 日本語 & edits")])])
    let other = node('doc',[node('p',[text("Another document")])])
    let identity = uuid()^
    let unrelated = uuid()^
    let file_state = {*:files.new_session(base,"Recovery test"),path:target}
    let st = {*:app.initial(),editor:model.open_editor(doc),file_state:file_state,
        autosave:{*:autosave.initial(),root:backups,document_id:identity,started:true}}
    let clean = {*:st,editor:model.open_editor(base)}
    let skipped = app.auto_saved(clean)
    let composing = app.auto_saved({*:st,editor:{*:st.editor,composition:{active:true}}})
    let first = app.auto_saved(st)
    let v1 = autosave.versions(backups,identity)^
    let original = input(v1[0].path,'text')^
    let second = app.auto_saved(first)
    let v2 = autosave.versions(backups,identity)^
    let retained = input(v1[0].path,'text')^==original
    let restored = autosave.read_snapshot(v1[0].path)^
    let another = autosave.write_snapshot(backups,unrelated,files.new_session(other,"Other"),other)^
    let corrupt = backups ++ "/" ++ identity ++ "--9999--bad.autosave.word.json"
    output("{broken",corrupt,'text')^
    let invalid = autosave.read_snapshot(corrupt) ^ { null }
    let candidates = autosave.recoveries(backups)^
    let blocker = root ++ "/not-a-folder"
    output("blocker",blocker,'text')^
    let failed_save = app.command({*:second,file_state:{*:file_state,path:blocker ++ "/document.word.json"}},"file.save",{},null)
    let kept_after_failure = len(autosave.versions(backups,identity)^)
    let failed_write = app.auto_saved({*:second,autosave:{*:second.autosave,root:v1[0].path}})
    let saved = app.command(second,"file.save",{},null)
    let disk = input(target,'text')^
    let reopened = files.read_document(target)^
    let verified = files.verify_saved(target,doc,file_state.envelope,disk)^
    let wrong_doc = files.verify_saved(target,other,file_state.envelope,disk)^
    let wrong_bytes = files.verify_saved(target,doc,file_state.envelope,disk ++ " ")^
    let saved_versions = autosave.versions(backups,identity)^
    let fresh = app.auto_saved({*:saved,editor:model.open_editor(other)})
    let v3 = autosave.versions(backups,identity)^
    let saved_recovery = autosave.read_snapshot(v3[0].path)^
    output("external change",target,'text')^
    let conflict = app.command(fresh,"file.save",{},null)
    let conflict_versions = autosave.versions(backups,identity)^
    let as_path = root ++ "/copy.word.json"
    let as_result = files.save_document(saved_recovery.session,other,as_path)
    let kept_target = autosave.clear_versions(backups,identity,session.resolve_path(sys.proc.self.cwd#,v3[0].path))
    let checks = [
        autosave.interval_ms==120000 and autosave.initial().root==sys.home# ++ "/.lambda/word",
        skipped==clean and composing.editor.doc==doc and composing.status==st.status,
        len(v1)==1 and len(v2)==2 and v2[0].path!=v2[1].path,
        retained,
        first.file_state==st.file_state and session.is_dirty(first.file_state,first.editor.doc),
        restored.doc==doc and restored.title=="Recovery test" and restored.session.path==absolute_target,
        restored.session.disk_text==null and session.is_dirty(restored.session,restored.doc),
        invalid==null and len(candidates)==4 and len([for (entry in candidates where entry.valid) entry])==3,
        failed_save.file_state.path==blocker ++ "/document.word.json" and session.is_dirty(failed_save.file_state,doc),
        starts_with(failed_save.status,"Not saved:") and kept_after_failure==3,
        starts_with(failed_write.status,"Auto-save failed:") and failed_write.file_state==second.file_state,
        not session.is_dirty(saved.file_state,doc) and saved.file_state.disk_text==disk,
        verified and not wrong_doc and not wrong_bytes and reopened.editor.doc==doc,
        len(saved_versions)==0 and not exists(corrupt) and exists(another.path),
        len(v3)==1 and session.is_dirty(fresh.file_state,other),
        saved_recovery.session.disk_text==disk and saved_recovery.session.path==absolute_target,
        conflict.dialog.kind=="changed" and session.is_dirty(conflict.file_state,other),
        len(conflict_versions)==1 and input(target,'text')^=="external change",
        as_result.ok and files.read_document(as_path)^.editor.doc==other,
        kept_target and exists(v3[0].path) and exists(another.path)
    ]
    io.delete(root)^
    return [*checks,not exists(root)]
}

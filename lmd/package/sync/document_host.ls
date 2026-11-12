// one trusted owner exposes effectful editor operations; every edit commits before reply.
import local: .local
import model: .document_state
import documents: .documents
import conversion: .document_format
import session: .session
import protocol: .protocol
import core: .core
import workspace: .workspace
import repo: .repository
import disk: lambda.io.disk
import codec: .codec
import v: .schema
import utf8: lambda.io.utf8
import filesystem: .filesystem
import mark: .mark

fn actor(handle, current) any^ => handle.writer ++ ":doc:" ++ string(current.serial+1)^
pub pn status(handle, document) any^ {
    let current = local.status(handle)^.state
    let record = model.get(current,document)^
    let value:any^ = session.value(record.endpoint.replica)
    return {id:record.id,entry:record.entry,profile:record.endpoint.replica.profile,format:record.format,
        path:workspace.path(current.endpoint.replica,record.entry)^,
        value:if (value is error) null else value,projection_error:if (value is error) value.message else null,
        frontier:record.endpoint.replica.clock,dirty:session.dirty(record.session)^,
        saved:record.session.base.id,versions:[for (saved in record.session.versions) saved.id],
        notices:record.session.notices,conflicts:record.session.conflicts,deleted:record.session.deleted,
        editing_conflicts:if (record.endpoint.replica.profile == 'mark') mark.conflicts(record.endpoint.replica)^ else [],
        prepared:record.prepared != null,coverage:model.coverage(record)^,
        file_heads:[for (head in workspace.entry(current.endpoint.replica,record.entry)^.heads) head.id],
        workspace_review:current.review != null}
}
pub pn open_document(handle, path, profile='text', encoding='text') any^ {
    local.scan(handle)^
    let result = local.mutate(handle,pn (tx,k,current)=> {
        let entries = workspace.read(current.endpoint.replica)^.entries
        let found = [for (entry in entries where entry.visible and entry.type == 'file' and
            workspace.path(current.endpoint.replica,entry.id)^ == path) entry]
        if (len(found) != 1) raise error("sync document: open requires one known regular file")
        let entry = found[0]
        if (len(entry.heads)^ != 1) raise error("sync document: resolve competing file revisions before opening")
        let document = model.id(k.space,entry.id)^
        let previous = v.lookup(model.all(current),document)^
        if (previous != null) {
            if (previous.endpoint.replica.profile != profile or previous.format != encoding)
                raise error("sync document: existing editing profile/format differs")
            return model.replace(current,{*:previous,opened:true})^
        }
        let captured = disk.capture(current.root,path,8388608)^
        if (hash(captured.bytes,'sha256')^ != entry.heads[0].content.hash)
            raise error("sync document: disk changed during open; refresh required")
        let record = model.create(k.space,entry.id,profile,encoding,if (captured.bytes == null) "" else utf8.decode(captured.bytes)^)^
        return model.replace(current,record)^
    })^
    let entry = [for (item in model.all(result.state)^ where workspace.path(result.state.endpoint.replica,item.entry)^ == path) item][0]
    return status(handle,entry.id)^
}
pub pn apply_edits(handle, document, commands, resolve=false, request_id=null) any^ {
    local.mutate(handle,pn (tx,k,current)=> {
        let record = model.get(current,document)^
        let fingerprint = codec.digest([commands,resolve])^
        let previous = model.request(record,'edit',request_id)^
        if (previous != null) {
            if (previous.fingerprint != fingerprint) raise error("sync document: edit request ID reused with different commands")
            return current
        }
        let edited = protocol.local_change(record.endpoint,actor(handle,current)^,commands)^
        let next = model.update(record,edited.endpoint)^
        let selected = if (resolve) {*:next,session:session.resolve(next.session,next.endpoint.replica)^} else next
        let remembered = model.record_request(selected,'edit',request_id,
            {fingerprint:fingerprint,frontier:edited.endpoint.replica.clock})^
        return {*:model.replace(current,remembered)^,serial:current.serial+1}
    })^
    return status(handle,document)^
}
pub pn refresh(handle, document) any^ {
    local.scan(handle)^
    return status(handle,document)^
}
pub pn compact(handle, document) any^ {
    local.mutate(handle,pn (tx,k,current)=> {
        return model.replace(current,model.compact(model.get(current,document)^)^)^
    })^
    return status(handle,document)^
}
pub pn restore_version(handle, document, version) any^ {
    let record = model.get(local.status(handle)^.state,document)^
    let saved = v.lookup(record.session.versions,version)^
    if (saved == null) raise error("sync document: saved version is not retained")
    let command = if (record.endpoint.replica.profile == 'text')
        {kind:'text',start:0,count:len(session.value(record.endpoint.replica)^)^,value:saved.value}
        else {kind:'root',value:mark.export_value(saved.replica)^}
    // restoration is a new compensating edit; the existing history and disk remain intact.
    return apply_edits(handle,document,[command],true)^
}
pub pn attachment(handle, bytes, media_type="application/octet-stream") any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        return {*:repo.content(tx,k,if (bytes is string) binary(bytes)^ else bytes)^,media_type:media_type}
    })^
}

// preparation is durable before disk effects; retries install this exact captured frontier.
pub pn prepare_save(handle, document, request_id=null) any^ {
    local.scan(handle)^
    let result = local.mutate(handle,pn (tx,k,current)=> {
        let record = model.get(current,document)^
        if (model.request(record,'save',request_id)^ != null) { return current }
        if (record.prepared != null) {
            if (request_id != null and record.prepared.request_id != request_id)
                raise error("sync document: another save intent is pending")
            return current
        }
        if (current.installation != null or current.review != null) raise error("sync document: finish workspace installation/review before saving")
        let path = workspace.path(current.endpoint.replica,record.entry)^
        let captured = disk.capture(current.root,path,8388608)^
        if (hash(captured.bytes,'sha256')^ != record.disk_hash) raise error("sync document: external write during save preparation")
        let prepared = session.prepare_save(record.session,conversion.export_raw(record.endpoint.replica,record.format)^)^
        return model.replace(current,{*:record,prepared:{*:prepared,path:path,expected:captured.token,
            expected_hash:record.disk_hash,installed:false,request_id:request_id}})^
    })^
    let record = model.get(result.state,document)^
    let previous = model.request(record,'save',request_id)^
    return if (previous != null) {completed:true,version:previous.version} else record.prepared
}
pub pn cancel_save(handle, document) any^ {
    local.mutate(handle,pn (tx,k,current)=> {
        let record = model.get(current,document)^
        if (record.prepared != null and record.prepared.installed)
            raise error("sync document: installed save must finish")
        if (record.prepared != null) {
            let observed = disk.inspect(current.root,record.prepared.path)^
            if (observed != null and observed.kind == 'file') {
                let bytes = disk.capture(current.root,record.prepared.path,8388608)^.bytes
                if (hash(bytes,'sha256')^ != record.prepared.expected_hash and
                    hash(bytes,'sha256')^ == hash(record.prepared.saved.raw,'sha256')^)
                    raise error("sync document: installed save must finish")
            }
        }
        return model.replace(current,{*:record,prepared:null})^
    })^
    return status(handle,document)^
}
pub pn install_save(handle, document) any^ {
    return local.mutate(handle,pn (tx,k,current)=> {
        let record = model.get(current,document)^
        let intent = record.prepared
        if (intent == null) raise error("sync document: prepare a save first")
        let path = workspace.path(current.endpoint.replica,record.entry)^
        if (path != intent.path or not workspace.entry(current.endpoint.replica,record.entry)^.visible)
            raise error("sync document: file moved/deleted during save; intent retained")
        let captured = disk.capture(current.root,path,8388608)^
        let wanted = hash(intent.saved.raw,'sha256')^
        if (hash(captured.bytes,'sha256')^ != wanted) {
            if (intent.installed or hash(captured.bytes,'sha256')^ != intent.expected_hash)
                raise error("sync document: external write during save; intent and edits retained")
            disk.install(current.root,path,binary(intent.saved.raw)^,captured.token)^
        }
        let readback = disk.capture(current.root,path,8388608)^
        if (hash(readback.bytes,'sha256')^ != wanted) raise error("sync document: external write after save; intent retained")
        return model.replace(current,{*:record,prepared:{*:intent,installed:true}})^
    })^
}
pub pn commit_save(handle, document) any^ {
    // keep the intent and finalization in separate commits; a kill between them is recoverable.
    install_save(handle,document)^
    local.mutate(handle,pn (tx,k,current)=> {
        let record = model.get(current,document)^
        let intent = record.prepared
        if (intent == null or not intent.installed) raise error("sync document: save intent not installed")
        let readback = disk.capture(current.root,intent.path,8388608)^
        if (hash(readback.bytes,'sha256')^ != hash(intent.saved.raw,'sha256')^)
            raise error("sync document: disk changed before save finalization; intent retained")
        let link = workspace.document(intent.saved)^
        repo.store_object(tx,k,"checkpoint",protocol.checkpoint(intent.saved.replica))^
        // reconcile the captured disk branch once, attaching the exact CRDT frontier to its revision.
        let reconciled = filesystem.reconciled(current,filesystem.capture(tx,k,current)^,
            actor(handle,current)^,[{id:record.entry,document:link}])^
        if (reconciled.review != null) raise error("sync document: workspace save needs review; intent retained")
        let saved = session.commit_save(record.session,intent)^
        let completed = model.record_request({*:record,session:saved,disk_hash:hash(readback.bytes,'sha256')^,prepared:null},
            'save',intent.request_id,{version:intent.saved.id})^
        let next = model.replace(reconciled,completed)^
        return next
    })^
    // scan updates the filesystem base against the same saved export; no import loop.
    local.scan(handle)^
    return status(handle,document)^
}
pub pn save(handle, document, request_id=null) any^ {
    let prepared = prepare_save(handle,document,request_id)^
    if (prepared.completed == true) { return status(handle,document)^ }
    return commit_save(handle,document)^
}

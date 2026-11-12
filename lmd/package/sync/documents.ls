// repository-side effects; the caller commits the returned state in the same head transaction.
import model: .document_state
import history: .history
import maintenance: .maintenance_state
import reach: .reachability
import cache_state: .cache_state
import conversion: .document_format
import core: .core
import protocol: .protocol
import session: .session
import workspace: .workspace
import repo: .repository
import blob: .blob
import codec: .codec
import disk: lambda.io.disk
import v: .schema
import utf8: lambda.io.utf8

pub fn roots(current) => [for (record in model.all(current)) {id:record.id,object:record.export_id}]
pub pn dependencies(tx, keys, record, cached=[]) any^ {
    let replicas = [record.endpoint.replica,record.origin.replica,
        record.session.base.replica,*if (record.prepared == null) [] else [record.prepared.saved.replica]]
    for (replica in replicas) {
        for (reference in blob.required(replica)^) { repo.payload(tx,keys,reference)^ }
    }
    for (saved in record.session.versions) {
        for (reference in blob.required(saved.replica)^) { repo.availability(tx,keys,reference,cached)^ }
    }
    return true
}
pub pn flush(tx, keys, current) any^ {
    var updated = current
    var observed_headers = []
    if (len(model.all(current))^ > 64) raise error("sync document: document count limit")
    for (original in model.all(current)) {
        let record = history.flush(tx,keys,original,now().unix)^
        observed_headers = [*observed_headers,*history.headers(tx,keys,record.history)^.entries]
        model.validate(record,keys.space)^
        dependencies(tx,keys,record,reach.cache_ids(current))^
        for (saved in [record.origin,*record.session.versions]) {
            repo.store_object(tx,keys,"checkpoint",protocol.checkpoint(saved.replica))^
        }
        let stored_id = repo.store_object(tx,keys,"document",model.public_state(record)^)^
        let endpoint = protocol.durable(record.endpoint,'local',record.endpoint.replica.clock,
            [for (reference in blob.required(record.endpoint.replica)^) reference.manifest])^
        updated = model.replace(updated,history.pack({*:record,endpoint:endpoint,export_id:stored_id}))^
    }
    return cache_state.recover(tx,keys,maintenance.observe(updated,observed_headers,now().unix)^)^
}
pub pn install_roots(tx, keys, current, roots) any^ {
    if (not (roots is array) or len(roots) > 64 or len(unique([for (root in roots) root.id])) != len(roots))
        raise error("sync document: invalid document roots")
    var updated = current
    for (root in roots) {
        v.fields(root,["id","object"])^
        let value = history.expand_public(tx,keys,repo.get(tx,keys,root.object,"document")^,history.retired(current))^
        if (value.id != root.id) raise error("sync document: root identity mismatch")
        if (workspace.entry(current.endpoint.replica,value.entry)^.type != 'file')
            raise error("sync document: document target must be a file")
        let record = model.merge_public(v.lookup(model.all(updated),root.id)^,value,keys.space)^
        dependencies(tx,keys,record,reach.cache_ids(current))^
        updated = model.replace(updated,record)^
    }
    return updated
}
pub pn record_cloud(tx, keys, current, roots, provider) any^ {
    var updated = current
    for (root in roots) {
        let saved = repo.get(tx,keys,root.object,"document")^
        model.validate_public(saved,keys.space)^
        let record = model.get(updated,root.id)^
        let endpoint = protocol.record_durable(record.endpoint,'cloud',saved.live,
            [for (reference in blob.required(saved.live.replica)^) reference.manifest])^
        let old = if (record.cloud_roots == null) [] else record.cloud_roots
        updated = model.replace(updated,{*:record,endpoint:endpoint,cloud_roots:[
            *[for (item in old where item.id != provider) item],{id:provider,object:root.object}]})^
    }
    return updated
}

// scans/readback advance the disk base; live updates never implicitly save a file.
pub pn refresh(tx, keys, current, actor) any^ {
    // unresolved namespace identity cannot authorize mapping captured bytes into a document.
    if (current.review != null) { return current }
    var updated = current
    for (record in model.all(current)) {
        let entry = workspace.entry(current.endpoint.replica,record.entry)^
        let members = [for (member in current.disk where member.id == record.entry and member.type == 'file') member]
        let path = if (len(members) == 0) null else members[0].path
        let observed = if (path == null) null else disk.inspect(current.root,path)^
        if (observed == null or observed.kind != 'file') {
            if (record.disk_hash != null and not record.session.deleted) {
                updated = model.replace(updated,{*:record,session:session.deleted(record.session)})^
            }
            continue
        }
        let captured = disk.capture(current.root,path,8388608)^
        let digest = hash(captured.bytes,'sha256')^
        if (digest == record.disk_hash) { continue }
        let decoded:any^ = if (captured.bytes == null) "" else utf8.decode(captured.bytes)
        let raw = if (decoded is error) captured.bytes else decoded
        let known = [for (saved in record.session.versions where hash(saved.raw,'sha256')^ == digest) saved]
        var next = record
        if (len(known) > 0) {
            // a replicated save already has editing IDs; reimport would duplicate its intent.
            next = {*:record,session:session.adopt_disk(record.session,known[len(known)-1])^,disk_hash:digest}
        } else {
            let parsed:any^ = if (decoded is error) decoded else conversion.parse_raw(raw,record.endpoint.replica.profile,record.format)
            let import_id = "external:" ++ string(current.serial) ++ ":" ++ record.id
            let imported = session.import_external(record.session,import_id,actor ++ ":external:" ++ record.id,
                record.session.base.id,raw,parsed)^
            next = {*:record,session:imported,endpoint:protocol.replace_replica(record.endpoint,imported.live)^,
                disk_hash:digest}
        }
        updated = model.replace(updated,next)^
    }
    return updated
}

pub pn expand(tx, keys, current) any^ {
    var expanded = current
    for (record in model.all(current)) {
        expanded = model.replace(expanded,history.expand(tx,keys,record,history.retired(current))^)^
    }
    return expanded
}

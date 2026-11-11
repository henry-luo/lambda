// ZIP-backed workspace lifecycle; capture and preparation are pure (S12.4.1v3).
import c: .core
import v: .schema
import codec: .codec
import p: .protocol
import store: .store
import w: .workspace
import rules: .workspace_rules
import tree: .workspace_tree
import paths: lambda.io.path
import capture_model: .workspace_capture

fn capture_children(node, prefix, accumulated, max_entries, max_bytes) any^ {
    var result = accumulated
    let children = content(node)^
    var i = 0
    while (i < len(children)) {
        let child = children[i]
        if (name(child)^ != 'fs' or not rules.component(child.name)^ or not contains(['file','dir'],child.kind)^)
            raise error("sync ZIP: invalid filesystem member")
        if (len(result.members)^ >= max_entries) raise error("sync ZIP: entry limit")
        let path = if (prefix == "") child.name else prefix ++ "/" ++ child.name
        var reference = null
        if (child.kind == 'file') {
            if (child.size != null and child.size > max_bytes-result.bytes) raise error("sync ZIP: content limit")
            let bytes = input(child,'binary')^
            let captured = w.content(bytes)^
            if (captured.reference.size > max_bytes-result.bytes) raise error("sync ZIP: content limit")
            reference = captured.reference
            result = {*:result,objects:w.objects(result.objects,[captured.object])^,
                bytes:result.bytes+captured.reference.size}
        }
        result = {*:result,members:[*result.members,{path:path,type:child.kind,content:reference}]}
        if (child.kind == 'dir') { result = capture_children(child,path,result,max_entries,max_bytes)^ }
        i = i+1
    }
    return result
}

pub fn capture(archive, max_entries=10000, max_bytes=67108864) any^ {
    if (not v.natural(max_entries)^ or not v.natural(max_bytes)^ or not (archive is element) or
        archive.format != 'zip') raise error("sync ZIP: expected captured ZIP archive")
    let captured = capture_children(archive,"",{members:[],objects:[],bytes:0},max_entries,max_bytes)^
    let members = sort(captured.members,(member)=>member.path)^
    if (len(unique([for (member in members) member.path])) != len(members)) raise error("sync ZIP: duplicate member path")
    return {*:captured,members:members,id:capture_model.capture_id(members)^}
}

pub fn from_archive(id, actor, archive, max_entries=10000, max_bytes=67108864) any^ {
    let captured = capture(archive,max_entries,max_bytes)^
    let imported = capture_model.import_branch(w.start(id)^,actor,captured)^
    return {endpoint:imported.endpoint,objects:captured.objects,
        base:capture_model.baseline(imported.endpoint.replica,imported.members,captured.id)^,imports:[],conflicts:[],notices:[],source:null}
}

pub fn read_file(id, actor, path) any^ {
    let raw = input(path,'binary')^
    let session = from_archive(id,actor,parse(raw,'zip')^)^
    return {*:session,source:{path:paths.normal_path(path)^,hash:hash(raw,'sha256')^}}
}

pub fn update(session, endpoint, objects=[]) any^ {
    if (endpoint.schema != w.model) raise error("sync ZIP: workspace schema mismatch")
    p.validate_endpoint(endpoint)^
    if (not c.dominates(endpoint.replica.clock,session.endpoint.replica.clock)^) raise error("sync ZIP: stale endpoint update")
    if (any([for (group in session.endpoint.journal where not c.dominates(endpoint.replica.floor,group.frontier)^)
        v.lookup(endpoint.journal,group.id)^ == null])) raise error("sync ZIP: atomic group history missing")
    return {*:session,endpoint:{*:endpoint,replica:c.merge(session.endpoint.replica,endpoint.replica)^},
        objects:w.objects(session.objects,objects)^}
}

fn file_tree(entries, objects, parent) any^ => [for (entry in entries where entry.visible and entry.parent == parent)
    if (entry.type == 'dir') <fs name:entry.materialized_name,kind:'dir',*file_tree(entries,objects,entry.id)^>
    else <fs name:entry.materialized_name,kind:'file',w.payload(entry.heads[0].content,objects)^>]

pub fn prepare_save(session) any^ {
    if (len(session.conflicts)^ != 0) raise error("sync ZIP: resolve external review before saving")
    let replica = session.endpoint.replica
    let projected = w.read(replica)^
    if (any([for (entry in projected.entries where entry.visible and entry.type == 'file') len(entry.heads)^ != 1]))
        raise error("sync ZIP: resolve file revisions before saving")
    let entries = projected.entries
    let members = capture_model.export_members(entries)^
    return {base_id:session.base.id,base:capture_model.baseline(replica,members,capture_model.capture_id(members)^)^,
        tree:<fs kind:'dir',*file_tree(entries,session.objects,w.root)^>}
}

pub fn commit_save(session, prepared) any^ {
    if (prepared.base_id != session.base.id or len(session.conflicts)^ != 0) raise error("sync ZIP: disk base changed during save")
    let replica = c.restore(prepared.base.checkpoint)^
    c.merge(session.endpoint.replica,replica)^
    if (not c.dominates(session.endpoint.replica.clock,replica.clock)^) raise error("sync ZIP: save frontier exceeds live state")
    capture_model.validate_base(prepared.base)^
    return {*:session,base:prepared.base}
}

pub fn dirty(session) any^ => c.compact(session.endpoint.replica) != session.base.checkpoint.replica

fn hold(session, record, reason) => {*:session,imports:[*session.imports,{*:record,status:'review',reason:reason}],
    conflicts:[*session.conflicts,{kind:'external-review',import_id:record.id,reason:reason}],
    notices:[*session.notices,"External ZIP changes need review; live work and captured input are retained."]}

pub fn import_external(session, import_id, actor, base_id, archive, hints=[]) any^ {
    let captured = capture(archive)^
    let fingerprint = codec.digest([actor,base_id,captured.id,hints])^
    let old = v.lookup(session.imports,import_id)^
    if (old != null) {
        if (old.fingerprint != fingerprint) raise error("sync ZIP: import ID reused")
        return session
    }
    if (not v.actor(import_id)^) raise error("sync ZIP: invalid import ID")
    let record = {id:import_id,actor:actor,base_id:base_id,fingerprint:fingerprint,capture:captured,hints:hints}
    if (base_id != session.base.id) { return hold(session,record,"unknown or stale archive base") }
    if (not p.fresh_writer(session.endpoint,actor)^ or any([for (record in session.imports) record.actor == actor])) {
        return hold(session,record,"external ZIP import requires a fresh actor")
    }
    let associated:any^ = capture_model.correspondence(session.base,captured,hints)
    if (associated is error) { return hold(session,record,associated.message) }
    let members = [for (member in associated) {*:member,original_path:v.lookup(session.base.members,member.id)^.path}]
    let branch:any^ = capture_model.import_branch(p.start(session.base.checkpoint.replica,w.model)^,actor,captured,members)
    if (branch is error) { return hold(session,record,branch.message) }
    let merged = p.join_branch(session.endpoint,branch.endpoint)^
    let conflicts = w.read(merged.replica)^.conflicts
    return {*:session,endpoint:merged,objects:w.objects(session.objects,captured.objects)^,
        base:capture_model.baseline(branch.endpoint.replica,branch.members,captured.id)^,
        imports:[*session.imports,{*:record,status:if (len(conflicts) == 0) 'merged' else 'conflict'}],
        notices:[*session.notices,if (len(conflicts) == 0) "External ZIP changes merged; live edits retained."
            else "Concurrent ZIP changes retained; review namespace or revision conflicts."]}
}

pub fn resolve_review(session, import_id, endpoint, objects=[]) any^ {
    let record = v.lookup(session.imports,import_id)^
    if (record == null or record.status != 'review') raise error("sync ZIP: no matching external review")
    let updated = update(session,endpoint,objects)^
    return {*:updated,imports:[for (item in updated.imports) if (item.id == import_id) {*:item,status:'resolved'} else item],
        conflicts:[for (conflict in updated.conflicts where conflict.import_id != import_id) conflict]}
}

pub fn refresh_file(session, import_id, actor, hints=[]) any^ {
    if (session.source == null) raise error("sync ZIP: no source file")
    let raw = input(session.source.path,'binary')^
    let imported = import_external(session,import_id,actor,session.base.id,parse(raw,'zip')^,hints)^
    let record = v.lookup(imported.imports,import_id)^
    return if (record.status == 'review') imported else
        {*:imported,source:{*:session.source,hash:hash(raw,'sha256')^}}
}

fn validate_capture(captured) any^ {
    v.fields(captured,["members","objects","bytes","id"])^
    if (not (captured.members is array) or not v.natural(captured.bytes)^ or
        captured.members != sort(captured.members,(member)=>member.path)^ or
        len(unique([for (member in captured.members) member.path])) != len(captured.members)^)
        raise error("sync ZIP: invalid captured members")
    w.objects([],captured.objects)^
    var bytes = 0
    var i = 0
    while (i < len(captured.members)^) {
        let member = captured.members[i]
        v.fields(member,["path","type","content"])^
        if (not (member.path is string) or not all([for (part in split(member.path,"/")^) rules.component(part)^]) or
            not contains(['file','dir'],member.type)^) raise error("sync ZIP: invalid captured member")
        let parent = capture_model.parent_path(member.path)^
        if (parent != "" and capture_model.association(captured.members,parent)^.type != 'dir') raise error("sync ZIP: missing captured parent")
        if (member.type == 'file') {
            w.payload(member.content,captured.objects)^
            bytes = bytes+member.content.size
        } else if (member.content != null) { raise error("sync ZIP: captured directory has content") }
        i = i+1
    }
    if (captured.id != capture_model.capture_id(captured.members)^ or captured.bytes != bytes) raise error("sync ZIP: corrupt external capture")
    return true
}

pub fn validate(session) any^ {
    v.fields(session,["endpoint","objects","base","imports","conflicts","notices","source"])^
    p.validate_endpoint(session.endpoint)^
    let base = capture_model.validate_base(session.base)^
    c.merge(session.endpoint.replica,base)^
    if (not c.dominates(session.endpoint.replica.clock,base.clock)^) raise error("sync ZIP: baseline exceeds live state")
    w.objects([],session.objects)^
    var i = 0
    while (i < len(session.base.members)^) {
        let member = session.base.members[i]
        if (member.type == 'file') w.payload(member.content,session.objects)^
        i = i+1
    }
    if (not (session.imports is array) or len(unique([for (record in session.imports) record.id])) != len(session.imports)^)
        raise error("sync ZIP: invalid imports")
    i = 0
    while (i < len(session.imports)^) {
        let record = session.imports[i]
        v.record(record,["id","actor","base_id","fingerprint","capture","hints","status"],["reason"])^
        validate_capture(record.capture)^
        if (not contains(['merged','conflict','review','resolved'],record.status)^ or
            record.fingerprint != codec.digest([record.actor,record.base_id,record.capture.id,record.hints])^)
            raise error("sync ZIP: corrupt external capture")
        i = i+1
    }
    if (not (session.conflicts is array) or not (session.notices is array) or
        not all([for (notice in session.notices) notice is string])) raise error("sync ZIP: invalid review state")
    i = 0
    while (i < len(session.conflicts)^) {
        let conflict = session.conflicts[i]
        v.fields(conflict,["kind","import_id","reason"])^
        if (conflict.kind != 'external-review' or v.lookup(session.imports,conflict.import_id)^.status != 'review' or
            not (conflict.reason is string)) raise error("sync ZIP: invalid external review")
        i = i+1
    }
    if (session.source != null) {
        v.fields(session.source,["path","hash"])^
        if (not (session.source.path is string) or not (session.source.hash is binary) or len(session.source.hash) != 32)
            raise error("sync ZIP: invalid source evidence")
    }
    return true
}

pub fn seal(session) any^ {
    validate(session)^
    let data = {*:session,endpoint:store.seal(session.endpoint)^}
    return codec.encode({format:"lambda-zip-sync-bundle",version:1,digest:codec.digest(data)^,data:data})^
}

pub fn load(encoded, actor) any^ {
    let bundle = codec.decode(encoded)^
    v.fields(bundle,["format","version","digest","data"])^
    if (bundle.format != "lambda-zip-sync-bundle" or bundle.version != 1 or bundle.digest != codec.digest(bundle.data)^)
        raise error("sync ZIP: corrupt restart bundle")
    let restored = store.load(bundle.data.endpoint,actor)^
    let session = {*:bundle.data,endpoint:restored.endpoint}
    validate(session)^
    if (any([for (record in session.imports) record.actor == actor])) raise error("sync ZIP: restoration requires a fresh writer actor")
    return {session:session,actor:actor}
}

// read/check/write matches the editor save boundary; host CAS/locking can close
// the final check-to-replacement race on filesystems that provide that contract.
pub pn save(session, target, expected_hash=null) {
    let prepared = prepare_save(session)^
    let same = session.source != null and paths.normal_path(target)^ == session.source.path
    let expected = if (expected_hash != null) expected_hash else if (same) session.source.hash else null
    let present = exists(target)
    if ((present and (expected == null or hash(input(target,'binary')^,'sha256')^ != expected)) or
        (not present and expected != null)) raise error("sync ZIP: target changed; capture external changes before saving")
    output(prepared.tree,target,{format:'zip',deterministic:true})^
    let raw = input(target,'binary')^
    if (capture(parse(raw,'zip')^)^.id != prepared.base.capture) raise error("sync ZIP: output changed before save confirmation")
    return {*:commit_save(session,prepared)^,source:{path:paths.normal_path(target),hash:hash(raw,'sha256')^}}
}

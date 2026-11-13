// bounded disk captures; namespace reconciliation reuses the ZIP/workspace algorithm.
import disk: lambda.io.disk
import repo: .repository
import cap: .workspace_capture
import v: .schema
import w: .workspace
import p: .protocol
import rules: .workspace_rules
import c: .core
import codec: .codec

pub let default_limits = {file_bytes:268435456,scan_bytes:1073741824}
pub let capture_changed_code = 8318
fn capture_changed(reason) error => error({code:capture_changed_code,message:"sync filesystem: " ++ reason})
pn capture_failure(root, path, before, failure, reason) any^ {
    if (failure.code == 319) raise failure
    let after = disk.inspect(root,path)^
    if (after == null or after.token != before.token) raise capture_changed(reason)
    raise failure
}
pub fn limits(current) any^ {
    let value = if (current.capture_limits == null) default_limits else current.capture_limits
    v.fields(value,["file_bytes","scan_bytes"])^
    if (not v.natural(value.file_bytes)^ or not v.natural(value.scan_bytes)^ or
        value.file_bytes > 68719476736 or value.scan_bytes < value.file_bytes)
        raise error("sync filesystem: invalid capture limits")
    return value
}
pub pn capture_file(tx, keys, root, path, config, maximum, token=null) any^ {
    let observed = disk.inspect(root,path)^
    if (observed == null or observed.kind != 'file' or (token != null and observed.token != token))
        raise capture_changed("capture changed or exceeds limit")
    if (observed.size > maximum) raise error("sync filesystem: capture changed or exceeds limit")
    if (observed.size <= 8388608) {
        let item = disk.capture(root,path,8388608) ^ {
            capture_failure(root,path,observed,^,"file changed during capture")^
        }
        if (item.token != observed.token) raise capture_changed("file changed during capture")
        return {*:item,content:repo.content(tx,keys,item.bytes,config)^}
    }
    let item = repo.content_file(tx,keys,root,path,config,maximum,observed.token) ^ {
        capture_failure(root,path,observed,^,"file changed during capture")^
    }
    return {*:item.source,content:item.content}
}
pub pn observe(root, path, maximum=268435456) any^ {
    let item = disk.read_stream(root,path,{max_bytes:maximum},pn (acc,bytes)=> { return null })^
    return {*:item.source,hash:item.hash}
}
pub fn matches(reference, observation) bool^ => reference.size == observation.size and reference.hash == observation.hash
fn overlaps(path, targets) any^ => targets == null or any([for (target in targets)
    path == target or starts_with(path,target ++ "/")^ or starts_with(target,path ++ "/")^])
pn walk(tx, keys, root, prefix, result, config, bounds, previous, depth=0, targets=null, old_members=[]) any^ {
    if (depth > 128) raise error("sync filesystem: depth limit")
    let before = disk.inspect(root,prefix)^
    if (before.kind != 'dir') raise capture_changed("missing directory")
    let listed = disk.entries(root,prefix,10000) ^ {
        capture_failure(root,prefix,before,^,"directory changed during scan")^
    }
    let children = sort(listed,(child)=>child.name)^
    var captured = result
    var i = 0
    while (i < len(children)) {
        let child = children[i]
        if (not rules.component(child.name)^ or len(captured.members)^ >= 10000)
            raise error("sync filesystem: unsafe name or entry limit")
        let path = if (prefix == "") child.name else prefix ++ "/" ++ child.name
        var reference = null
        var token = child.token
        if (child.kind == 'file') {
            if (child.size > bounds.file_bytes or child.size > bounds.scan_bytes-captured.bytes)
                raise error("sync filesystem: capture byte limit")
            let old = previous[path]
            // an exact inode/revision observation reuses the previously authenticated content reference.
            if (old != null and old.type == 'file' and old.identity == child.identity and old.token == child.token) {
                reference = old.content
                let after = disk.inspect(root,path)^
                if (after == null or after.identity != child.identity or after.token != child.token)
                    raise capture_changed("cached file observation changed")
            } else {
                let item = capture_file(tx,keys,root,path,config,bounds.file_bytes,child.token)^
                reference = item.content
                token = item.token
            }
            captured = {*:captured,bytes:captured.bytes+reference.size}
        }
        captured = {*:captured,members:[*captured.members,
            {path:path,type:child.kind,identity:child.identity,token:token,content:reference}]}
        if (child.kind == 'dir') {
            let old = previous[path]
            if (not overlaps(path,targets)^ and old != null and old.type == 'dir' and
                old.identity == child.identity and old.token == child.token) {
                // A stable unhinted subtree keeps its accepted observations; verification scans audit it.
                let retained = [for (member in old_members where starts_with(member.path,path ++ "/")^)
                    {path:member.path,type:member.type,identity:member.identity,token:member.token,content:member.content}]
                let bytes = sum([for (member in retained where member.type == 'file') member.content.size])
                if (len(captured.members)^+len(retained)^ > 10000 or bytes > bounds.scan_bytes-captured.bytes or
                    any([for (member in retained where member.type == 'file') member.content.size > bounds.file_bytes]))
                    raise error("sync filesystem: capture limit")
                captured = {members:[*captured.members,*retained],bytes:captured.bytes+bytes}
            } else {
                captured = walk(tx,keys,root,path,captured,config,bounds,previous,depth+1,targets,old_members)^
            }
        }
        i = i+1
    }
    if (disk.inspect(root,prefix)^.token != before.token) raise capture_changed("directory changed during scan")
    return captured
}

pn capture_selected(tx, keys, current, targets, verify=false) any^ {
    let root = disk.inspect(current.root,"")^
    if (root.kind != 'dir' or root.identity != current.root_identity)
        raise error("sync filesystem: working root missing or replaced")
    // Notifications invalidate content even when an external writer preserves size/mtime.
    let cached = map([for (member in current.disk where not verify and
        not (targets != null and member.type == 'file' and overlaps(member.path,targets)^)) (member.path,member)])
    let result = walk(tx,keys,current.root,"",{members:[],bytes:0},current.chunker,limits(current)^,
        cached,0,targets,current.disk)^
    let members = sort(result.members,(member)=>member.path)^
    if (len(unique([for (member in members) lower(member.path)^])) != len(members))
        raise error("sync filesystem: ambiguous portable path spelling")
    if (disk.inspect(current.root,"")^.token != root.token) raise capture_changed("root changed during scan")
    return {*:result,members:members,id:cap.capture_id(members)^}
}
pn stable_capture(tx, keys, current, targets, verify, attempts) any^ {
    if (not v.natural(attempts)^ or attempts < 1 or attempts > 16) raise error("sync filesystem: invalid capture retry budget")
    var i = 0
    while (i < attempts) {
        let result = capture_selected(tx,keys,current,targets,verify) ^ { ^ }
        if (not (result is error)) { return result }
        // External writes can race installation's readback; retry a fresh bounded snapshot without closing the peer.
        if (result.code != capture_changed_code or i+1 == attempts) raise result
        i = i+1
    }
    raise error("sync filesystem: capture retry budget exhausted")
}
pub pn capture(tx, keys, current, verify=false, attempts=3) any^ {
    return stable_capture(tx,keys,current,null,verify,attempts)^
}
pub pn capture_paths(tx, keys, current, paths, attempts=3) any^ {
    if (not (paths is array) or len(paths)^ > 256) raise error("sync filesystem: invalid notification batch")
    for (path in paths) {
        if (not (path is string) or len(path)^ > 4096 or
            not all([for (part in split(path,"/")^) rules.component(part)^]))
            raise error("sync filesystem: unsafe notification path")
    }
    return stable_capture(tx,keys,current,if (len(paths)^ == 0) null else unique(paths),false,attempts)^
}

fn hints(previous, captured) any^ {
    var result = []
    var i = 0
    while (i < len(previous)) {
        let old = previous[i]
        let matches = [for (member in captured where member.identity == old.identity and member.type == old.type) member]
        if (len(matches) == 1 and matches[0].path != old.path) {
            result = [*result,{entry:old.id,path:matches[0].path}]
        }
        i = i+1
    }
    return result
}

fn merge_capture(endpoint, branch, actor) any^ {
    var merged = endpoint
    var rounds = 0
    // The branch was authored from the validated disk base. Apply its atomic deltas
    // instead of validating and unioning the entire historical replica again.
    while (not c.dominates(merged.replica.clock,branch.replica.clock)^) {
        if (rounds >= 10000) raise error("sync filesystem: capture transaction budget")
        let packet = p.next(branch,w.have(merged)^)^
        if (packet.kind != 'transaction' and packet.kind != 'updates') { return p.join_branch(endpoint,branch)^ }
        merged = p.accept(merged,codec.encode(packet)^,{actors:[actor],checkpoints:[]})^
        rounds = rounds+1
    }
    return p.compact_log(merged,branch.replica.floor)^
}

pub fn reconciled(current, captured, actor, documents=[], saved_at=null) any^ {
    if (current.installation != null) raise error("sync filesystem: installation must finish before scanning")
    if (len(documents) == 0 and current.review == null and captured.id == current.base.capture and
        [for (member in current.disk) {path:member.path,type:member.type,identity:member.identity,
            token:member.token,content:member.content}] == captured.members) { return current }
    let associated = cap.correspondence(current.base,captured,hints(current.disk,captured.members)^)^
    // correspondence preserves baseline order while changing only projected paths.
    let known = [for (i in 0 to (len(associated)^-1))
        {*:associated[i],original_path:current.base.members[i].path}]
    let imported:any^ = cap.import_branch(p.start(current.base.checkpoint.replica,w.model)^,actor,captured,known,documents,saved_at)
    if (imported is error) {
        return {*:current,serial:current.serial+1,review:{capture:captured,reason:imported.message}}
    }
    let endpoint = merge_capture(current.endpoint,imported.endpoint,actor)^
    let imported_paths = cap.index_paths(imported.members)^
    let members = [for (member in captured.members) {*:member,id:imported_paths[member.path].id}]
    return {*:current,serial:current.serial+1,endpoint:endpoint,
        base:cap.baseline(imported.endpoint.replica,imported.members,captured.id)^,disk:members,review:null}
}

pub fn desired_entries(entries) any^ {
    if (any([for (entry in entries where entry.visible and entry.type == 'file') len(entry.heads)^ != 1]))
        raise error("sync filesystem: resolve competing revisions before installation")
    let members = cap.export_members(entries)^
    if (len(unique([for (member in members) lower(member.path)^])) != len(members))
        raise error("sync filesystem: ambiguous portable path spelling")
    return members
}
pub fn desired(endpoint) any^ { return desired_entries(w.read(endpoint.replica)^.entries)^ }

// bounded disk captures; namespace reconciliation reuses the ZIP/workspace algorithm.
import disk: lambda.io.disk
import repo: .repository
import cap: .workspace_capture
import v: .schema
import w: .workspace
import p: .protocol
import rules: .workspace_rules

pub let default_limits = {file_bytes:268435456,scan_bytes:1073741824}
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
    if (observed == null or observed.kind != 'file' or observed.size > maximum or
        (token != null and observed.token != token)) raise error("sync filesystem: capture changed or exceeds limit")
    if (observed.size <= 8388608) {
        let item = disk.capture(root,path,8388608)^
        if (item.token != observed.token) raise error("sync filesystem: file changed during capture")
        return {*:item,content:repo.content(tx,keys,item.bytes,config)^}
    }
    let item = repo.content_file(tx,keys,root,path,config,maximum,observed.token)^
    return {*:item.source,content:item.content}
}
pub pn observe(root, path, maximum=268435456) any^ {
    let item = disk.read_stream(root,path,{max_bytes:maximum},pn (acc,bytes)=> { return null })^
    return {*:item.source,hash:item.hash}
}
pub fn matches(reference, observation) bool^ => reference.size == observation.size and reference.hash == observation.hash
pn walk(tx, keys, root, prefix, result, config, bounds, previous, depth=0) any^ {
    if (depth > 128) raise error("sync filesystem: depth limit")
    let before = disk.inspect(root,prefix)^
    if (before.kind != 'dir') raise error("sync filesystem: missing directory")
    let children = sort(disk.entries(root,prefix,10000)^,(child)=>child.name)^
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
                    raise error("sync filesystem: cached file observation changed")
            } else {
                let item = capture_file(tx,keys,root,path,config,bounds.file_bytes,child.token)^
                reference = item.content
                token = item.token
            }
            captured = {*:captured,bytes:captured.bytes+reference.size}
        }
        captured = {*:captured,members:[*captured.members,
            {path:path,type:child.kind,identity:child.identity,token:token,content:reference}]}
        if (child.kind == 'dir') { captured = walk(tx,keys,root,path,captured,config,bounds,previous,depth+1)^ }
        i = i+1
    }
    if (disk.inspect(root,prefix)^.token != before.token) raise error("sync filesystem: directory changed during scan")
    return captured
}

pub pn capture(tx, keys, current) any^ {
    let root = disk.inspect(current.root,"")^
    if (root.kind != 'dir' or root.identity != current.root_identity)
        raise error("sync filesystem: working root missing or replaced")
    let result = walk(tx,keys,current.root,"",{members:[],bytes:0},current.chunker,limits(current)^,map([for (member in current.disk) (member.path,member)]))^
    let members = sort(result.members,(member)=>member.path)^
    if (len(unique([for (member in members) lower(member.path)^])) != len(members))
        raise error("sync filesystem: ambiguous portable path spelling")
    if (disk.inspect(current.root,"")^.token != root.token) raise error("sync filesystem: root changed during scan")
    return {*:result,members:members,id:cap.capture_id(members)^}
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

pub fn reconciled(current, captured, actor, documents=[], saved_at=null) any^ {
    if (current.installation != null) raise error("sync filesystem: installation must finish before scanning")
    if (len(documents) == 0 and current.review == null and captured.id == current.base.capture and
        [for (member in current.disk) {path:member.path,type:member.type,identity:member.identity,
            token:member.token,content:member.content}] == captured.members) { return current }
    let associated = cap.correspondence(current.base,captured,hints(current.disk,captured.members)^)^
    let known = [for (member in associated) {*:member,original_path:v.lookup(current.base.members,member.id)^.path}]
    let imported:any^ = cap.import_branch(p.start(current.base.checkpoint.replica,w.model)^,actor,captured,known,documents,saved_at)
    if (imported is error) {
        return {*:current,serial:current.serial+1,review:{capture:captured,reason:imported.message}}
    }
    let endpoint = p.join_branch(current.endpoint,imported.endpoint)^
    let members = [for (member in captured.members) {*:member,id:cap.association(imported.members,member.path)^.id}]
    return {*:current,serial:current.serial+1,endpoint:endpoint,
        base:cap.baseline(imported.endpoint.replica,imported.members,captured.id)^,disk:members,review:null}
}

pub fn desired(endpoint) any^ {
    let entries = w.read(endpoint.replica)^.entries
    if (any([for (entry in entries where entry.visible and entry.type == 'file') len(entry.heads)^ != 1]))
        raise error("sync filesystem: resolve competing revisions before installation")
    let members = cap.export_members(entries)^
    if (len(unique([for (member in members) lower(member.path)^])) != len(members))
        raise error("sync filesystem: ambiguous portable path spelling")
    return members
}

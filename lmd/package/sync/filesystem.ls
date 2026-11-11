// bounded disk captures; namespace reconciliation reuses the ZIP/workspace algorithm.
import disk: lambda.io.disk
import repo: .repository
import cap: .workspace_capture
import v: .schema
import w: .workspace
import p: .protocol
import rules: .workspace_rules

pn walk(tx, keys, root, prefix, result, config, depth=0) any^ {
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
            if (child.size > 8388608 or child.size > 67108864-captured.bytes)
                raise error("sync filesystem: capture byte limit")
            let item = disk.capture(root,path,8388608)^
            if (item.token != child.token) raise error("sync filesystem: file changed during scan")
            reference = repo.content(tx,keys,item.bytes,config)^
            token = item.token
            captured = {*:captured,bytes:captured.bytes+reference.size}
        }
        captured = {*:captured,members:[*captured.members,
            {path:path,type:child.kind,identity:child.identity,token:token,content:reference}]}
        if (child.kind == 'dir') { captured = walk(tx,keys,root,path,captured,config,depth+1)^ }
        i = i+1
    }
    if (disk.inspect(root,prefix)^.token != before.token) raise error("sync filesystem: directory changed during scan")
    return captured
}

pub pn capture(tx, keys, current) any^ {
    let root = disk.inspect(current.root,"")^
    if (root.kind != 'dir' or root.identity != current.root_identity)
        raise error("sync filesystem: working root missing or replaced")
    let result = walk(tx,keys,current.root,"",{members:[],bytes:0},current.chunker)^
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

pub fn reconciled(current, captured, actor) any^ {
    if (current.installation != null) raise error("sync filesystem: installation must finish before scanning")
    if (current.review == null and captured.id == current.base.capture and
        [for (member in current.disk) {path:member.path,type:member.type,identity:member.identity,
            token:member.token,content:member.content}] == captured.members) { return current }
    let associated = cap.correspondence(current.base,captured,hints(current.disk,captured.members)^)^
    let known = [for (member in associated) {*:member,original_path:v.lookup(current.base.members,member.id)^.path}]
    let imported:any^ = cap.import_branch(p.start(current.base.checkpoint.replica,w.model)^,actor,captured,known)
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

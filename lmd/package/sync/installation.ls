// a durable intent precedes each effect; tokens detect substitution, not OS-wide writer exclusion.
import disk: lambda.io.disk
import repo: .repository
import cap: .workspace_capture
import fs: .filesystem
import codec: .codec
import blob: .blob

pub fn prepare(current) any^ {
    if (current.review != null) raise error("sync installation: external capture needs review")
    let desired = fs.desired(current.endpoint)^
    let replaced = [for (member in current.disk where any([for (target in desired)
        target.path == member.path and target.type != member.type])) member.path]
    let early = [for (member in current.disk where any([for (path in replaced)
        member.path == path or starts_with(member.path,path ++ "/")^]))
        {kind:'erase',path:member.path,expected:member,content:null}]
    let additions = [for (member in desired where
        cap.association(current.disk,member.path)^.type != member.type or
        (member.type == 'file' and cap.association(current.disk,member.path)^.content != member.content))
        {kind:if (member.type == 'dir') 'directory' else 'write',path:member.path,
         expected:if (contains(replaced,member.path)^) null else cap.association(current.disk,member.path)^,content:member.content}]
    let removals = sort([for (member in current.disk where cap.association(desired,member.path)^ == null and
        not any([for (op in early) op.path == member.path]))
        {kind:'erase',path:member.path,expected:member,content:null}],(op) any^ =>0-len(op.path)^)^
    let operations = [*sort(early,(op) any^ =>0-len(op.path)^)^,*additions,*removals]
    if (len(operations) == 0) {
        return {*:current,base:cap.baseline(current.endpoint.replica,desired,cap.capture_id(desired)^)^,
            disk:[for (member in current.disk) {*:member,id:cap.association(desired,member.path)^.id}]}
    }
    return {*:current,installation:{id:codec.digest([current.base.id,current.endpoint.replica.clock,desired])^,
        endpoint:current.endpoint,desired:desired,operations:operations,next:0,blocked:null,recoveries:[]}}
}

pn hold(tx, keys, current, operation, observed, reason) any^ {
    let captured = if (observed != null and observed.kind == 'file')
        fs.capture_file(tx,keys,current.root,operation.path,current.chunker,fs.limits(current)^.file_bytes,observed.token)^ else null
    let reference = if (captured == null) null else captured.content
    let recovery = {path:operation.path,content:reference,observed:observed,reason:reason}
    return {*:current,installation:{*:current.installation,blocked:recovery,
        recoveries:unique([*current.installation.recoveries,recovery])}}
}

pub pn step(tx, keys, current, actor) any^ {
    let journal = current.installation
    if (journal == null) { return current }
    if (disk.inspect(current.root,"")^.identity != current.root_identity)
        raise error("sync installation: working root replaced")
    if (journal.next >= len(journal.operations)^) {
        let actual = fs.capture(tx,keys,current)^
        let wanted = journal.desired
        let expected_id = cap.capture_id(wanted)^
        if (actual.id != expected_id) {
            let touched = unique([for (operation in journal.operations) operation.path])
            let intact = all([for (path in touched) {
                let member = cap.association(wanted,path)^
                let found = cap.association(actual.members,path)^
                if (member == null) found == null else
                    found != null and found.type == member.type and found.content == member.content
            }])
            if (intact) {
                // Unrelated edits cannot invalidate completed guarded writes. Untouched paths
                // retain their old inode evidence while the installed frontier becomes the base.
                let established = {*:current,installation:null,
                    base:cap.baseline(journal.endpoint.replica,wanted,expected_id)^,
                    disk:[for (member in wanted) {
                        let old = cap.association(current.disk,member.path)^
                        let observed = cap.association(actual.members,member.path)^
                        let evidence = if (not contains(touched,member.path)^ and old != null) old else observed
                        {*:evidence,id:member.id,path:member.path,type:member.type,content:member.content}
                    }]}
                return fs.reconciled(established,actual,actor,[],now().unix)^
            }
            return {*:current,installation:{*:journal,blocked:{reason:"tree changed before completion",capture:actual},
                recoveries:unique([*journal.recoveries,{capture:actual}])}}
        }
        let members = [for (member in actual.members) {*:member,id:cap.association(wanted,member.path)^.id}]
        return {*:current,base:cap.baseline(journal.endpoint.replica,wanted,expected_id)^,
            disk:members,installation:null}
    }
    let operation = journal.operations[journal.next]
    let observed = disk.inspect(current.root,operation.path)^
    var done = false
    var action = null
    if (operation.kind == 'directory') {
        if (observed != null and observed.kind != 'dir') {
            return hold(tx,keys,current,operation,observed,"directory path occupied")^
        }
        action = disk.directory(current.root,operation.path)
        done = not (action is error)
    } else if (operation.kind == 'write') {
        let captured = if (observed == null or observed.kind != 'file') null else fs.observe(current.root,operation.path,fs.limits(current)^.file_bytes)^
        if (captured != null and fs.matches(operation.content,captured)^) { done = true }
        else {
            if ((observed != null and (captured == null or operation.expected == null or
                operation.expected.type != 'file' or not fs.matches(operation.expected.content,captured)^)) or
                (observed == null and operation.expected != null)) {
                return hold(tx,keys,current,operation,observed,"external change before write")^
            }
            action = repo.install_content(tx,keys,operation.content,current.root,operation.path,if (captured == null) null else captured.token)
            if (not (action is error)) {
                let readback = fs.observe(current.root,operation.path,fs.limits(current)^.file_bytes)^
                if (not fs.matches(operation.content,readback)^) {
                    return hold(tx,keys,current,operation,readback,"external change after write")^
                }
                done = true
            }
        }
    } else {
        if (observed == null) { done = true }
        else if (observed.kind != operation.expected.type) {
            return hold(tx,keys,current,operation,observed,"external replacement before removal")^
        } else {
            if (observed.kind == 'file') {
                let captured = fs.observe(current.root,operation.path,fs.limits(current)^.file_bytes)^
                if (not fs.matches(operation.expected.content,captured)^) {
                    return hold(tx,keys,current,operation,observed,"external change before removal")^
                }
            } else if (len(disk.entries(current.root,operation.path,10000)^) != 0) {
                return hold(tx,keys,current,operation,observed,"directory contains external entries")^
            }
            action = disk.erase(current.root,operation.path,observed.token)
            done = not (action is error)
        }
    }
    let updated = {*:journal,next:journal.next+(if (done) 1 else 0),
        blocked:if (done) null else {path:operation.path,reason:action.message}}
    return {*:current,installation:updated}
}

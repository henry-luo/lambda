// profile validation is shared by ready operations and compacted checkpoints.
import v: .schema
import codec: .codec
import tree: .workspace_tree

pub fn component(value) any^ => value is string and value != "" and value != "." and value != ".." and
    not contains(value,"/")^ and not contains(value,"\\")^ and not contains(value,"\u0000")^

fn digest(value) => value is binary and len(value) == 32

pub fn document(value) any^ {
    if (value == null) { return true }
    v.fields(value,["document","profile","epoch","schema","frontier","checkpoint"])^
    if (not v.actor(value.document)^ or not contains(['text','mark','workspace'],value.profile)^ or
        not v.natural(value.epoch)^ or not (value.schema is array) or len(value.schema) != 2 or
        not v.actor(value.schema[0])^ or not v.natural(value.schema[1])^ or value.schema[1] == 0 or
        not digest(value.checkpoint) or (value.profile == 'workspace' and value.schema != ["workspace",1]))
        raise error("sync workspace: invalid document link")
    v.clock(value.frontier)^
    return true
}

pub fn revision(workspace, epoch, file, parents, content, document=null) any^ {
    let body = {workspace:workspace,epoch:epoch,file:file,parents:sort(parents)^,content:content,document:document}
    let result = {id:codec.digest(["lambda-file-revision-1",body])^,*:body}
    validate_revision(result)^
    return result
}

pub fn validate_revision(value) any^ {
    v.fields(value,["id","workspace","epoch","file","parents","content","document"])^
    if (not digest(value.id) or not v.actor(value.workspace)^ or not v.natural(value.epoch)^ or
        not v.id(value.file)^ or not (value.parents is array) or
        value.parents != sort(unique(value.parents))^ or not all([for (parent in value.parents) digest(parent)]))
        raise error("sync workspace: invalid file revision")
    v.descriptor(value.content)^
    if (value.content.kind != 'blob') raise error("sync workspace: revision requires immutable content")
    document(value.document)^
    if (value.id != codec.digest(["lambda-file-revision-1",{workspace:value.workspace,epoch:value.epoch,file:value.file,parents:value.parents,
        content:value.content,document:value.document}])^) raise error("sync workspace: revision digest mismatch")
    return true
}

pub fn shape(event) any^ {
    if (not (event is map)) raise error("sync workspace: expected event")
    if (event.action == 'create') {
        v.fields(event,["action","type","parent","name","revision"])^
        if (not contains(['file','dir'],event.type)^ or not v.id(event.parent,true)^ or not component(event.name)^ or
            (event.type == 'dir' and event.revision != null)) raise error("sync workspace: invalid creation")
        if (event.type == 'file') validate_revision(event.revision)^
    } else if (event.action == 'place') {
        v.fields(event,["action","entry","parent","name"])^
        if (not v.id(event.entry)^ or not v.id(event.parent,true)^ or not component(event.name)^)
            raise error("sync workspace: invalid placement")
    } else if (event.action == 'delete') {
        v.fields(event,["action","entry"])^
        if (not v.id(event.entry)^) raise error("sync workspace: invalid deletion")
    } else if (event.action == 'restore') {
        v.fields(event,["action","entry","deletions"])^
        if (not v.id(event.entry)^ or not (event.deletions is array) or len(event.deletions) == 0 or
            event.deletions != sort(unique(event.deletions))^ or not all([for (id in event.deletions) v.id(id)^]))
            raise error("sync workspace: invalid restoration")
    } else if (event.action == 'publish') {
        v.fields(event,["action","entry","revision"])^
        if (not v.id(event.entry)^) raise error("sync workspace: invalid file publication")
        validate_revision(event.revision)^
    } else { raise error("sync workspace: unknown event") }
    return true
}

pub fn envelope(replica, op) any^ {
    if (replica.profile == 'workspace') {
        if (op.kind != 'insert' or op.node != v.root or op.slot != 'items' or op.left != null or op.value.kind != 'workspace')
            raise error("sync workspace: only immutable root events are allowed")
        shape(op.value.event)^
    } else if (op.kind == 'insert' or op.kind == 'set') {
        if (op.value.kind == 'workspace') raise error("sync workspace: event outside workspace profile")
    }
    return true
}

fn creation(replica, id, context, directory=false) any^ {
    if (id == v.root) { return {type:'dir'} }
    let atom = v.lookup(replica.atoms,id)^
    if (atom == null or atom.value.event.action != 'create' or not tree.seen(context,id)^ or
        (directory and atom.value.event.type != 'dir')) raise error("sync workspace: unobserved or invalid entry")
    return atom.value.event
}

pub fn ready(replica, op) any^ {
    if (replica.profile != 'workspace') { return true }
    let event = op.value.event
    if (event.action == 'create' or event.action == 'place') creation(replica,event.parent,op.context,true)^
    if (event.action != 'create') creation(replica,event.entry,op.context)^
    if (event.action == 'restore') {
        var i = 0
        while (i < len(event.deletions)^) {
            let deleted = v.lookup(replica.atoms,event.deletions[i])^
            if (deleted == null or deleted.value.event.action != 'delete' or deleted.value.event.entry != event.entry or
                not tree.seen(op.context,deleted.id)^) raise error("sync workspace: restore requires observed deletions")
            i = i+1
        }
    }
    if ((event.action == 'create' and event.type == 'file') or event.action == 'publish') {
        let file = if (event.action == 'create') op.id else event.entry
        if (event.revision.workspace != replica.document or event.revision.epoch != replica.epoch or
            event.revision.file != file or (event.action == 'publish' and creation(replica,file,op.context)^.type != 'file'))
            raise error("sync workspace: revision file mismatch")
        let known = [for (atom in replica.atoms where tree.seen(op.context,atom.id)^ and
            (atom.value.event.action == 'publish' or (atom.value.event.action == 'create' and atom.value.event.type == 'file')))
            atom.value.event.revision]
        if (not all([for (parent in event.revision.parents)
            any([for (revision in known) revision.id == parent and revision.file == file])]))
            raise error("sync workspace: unobserved revision parent")
        if (event.action == 'create' and len(event.revision.parents) != 0) raise error("sync workspace: initial revision has parents")
    }
    return true
}

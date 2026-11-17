// test-only trusted local exchange; production hosts must supply authenticated admission.
import local: lambda.sync.local
import w: lambda.sync.workspace
import c: lambda.sync.core
import p: lambda.sync.protocol
import codec: lambda.sync.codec
import repo: lambda.sync.repository
import packs: lambda.sync.pack_store
import hex: lambda.security.tuf.metadata

pub pn physical(handle, id) any^ {
    return repo.access(handle.path,handle.keys,pn (tx,k)=> {
        let found = packs.location(k,id)^
        return if (found == null) hex.hex(id)^ ++ ".object" else found.pack ++ ".pack"
    })^
}

pub pn transfer(source, destination) any^ {
    let available = local.inventory(destination)^
    let missing = [for (id in local.inventory(source)^ where not contains(available,id)^) id]
    var i = 0
    while (i < len(missing)) {
        local.import_objects(destination,local.export_objects(source,slice(missing,i,i+128)^)^)^
        i = i+128
    }
    let original = local.status(source)^.state.endpoint
    let digest = codec.digest(p.checkpoint(original.replica))^
    let admission = {actors:[for (entry in original.replica.clock) entry.actor],checkpoints:[digest]}
    var rounds = 0
    while (not c.dominates(local.status(destination)^.state.endpoint.replica.clock,original.replica.clock)^) {
        if (rounds >= 100) raise error("sync local test: exchange did not complete")
        let target = local.status(destination)^.state.endpoint
        let packets = local.messages(source,w.have(target)^)^
        var index = 0
        while (index < len(packets)) {
            local.receive(destination,packets[index],admission)^
            index = index+1
        }
        rounds = rounds+1
    }
    return true
}

pub fn at_path(current, path) any^ {
    let entries = w.read(current.endpoint.replica)^.entries
    let matches = [for (entry in entries where entry.visible and w.path(current.endpoint.replica,entry.id)^ == path) entry]
    if (len(matches) != 1) raise error("sync local test: path missing or ambiguous")
    return matches[0]
}

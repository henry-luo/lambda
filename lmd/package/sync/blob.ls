// indivisible immutable descriptors; transfer and encryption are host responsibilities.
import c: .core
import v: .schema

pub fn reference(manifest, size, digest, media_type="application/octet-stream") any^ {
    let descriptor = {kind:'blob',manifest:manifest,size:size,hash:digest,media_type:media_type}
    v.descriptor(descriptor)^
    return descriptor
}

pub fn required(replica) any^ => unique([*[for (cell in replica.cells where cell.value.kind == 'blob') cell.value],
    *[for (atom in replica.atoms where atom.value != null and atom.value.kind == 'blob' and
        not contains(replica.dead,atom.id)^) atom.value],
    *[for (atom in replica.atoms where atom.value.kind == 'workspace' and
        (atom.value.event.action == 'publish' or (atom.value.event.action == 'create' and atom.value.event.type == 'file')))
        atom.value.event.revision.content]])

pub fn checkpoints(replica) any^ => unique([for (atom in replica.atoms where atom.value.kind == 'workspace' and
    (atom.value.event.action == 'publish' or (atom.value.event.action == 'create' and atom.value.event.type == 'file')) and
    atom.value.event.revision.document != null) atom.value.event.revision.document.checkpoint])

pub fn verify(descriptor, bytes) any^ {
    v.descriptor(descriptor)^
    // empty binary is null (S2.2.4); successful receipt remains a host obligation.
    let sized = (bytes is binary and len(bytes) == descriptor.size) or (bytes == null and descriptor.size == 0)
    return descriptor.kind == 'blob' and sized and hash(bytes,'sha256')^ == descriptor.hash
}

// includes losing alternatives and detached nodes conservatively, plus retained versions.
pub fn roots(live, versions) any^ => unique([*required(live)^,
    *[for (saved in versions) for (descriptor in required(saved.replica)^) descriptor]])

pub fn missing(replica, available) any^ => [*[for (descriptor in required(replica)^
    where not contains(available,descriptor.manifest)^) descriptor],
    *[for (digest in checkpoints(replica)^ where not contains(available,digest)^) {kind:'checkpoint',manifest:digest}]]

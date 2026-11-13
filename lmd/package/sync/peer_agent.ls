// one native host owns credentials and filesystem effects; apps receive no implicit network grant.
import identity: .peer_identity
import peer: .peer
import local: .local
import disk: lambda.io.disk
import crypto: lambda.io.crypto
import ws: lambda.io.websocket
import v: .schema
import workspace: .workspace
import protocol: .protocol
import documents: .document_state
import cloud: .cloud
import owner: .task_owner

fn cloud_saved(current) any^ => any([for (provider in (if (current.cloud_objects == null) [] else current.cloud_objects))
    cloud.covered(current,provider.provider)^])

pub pn device(directory, wrapping_key) any^ {
    if (disk.inspect(directory,"device.sqlite")^ != null) raise error("sync agent: device already exists")
    let credentials = identity.create(uuid()^,crypto.key()^)^
    identity.save(directory,wrapping_key,credentials)^
    return credentials.device
}
pub pn initialize(directory, wrapping_key, root, space, label) any^ {
    if (disk.inspect(directory,"repo.sqlite")^ != null) raise error("sync agent: repository already exists")
    if (not v.actor(space)^) raise error("sync agent: invalid space")
    let selected = identity.update(directory,wrapping_key,(credentials)=> {*:credentials,space:space})^
    local.create(directory ++ "/repo.sqlite",selected.secret,selected.space,root,label)^
    return selected.device
}
pub pn invite(directory, wrapping_key, target) any^ {
    let credentials = identity.load(directory,wrapping_key)^
    if (disk.inspect(directory,"repo.sqlite")^ == null) raise error("sync agent: initialize a space before inviting")
    let offer = identity.invite(credentials,target)^
    identity.update(directory,wrapping_key,(current) any^ => identity.trust(current,target)^)^
    return offer
}
pub pn enroll(directory, wrapping_key, root, label, offer, expected) any^ {
    if (disk.inspect(directory,"repo.sqlite")^ != null) raise error("sync agent: cannot enroll an existing repository")
    let credentials = identity.update(directory,wrapping_key,
        (current) any^ => identity.enroll(current,offer,expected)^)^
    local.create(directory ++ "/repo.sqlite",credentials.secret,credentials.space,root,label)^
    return credentials.device
}
pub pn revoke(directory, wrapping_key, id) any^ {
    identity.update(directory,wrapping_key,(current)=> identity.revoke(current,id))^
    return true
}
pub pn open_agent(directory, wrapping_key) any^ {
    let credentials = identity.load(directory,wrapping_key)^
    let handle = local.open_repository(directory ++ "/repo.sqlite",credentials.secret,credentials.space)^
    return {handle:handle,credentials:credentials}
}
// resume a durable install before scanning; a raced install keeps its recovery capture.
pub pn resume_agent(directory, wrapping_key) any^ {
    let opened = open_agent(directory,wrapping_key)^
    let pending = resume_installation(opened.handle)^
    if (pending.state.installation != null)
        raise error("Sync needs review of an interrupted installation. Local files and recovery versions are preserved.")
    return opened
}
pub pn resume_installation(handle) any^ {
    if (handle.keys.owner != null and handle.keys.owner != self()) {
        return owner.cached(handle.keys.owner,pn (cache)=> {
            return resume_installation({*:handle,keys:{*:handle.keys,cached_head:cache}})^
        })^
    }
    var pending = local.status(handle)^
    while (pending.state.installation != null) {
        let resumed = materialize(handle)^
        if (resumed.state.installation != null and
            (resumed.state.installation.blocked != null or resumed.id == pending.id)) {
            return resumed
        }
        pending = resumed
    }
    return pending
}
pub pn status(directory, wrapping_key) any^ {
    let agent = open_agent(directory,wrapping_key)^
    let current = local.status(agent.handle)^.state
    return {device:agent.credentials.device,space:agent.credentials.space,
        peers:[for (member in agent.credentials.members) member.id],
        frontier:current.endpoint.replica.clock,replicated:current.endpoint.peers,
        review:current.review != null,installation:current.installation != null,
        conflicts:workspace.read(current.endpoint.replica)^.conflicts,
        cloud_saved:cloud_saved(current)^}
}
pub pn materialize(handle) any^ {
    if (handle.keys.owner != null and handle.keys.owner != self()) {
        return owner.cached(handle.keys.owner,pn (cache)=> {
            return materialize({*:handle,keys:{*:handle.keys,cached_head:cache}})^
        })^
    }
    let current = local.status(handle)^
    if (current.state.installation == null and
        current.state.base.checkpoint.replica.epoch == current.state.endpoint.replica.epoch and
        current.state.base.checkpoint.replica.clock == current.state.endpoint.replica.clock) {
        // No managed changes require writes; external edits enter through capture, not ACKs.
        return current
    }
    let proof = current.repository_cache.validation
    let entries = if (proof == null) workspace.read(current.state.endpoint.replica)^.entries else proof.projection.entries
    // content alternatives and retained external captures require an explicit resolution.
    if (current.state.review != null or any([for (entry in entries where entry.visible and entry.type == 'file')
        len(entry.heads)^ != 1])) { return current }
    return local.materialize(handle)^
}
pub pn current_member(directory, wrapping_key, original, id) any^ {
    let current = identity.load(directory,wrapping_key)^
    if (current.space != original.space or current.device != original.device)
        raise error("sync agent: local identity/space changed")
    return identity.member(current,id)^
}
pub pn sync(directory, wrapping_key, url, expected) any^ {
    let agent = resume_agent(directory,wrapping_key)^
    let result = peer.synchronize(agent.handle,agent.credentials,url,expected,
        pn (id)=> { return current_member(directory,wrapping_key,agent.credentials,id)^ })^
    let installed = materialize(agent.handle)^
    return {*:result,review:installed.state.review != null,installation:installed.state.installation != null,
        conflicts:workspace.read(installed.state.endpoint.replica)^.conflicts,
        cloud_saved:cloud_saved(installed.state)^}
}
pub pn serve(directory, wrapping_key, port, bind="127.0.0.1", max_sessions=0, ready=null) any^ {
    // capture the initial working tree before advertising a ready listener or starting peer deadlines.
    let prepared = resume_agent(directory,wrapping_key)^
    local.scan(prepared.handle)^
    let listener = peer.listener(port,bind)^
    if (ready != null) output(string(listener.port),ready,{format:'text',atomic:true})^
    var completed = 0
    while (max_sessions == 0 or completed < max_sessions) {
        // reload local membership for every new connection, including revocations.
        let agent = open_agent(directory,wrapping_key)^
        let accepted = peer.accept(listener,agent.handle,agent.credentials,1000,
            pn (id)=> { return current_member(directory,wrapping_key,agent.credentials,id)^ }) ^ { ^ }
        if (accepted is error) { continue }
        materialize(agent.handle)^
        completed = completed+1
    }
    ws.close(listener)^
    return {sessions:completed}
}
pub pn watch(directory, wrapping_key, url, expected, interval_ms=5000, rounds=0) any^ {
    if (not v.natural(interval_ms)^ or interval_ms < 1000 or interval_ms > 3600000 or not v.natural(rounds)^)
        raise error("sync agent: invalid scheduling bounds")
    var i = 0
    var failures = 0
    var latest = null
    while (rounds == 0 or i < rounds) {
        let result = sync(directory,wrapping_key,url,expected) ^ { ^ }
        if (result is error) { failures = failures+1; latest = {connected:false} }
        else { failures = 0; latest = {*:result,connected:true} }
        i = i+1
        if (rounds == 0 or i < rounds) sleep(min(60000,interval_ms*(1+min(failures,11))))^
    }
    return latest
}

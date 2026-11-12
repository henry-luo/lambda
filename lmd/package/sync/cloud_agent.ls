// P2P stays preferred; cloud publication/polling runs independently for durable offline delivery.
import peer: .peer_agent
import identity: .peer_identity
import cloud: .cloud
import schema: .cloud_schema
import transport: .cloud_transport
import local: .local
import protocol: .protocol
import v: .schema
import network: .peer
import ws: lambda.io.websocket

pub pn status(directory, wrapping_key, config=null) any^ {
    let selected = peer.status(directory,wrapping_key)^
    if (config == null) { return selected }
    let agent = peer.open_agent(directory,wrapping_key)^
    return {*:selected,cloud_saved:cloud.covered(local.status(agent.handle)^.state,cloud.provider(config)^)^}
}
pub pn synchronize(directory, wrapping_key, config, direct=null, publish_cloud=true) any^ {
    let agent = peer.open_agent(directory,wrapping_key)^
    var direct_result = null
    if (direct != null) {
        direct_result = peer.sync(directory,wrapping_key,direct.url,direct.peer) ^ { ^ }
    }
    let reachable = if (direct_result is error) false else direct_result != null
    var cloud_result = null
    if (publish_cloud or not reachable^) {
        local.scan(agent.handle)^
        cloud_result = cloud.synchronize(agent.handle,agent.credentials,config)^
        peer.materialize(agent.handle)^
    }
    let selected = status(directory,wrapping_key,config)^
    return {*:selected,transport:if (reachable^) 'peer' else 'http',peer_connected:reachable,
        cloud_checked:cloud_result != null,cloud:cloud_result}
}
pub pn watch(directory, wrapping_key, config, direct=null, interval_ms=5000, cloud_every=6, rounds=0) any^ {
    if (not v.natural(interval_ms)^ or interval_ms < 1000 or interval_ms > 3600000 or
        not v.natural(cloud_every)^ or cloud_every < 1 or cloud_every > 720 or not v.natural(rounds)^)
        raise error("sync cloud: invalid schedule")
    var i = 0
    var failures = 0
    var latest = null
    while (rounds == 0 or i < rounds) {
        let result = synchronize(directory,wrapping_key,config,direct,i%cloud_every == 0) ^ { ^ }
        if (result is error) { failures = failures+1; latest = {connected:false,cloud_saved:false} }
        else { failures = 0; latest = {*:result,connected:true} }
        i = i+1
        if (rounds == 0 or i < rounds) sleep(min(60000,interval_ms*(1+min(failures,11))))^
    }
    return latest
}
pub pn membership(directory, wrapping_key, config, target, active) any^ {
    let credentials = identity.load(directory,wrapping_key)^
    identity.validate_device(target)^
    if (not (active is bool)) raise error("sync cloud: invalid member activity")
    let remote = transport.call(config,credentials,"pull",{})^
    let control = schema.control(remote.control,config.authority)^
    if (credentials.device.id != control.owner.id) raise error("sync cloud: owner authority required")
    let existing = [for (entry in control.members where entry.device.id == target.id) entry]
    if (len(existing) > 0 and existing[0].device != target) raise error("sync cloud: cannot replace historical identity")
    let next = schema.signed(credentials.seed,"lambda-cloud-control-1",{*:control,generation:control.generation+1,
        members:[*[for (entry in control.members where entry.device.id != target.id) entry],{device:target,active:active}]})^
    return transport.call(config,credentials,"membership",{control:next,expected:remote.control_etag})^
}

// one native owner services peer sessions and periodic cloud work without overlapping writers.
pub pn run(directory, wrapping_key, config, port, bind="127.0.0.1", direct=null,
    interval_ms=5000, rounds=0, ready=null) any^ {
    if (not v.natural(interval_ms)^ or interval_ms < 1000 or interval_ms > 3600000 or not v.natural(rounds)^)
        raise error("sync cloud: invalid agent schedule")
    let listener = network.listener(port,bind)^
    let outcome = run_loop(listener,directory,wrapping_key,config,direct,interval_ms,rounds,ready) ^ { ^ }
    ws.close(listener) ^ { null }
    if (outcome is error) raise outcome
    return outcome
}
pn run_loop(listener, directory, wrapping_key, config, direct, interval_ms, rounds, ready) any^ {
    if (ready != null) output(string(listener.port),ready,{format:'text',atomic:true})^
    var next_due = 0
    var failures = 0
    var i = 0
    var sessions = 0
    var latest = null
    while (rounds == 0 or i < rounds) {
        if (now().unix >= next_due) {
            let result = synchronize(directory,wrapping_key,config,direct) ^ { ^ }
            if (result is error) { failures = failures+1; latest = {connected:false,cloud_saved:false} }
            else { failures = 0; latest = {*:result,connected:true} }
            next_due = now().unix+min(60000,interval_ms*(1+min(failures,11)))
        }
        let agent = peer.open_agent(directory,wrapping_key)^
        let received = network.accept(listener,agent.handle,agent.credentials,1000,
            pn (id)=> { return peer.current_member(directory,wrapping_key,agent.credentials,id)^ }) ^ { ^ }
        if (not (received is error)) {
            peer.materialize(agent.handle)^
            sessions = sessions+1
        }
        i = i+1
    }
    return {*:status(directory,wrapping_key,config)^,sessions:sessions,last:latest}
}

import identity: lambda.sync.peer_identity
import exchange: lambda.sync.peer_exchange
import local: lambda.sync.local
import crypto: lambda.io.crypto
import workspace: lambda.sync.workspace
import host: lambda.sync.document_host
import owner: lambda.sync.task_owner
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-warm-protocol-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/a")^; io.mkdir(area ++ "/b")^
    let alice = identity.create("warm-protocol",crypto.key()^)^
    // The receiver's admission uses the authenticated session peer, not a shared writer ID.
    let receiver = identity.create(alice.space,alice.secret)^
    let a = local.create(area ++ "/a.sqlite",alice.secret,alice.space,area ++ "/a","a")^
    let raw_b = local.create(area ++ "/b.sqlite",alice.secret,alice.space,area ++ "/b","b")^
    let cached_b = {*:raw_b,keys:{*:raw_b.keys,cache_enabled:true}}
    let initial = local.status(cached_b)^
    let broker = start(owner.run,[exchange.token(initial.state)^,exchange.token,initial.repository_cache])
    let b = {*:cached_b,keys:{*:cached_b.keys,owner:broker,token_cached:true}}
    for (i in 0 to 19) { output("value " ++ string(i),area ++ "/a/file-" ++ string(i))^ }
    local.scan(a)^
    let offered = exchange.offer(a,alice,true,true)^
    let bundle = exchange.bundle(a,offered.batch,offered.ids)^
    let before = local.status(b)^
    let tail_frame = bundle.frames[len(bundle.frames)^-1]
    let damaged = {*:bundle,frames:[*take(bundle.frames,len(bundle.frames)^-1)^,
        {*:tail_frame,packet:{*:tail_frame.packet,body:{*:tail_frame.packet.body,text:tail_frame.packet.body.text ++ " "}}}]}
    let rejected = exchange.receive_bundle(b,receiver,alice.device,damaged) ^ { ^ }
    let held = local.status(b)^
    let accepted = exchange.receive_bundle(b,receiver,alice.device,bundle)^
    let duplicate = exchange.receive_bundle(b,receiver,alice.device,bundle)^
    let malformed = local.mutate(b,pn (tx,k,current)=> {
        return {*:current,endpoint:{*:current.endpoint,replica:{*:current.endpoint.replica,time:float(current.endpoint.replica.time)^}}}
    }) ^ { ^ }
    let stable = local.status(b)^
    local.materialize(b)^
    local.transact(a,[{kind:'create',type:'dir',parent:workspace.root,name:"managed"}])^
    local.transact(a,[{kind:'create',type:'dir',parent:workspace.root,name:"managed-two"}])^
    let delta = exchange.offer(a,alice,true,true,exchange.have_frontier(alice.space,exchange.current_token(b)^)^)^
    let delta_bundle = exchange.bundle(a,delta.batch,[])^
    let second = delta.batch[1]
    let invalid_delta = exchange.attest_delta(alice,{*:second.packet,body:{*:second.packet.body,id:b'00'}})^
    let delta_before = local.status(b)^.generation
    let bad_delta = exchange.receive_bundle(b,receiver,alice.device,
        {*:delta_bundle,frames:[delta.batch[0],invalid_delta]}) ^ { ^ }
    let delta_held = local.status(b)^.generation
    let delta_applied = exchange.receive_bundle(b,receiver,alice.device,delta_bundle)^
    output("one",area ++ "/a/external")^
    local.scan(a)^
    output("two",area ++ "/a/external")^
    let scanned = local.scan(a)^
    let opened = host.open_document(a,"external")^
    output("three",area ++ "/a/external")^; host.refresh(a,opened.id)^
    output("four",area ++ "/a/external")^; let refreshed = host.refresh(a,opened.id)^
    let result = [offered.batch != null,len(offered.frames)^ > 1,rejected is error,
        held.generation == before.generation and len(held.state.endpoint.transfers)^ == 0,
        accepted.generation == before.generation+1,duplicate.generation == accepted.generation,
        malformed is error,stable.generation == duplicate.generation,
        input(area ++ "/b/file-19",'text')^ == "value 19",
        delta.summary.delta == true and len(delta.batch)^ == 2,
        bad_delta is error and delta_held == delta_before,
        delta_applied.generation == delta_before+1 and delta_applied.state.endpoint.replica.clock == delta.summary.clock,
        len(scanned.state.endpoint.replica.clock)^ == 2,
        h.at_path(scanned.state,"managed")^.type == 'dir',scanned.state.review == null,
        refreshed.value == "four" and len(refreshed.conflicts)^ == 0]
    send(broker,{stop:true})^; wait(broker)^
    io.delete(area)^
    return result
}

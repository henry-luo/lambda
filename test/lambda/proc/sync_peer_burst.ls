import agent: lambda.sync.peer_agent
import peer: lambda.sync.peer
import burst: lambda.sync.peer_burst
import local: lambda.sync.local
import crypto: lambda.io.crypto
import owner: lambda.sync.task_owner
import ws: lambda.io.websocket
import exchange: lambda.sync.peer_exchange
pn after_round(event, handle, root, name) any^ {
    if (event.type == 'round' and event.rounds == 1) {
        output(name,root ++ "/" ++ name ++ ".txt")^
        local.scan(handle,[name ++ ".txt"])^
    }
    return true
}
pn main() {
    let area = "temp/sync-peer-burst-" ++ string(sys.proc.self.pid#)
    for (name in ["a","b"]) { io.mkdir(area ++ "/" ++ name ++ "/agent")^; io.mkdir(area ++ "/" ++ name ++ "/tree")^ }
    let ak = crypto.key()^
    let bk = crypto.key()^
    let a = agent.device(area ++ "/a/agent",ak)^
    let b = agent.device(area ++ "/b/agent",bk)^
    agent.initialize(area ++ "/a/agent",ak,area ++ "/a/tree","burst","a")^
    let invitation = agent.invite(area ++ "/a/agent",ak,b)^
    agent.enroll(area ++ "/b/agent",bk,area ++ "/b/tree","b",invitation,a.id)^
    let ao = agent.open_agent(area ++ "/a/agent",ak)^
    let bo = agent.open_agent(area ++ "/b/agent",bk)^
    output("before",area ++ "/a/tree/initial.txt")^
    local.scan(ao.handle)^
    let ac = {*:ao.handle,keys:{*:ao.handle.keys,cache_enabled:true}}
    let bc = {*:bo.handle,keys:{*:bo.handle.keys,cache_enabled:true}}
    let a_loaded = local.status(ac)^
    let b_loaded = local.status(bc)^
    let a_owner = start(owner.run,[exchange.token(a_loaded.state)^,exchange.token,a_loaded.repository_cache])
    let b_owner = start(owner.run,[exchange.token(b_loaded.state)^,exchange.token,b_loaded.repository_cache])
    let ah = {*:ac,keys:{*:ac.keys,owner:a_owner,token_cached:true}}
    let bh = {*:bc,keys:{*:bc.keys,owner:b_owner,token_cached:true}}
    // Repository replies must not consume unrelated events in the caller's mailbox.
    send(self(),'unrelated')^
    let called = owner.call(a_owner,pn ()=> { return 42 })^
    let retained = receive()^
    let before = exchange.current_token(ah)^
    let rejected = local.mutate(ah,pn (tx,k,current)=> { raise error("rollback") }) ^ { ^ }
    let unchanged = exchange.current_token(ah)^ == before
    let listener = peer.listener(0,"127.0.0.1",true)^
    let options = {idle_ms:500,burst_ms:10000,interval_ms:100}
    let a_report = pn (event)=> { return after_round(event,ah,area ++ "/a/tree","a")^ }
    let b_report = pn (event)=> { return after_round(event,bh,area ++ "/b/tree","b")^ }
    let server = start(burst.accept,[listener,bh,bo.credentials,null,options,b_report])
    let started = clock()
    let client = start(burst.connect,["ws://127.0.0.1:" ++ string(listener.port) ++ "/sync",ah,ao.credentials,b.id,null,options,a_report])
    let sent = wait(client)^
    let elapsed = clock()-started
    let served = wait(server)^
    let result = [called == 42,retained == 'unrelated',rejected is error,unchanged,sent.rounds >= 2,served.rounds >= 2,
        input(area ++ "/a/tree/b.txt",'text')^ == "b",input(area ++ "/b/tree/a.txt",'text')^ == "a",
        input(area ++ "/b/tree/initial.txt",'text')^ == "before",elapsed >= 0.5]
    send(a_owner,{stop:true})^
    send(b_owner,{stop:true})^
    wait(a_owner)^
    wait(b_owner)^
    ws.close(listener)^
    io.delete(area)^
    return result
}

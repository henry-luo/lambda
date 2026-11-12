import agent: lambda.sync.peer_agent
import connectivity: lambda.sync.connectivity_agent
import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-agent-recovery-" ++ string(sys.proc.self.pid#)
    io.delete(area) ^ { null }
    for (name in ["a","b"]) { io.mkdir(area ++ "/" ++ name ++ "/agent")^; io.mkdir(area ++ "/" ++ name ++ "/tree")^ }
    let a_key = crypto.key()^
    let b_key = crypto.key()^
    let a = agent.device(area ++ "/a/agent",a_key)^
    let b = agent.device(area ++ "/b/agent",b_key)^
    agent.initialize(area ++ "/a/agent",a_key,area ++ "/a/tree","recover-agent","a")^
    let offer = agent.invite(area ++ "/a/agent",a_key,b)^
    agent.enroll(area ++ "/b/agent",b_key,area ++ "/b/tree","b",offer,a.id)^
    let ah = agent.open_agent(area ++ "/a/agent",a_key)^.handle
    let bh = agent.open_agent(area ++ "/b/agent",b_key)^.handle
    output("original",area ++ "/a/tree/note.txt")^
    local.scan(ah)^
    h.transfer(ah,bh)^
    let pending = local.prepare_install(bh)^.state.installation
    let completed = connectivity.run(area ++ "/b/agent",b_key,null,{mdns:false,rtc:false,rounds:1,interval_ms:100})^
    let first = input(area ++ "/b/tree/note.txt",'text')^
    output("new",area ++ "/a/tree/note.txt")^
    local.scan(ah)^
    h.transfer(ah,bh)^
    local.prepare_install(bh)^
    output("external",area ++ "/b/tree/note.txt")^
    let refused = connectivity.run(area ++ "/b/agent",b_key,null,{mdns:false,rtc:false,rounds:1,interval_ms:100}) ^ { ^ }
    let blocked = local.status(bh)^.state.installation
    let result = [pending != null,completed.phase == 'stopped' and first == "original",
        refused is error and contains(refused.message,"needs review")^,
        input(area ++ "/b/tree/note.txt",'text')^ == "external" and len(blocked.recoveries)^ > 0]
    io.delete(area)^
    return result
}

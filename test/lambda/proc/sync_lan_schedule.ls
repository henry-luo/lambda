import agent: lambda.sync.peer_agent
import connectivity: lambda.sync.connectivity_agent
import local: lambda.sync.local
import crypto: lambda.io.crypto
import h: ~~.mod_sync_local_helpers
pn main() {
    let area = "temp/sync-lan-schedule-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/agent")^
    io.mkdir(area ++ "/tree")^
    let key = crypto.key()^
    agent.device(area ++ "/agent",key)^
    agent.initialize(area ++ "/agent",key,area ++ "/tree",uuid()^,"schedule")^
    output("offline before startup",area ++ "/tree/offline.txt")^
    let first = connectivity.run(area ++ "/agent",key,null,
        {mdns:true,rtc:false,rounds:14,interval_ms:100,verify_ms:1000,idle_ms:500})^
    let opened = agent.open_agent(area ++ "/agent",key)^.handle
    let before = local.status(opened)^
    let entry = h.at_path(before.state,"offline.txt")^
    let second = connectivity.run(area ++ "/agent",key,null,
        {mdns:true,rtc:false,rounds:1,interval_ms:100})^
    let after = local.status(opened)^
    let parent = self()
    let observer = pn (status)=> { if (status.phase == 'discovering') send(parent,true)^ }
    let running = start(connectivity.run,[area ++ "/agent",key,null,{mdns:true,rtc:false},observer])
    receive()^
    cancel(running)
    let cancelled = wait(running) ^ { ^ }
    let result = [first.monitor == 'os',first.scans >= 2,first.phase == 'stopped' and first.active_connections == 0,
        second.phase == 'stopped',before.state.endpoint.replica.clock == after.state.endpoint.replica.clock,
        len(local.history(opened,entry.id)^) == 1,cancelled is error and cancelled.code == 319]
    io.delete(area)^
    return result
}

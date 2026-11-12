import peer: lambda.sync.peer_agent
import cloud_agent: lambda.sync.cloud_agent
import cloud: lambda.sync.cloud
import local: lambda.sync.local
import host: lambda.sync.document_host
import crypto: lambda.io.crypto
import core: lambda.sync.core
import documents: lambda.sync.documents
import protocol: lambda.sync.protocol
pn main() {
    let area = "temp/sync-document-coverage-" ++ string(sys.proc.self.pid#)
    io.mkdir(area ++ "/agent")^
    io.mkdir(area ++ "/tree")^
    let key = crypto.key()^
    peer.device(area ++ "/agent",key)^
    peer.initialize(area ++ "/agent",key,area ++ "/tree","document-coverage","owner")^
    let agent = peer.open_agent(area ++ "/agent",key)^
    output("a",area ++ "/tree/note")^
    let opened = host.open_document(agent.handle,"note")^
    let current = local.status(agent.handle)^.state
    // trusted-host evidence covers the workspace, with no claim for the document root.
    local.record_cloud(agent.handle,"provider",agent.credentials.device.id,1,"workspace-publication",
        core.snapshot(current.endpoint.replica)^)^
    let workspace_evidence = protocol.status(local.status(agent.handle)^.state.endpoint)^.cloud
    let workspace_only = peer.status(area ++ "/agent",key)^
    let pending = host.status(agent.handle,opened.id)^
    let before = local.status(agent.handle)^.state
    local.record_cloud(agent.handle,"provider",agent.credentials.device.id,2,"document-publication",
        core.snapshot(before.endpoint.replica)^,local.inventory(agent.handle)^,documents.roots(before))^
    let complete = peer.status(area ++ "/agent",key)^
    host.apply_edits(agent.handle,opened.id,[{kind:'text',start:1,count:0,value:"B"}])^
    let later = peer.status(area ++ "/agent",key)^
    let config = {url:"https://another.invalid/sync",authority:agent.credentials.device.id}
    let other = cloud_agent.status(area ++ "/agent",key,config)^
    io.delete(area)^
    return [workspace_evidence,not workspace_only.cloud_saved,not pending.coverage.cloud,complete.cloud_saved,
        not later.cloud_saved,not other.cloud_saved]
}

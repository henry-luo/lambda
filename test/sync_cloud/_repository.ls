// Explicit process-gate helper; public repository APIs supply verification and historical restore.
import peer: lambda.sync.peer_agent
import local: lambda.sync.local
import codec: lambda.sync.codec
import hex: lambda.security.tuf.metadata
pn main() {
    let environment = sys.proc.self.env#
    let request = input(environment.LAMBDA_SYNC_REQUEST,'json')^
    let agent = peer.open_agent(request.directory,hex.unhex(environment.LAMBDA_SYNC_KEY)^)^
    var result = null
    if (request.action == "snapshot") {
        local.scan(agent.handle)^
        result = local.snapshot(agent.handle,request.label)^
    }
    if (request.action == "verify") {
        result = {verified:local.verify(agent.handle)^.verified,snapshots:local.status(agent.handle)^.state.snapshots}
    }
    if (request.action == "restore") {
        let snapshots = local.status(agent.handle)^.state.snapshots
        result = local.restore(agent.handle,snapshots[0],request.destination)^
    }
    output(codec.encode(result)^,environment.LAMBDA_CLOUD_RESULT,{format:'text',atomic:true})^
    return null
}

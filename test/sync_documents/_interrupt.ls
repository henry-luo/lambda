import peer: lambda.sync.peer_agent
import host: lambda.sync.document_host
import hex: lambda.security.tuf.metadata
pn main() {
    let environment = sys.proc.self.env#
    let request = input(environment.LAMBDA_SYNC_REQUEST,'json')^
    let agent = peer.open_agent(request.directory,hex.unhex(environment.LAMBDA_SYNC_KEY)^)^
    host.prepare_save(agent.handle,request.document)^
    host.install_save(agent.handle,request.document)^
    output("installed",request.ready,{format:'text',atomic:true})^
    sleep(300000)^
    return null
}

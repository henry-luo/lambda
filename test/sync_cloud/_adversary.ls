// Qualify provider checksum enforcement through an authenticated scoped upload grant.
import peer: lambda.sync.peer_agent
import transport: lambda.sync.cloud_transport
import schema: lambda.sync.cloud_schema
import codec: lambda.sync.codec
import hex: lambda.security.tuf.metadata
pn main() {
    let environment = sys.proc.self.env#
    let request = input(environment.LAMBDA_SYNC_REQUEST,'json')^
    let agent = peer.open_agent(request.directory,hex.unhex(environment.LAMBDA_SYNC_KEY)^)^
    let key = "objects/" ++ schema.digest(uuid()^)^
    let descriptor = transport.descriptor(key,"expected")^
    let offered = transport.call(request.cloud,agent.credentials,"upload",{objects:[descriptor]})^[0]
    let result = transport.transfer(offered.capability,"PUT","modified")^
    let stage = transport.call(request.cloud,agent.credentials,"stage",{keys:[key]}) ^ { ^ }
    output(codec.encode({rejected:not result.ok,missing:stage is error,status:result.status})^,
        environment.LAMBDA_CLOUD_RESULT,{format:'text',atomic:true})^
    return null
}

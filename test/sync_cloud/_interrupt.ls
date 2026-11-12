// Leave complete immutable objects unpublished, then let the process gate kill this client.
import peer: lambda.sync.peer_agent
import local: lambda.sync.local
import bundle: lambda.sync.cloud_bundle
import transport: lambda.sync.cloud_transport
import codec: lambda.sync.codec
import hex: lambda.security.tuf.metadata
pn main() {
    let environment = sys.proc.self.env#
    let request = input(environment.LAMBDA_SYNC_REQUEST,'json')^
    let agent = peer.open_agent(request.directory,hex.unhex(environment.LAMBDA_SYNC_KEY)^)^
    local.scan(agent.handle)^
    let captured = bundle.capture(agent.handle,agent.credentials)^
    for (id in captured.payload.ids) {
        let record = local.export_objects(agent.handle,[id])^[0]
        let bytes = codec.encode(record)^
        transport.ensure(request.cloud,agent.credentials,
            transport.descriptor(bundle.object_key(id)^,bytes)^,bytes)^
    }
    transport.ensure(request.cloud,agent.credentials,
        transport.descriptor(captured.key,captured.bytes)^,captured.bytes)^
    output(codec.encode({frontier:captured.payload.saved.frontier})^,request.ready,{format:'text',atomic:true})^
    sleep(3600000)^
    return null
}

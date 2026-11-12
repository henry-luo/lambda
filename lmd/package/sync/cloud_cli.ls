// Public request JSON never contains storage credentials or device wrapping keys.
import agent: .cloud_agent
import cloud: .cloud
import peer: .peer_agent
import identity: .peer_identity
import service: .cloud_function
import s3: .s3
import schema: .cloud_schema
import codec: .codec
import cli: .cli
pn execute(request, wrapping_key, environment) any^ {
    if (request.action == "bootstrap") {
        let credentials = identity.load(request.directory,wrapping_key)^
        let members = [credentials.device,*[for (path in request.members) codec.decode(input(path,'text')^,16384)^]]
        let control = schema.signed(credentials.seed,"lambda-cloud-control-1",{version:1,space:credentials.space,
            owner:credentials.device,generation:1,members:[for (device in members) {device:device,active:true}]})^
        let storage = parse(environment.LAMBDA_CLOUD_STORAGE,'json')^
        service.bootstrap(s3.adapter(storage),control)^
        return {authority:credentials.device.id,space:credentials.space}
    }
    if (request.action == "sync") {
        return agent.synchronize(request.directory,wrapping_key,request.cloud,request.direct)^
    }
    if (request.action == "run") {
        return agent.run(request.directory,wrapping_key,request.cloud,request.port,request.bind or "127.0.0.1",
            request.direct,request.interval_ms or 5000,request.rounds or 0,request.ready)^
    }
    if (request.action == "watch") {
        return agent.watch(request.directory,wrapping_key,request.cloud,request.direct,
            request.interval_ms or 5000,request.cloud_every or 6,request.rounds or 0)^
    }
    if (request.action == "membership") {
        let target = codec.decode(input(request.recipient,'text')^,16384)^
        return agent.membership(request.directory,wrapping_key,request.cloud,target,request.active)^
    }
    if (request.action == "history") {
        let credentials = identity.load(request.directory,wrapping_key)^
        return cloud.history(credentials,request.cloud,request.actor,request.start,request.limit or 32)^
    }
    if (request.action == "status") { return agent.status(request.directory,wrapping_key,request.cloud)^ }
    raise error("sync cloud: unknown action")
}
pn main() {
    return cli.invoke(execute,"LAMBDA_CLOUD_RESULT")^
}

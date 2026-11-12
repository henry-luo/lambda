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
import transport: .cloud_transport
import maintenance: .cloud_maintenance
import runner: .maintenance_runner
import cache: .cache
import audit: .cloud_audit
pn execute(request, wrapping_key, environment) any^ {
    if (request.action == "bootstrap") {
        let credentials = identity.load(request.directory,wrapping_key)^
        let members = [credentials.device,*[for (path in request.members) codec.decode(input(path,'text')^,16384)^]]
        let control = schema.signed(credentials.seed,"lambda-cloud-control-1",{version:1,space:credentials.space,
            owner:credentials.device,generation:1,storage:request.storage_protocol or "catalog-2",members:[for (device in members) {device:device,active:true}]})^
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
    if (request.action == "backup_discover") {
        let credentials = identity.load(request.directory,wrapping_key)^
        return transport.call(request.cloud,credentials,"backup_discover",{cursor:request.cursor,limit:request.limit or 4})^
    }
    if (starts_with(request.action,"audit_")^) {
        let opened = peer.open_agent(request.directory,wrapping_key)^
        if (request.action == "audit_begin") { return audit.begin(opened.handle,opened.credentials,request.cloud)^ }
        if (request.action == "audit_scan") { return audit.scan(opened.handle,opened.credentials,request.cloud,request.budget or 32)^ }
        if (request.action == "audit_step") { return audit.step(opened.handle,opened.credentials,request.cloud,request.budget or 8)^ }
        if (request.action == "audit_status") { return audit.status(opened.handle,request.cloud)^ }
        if (request.action == "audit_prepare") { return audit.prepare(opened.handle,opened.credentials,request.cloud)^ }
        if (request.action == "audit_abort") { return audit.abort(opened.handle,opened.credentials,request.cloud)^ }
    }
    if (request.action == "catalog_recover") {
        let credentials = identity.load(request.directory,wrapping_key)^
        return transport.call(request.cloud,credentials,"recover_catalog",{snapshot:request.snapshot,expected:request.expected})^
    }
    if (request.action == "cancel_publication") {
        let agent = peer.open_agent(request.directory,wrapping_key)^
        return transport.call(request.cloud,agent.credentials,"cancel_publication",{})^
    }
    if (starts_with(request.action,"cache_")^) {
        let agent = peer.open_agent(request.directory,wrapping_key)^
        if (request.action == "cache_plan") { return cache.plan(agent.handle,agent.credentials,request.cloud,request.limit or 64)^ }
        if (request.action == "cache_step") { return cache.step(agent.handle,request.cloud,request.id,request.budget or 16)^ }
        if (request.action == "cache_hydrate") { return cache.hydrate(agent.handle,agent.credentials,request.cloud,request.budget or 64)^ }
        if (request.action == "cache_cancel") { return cache.cancel(agent.handle,agent.credentials,request.cloud)^ }
    }
    if (starts_with(request.action,"maintenance_")^) {
        let agent = peer.open_agent(request.directory,wrapping_key)^
        let action = request.action
        if (action == "maintenance_tick") { return runner.tick(agent.handle,agent.credentials,request.cloud,
            request.at or now().unix,request.options or runner.defaults)^ }
        if (action == "maintenance_prepare") { return maintenance.prepare(agent.handle,agent.credentials,request.cloud)^ }
        if (action == "maintenance_step") { return maintenance.step(agent.handle,agent.credentials,request.cloud,request.budget or 32)^ }
        if (action == "maintenance_abort") { return maintenance.abort(agent.handle,agent.credentials,request.cloud)^ }
        if (action == "maintenance_status") { return maintenance.status(agent.handle,request.cloud)^ }
        if (action == "maintenance_prune" or action == "maintenance_reap") { return maintenance.prune(agent.credentials,request.cloud,request.id,request.budget or 32,action == "maintenance_reap")^ }
    }
    if (request.action == "status") { return agent.status(request.directory,wrapping_key,request.cloud)^ }
    raise error("sync cloud: unknown action")
}
pn main() {
    return cli.invoke(execute,"LAMBDA_CLOUD_RESULT")^
}

// a scheduler advances one bounded phase; encrypted selection waits for the owner's key holder.
import local: .local
import maintenance: .maintenance
import remote: .cloud_maintenance
import cloud: .cloud
import transport: .cloud_transport
import metadata: .maintenance_state
import v: .schema

pub let defaults = {enabled:true,interval_ms:3600000,budget:32}
pn schedule(handle, value) any^ {
    local.mutate(handle,pn (tx,k,current)=> { return {*:current,maintenance_schedule:value} })^
    return value
}
pub pn tick(handle, credentials, config, milliseconds, options=defaults) any^ {
    if (options.enabled != true) { return {phase:'disabled'} }
    if (credentials.device.id != config.authority) raise error("sync maintenance: owner scheduler required")
    if (not v.natural(options.interval_ms)^ or options.interval_ms < 1000 or
        not v.natural(options.budget)^ or options.budget < 1 or options.budget > 128 or
        not (milliseconds is number) or milliseconds < 0) raise error("sync maintenance: invalid schedule")
    let provider = cloud.provider(config)^
    var current = local.status(handle)^.state
    var task = current.maintenance_schedule
    if (task == null or (not task.active and task.provider != provider)) {
        task = {provider:provider,active:false,stage:'idle',next_due:0}
    }
    if (task.provider != provider) raise error("sync maintenance: finish the active provider's job first")
    if (not task.active) {
        if (milliseconds < task.next_due) { return {phase:'idle',next_due:task.next_due} }
        task = schedule(handle,{provider:provider,active:true,stage:'select',next_due:milliseconds+options.interval_ms})^
    }
    let job = remote.status(handle,config)^
    if (job != null) {
        task = schedule(handle,{*:task,stage:'copy',job:job.id})^
        let copied = remote.step(handle,credentials,config,options.budget)^
        if (copied.replan == true) {
            schedule(handle,{*:task,stage:'select'})^
            return {phase:'replan'}
        }
        if (copied.committed) { schedule(handle,{*:task,stage:'cloud-prune'})^ }
        return {*:copied,phase:if (copied.committed) 'cloud-prune' else 'copy'}
    }
    if (task.stage == 'select') {
        // pull every admitted head before selecting a new generation; subsequent commits still fence the plan.
        cloud.pull(handle,credentials,config)^
        let plan = maintenance.plan(handle,milliseconds)^
        if (not maintenance.ready(handle,plan.id,milliseconds)^) { return {phase:'grace',id:plan.id} }
        maintenance.forget(handle,plan.id,milliseconds)^
        schedule(handle,{*:task,stage:'prepare'})^
        return {phase:'prepare'}
    }
    if (task.stage == 'prepare') {
        let prepared = remote.prepare(handle,credentials,config)^
        schedule(handle,{*:task,stage:'copy',job:prepared.id})^
        return {*:prepared,phase:'copy'}
    }
    if (task.stage == 'copy' or task.stage == 'cloud-prune') {
        let server_state = transport.call(config,credentials,"pull",{})^
        if (server_state.maintenance != null and server_state.maintenance.id == task.job and server_state.maintenance.aborted == true) {
            schedule(handle,{*:task,stage:'select'})^
            return {phase:'replan'}
        }
        if (server_state.maintenance == null or server_state.maintenance.id != task.job or not server_state.maintenance.committed)
            raise error("sync maintenance: recover or abort the cloud job before continuing")
        cloud.pull(handle,credentials,config)^
        let pruned = remote.prune(credentials,config,task.job,min(options.budget,64))^
        if (pruned.complete) { schedule(handle,{*:task,stage:'local-prune'})^ }
        return {*:pruned,phase:if (pruned.complete) 'local-prune' else 'cloud-prune'}
    }
    if (task.stage != 'local-prune') raise error("sync maintenance: invalid scheduled phase")
    current = local.status(handle)^.state
    var plan = current.maintenance
    if (plan == null or plan.kind != 'prune' or plan.fingerprint != metadata.fingerprint(current)^) {
        plan = maintenance.plan_prune(handle)^
    }
    let pruned = maintenance.prune_step(handle,plan.id,options.budget)^
    if (pruned.complete) {
        schedule(handle,{*:task,active:false,stage:'idle',next_due:milliseconds+options.interval_ms})^
    }
    return {*:pruned,phase:if (pruned.complete) 'complete' else 'local-prune'}
}

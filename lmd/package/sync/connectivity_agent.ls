// A single owner serializes repository effects; rendezvous and polling use finite requests.
import rv: .rendezvous
import connectivity: .connectivity
import peer: .peer
import agent: .peer_agent
import cloud: .cloud
import wire: .peer_transport
import local: .local
import v: .schema
import schema: .cloud_schema
import rtc: lambda.io.rtc
import ws: lambda.io.websocket

pub let defaults = {port:0,bind:"0.0.0.0",stun:[],mdns:true,rtc:true,establish_ms:15000,
    retry_ms:5000,interval_ms:1000,cloud_ms:30000,rounds:0,stop_after:0,status:null,ready:null}
fn options(value) any^ {
    v.record(value,[],[for (key at defaults) string(key)])^
    let result = {*:defaults,*:value}
    for (bound in [["port",0,65535],["establish_ms",1000,60000],["retry_ms",1000,3600000],
        ["interval_ms",100,60000],["cloud_ms",1000,3600000],["rounds",0,1000000],["stop_after",0,1000000]]) {
        let n = result[bound[0]]
        if (not v.natural(n)^ or n < bound[1] or n > bound[2]) raise error("sync connectivity: schedule bounds")
    }
    if (not (result.mdns is bool) or not (result.rtc is bool)) raise error("sync connectivity: boolean options required")
    return result
}
pn report(options, current) any^ {
    if (options.status != null) output(format(current,'json')^,options.status,{format:'text',atomic:true})^
    if (options.observer != null) options.observer(current)^
    return current
}
pn coverage(handle, config) any^ {
    return if (config == null) false else cloud.covered(local.status(handle)^.state,cloud.provider(config)^)^
}
pn authorized(directory, key, credentials, id) any^ { return agent.current_member(directory,key,credentials,id)^ }
pn exchange(directory, key, pending, options, current, config) any^ {
    let opened = agent.resume_agent(directory,key)^
    let socket = wire.channel(pending.handle)
    let authorize = pn (id)=> { return authorized(directory,key,opened.credentials,id)^ }
    let observe = pn (progress)=> { report(options,{*:current,phase:'syncing',connected:true,
        cloud_saved:coverage(opened.handle,config)^,peer:pending.peer,progress:progress})^ }
    let result = if (pending.role == 'client')
        peer.exchange_connection(socket,opened.handle,opened.credentials,pending.peer,authorize,observe)^
        else peer.serve_connection(socket,opened.handle,opened.credentials,authorize,observe)^
    agent.materialize(opened.handle)^
    return result
}
pn begin(credentials, config, options, remote, instance) any^ {
    let handle = rtc.create({stun:options.stun})^
    let result = begin_description(handle,credentials,config,options,remote,instance) ^ { ^ }
    if (result is error) { rtc.close(handle)^; raise result }
    return result
}
pn begin_description(handle, credentials, config, options, remote, instance) any^ {
    let started = now().unix
    let deadline = started+options.establish_ms
    if (remote == null) raise error("sync connectivity: peer selection required")
    let answering = remote.body != null
    let target = if (answering) remote.body.actor else remote.peer
    let session = if (answering) remote.body.session else connectivity.session_id()^
    let remote_instance = if (answering) remote.payload.instance else remote.instance
    if (answering) rtc.remote(handle,connectivity.offer(remote,instance)^,"offer")^
    let sdp = rtc.description(handle,if (answering) "answer" else "offer",max(1,deadline-now().unix))^
    if (now().unix >= deadline) raise error("sync connectivity: gathering exceeded attempt deadline")
    let offer = if (answering) schema.digest(remote.body)^ else null
    let published = connectivity.publish(config,credentials,if (answering) "answer" else "offer",target,session,
        {protocol:"lambda-peer-lan-1",sdp:sdp,instance:instance,target_instance:remote_instance,offer:offer})^
    return {handle:handle,peer:target,session:session,deadline:deadline,started:started,
        role:if (answering) 'server' else 'client',applied:answering,instance:instance,
        remote_instance:remote_instance,offer:if (answering) offer else schema.digest(published.body)^}
}
pn advance(credentials, config, pending) any^ {
    if (now().unix >= pending.deadline) raise error("sync connectivity: direct attempt deadline exceeded")
    var selected = pending
    if (pending.role == 'client' and not pending.applied) {
        let answers = connectivity.query(config,credentials,"answer",[pending.peer])^
        let matching = [for (answer in answers where answer.body.session == pending.session) answer]
        if (len(matching)^ > 0) {
            rtc.remote(pending.handle,connectivity.answer(matching[0],pending)^,"answer")^
            selected = {*:pending,applied:true}
        }
    }
    let status = rtc.status(pending.handle)^
    if (status.failed or status.closed) raise error("sync connectivity: direct channel failed")
    return {pending:selected,ready:status.open}
}

pub pn run(directory, key, config=null, settings={}, observer=null) any^ {
    let selected = options(settings)^
    let initial = agent.resume_agent(directory,key)^
    local.scan(initial.handle)^
    let listener = peer.listener(selected.port,selected.bind)^
    let advertisement = if (selected.mdns) connectivity.advertise(initial.credentials,listener.port) ^ { ^ } else null
    let advertised = if (advertisement is error) false else advertisement != null
    let diagnostics = {*:selected,observer:observer,advertising:advertised,
        discovery_error:if (advertisement is error) advertisement.code else null}
    let worker = start(loop,[listener,directory,key,config,diagnostics])
    let result: any^ = wait(worker)
    if (advertised^) connectivity.withdraw(advertisement^) ^ { null }
    ws.close(listener) ^ { null }
    if (result is error) raise result
    return result
}
pn loop(listener, directory, key, config, options) any^ {
    if (options.ready != null) output(string(listener.port),options.ready,{format:'text',atomic:true})^
    let capabilities = connectivity.capabilities()^
    let instance = connectivity.session_id()^
    var pending = null
    var hints = []
    var seen = []
    var next_presence = 0
    var next_cloud = 0
    var next_discovery = 0
    var next_direct = 0
    var next_signal = 0
    var failures = 0
    var turn = 0
    var count = 0
    var current = {connected:false,advertising:options.advertising,discovery_error:options.discovery_error,
        phase:'discovering',transport:'offline',peer:null,attempts:0,sessions:0,retries:0,
        fallback_reason:null,error_code:null,cloud_saved:false,progress:null,capabilities:capabilities}
    while (options.rounds == 0 or count < options.rounds) {
        if (options.stop_after > 0 and current.sessions >= options.stop_after) { break }
        let opened = agent.resume_agent(directory,key)^
        if (pending == null) local.scan(opened.handle)^
        let credentials = opened.credentials
        current = {*:current,cloud_saved:coverage(opened.handle,config)^}
        if (pending == null and config != null and now().unix >= next_presence) {
            let presence = connectivity.publish(config,credentials,"presence",null,instance,
                {protocol:"lambda-peer-lan-1",rtc:options.rtc and capabilities.rtc}) ^ { ^ }
            next_presence = now().unix+20000
            if (presence is error) { current = {*:current,fallback_reason:"rendezvous unavailable",error_code:presence.code} }
        }
        if (pending != null) {
            let progress = advance(credentials,config,pending) ^ { ^ }
            if (progress is error) {
                rtc.close(pending.handle) ^ { null }
                pending = null; failures = failures+1
                next_direct = now().unix+min(60000,options.retry_ms*(1+min(failures,11)))
                next_cloud = 0
                current = {*:current,phase:'fallback',transport:'offline',peer:null,retries:current.retries+1,
                    fallback_reason:"direct connection failed or expired",error_code:progress.code}
            } else {
                pending = (progress^).pending
                if ((progress^).ready) {
                    report(options,{*:current,phase:'authenticating',connected:true,peer:pending.peer})^
                    let transferred = exchange(directory,key,pending,options,current,config) ^ { ^ }
                    rtc.close(pending.handle) ^ { null }
                    pending = null; next_direct = now().unix+options.retry_ms
                    if (transferred is error) {
                        failures = failures+1; next_cloud = 0
                        current = {*:current,phase:'fallback',transport:'offline',retries:current.retries+1,
                            fallback_reason:"authenticated exchange failed",error_code:transferred.code}
                    } else {
                        failures = 0
                        current = {*:current,phase:'idle',transport:'peer',sessions:current.sessions+1,
                            fallback_reason:null,error_code:null,progress:transferred}
                    }
                }
            }
        }
        if (pending == null and options.mdns and capabilities.bonjour and now().unix >= next_discovery) {
            let discovered = connectivity.discover(credentials,1000) ^ { ^ }
            hints = if (discovered is error) [] else discovered
            next_discovery = now().unix+10000
        }
        if (pending == null and now().unix >= next_direct and len(credentials.members)^ > 0) {
            let members = sort(credentials.members,(member)=>member.id)
            let member = members[turn%len(members)^]
            turn = turn+1
            let lan = [for (hint in hints where hint.peer == member.id) hint]
            if (credentials.device.id < member.id and len(lan)^ > 0) {
                current = {*:current,phase:'connecting',peer:member.id,attempts:current.attempts+1}
                report(options,current)^
                let synced = agent.sync(directory,key,lan[0].url,member.id) ^ { ^ }
                next_direct = now().unix+options.retry_ms
                if (not (synced is error)) { current = {*:current,phase:'idle',transport:'peer',sessions:current.sessions+1,
                    fallback_reason:null,error_code:null,progress:synced} }
                else { hints = []; next_cloud = 0; current = {*:current,fallback_reason:"LAN candidate failed",error_code:synced.code} }
            } else if (config != null and options.rtc and capabilities.rtc and credentials.device.id < member.id) {
                let available = connectivity.query(config,credentials,"presence",[member.id]) ^ { ^ }
                if (not (available is error) and len(available)^ > 0 and available[0].payload.rtc == true) {
                    current = {*:current,phase:'connecting',peer:member.id,attempts:current.attempts+1}
                    report(options,current)^
                    let begun = begin(credentials,config,options,{peer:member.id,instance:available[0].body.session},instance) ^ { ^ }
                    if (begun is error) {
                        failures = failures+1; next_cloud = 0
                        current = {*:current,retries:current.retries+1,phase:'fallback',fallback_reason:"channel establishment failed",error_code:begun.code}
                    } else { pending = begun^ }
                }
                next_direct = now().unix+min(60000,options.retry_ms*(1+min(failures,11)))
            } else { next_direct = now().unix+options.retry_ms }
        }
        if (pending == null and config != null and options.rtc and capabilities.rtc and len(credentials.members)^ > 0 and now().unix >= next_signal) {
            // Rotate finite inbox pages; deterministic caller ordering prevents simultaneous offers.
            let members = sort(credentials.members,(member)=>member.id)
            let page_count = max(1,int((len(members)^+7)/8)^)
            let offset = (count%page_count)*8
            let page = slice(members,offset,offset+8)^
            let inbox = connectivity.query(config,credentials,"offer",[for (member in page) member.id]) ^ { ^ }
            next_signal = now().unix+1000
            if (not (inbox is error)) {
                let offers = [for (offer in inbox where offer.body.actor < credentials.device.id and
                    not contains(seen,offer.body.session)^) offer]
                if (len(offers)^ > 0) {
                    let offer = offers[0]
                    seen = [*slice(seen,max(0,len(seen)^-63),len(seen)^)^,offer.body.session]
                    let begun = begin(credentials,config,options,offer,instance) ^ { ^ }
                    if (not (begun is error)) { pending = begun^; current = {*:current,phase:'connecting',peer:begun.peer,attempts:current.attempts+1} }
                    else { next_cloud = 0; current = {*:current,phase:'fallback',fallback_reason:"incoming offer failed",error_code:begun.code} }
                }
            }
        }
        // Durable publication is independent of direct delivery; an ICE attempt cannot starve polling.
        if (pending == null and config != null and now().unix >= next_cloud) {
            report(options,{*:current,phase:'cloud'})^
            let synced = cloud.synchronize(opened.handle,credentials,config) ^ { ^ }
            next_cloud = now().unix+options.cloud_ms
            if (synced is error) { current = {*:current,cloud_saved:false,error_code:synced.code,
                fallback_reason:if (current.transport == 'peer') current.fallback_reason else "cloud unavailable"} }
            else {
                agent.materialize(opened.handle)^
                current = {*:current,phase:'idle',cloud_saved:true,
                    transport:if (current.transport == 'peer') 'peer' else 'http'}
            }
        }
        // A direct merge can advance captured state after the last cloud receipt.
        current = {*:current,cloud_saved:coverage(opened.handle,config)^}
        report(options,current)^
        if (options.stop_after > 0 and current.sessions >= options.stop_after) { break }
        // Native LAN acceptance shares the same owner and current-member callback.
        if (pending == null) {
            let accepted = peer.accept(listener,opened.handle,credentials,options.interval_ms,
                pn (id)=> { return authorized(directory,key,credentials,id)^ }) ^ { ^ }
            if (not (accepted is error)) {
                agent.materialize(opened.handle)^
                current = {*:current,phase:'idle',transport:'peer',peer:accepted.peer,sessions:current.sessions+1}
            }
        }
        if (pending != null) sleep(options.interval_ms)^
        count = count+1
    }
    if (pending != null) rtc.close(pending.handle)^
    let final = agent.resume_agent(directory,key)^
    return report(options,{*:current,phase:'stopped',cloud_saved:coverage(final.handle,config)^})^
}

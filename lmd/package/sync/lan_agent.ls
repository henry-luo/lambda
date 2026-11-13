// OS events schedule finite LAN bursts. One owner serializes all repository effects.
import disk: lambda.io.disk
import ws: lambda.io.websocket
import peer: .peer
import agent: .peer_agent
import burst: .peer_burst
import exchange: .peer_exchange
import connectivity: .connectivity
import local: .local
import owner: .task_owner
import timing: .timing

pn watch_events(watch, parent) any^ {
    while (true) {
        let changes = disk.changes(watch,60000) ^ { ^ }
        if (changes is error) {
            if (changes.code == 319) raise changes
            send(parent,{type:'monitor_error',failure:changes})^
            return false
        }
        if ((changes^).rescan or len((changes^).paths)^ > 0) {
            send(parent,{type:'filesystem',*:(changes^)})^
            // One queued batch bounds mailbox pressure; the native queue coalesces the rest.
            receive()^
        }
    }
}
pn ticks(parent, interval_ms) any^ {
    while (true) {
        sleep(interval_ms)^
        send(parent,{type:'tick'})^
        receive()^
    }
}
pn discovery(credentials, parent) any^ {
    while (true) {
        let hints = connectivity.discover(credentials,1000) ^ { ^ }
        send(parent,{type:'discovery',hints:if (hints is error) [] else hints})^
        receive()^
        sleep(10000)^
    }
}
pn outgoing(directory, key, handle, credentials, member, parent, options) any^ {
    let authorize = pn (id)=> { return agent.current_member(directory,key,credentials,id)^ }
    let report = pn (event)=> { send(parent,{*:event,worker:self(),direction:'outgoing'})^ }
    while (true) {
        let job = receive()^
        let child = start(burst.connect,[job.url,handle,credentials,member.id,authorize,options,report])
        let result = wait(child) ^ { ^ }
        if (result is error and result.code == 319) raise result
        send(parent,{type:'ended',peer:member.id,worker:child,direction:'outgoing',result:result})^
    }
}
pn incoming(listener, directory, key, handle, credentials, parent, options) any^ {
    let authorize = pn (id)=> { return agent.current_member(directory,key,credentials,id)^ }
    let report = pn (event)=> { send(parent,{*:event,worker:self(),direction:'incoming'})^ }
    while (true) {
        let child = start(burst.accept,[listener,handle,credentials,authorize,options,report])
        let result = wait(child) ^ { ^ }
        if (result is error and result.code == 319) raise result
        if (result is error and result.code == 406) { continue }
        send(parent,{type:'ended',peer:if (result is error) null else result.peer,
            worker:child,direction:'incoming',result:result})^
    }
}
fn update_peer(peers, id, value) => [for (item in peers) if (item.id == id) {*:item,*:value} else item]
fn merge_paths(old, fresh) any^ => unique([*old,*fresh])^
pn report(options, value) any^ {
    if (options.status != null) output(value,options.status,{format:'json',atomic:true})^
    if (options.observer != null) options.observer(value)^
    return value
}
pub pn run(directory, key, options) any^ {
    let initial = agent.resume_agent(directory,key)^
    let root = local.status(initial.handle)^.state.root
    // Register first so external writes racing inventory remain queued by the OS.
    let watch = disk.watch(root)^
    let base_handle = {*:initial.handle,keys:{*:initial.handle.keys,cache_enabled:true}}
    let inventory = local.scan(base_handle,null,true)^
    let first_token = exchange.token(inventory.state)^
    let broker = start(owner.run,[first_token,exchange.token,inventory.repository_cache])
    let handle = {*:base_handle,keys:{*:base_handle.keys,owner:broker,token_cached:true}}
    let listener = peer.listener(options.port,options.bind,true)^
    let advertisement = connectivity.advertise(initial.credentials,listener.port) ^ { ^ }
    if (options.ready != null) output(string(listener.port),options.ready,{format:'text',atomic:true})^
    let me = self()
    let watcher = start(watch_events,[watch,me])
    let ticker = start(ticks,[me,options.interval_ms])
    let browser = start(discovery,[initial.credentials,me])
    let receiver = start(incoming,[listener,directory,key,handle,initial.credentials,me,options])
    let workers = [for (member in initial.credentials.members)
        {id:member.id,task:start(outgoing,[directory,key,handle,initial.credentials,member,me,options])}]
    var peers = [for (worker in workers) {*:worker,url:null,busy:false,delivered:null,retry_at:0}]
    var sessions = []
    var pending_paths = []
    var full = false
    var batch_started = null
    var batch_due = 0
    var scan_retry_at = 0
    var next_verify = timing.milliseconds()+options.verify_ms
    var token = first_token
    var turns = 0
    var current = {phase:'discovering',connected:false,transport:'offline',peer:null,
        sessions:0,attempts:0,retries:0,error_code:null,fallback_reason:null,cloud_saved:false,
        advertising:not (advertisement is error),discovery_error:if (advertisement is error) advertisement.code else null,
        capabilities:connectivity.capabilities()^,progress:null,monitor:'os',scans:1,
        incremental_scans:0,watch_events:0,connections:0,active_connections:0,rounds:0}
    report(options,current)^
    while (options.rounds == 0 or turns < options.rounds) {
        let event = receive()^
        if (event.type == 'monitor_error') raise event.failure
        if (event.type == 'tick') { send(ticker,true)^; turns = turns+1 }
        else if (event.type == 'filesystem') {
            current = {*:current,watch_events:current.watch_events+1}
            if (batch_started == null) { batch_started = timing.milliseconds() }
            full = full or event.rescan
            pending_paths = if (full) [] else merge_paths(pending_paths,event.paths)^
            if (len(pending_paths)^ > 256) { full = true; pending_paths = [] }
            batch_due = min(timing.milliseconds()+options.debounce_ms,batch_started+options.batch_ms)
            send(watcher,true)^
        } else if (event.type == 'discovery') {
            peers = [for (item in peers) {
                let matches = [for (hint in event.hints where hint.peer == item.id) hint]
                let url = if (len(matches)^ == 0) null else matches[0].url
                {*:item,url:url,delivered:if (url != item.url) null else item.delivered}
            }]
            send(browser,true)^
        } else if (event.type == 'connected') {
            sessions = [*sessions,{worker:event.worker,peer:event.peer}]
            current = {*:current,connected:true,phase:'syncing',peer:event.peer,connections:current.connections+1}
        } else if (event.type == 'round') {
            peers = update_peer(peers,event.peer,{delivered:event.token})
            token = exchange.current_token(handle)^
            current = {*:current,transport:'peer',phase:'idle',peer:event.peer,sessions:current.sessions+1,
                rounds:current.rounds+1,error_code:null,fallback_reason:event.warning,progress:event.progress,
                installation:event.installation,review:event.review}
        } else if (event.type == 'ended') {
            let matching = [for (entry in sessions where entry.worker == event.worker) entry]
            let id = if (event.peer != null) event.peer else if (len(matching)^ > 0) matching[0].peer else null
            sessions = [for (entry in sessions where entry.worker != event.worker) entry]
            let failed = event.result is error
            if (id != null) {
                let update = {*:if (event.direction == 'outgoing') {busy:false} else ({}),
                    *:if (failed) {retry_at:timing.milliseconds()+options.retry_ms} else {delivered:event.result.token,retry_at:0}}
                peers = update_peer(peers,id,update)
            }
            if (failed) { current = {*:current,retries:current.retries+1,error_code:event.result.code,
                fallback_reason:"LAN burst ended before completion"} }
        }
        if (timing.milliseconds() >= next_verify) {
            full = true; batch_due = 0
            if (batch_started == null) { batch_started = timing.milliseconds() }
            peers = [for (item in peers) {*:item,delivered:null}]
        }
        if (batch_started != null and timing.milliseconds() >= max(batch_due,scan_retry_at)) {
            let selected_paths = if (full) null else pending_paths
            let captured = owner.cached(broker,pn (cache)=> {
                let active = {*:handle,keys:{*:handle.keys,cached_head:cache}}
                let resumed = agent.resume_installation(active)^
                if (resumed.state.installation != null)
                    raise error("Sync needs review of an interrupted installation. Local files and recovery versions are preserved.")
                return local.scan(active,selected_paths,selected_paths == null)^
            }) ^ { ^ }
            if (captured is error) {
                if (captured.code == 319) raise captured
                // Failed or raced captures retain both hints and the accepted disk baseline.
                scan_retry_at = timing.milliseconds()+options.retry_ms
                current = {*:current,error_code:captured.code,fallback_reason:captured.message}
                next_verify = max(next_verify,scan_retry_at)
            } else {
                current = {*:current,scans:current.scans+(if (full) 1 else 0),
                    incremental_scans:current.incremental_scans+(if (full) 0 else 1),
                    error_code:null,fallback_reason:null}
                if (full) { next_verify = timing.milliseconds()+options.verify_ms }
                full = false; pending_paths = []; batch_started = null; scan_retry_at = 0
                token = exchange.current_token(handle)^
            }
        }
        for (item in peers where item.url != null and not item.busy and item.delivered != token and
            timing.milliseconds() >= item.retry_at and not any([for (entry in sessions) entry.peer == item.id])) {
            send(item.task,{url:item.url})^
            peers = update_peer(peers,item.id,{busy:true})
            current = {*:current,phase:'connecting',peer:item.id,attempts:current.attempts+1}
        }
        current = {*:current,connected:len(sessions)^ > 0,active_connections:len(sessions)^,
            ticks:turns,last_event:event.type,pending_changes:len(pending_paths)^,
            batch_wait_ms:if (batch_started == null) null else max(0,max(batch_due,scan_retry_at)-timing.milliseconds()),
            frontier:token.clock,peers:[for (item in peers) {id:item.id,online:item.url != null,busy:item.busy,
                pending:item.delivered != token}]}
        if (event.type != 'tick' or turns%10 == 0) report(options,current)^
        if (options.stop_after > 0 and current.sessions >= options.stop_after) { break }
    }
    for (task in [watcher,ticker,browser,receiver,*[for (worker in workers) worker.task]]) { cancel(task) }
    for (task in [watcher,ticker,browser,receiver,*[for (worker in workers) worker.task]]) { wait(task) ^ { null } }
    send(broker,{stop:true})^
    wait(broker)^
    disk.unwatch(watch)^
    if (not (advertisement is error)) connectivity.withdraw(advertisement) ^ { null }
    ws.close(listener) ^ { null }
    return report(options,{*:current,phase:'stopped',connected:false,active_connections:0})^
}

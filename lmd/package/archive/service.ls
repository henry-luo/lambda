// The job service (§8, AR12v2): a long-running task that archives URLs
// sent to it, up to `defaults.jobs` at a time, and reports to a callback.
// Requests: {op: 'add', url, options?, tag?}, {op: 'refresh', id, formats?,
// tag?} (re-derive a snapshot, AR3), {op: 'cancel', tag}, {op: 'stop'}. Reports: {type: 'queued', tag, url}, each job's
// {type: 'progress', tag, stage, …}, {type: 'done', tag, snapshot} and
// {type: 'failed', tag, url, message}. Workers are started once, at the
// service's own level, so S13.3.1v2's block-exit join never serializes
// jobs; each job runs as a worker's child, so cancelling it reaches its
// transfers, and stopping or cancelling the service cancels them all.
import jobs: .jobs

pn report(notify, message) { if (notify != null) notify(message) }

// one worker: asks for a job, runs it as a child it can cancel, says how it ended
pn worker(service, notify) {
    send(service, {op: 'ready', worker: self()})^
    while (true) {
        let m = receive()^
        let h = if (m.op == 'refresh') start(jobs.refresh, [m.lib, m.id, m.formats])
            else start(jobs.run, [m.lib, m.url, {*: (m.options or {}), tag: m.tag, progress: notify}])
        send(service, {op: 'running', tag: m.tag, handle: h})^
        let outcome: map | error = wait(h)
        let failed = outcome is error
        send(service, {op: 'finished', worker: self(), tag: m.tag, url: m.url, snapshot: if (failed) null else outcome,
            message: if (failed) (if (outcome.code == 319) "cancelled" else outcome.message) else null})^
    }
}

fn drop_key(m, key) => reduce([{}, *[for (k, v at m where string(k) != key) [k, v]]], (acc, kv) => {*: acc, [kv[0]]: kv[1]})

pub pn serve(lib, notify) null^ {
    let me = self()
    let count = max(1, int(lib.config.defaults.jobs))
    let workers = [for (i in 1 to count) start(worker, [me, notify])]
    var library = lib
    var queue = []
    var idle = []
    var busy = {}
    var serial = 0
    var running = true
    while (running) {
        let m = receive()^
        if (m.op == 'add' or m.op == 'refresh') {
            serial = serial + 1
            let tag = m.tag or ("job-" ++ string(serial))
            queue = [*queue, {op: m.op, url: m.url, id: m.id, formats: m.formats, options: m.options, tag: tag}]
            report(notify, {type: "queued", tag: tag, url: m.url, id: m.id, op: m.op})
        } else if (m.op == 'ready') {
            idle = [*idle, m.worker]
        } else if (m.op == 'running') {
            busy = {*: busy, [m.tag]: m.handle}
        } else if (m.op == 'finished') {
            busy = drop_key(busy, m.tag)
            idle = [*idle, m.worker]
            if (m.snapshot != null) { library = jobs.with_record(library, m.snapshot) }
            report(notify, if (m.snapshot != null) {type: "done", tag: m.tag, snapshot: m.snapshot}
                else {type: "failed", tag: m.tag, url: m.url, message: m.message})
        } else if (m.op == 'cancel') {
            let waiting = [for (q in queue where q.tag == m.tag) q]
            if (len(waiting) > 0) {
                queue = [for (q in queue where q.tag != m.tag) q]
                report(notify, {type: "failed", tag: m.tag, url: waiting[0].url, message: "cancelled"})
            } else if (busy[m.tag] != null) {
                cancel(busy[m.tag])
            }
        } else if (m.op == 'stop') {
            running = false
        }
        // hand queued requests to idle workers, oldest first
        while (running and len(queue) > 0 and len(idle) > 0) {
            send(idle[0], {*: queue[0], lib: library})^
            idle = drop(idle, 1)
            queue = drop(queue, 1)
        }
    }
    // stopping cancels the jobs in flight; leaving this block joins the workers
    for (h in [for (k, v at busy) v]) { cancel(h) }
    for (w in workers) { cancel(w) }
    null
}

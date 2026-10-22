// The transfer layer: io.fetch with the configured User-Agent and timeout, to
// memory or to disk, forwarding each progress level to the job's callback as
// an AR12v2 message. A procedure handler is statement-only (S7.6.7v4), so the
// outcome is returned as {ok, response} or {ok, error} — except cancellation
// (319), which is the task being stopped, not a transfer failing, and raises.

pub pn fetch(url, settings, options = null) map^ {
    let o = options or {}
    let forward = o.progress
    let stage = o.stage or "fetch"
    let tag = o.tag
    let progress = if (forward == null) null else pn (p) => {
        forward({type: "progress", tag: tag, stage: stage, url: p.url, received: p.received, total: p.total, rate: p.rate})
    }
    let request = {user_agent: settings.user_agent, timeout: int(settings.timeout_s), to: o.to, max_bytes: o.max_bytes,
        headers: o.headers, progress: progress, progress_ms: int(settings.progress_ms)}
    var response = null
    var failure = null
    io.fetch(url, request) ^ { failure = ^ } ~ { response = ~ }
    if (failure is error and failure.code == 319) raise failure
    if (failure is error) {ok: false, error: failure, url: url}
    else {ok: true, response: response, url: url}
}

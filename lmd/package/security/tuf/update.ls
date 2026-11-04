// D7.5.8: a pure verifier state machine. Only the trusted host executes actions.
// An acknowledgement is supplied AFTER atomic durable publication; fetches are
// public repository-relative names. A final verified artifact is not installed.
import tuf: lambda.security.tuf
import json: lambda.security.tuf.json
import meta: lambda.security.tuf.metadata
import delegation: lambda.security.tuf.delegation
import uri: lambda.security.tuf.path

fn cached(cache, role) => [for (entry in cache where entry.role == role) entry.bytes][0]
fn stored(cache, role, bytes) => [*[for (entry in cache where entry.role != role) entry], {role: role, bytes: bytes}]
fn checkpoint(session, phase) => {*: session, phase: phase, action: {kind: 'checkpoint', entries: session.cache}}
fn request_download(session, phase, path, ceiling) map^ =>
    if (not uri.repository_path(path)) raise meta.invalid("unsafe repository path")
    else {*: session, phase: phase, action: {kind: 'fetch', path: path, limit: ceiling}}
fn root_fetch(session) map^ {
    if (session.rotations >= 32) raise meta.invalid("root rotation budget exhausted")
    else request_download(session, 'root', json.increment(session.root.version)^ ++ ".root.json", 65536)^
}
fn metadata_fetch(session, phase, role, reference) map^ {
    let pending = request_download(session, phase, tuf.metadata_path(session.root, role, reference.version)^, meta.bounded_length(reference, 262144)^)^
    let raw = cached(session.cache, role);
    // Cache bytes carry no new authority: apply the same reference, signature,
    // expiry and current-delegator checks before reusing them. Rejected cache
    // data leaves rollback history intact and falls back to a bounded fetch.
    if (raw == null) pending
    else advance(pending, {status: 'ok', bytes: raw}) ^ { pending }
}
fn search(session, metadata, pending, ancestry) map^ {
    let description = json.get(meta.require_member(metadata.signed, "targets")^, session.target);
    if (description != null) {
        let info = meta.target_description(description)^
        let byte_limit = meta.bounded_length(info, 67108864)^;
        request_download({*: session, description: description}, 'artifact', tuf.artifact_path(session.root, session.target, description)^, byte_limit)^
    } else {
        let delegations = delegation.rules(metadata)^
        let children = [for (rule in delegations.roles where delegation.matches(rule, session.target)^)
            {parent: metadata, rule: rule, ancestry: ancestry}];
        search_next({*: session, pending: [*children, *pending]})^
    }
}
fn search_next(session) map^ {
    let next = session.pending[0]
    // A terminating edge discards fallback even when its child is an
    // ancestry cycle and therefore cannot be searched again.
    let pending = if (next != null and next.rule.terminating) [] else slice(session.pending, 1, len(session.pending));
    if (next == null) raise meta.invalid("target not found")
    else if (contains(next.ancestry, next.rule.name)) search_next({*: session, pending: pending})^
    else if (session.visited >= 64) raise meta.invalid("delegation budget exhausted")
    else {
        let info = meta.file_reference(session.snapshot, next.rule.name ++ ".json")^;
        metadata_fetch({*: session, pending: pending, current: next, visited: session.visited + 1}, 'delegated', next.rule.name, info)^
    }
}
pub fn begin_refresh(cache, reference_time, target_path, stop_on_unchanged = false) map^ {
    let fixed_time = meta.utc(reference_time)^
    let names = [for (entry in cache) entry.role]
    let root_bytes = cached(cache, "root");
    if (root_bytes == null or len(cache) > 64 or len(unique(names)) != len(names) or
        (target_path != null and not delegation.public_path(target_path))) raise meta.invalid("invalid trusted checkpoint or target")
    else {
        let root = meta.root(root_bytes)^
        let ordered = [{role: "root", bytes: root_bytes}, *[for (entry in cache where entry.role != "root") entry]];
        checkpoint({cache: ordered, reference_time: fixed_time, target: target_path, root: root,
            rotations: 0, visited: 1, stop_on_unchanged: stop_on_unchanged or target_path == null}, 'begin')
    }
}
// Metadata refresh and target lookup share one verified snapshot and fixed
// time. A later target never restarts refresh or silently changes authority.
pub fn select_target(session, target_path) map^ {
    if (session.timestamp == null or not delegation.public_path(target_path))
        raise meta.invalid("target lookup requires a completed metadata refresh")
    else {
        let selected = {*: session, target: target_path, visited: 1};
        if (session.targets != null and session.snapshot != null) search(selected, session.targets, [], ["targets"])^
        else metadata_fetch(selected, 'snapshot', "snapshot", meta.file_reference(session.timestamp, "snapshot.json")^)^
    }
}
pub fn advance(session, response) map^ {
    if (session.action.kind == 'done') raise meta.invalid("refresh already finished")
    else if (session.action.kind == 'checkpoint') {
        if (response.status != 'ok') raise meta.invalid("checkpoint was not committed")
        else if (contains(['begin', 'root_saved'], session.phase)) root_fetch(session)^
        else if (session.phase == 'timestamp_saved') metadata_fetch(session, 'snapshot', "snapshot", meta.file_reference(session.timestamp, "snapshot.json")^)^
        else if (session.phase == 'snapshot_saved') metadata_fetch(session, 'targets', "targets", meta.file_reference(session.snapshot, "targets.json")^)^
        else if (session.phase == 'targets_saved')
            if (session.target == null) {*: session, action: {kind: 'done', status: 'refreshed'}}
            else select_target(session, session.target)^
        else if (session.phase == 'delegated_saved') search(session, session.delegated, session.pending, [*session.current.ancestry, session.current.rule.name])^
        else raise meta.invalid("unknown checkpoint phase")
    } else if (session.phase == 'root' and response.status == 'not_found') {
        let fresh = meta.fresh(session.root, session.reference_time)^;
        request_download(session, 'timestamp', "timestamp.json", 262144)^
    } else if (response.status != 'ok' or not (response.bytes is binary) or len(response.bytes) > session.action.limit)
        raise meta.invalid("download failed or exceeded limit")
    else if (session.phase == 'root') {
        let candidate = tuf.rotate_root(session.root, response.bytes)^
        let reset = tuf.rotation_resets(session.root, candidate)^
        let cache = stored(session.cache, "root", response.bytes)
        let filtered = [for (entry in cache where not reset or not contains(["timestamp", "snapshot"], entry.role)) entry]
        let ordered = [{role: "root", bytes: response.bytes}, *[for (entry in filtered where entry.role != "root") entry]];
        checkpoint({*: session, root: candidate, cache: ordered, rotations: session.rotations + 1}, 'root_saved')
    } else if (session.phase == 'timestamp') {
        let old_bytes = cached(session.cache, "timestamp")
        let previous = if (old_bytes == null) null else meta.read(old_bytes, "timestamp")^
        let result = tuf.timestamp(session.root, response.bytes, previous, session.reference_time)^;
        if (result.unchanged) {
            // A persisted timestamp is not evidence that its target was fetched
            // or installed. Resume from the trusted old envelope, without letting
            // an equal-version response extend expiry or change references.
            let fresh = meta.fresh(result.metadata, session.reference_time)^;
            let continued = {*: session, timestamp: result.metadata};
            if (session.stop_on_unchanged) {*: continued, action: {kind: 'done', status: 'unchanged'}}
            else metadata_fetch(continued, 'snapshot', "snapshot", meta.file_reference(result.metadata, "snapshot.json")^)^
        }
        else checkpoint({*: session, timestamp: result.metadata, cache: stored(session.cache, "timestamp", response.bytes)}, 'timestamp_saved')
    } else if (session.phase == 'snapshot') {
        let old_bytes = cached(session.cache, "snapshot")
        let previous = if (old_bytes == null) null else meta.read(old_bytes, "snapshot")^
        let candidate = tuf.snapshot(session.root, session.timestamp, response.bytes, previous, session.reference_time)^;
        checkpoint({*: session, snapshot: candidate, cache: stored(session.cache, "snapshot", response.bytes)}, 'snapshot_saved')
    } else if (session.phase == 'targets') {
        let candidate = tuf.targets(session.root, session.snapshot, response.bytes, session.reference_time)^;
        checkpoint({*: session, targets: candidate, cache: stored(session.cache, "targets", response.bytes)}, 'targets_saved')
    } else if (session.phase == 'delegated') {
        let candidate = delegation.authenticate(session.current.parent, session.current.rule, session.snapshot, response.bytes, session.reference_time)^;
        checkpoint({*: session, delegated: candidate, cache: stored(session.cache, session.current.rule.name, response.bytes)}, 'delegated_saved')
    } else if (session.phase == 'artifact') {
        let verified = tuf.verify_target(session.description, response.bytes)^;
        {*: session, action: {kind: 'done', status: 'verified', path: session.target, description: session.description, bytes: response.bytes}}
    } else raise meta.invalid("unknown refresh phase")
}

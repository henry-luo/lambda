import updater: lambda.security.tuf.update
import delegation: lambda.security.tuf.delegation
let base = "test/safety/tuf/fixtures/"
// fixture IO failures propagate under the same contract as verifier failures.
fn fixture_bytes(filename) binary^ => input(base ++ filename, 'binary')^
fn fixture_response(action) map^ {
    if (action.kind == 'checkpoint') {status: 'ok'}
    else if (action.path == "2.root.json") {status: 'not_found'}
    else if (action.path == "timestamp.json") {status: 'ok', bytes: fixture_bytes("timestamp.json")}
    else if (contains(["1.snapshot.json", "1.targets.json"], action.path)) {status: 'ok', bytes: fixture_bytes(action.path)}
    else {status: 'ok', bytes: fixture_bytes("artifact.bin")}
}
fn drive(session, steps, trace) map^ {
    if (steps > 20) raise error("fixture refresh loop")
    else if (session.action.kind == 'done') {session: session, trace: trace}
    else drive(updater.advance(session, fixture_response(session.action)^)^, steps + 1, [*trace, session.action.kind])^
}
let initial = updater.begin_refresh([{role: "root", bytes: fixture_bytes("root.json")}], "2026-11-03T00:00:00Z", "lambda.pkg")^
let complete = drive(initial, 0, [])^
let unchanged = drive(updater.begin_refresh(complete.session.cache, "2026-11-03T00:00:00Z", "lambda.pkg", true)^, 0, [])^
let root_fetch = updater.advance(initial, {status: 'ok'})^
let timestamp_fetch = updater.advance(root_fetch, {status: 'not_found'})^
let interrupted = updater.advance(timestamp_fetch, {status: 'ok', bytes: fixture_bytes("timestamp.json")})^
let resumed = drive(updater.begin_refresh(interrupted.cache, "2026-11-03T00:00:00Z", "lambda.pkg")^, 0, [])^
let rotating = updater.advance(root_fetch, {status: 'ok', bytes: fixture_bytes("2.root.json")})^
let with_cache = updater.begin_refresh(complete.session.cache, "2026-11-03T00:00:00Z", "lambda.pkg")^
let rotated_cache = updater.advance(updater.advance(with_cache, {status: 'ok'})^, {status: 'ok', bytes: fixture_bytes("2.root.json")})^
let metadata_only = drive(updater.begin_refresh(initial.cache, "2026-11-03T00:00:00Z", null)^, 0, [])^
let selected = drive(updater.select_target(metadata_only.session, "lambda.pkg")^, 0, [])^
let polled = drive(updater.begin_refresh(metadata_only.session.cache, "2026-11-03T00:00:00Z", null)^, 0, [])^
let selected_cached = drive(updater.select_target(polled.session, "lambda.pkg")^, 0, [])^
let expired_initial = updater.begin_refresh([{role: "root", bytes: fixture_bytes("root.json")},
    {role: "timestamp", bytes: fixture_bytes("timestamp-expired.json")}], "2026-11-03T00:00:00Z", "lambda.pkg", true)^
let expired_fetch = updater.advance(updater.advance(expired_initial, {status: 'ok'})^, {status: 'not_found'})^;
[complete.session.action.status, complete.trace, [for (entry in complete.session.cache) entry.role],
 unchanged.session.action.status, unchanged.trace,
 rotating.phase, [for (entry in rotated_cache.cache) entry.role],
 updater.advance(initial, {status: 'failed'}) ^ { 'denied' },
 updater.advance(timestamp_fetch, {status: 'not_found'}) ^ { 'denied' },
 updater.begin_refresh(initial.cache, "2035-02-30T00:00:00Z", "lambda.pkg") ^ { 'denied' },
 updater.begin_refresh(initial.cache, "2026-11-03T00:00:00Z", "../private") ^ { 'denied' },
 delegation.glob("targets/*.tgz", "targets/foo.tgz")^,
 delegation.glob("*.tgz", "targets/foo.tgz")^,
 delegation.glob("foo-?.tgz", "foo-2.tgz")^,
 delegation.glob("*a*a*a*b", "aaaaaaaaaaaaaaaaaaaac")^,
 delegation.glob("[ab]", "a") ^ { 'denied' }, resumed.session.action.status,
 metadata_only.session.action.status, selected.session.action.status, len(selected.trace),
 polled.session.action.status, selected_cached.session.action.status,
 updater.select_target(initial, "lambda.pkg") ^ { 'denied' },
 updater.advance(expired_fetch, {status: 'ok', bytes: fixture_bytes("timestamp.json")}) ^ { 'denied' }]

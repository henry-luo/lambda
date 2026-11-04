// D7.5.8: update authentication decisions are Lambda Script. Host callbacks own
// admitted downloads, atomic protected checkpoints, policy checks and staging.
import json: lambda.security.tuf.json
import meta: lambda.security.tuf.metadata
import uri: lambda.security.tuf.path

pub fn bootstrap(raw, expected_sha256) map^ {
    if (meta.hex(hash(binary(raw), 'sha256')^)^ != lower(expected_sha256)) raise meta.invalid("bootstrap root fingerprint mismatch")
    else meta.root(raw)^
}
pub fn rotate_root(trusted, raw) map^ {
    let candidate = meta.root(raw)^
    let old_signature = meta.authenticate(candidate, trusted.keys, meta.root_role(trusted, "root")^)^;
    if (candidate.version != json.increment(trusted.version)^) raise meta.invalid("root version must advance by one") else candidate
}
fn authority_keys(root_metadata, role) array^ {
    let rule = meta.policy(meta.root_role(root_metadata, role)^)^;
    sort([for (id in rule.keyids) {id: id, key: json.canonical(meta.require_member(root_metadata.keys, id)^)^}], {by: (entry) => entry.id})
}
pub fn rotation_resets(previous, current) bool^ => authority_keys(previous, "timestamp")^ != authority_keys(current, "timestamp")^ or
    authority_keys(previous, "snapshot")^ != authority_keys(current, "snapshot")^
pub fn timestamp(root_metadata, raw, previous, reference_time) map^ {
    let fresh_root = meta.fresh(root_metadata, reference_time)^
    let candidate = meta.authenticated(root_metadata, raw, "timestamp")^
    let info = meta.file_reference(candidate, "snapshot.json")^
    let entries = json.members(meta.require_member(candidate.signed, "meta")^)^;
    if (len(entries) != 1 or info.version == null) raise meta.invalid("invalid timestamp snapshot reference")
    else if (previous != null and json.compare(candidate.version, previous.version) < 0) raise meta.invalid("timestamp rollback")
    else if (previous != null and candidate.version == previous.version) {unchanged: true, metadata: previous}
    else if (previous != null and json.compare(info.version, meta.file_reference(previous, "snapshot.json")^.version) < 0)
        raise meta.invalid("snapshot version rollback in timestamp")
    else {
        let checked = meta.fresh(candidate, reference_time)^;
        {unchanged: false, metadata: candidate}
    }
}
pub fn snapshot(root_metadata, timestamp_metadata, raw, previous, reference_time) map^ {
    let info = meta.file_reference(timestamp_metadata, "snapshot.json")^
    let hashes = meta.matches(raw, info)^
    let candidate = meta.authenticated(root_metadata, raw, "snapshot")^
    let entries = meta.snapshot_entries(candidate)^
    let old_entries = if (previous == null) [] else meta.snapshot_entries(previous)^;
    if (candidate.version != info.version) raise meta.invalid("snapshot version mismatch")
    else if (any([for (old in old_entries)
        (let current = [for (entry in entries where entry.file == old.file) entry][0],
         current == null or json.compare(current.info.version, old.info.version) < 0)]))
        raise meta.invalid("snapshot removed or rolled back metadata")
    else {
        let checked = meta.fresh(candidate, reference_time)^;
        candidate
    }
}
pub fn targets(root_metadata, snapshot_metadata, raw, reference_time) map^ {
    let info = meta.file_reference(snapshot_metadata, "targets.json")^
    let hashes = meta.matches(raw, info)^
    let candidate = meta.authenticated(root_metadata, raw, "targets")^
    let entries = meta.validate_targets(candidate)^;
    if (candidate.version != info.version) raise meta.invalid("targets version mismatch")
    else {
        let checked = meta.fresh(candidate, reference_time)^;
        candidate
    }
}
pub fn verify_target(description, bytes) bool^ {
    let info = meta.file_info(description)^;
    if (info.length == null) raise meta.invalid("target length is required") else meta.matches(bytes, info, true)^
}
pub fn metadata_path(root_metadata, role, revision) string^ =>
    uri.component((if (root_metadata.consistent) revision ++ "." else "") ++ role ++ ".json")^
pub fn artifact_path(root_metadata, target_path, description) string^ {
    let info = meta.file_info(description)^
    let hashes = [for (entry in info.hashes where contains(["sha256", "sha384", "sha512"], entry.name)^) entry];
    if (len(hashes) == 0) raise meta.invalid("target has no supported hash")
    else if (not root_metadata.consistent) uri.target_path(target_path)^
    else {
        let parts = split(target_path, "/")
        let leaf = parts[len(parts) - 1]
        let folder = if (len(parts) == 1) "" else join(take(parts, len(parts) - 1), "/") ++ "/";
        uri.target_path(folder ++ lower(json.text(hashes[0].node)^) ++ "." ++ leaf)^
    }
}

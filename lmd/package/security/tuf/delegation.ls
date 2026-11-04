// D7.5.8: constrained repository paths and ordered delegation checks.
import json: lambda.security.tuf.json
import meta: lambda.security.tuf.metadata
import uri: lambda.security.tuf.path

pub fn public_path(text, ceiling = 4096) bool => uri.target_name(text) and len(binary(text)) < ceiling
fn char_at(chars, at) => if (at >= 0 and at < len(chars)) chars[at] else null
// A remembered star advances at most once per input position; no exponential
// recursive wildcard branching. Wildcards never consume a path separator.
fn glob_walk(pattern, path, i, j, star, matched) bool {
    let p = char_at(pattern, i)
    let c = char_at(path, j);
    if (c == null) all([for (tail in slice(pattern, i, len(pattern))) tail == "*"])
    else if (p == "*") glob_walk(pattern, path, i + 1, j, i, j)
    else if (p == c or (p == "?" and c != "/")) glob_walk(pattern, path, i + 1, j + 1, star, matched)
    else if (star >= 0 and char_at(path, matched) != "/" and matched < len(path))
        glob_walk(pattern, path, star + 1, matched + 1, star, matched + 1)
    else false
}
pub fn glob(pattern, path) bool^ {
    // This first profile supports TUF's * and ? forms. Bracket classes fail
    // closed until the repository profile and conformance tests cover them.
    if (not (pattern is string) or len(pattern) > 1024 or contains(pattern, "[") or contains(pattern, "]"))
        raise meta.invalid("unsupported delegation pattern")
    else glob_walk(split(pattern, ""), split(path, ""), 0, 0, -1, 0)
}
pub fn rules(metadata) map^ {
    let node = json.get(metadata.signed, "delegations");
    if (node == null) {keys: {kind: 'object', entries: []}, roles: []}
    else {
        let keys = meta.require_member(node, "keys")^
        let key_ids = meta.validate_keys(keys)^
        let key_entries = json.members(keys)^
        let entries = json.items(meta.require_member(node, "roles")^)^
        let names = [for (entry in entries) meta.field_text(entry, "name")^];
        if (len(entries) > 64 or len(key_entries) > 256 or len(unique(names)) != len(names) or
            any([for (role in names) not uri.role_name(role) or contains(["root", "timestamp", "snapshot", "targets"], role)]))
            raise meta.invalid("invalid or excessive delegations")
        else {
            let roles = [for (entry in entries)
                (let policy = meta.policy(entry)^,
                 let paths = json.get(entry, "paths"),
                 let prefixes = json.get(entry, "path_hash_prefixes"),
                 let terms = json.boolean(meta.require_member(entry, "terminating")^)^,
                 let patterns = if (paths == null) [] else [for (p in json.items(paths)^) json.text(p)^],
                 let hashes = if (prefixes == null) [] else [for (p in json.items(prefixes)^) json.text(p)^],
                 if ((paths == null) == (prefixes == null) or len(patterns) + len(hashes) > 256 or
                     any([for (id in policy.keyids) json.get(keys, id) == null]) or
                     any([for (h in hashes) len(h) < 1 or len(h) > 64 or not all([for (c in split(h, "")) contains("0123456789abcdef", c)])]))
                     raise meta.invalid("invalid delegated authority")
                 else {name: meta.field_text(entry, "name")^, policy: entry, terminating: terms, patterns: patterns, prefixes: hashes})];
            {keys: keys, roles: roles}
        }
    }
}
pub fn matches(rule, target_path) bool^ {
    let digest = meta.hex(hash(target_path, 'sha256')^);
    any([for (pattern in rule.patterns) glob(pattern, target_path)^]) or
        any([for (prefix in rule.prefixes) starts_with(digest, prefix)])
}
pub fn authenticate(parent, rule, snapshot, raw, reference_time) map^ {
    let info = meta.file_reference(snapshot, rule.name ++ ".json")^
    let hashes = meta.matches(raw, info)^
    let candidate = meta.read(raw, "targets")^
    let delegated = rules(parent)^
    let signature = meta.authenticate(candidate, delegated.keys, rule.policy)^
    let fresh = meta.fresh(candidate, reference_time)^
    let entries = meta.validate_targets(candidate)^;
    if (candidate.version != info.version) raise meta.invalid("delegated version mismatch") else candidate
}

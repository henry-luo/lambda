// D7.5.8: wire/schema checks and public-key primitives; no IO or authority.
import json: lambda.security.tuf.json

pub fn invalid(message) error => error("tuf: " ++ message)
pub fn require_member(node, field) map^ {
    let child = json.get(node, field);
    if (child == null) raise invalid("missing " ++ field) else child
}
pub fn field_text(node, field) string^ => json.text(require_member(node, field)^)^
pub fn version(node) string^ => json.positive(require_member(node, "version")^)^
pub fn hex_valid(text) => text is string and len(text) % 2 == 0 and
    all([for (c in split(lower(text), "")) contains("0123456789abcdef", c)])
pub fn unhex(text) binary^ {
    if (not hex_valid(text)) raise invalid("invalid hexadecimal bytes")
    // S2.2.2v2: binary([]) is null; the wire decoder retains a zero-byte carrier.
    else if (len(text) == 0) binary("")
    else binary([for (i in 0 to int(len(text) / 2) - 1)
        index_of("0123456789abcdef", lower(slice(text, i * 2, i * 2 + 1))) * 16 +
        index_of("0123456789abcdef", lower(slice(text, i * 2 + 1, i * 2 + 2)))])
}
pub fn hex(bytes) => join([for (b in binary(bytes))
    slice("0123456789abcdef", int(b / 16), int(b / 16) + 1) ++
    slice("0123456789abcdef", b % 16, b % 16 + 1)], "")
fn digits(text) => len(text) > 0 and all([for (c in split(text, "")) contains("0123456789", c)])
fn numeric_identifier(text) => digits(text) and (len(text) == 1 or slice(text, 0, 1) != "0")
fn identifiers(text, prerelease) => len(text) > 0 and all([for (part in split(text, "."))
    len(part) > 0 and all([for (c in split(part, "")) contains("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-", c)]) and
    (not prerelease or not digits(part) or numeric_identifier(part))])
fn spec_version(text) {
    let build = split(text, "+")
    let pre = split(build[0], "-")
    let core = split(pre[0], ".")
    let suffix = join(slice(pre, 1, len(pre)), "-");
    len(text) <= 256 and len(build) <= 2 and len(core) == 3 and core[0] == "1" and
        all([for (part in core) numeric_identifier(part)]) and
        (len(build) == 1 or identifiers(build[1], false)) and (len(pre) == 1 or identifiers(suffix, true))
}
pub fn utc(text) string^ {
    let components = [slice(text, 0, 4), slice(text, 5, 7), slice(text, 8, 10),
        slice(text, 11, 13), slice(text, 14, 16), slice(text, 17, 19)]
    let separators = len(text) == 20 and slice(text, 4, 5) == "-" and slice(text, 7, 8) == "-" and
        slice(text, 10, 11) == "T" and slice(text, 13, 14) == ":" and slice(text, 16, 17) == ":" and slice(text, 19, 20) == "Z";
    if (not separators or not all([for (part in components) digits(part)])) raise invalid("invalid UTC expiry")
    else {
        let y = int(components[0])
        let m = int(components[1])
        let d = int(components[2])
        let leap = y % 400 == 0 or (y % 4 == 0 and y % 100 != 0)
        let days = [31, if (leap) 29 else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
        if (y < 1 or m < 1 or m > 12 or d < 1 or d > days[m - 1] or int(components[3]) > 23 or
            int(components[4]) > 59 or int(components[5]) > 59) raise invalid("invalid calendar expiry") else text
    }
}
pub fn read(raw, expected_type) map^ {
    let envelope = json.read(raw)^
    let payload = require_member(envelope, "signed")^
    let signatures = json.items(require_member(envelope, "signatures")^)^
    let tag = field_text(payload, "_type")^
    let spec = field_text(payload, "spec_version")^
    let revision = version(payload)^
    let expiry = utc(field_text(payload, "expires")^)^;
    if (tag != expected_type or not spec_version(spec) or len(signatures) > 256) raise invalid("metadata type, specification or signature limit")
    else {
        let ids = [for (sig in signatures) field_text(sig, "keyid")^];
        if (any([for (id in ids) id == "" or len(id) > 256]) or len(unique(ids)) != len(ids))
            raise invalid("duplicate or invalid signature key identifier")
        else {raw: binary(raw), signed: payload, signatures: signatures, version: revision, expires: expiry,
            canonical: json.canonical(payload)^}
    }
}
pub fn fresh(metadata, reference_time) bool^ =>
    if (metadata.expires <= utc(reference_time)^) raise invalid("expired " ++ field_text(metadata.signed, "_type")^) else true
pub fn policy(node) map^ {
    let ids = [for (entry in json.items(require_member(node, "keyids")^)^) json.text(entry)^]
    let minimum = json.positive(require_member(node, "threshold")^)^;
    if (len(ids) == 0 or len(ids) > 256 or len(unique(ids)) != len(ids) or
        any([for (id in ids) id == "" or len(id) > 256]))
        raise invalid("invalid signature threshold or key set")
    // An impossible child threshold does not invalidate a signed root rotation.
    // Keep the numeral exact and refuse that child's authentication separately.
    else {keyids: ids, minimum: minimum}
}
let SCHEMES = [["ed25519", "ed25519"], ["rsa", "rsassa-pss-sha256"], ["ecdsa", "ecdsa-sha2-nistp256"]]
fn supported(key) => contains(SCHEMES, [key.kind, key.scheme])
fn public_key(node) map^ => {kind: field_text(node, "keytype")^, scheme: field_text(node, "scheme")^,
    public: field_text(require_member(node, "keyval")^, "public")^}
pub fn validate_keys(keys) bool^ {
    let entries = json.members(keys)^;
    if (len(entries) > 256) raise invalid("key count limit")
    else {
        let checked = [for (entry in entries)
            (let key = public_key(entry.node)^,
             entry.name == hex(hash(json.canonical(entry.node)^, 'sha256')^))];
        // TUF 4.3: recompute identifiers; a caller-supplied alias is not another key.
        if (not all(checked)) raise invalid("key identifier fingerprint mismatch") else true
    }
}
fn verifies(metadata, sig, key_node) bool^ {
    let key = public_key(key_node)^
    let encoded = field_text(sig, "sig")^;
    if (not supported(key) or not hex_valid(encoded)) false
    else if (key.kind == "ed25519" and (len(key.public) != 64 or not hex_valid(key.public))) false
    else {
        let bytes = if (key.kind == "ed25519") unhex(key.public)^ else binary(key.public);
        verify_signature(bytes, metadata.canonical, unhex(encoded)^, key.scheme)^
    }
}
pub fn authenticate(metadata, keys, role_policy) bool^ {
    let key_ids = validate_keys(keys)^
    let allowed = policy(role_policy)^
    let valid = [for (sig in metadata.signatures)
        (let id = field_text(sig, "keyid")^,
         let key = json.get(keys, id),
         if (contains(allowed.keyids, id) and key != null) verifies(metadata, sig, key) ^ { false } else false)];
    // A malformed extra signature contributes nothing; independent valid keys
    // may still meet the threshold. Duplicate key IDs were rejected at read.
    if (json.compare(string(len([for (ok in valid where ok) true])), allowed.minimum) < 0)
        raise invalid("signature threshold not met") else true
}
pub fn root(raw) map^ {
    let metadata = read(raw, "root")^
    let keys = require_member(metadata.signed, "keys")^
    let roles = require_member(metadata.signed, "roles")^
    let entries = json.members(keys)^
    let policies = [for (role in ["root", "timestamp", "snapshot", "targets"]) policy(require_member(roles, role)^)^]
    let consistent = json.get(metadata.signed, "consistent_snapshot");
    if (len(entries) == 0 or len(entries) > 256 or
        any([for (p in policies) any([for (id in p.keyids) json.get(keys, id) == null])])) raise invalid("missing root key")
    else {
        let signed = authenticate(metadata, keys, require_member(roles, "root")^)^;
        {*: metadata, keys: keys, roles: roles, consistent: if (consistent == null) false else json.boolean(consistent)^}
    }
}
pub fn root_role(root_metadata, role) map^ => require_member(root_metadata.roles, role)^
pub fn authenticated(root_metadata, raw, role) map^ {
    let metadata = read(raw, role)^
    let checked = authenticate(metadata, root_metadata.keys, root_role(root_metadata, role)^)^;
    metadata
}
pub fn file_info(node) map^ {
    let revision = json.get(node, "version")
    let length = json.get(node, "length")
    let hashes = json.get(node, "hashes")
    let values = if (hashes == null) [] else json.members(hashes)^
    let checked = [for (entry in values) json.text(entry.node)^];
    {version: if (revision == null) null else json.positive(revision)^,
     length: if (length == null) null else json.natural(length)^, hashes: values}
}
pub fn file_reference(metadata, filename) map^ =>
    file_info(require_member(require_member(metadata.signed, "meta")^, filename)^)^
pub fn matches(raw, info, required_hash = false) bool^ {
    let bytes = binary(raw)
    let supported_hashes = [for (entry in info.hashes where contains(["sha256", "sha384", "sha512"], entry.name)) entry]
    let verified = [for (entry in supported_hashes) hex(hash(bytes, symbol(entry.name))^) == lower(json.text(entry.node)^)];
    if (info.length != null and info.length != string(len(bytes))) raise invalid("signed byte length mismatch")
    else if ((required_hash or len(info.hashes) > 0) and len(supported_hashes) == 0) raise invalid("no supported content hash")
    else if (not all(verified)) raise invalid("signed content hash mismatch") else true
}
pub fn bounded_length(info, ceiling) int^ => if (info.length == null) ceiling
    else if (json.compare(info.length, string(ceiling)) > 0) raise invalid("signed length exceeds host ceiling") else int(info.length)
pub fn snapshot_entries(metadata) array^ {
    let entries = json.members(require_member(metadata.signed, "meta")^)^;
    if (json.get(require_member(metadata.signed, "meta")^, "targets.json") == null) raise invalid("missing targets snapshot")
    else [for (entry in entries)
        (let info = file_info(entry.node)^,
         if (info.version == null) raise invalid("missing metadata version") else {file: entry.name, info: info})]
}
pub fn target_description(node) map^ {
    let info = file_info(node)^
    let custom = json.get(node, "custom")
    let checked = if (custom == null) [] else json.members(custom)^;
    if (info.length == null or len(info.hashes) == 0 or
        any([for (entry in info.hashes) (let text = json.text(entry.node)^, len(text) == 0 or not hex_valid(text))]))
        raise invalid("invalid target length or hashes") else info
}
pub fn validate_targets(metadata) bool^ {
    let entries = json.members(require_member(metadata.signed, "targets")^)^
    let checked = [for (entry in entries) target_description(entry.node)^];
    true
}

// D7.5.2, D7.5.8: inactive package decoding; no filesystem or install authority.
import meta: lambda.security.tuf.metadata

pub fn limits() => {type: "zip", max_archive_bytes: 67108864, max_index_bytes: 8388608,
    max_member_bytes: 16777216, max_expanded_bytes: 67108864, max_entries: 4096,
    max_name_bytes: 1024, max_path_depth: 32, max_nesting_depth: 0}

// The initial managed-image profile uses printable ASCII names to avoid Unicode
// normalization aliases across filesystems. TUF target/role names stay UTF-8.
pub fn path_key(text) string^ {
    if (not (text is string) or len(binary(text)) == 0 or len(binary(text)) > 1024)
        raise meta.invalid("package path length")
    let parts = split(text, "/");
    if (len(parts) > 32) raise meta.invalid("package path depth")
    for (part in parts) {
        let stem = lower(split(part, ".")[0]);
        if (part == "" or part == "." or part == ".." or len(part) > 255 or
            part != trim(part) or ends_with(part, ".") or stem != trim(stem) or
            not all([for (c in split(part, "")^) ord(c) >= 32 and ord(c) < 127 and
                not contains("<>:\"\\|?*", c)]) or
            contains(["con", "conin$", "conout$", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5",
                "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5",
                "lpt6", "lpt7", "lpt8", "lpt9"], stem) or
            lower(part) == ".lambda" or starts_with(lower(part), ".lambda-stage-"))
            raise meta.invalid("unsafe package path")
    }
    return lower(text)
}

fn mode(node) => if (node.mode == null) 0 else node.mode
fn action(session) map^ {
    if (session.cursor == len(session.nodes)) {
        return {*: session, action: {kind: 'done', entries: len(session.nodes), bytes: session.total,
            digest: session.digest}}
    }
    let node = session.nodes[session.cursor]
    let directory = node.kind == 'dir'
    let decoded = if (directory) null else parse(node, 'binary')^
    // S2.2.2v2: empty captured members parse as null; IPC needs an empty carrier.
    let bytes = if (decoded == null) binary("") else decoded;
    if (not (bytes is binary) or len(bytes) != (if (directory) 0 else node.size))
        raise meta.invalid("package member size")
    return {*: session, action: {kind: 'entry', path: node.entry_path, directory: directory,
        executable: not directory and band(mode(node)^, 73) != 0, bytes: bytes}}
}

pub fn begin(bytes) map^ {
    if (not (bytes is binary)) raise meta.invalid("package bytes required")
    let archive = parse(bytes, limits())^
    // D7.4.5v2: enumerate the archive's virtual child view through its iterator.
    var nodes = [for (node in content(archive)^) node], cursor = 0, total = 0, keys = []
    // Validate the complete namespace before decoding/emitting the first member.
    while (cursor < len(nodes)) {
        if (len(nodes) > 4096) raise meta.invalid("package entry limit")
        let node = nodes[cursor]
        let key = path_key(node.entry_path)^;
        if (node.is_link == true or band(mode(node)^, 3584) != 0 or
            (node.kind != 'dir' and node.kind != 'file')) raise meta.invalid("package member kind or privileged mode")
        if (node.kind == 'dir') { nodes = [*nodes, *[for (child in content(node)^) child]] }
        else { total = total + node.size }
        keys = [*keys, key]
        cursor = cursor + 1
    }
    if (len(unique(keys)) != len(keys) or total > 67108864) raise meta.invalid("package namespace collision or size")
    return action({nodes: nodes, cursor: 0, total: total, digest: hash(bytes, 'sha256')^})^
}

pub fn advance(session) map^ {
    if (session.action.kind != 'entry') raise meta.invalid("package stage already complete")
    else action({*: session, cursor: session.cursor + 1})^
}

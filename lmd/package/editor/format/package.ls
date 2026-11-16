// Replace selected ZIP members while keeping all other member bytes (D9.3.4).
// The original archive remains the snapshot; writing the returned tree is an
// effect owned by the session, not by this format helper (S12.1.1v2).

fn members(node) array^ => [for (child in content(node)^)
  if (child.kind == 'dir') *members(child)^ else child]

fn rewrite(node, replacements, preserve, root = false) element^ {
  // The captured archive root is a file-shaped node with ZIP children.
  if (node.kind == 'dir' or (node.format == 'zip' and node.entry_path == "")) {
    let children = [for (child in content(node)^) rewrite(child, replacements, preserve)^];
    <fs name: node.name, kind: 'dir', mode: node.mode, archive_mode: node.archive_mode, modified: node.modified,
      explicit_dir: node.explicit_dir, archive_source: if (preserve) node else null,
      preserve_zip: if (root) preserve else null, *children>
  }
  else if (replacements[node.entry_path] != null) {
    <fs name: node.name, kind: 'file', mode: node.mode, archive_mode: node.archive_mode, modified: node.modified,
      compression: node.compression, archive_source: if (preserve) node else null, replacements[node.entry_path]>
  }
  else node
}

// Updates name existing files by their complete archive path. Requiring one
// match prevents an ambiguous ZIP entry or typo from silently changing data.
pub fn replace_members(archive, replacements, preserve = false) element^ {
  if (not (archive is element) or archive.format != 'zip' or archive.entry_path != "")
    raise error("editor package: expected a captured ZIP root")
  else if (not (replacements is map)) raise error("editor package: replacements must be a map")
  else {
    let files = members(archive)^
    let invalid = [for (key, value at replacements,
      let matches = [for (entry in files where entry.entry_path == string(key)) entry]
      where (len(matches) != 1 or not (value is string or value is binary))) string(key)]
    if (len(invalid) > 0) raise error("editor package: invalid or ambiguous member " ++ invalid[0])
    else rewrite(archive, replacements, preserve, true)^
  }
}

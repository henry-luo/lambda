// Source offsets are shared by property adapters; whitespace is JSON/XML space.
pub fn raw(source: string, first: int, end_pos: int) string => slice(source, first, end_pos) ^ { "" }
pub fn char_at(source: string, at: int) string => raw(source, at, at + 1)
pub fn skip_space(source: string, at: int) {
  var i = at
  while (i < len(source) and (contains([" ", "\t", "\r", "\n"], char_at(source, i)) ^ { false })) { i = i + 1 }
  i
}

// Appended rows follow the nearest existing line ending in mixed files.
pub fn trailing_line_ending(source) any^ {
  let lf = last_index_of(source, "\n")
  let cr = last_index_of(source, "\r")
  if (cr != null and (lf == null or cr > lf)) "\r"
  else if (lf != null and lf > 0 and slice(source, lf - 1, lf)^ == "\r") "\r\n" else "\n"
}

// Property adapters identify a node by its stable source offset.
pub fn find_node(node, first) any^ {
  if (node.first == first) node
  else if (node.kind == 'scalar') null
  else {
    let matches = [for (child in node.children, let found = find_node(child.node, first)^ where found != null) found]
    if (len(matches) == 0) null else matches[0]
  }
}

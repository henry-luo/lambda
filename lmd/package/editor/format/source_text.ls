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

pub fn natural_line(source: string, first: int) map^ {
  var end_pos = first
  while (end_pos < len(source) and not (raw(source, end_pos, end_pos + 1) in ["\r", "\n"])) { end_pos = end_pos + 1 }
  let next = if (raw(source, end_pos, end_pos + 2) == "\r\n") end_pos + 2
             else if (end_pos < len(source)) end_pos + 1 else end_pos;
  {first: first, end: end_pos, next: next}
}

pub fn find_member(node, first) any^ {
  if (node.kind == 'scalar') null
  else {
    let direct = if (node.kind == 'object') [for (child in node.children where child.first == first) child] else []
    let nested = [for (child in node.children, let found = find_member(child.node, first)^ where found != null) found]
    if (len(direct) > 0) direct[0] else if (len(nested) > 0) nested[0] else null
  }
}

pub fn find_parent(node, first) any^ {
  if (node.kind == 'scalar') null
  else {
    let direct = [for (child in node.children where child.node.first == first) child]
    if (len(direct) > 0) node
    else {
      let nested = [for (child in node.children, let found = find_parent(child.node, first)^ where found != null) found]
      if (len(nested) > 0) nested[0] else null
    }
  }
}

// JSON, TOML and YAML share quote boundaries; their providers own validation.
pub fn quoted_end(text: string, first: int, dialect: symbol = 'json') int^ {
  let quote = char_at(text, first)
  let triple = dialect == 'toml' and raw(text, first, first + 3) == (quote ++ quote ++ quote)
  let quote_count = if (triple) 3 else 1
  var i = first + quote_count
  while (i < len(text)) {
    let ch = char_at(text, i)
    if (ch == "\\" and quote == "\"") { i = i + 2 }
    else if (ch == quote) {
      if (triple) {
        var end_pos = i
        while (end_pos < len(text) and char_at(text, end_pos) == quote) { end_pos = end_pos + 1 }
        if (end_pos - i >= 3) { return end_pos }
        i = end_pos
      } else if (dialect == 'yaml' and quote == "'" and char_at(text, i + 1) == quote) { i = i + 2 }
      else { return i + 1 }
    } else { i = i + 1 }
  }
  raise error("unterminated quoted value")
}

pub fn skip_horizontal(text: string, first: int) int {
  var i = first
  while (i < len(text) and char_at(text, i) in [" ", "\t"]) { i = i + 1 }
  i
}

pub fn has_controls(text: string, allowed = []) any^ =>
  any([for (ch in split(text, "")) (ord(ch) < 32 or ord(ch) == 127) and not (ch in allowed)])

// Folded records and normalized controls share the same minimal changed span.
pub fn changed_span(before: string, after: string) map {
  var prefix = 0, suffix = 0
  while (prefix < len(before) and prefix < len(after) and char_at(before, prefix) == char_at(after, prefix)) { prefix = prefix + 1 }
  while (suffix < len(before) - prefix and suffix < len(after) - prefix and
    char_at(before, len(before) - suffix - 1) == char_at(after, len(after) - suffix - 1)) { suffix = suffix + 1 }
  {prefix: prefix, suffix: suffix}
}
pub fn normalized_offset(text: string, offset: int) int {
  var i = 0, logical = 0
  while (i < len(text) and logical < offset) {
    i = i + (if (raw(text, i, i + 2) == "\r\n") 2 else 1)
    logical = logical + 1
  }
  i
}
pub fn replace_normalized(original: string, value: string, newline: string) string^ {
  let before = replace(replace(original, "\r\n", "\n"), "\r", "\n")
  let changed = changed_span(before, value)
  let first = normalized_offset(original, changed.prefix)
  let end_pos = normalized_offset(original, len(before) - changed.suffix)^
  return raw(original, 0, first) ++ replace(raw(value, changed.prefix, len(value) - changed.suffix), "\n", newline)^ ++ raw(original, end_pos, len(original))
}

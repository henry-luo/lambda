// Appended rows follow the nearest existing line ending in mixed files.
pub fn trailing_line_ending(source) any^ {
  let at = last_index_of(source, "\n")^
  if (at != null and at > 0 and slice(source, at - 1, at)^ == "\r") "\r\n" else "\n"
}

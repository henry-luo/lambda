// Graphviz arrow names and edge direction lowered to paint marker roles.

pub fn canonical(raw) {
  if (raw == null or trim(string(raw)) == "") null
  else lower(trim(string(raw)))
}

fn direction(raw, directed) {
  let value = lower(string(if (raw == null) (if (directed) "forward" else "none") else raw));
  if (contains(["forward", "back", "both", "none"], value)) value
  else if (directed) "forward" else "none"
}

pub fn head(raw, dir, directed) {
  let value = direction(dir, directed);
  if (value == "none" or value == "back") "none"
  else if (canonical(raw) != null) canonical(raw) else "normal"
}

pub fn tail(raw, dir, directed) {
  let value = direction(dir, directed);
  if (value == "back" or value == "both")
    if (canonical(raw) != null) canonical(raw) else "normal"
  else "none"
}

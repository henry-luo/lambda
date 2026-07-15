// Graphviz arrow names and edge direction lowered to paint marker roles.

let ARROW_SHAPES = ["diamond", "normal", "icurve", "curve", "crow",
  "none", "box", "dot", "inv", "tee", "vee"]

fn arrow_shape(text, index) {
  let rest = slice(text, index, len(text));
  let matches = [for (shape in ARROW_SHAPES where starts_with(rest, shape)) shape];
  if (len(matches) > 0) matches[0] else null
}

fn component_name(shape, open, side) {
  let base = if (open and contains(["box", "diamond", "dot"], shape)) "o" ++ shape
    else shape ++ (if (open) ":open" else "");
  base ++ (if (side == "l") ":left" else if (side == "r") ":right" else "")
}

fn parse_result(valid, values, next = null) =>
  {valid: valid, values: values, next: next}

fn arrow_component(text, index) {
  let open = slice(text, index, index + 1) == "o";
  let modifier_index = index + (if (open) 1 else 0);
  let modifier = slice(text, modifier_index, modifier_index + 1);
  let side = if (modifier == "l" or modifier == "r") modifier else "";
  let shape_index = modifier_index + (if (side != "") 1 else 0);
  let shape = arrow_shape(text, shape_index);
  if (shape == null) parse_result(false, [])
  else parse_result(true, [component_name(shape, open, side)], shape_index + len(shape))
}

fn arrow_components(text, index, values) {
  if (index == len(text)) parse_result(true, values)
  else if (len(values) >= 4) parse_result(false, values)
  else {
    let component = arrow_component(text, index);
    if (component.valid != true) parse_result(false, values)
    else arrow_components(text, component.next, [*values, component.values[0]])
  }
}

pub fn canonical(raw) {
  if (raw == null or trim(string(raw)) == "") null
  else {
    let text = lower(trim(string(raw)));
    let parsed = arrow_components(text, 0, []);
    if (parsed.valid) join(parsed.values, " ") else text
  }
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

// Safe Structurizr style lowering shared by validation and view projection.

import graph_style: lambda.package.graph.style

fn opacity_declaration(value) {
  let numeric = graph_style.unsigned_number_text(value, false);
  if (numeric == null or float(numeric) > 100.0) { "" }
  else { "opacity:" ++ string(float(numeric) / 100.0) }
}

fn dash_declaration(value) {
  let style = lower(string(value));
  if (style == "dashed") { "stroke-dasharray:6 4" }
  else if (style == "dotted") { "stroke-dasharray:2 3" }
  else if (style == "solid") { "stroke-dasharray:0" }
  else { "" }
}

pub fn shape(value) {
  let name = lower(string(value));
  if (name == "box") { "box" }
  else if (name == "roundedbox") { "rounded" }
  else if (contains(["circle", "ellipse", "hexagon", "diamond", "cylinder", "person"], name)) {
    name
  }
  else if (name == "bucket") { "cylinder" }
  else if (name == "pipe") { "h-cyl" }
  else if (name == "folder") { "notch-pent" }
  else if (contains(["webbrowser", "window", "component"], name)) { "win-pane" }
  else if (contains(["terminal", "shell"], name)) { "lin-rect" }
  else if (contains(["robot", "mobiledeviceportrait", "mobiledevicelandscape"], name)) {
    "rounded"
  }
  else { null }
}

pub fn route(value) {
  let name = lower(string(value));
  if (name == "direct") { "line" }
  else if (name == "orthogonal") { "orthogonal" }
  else if (name == "curved") { "curved" }
  else { null }
}

pub fn declaration(target, property) {
  let name = lower(string(property.name));
  let value = string(property.value);
  let color = graph_style.safe_color(value);
  let numeric = graph_style.unsigned_number_text(value, true);
  if (name == "background" and target == "node" and color != null) "fill:" ++ color
  else if ((name == "color" or name == "colour") and color != null)
    (if (target == "node") "color:" else "stroke:") ++ color
  else if (name == "stroke" and target == "node" and color != null) "stroke:" ++ color
  else if ((name == "strokewidth" or name == "thickness") and numeric != null)
    "stroke-width:" ++ numeric
  else if (name == "fontsize" and target == "node" and numeric != null)
    "font-size:" ++ numeric
  else if ((name == "width" or name == "height") and target == "node" and numeric != null)
    name ++ ":" ++ numeric
  else if (name == "opacity") opacity_declaration(value)
  else if ((name == "border" and target == "node") or
      (name == "style" and target == "edge")) dash_declaration(value)
  else if (name == "dashed" and target == "edge" and lower(value) == "true")
    "stroke-dasharray:6 4"
  else ""
}

pub fn supported(target, property) => declaration(target, property) != "" or
  (target == "node" and lower(string(property.name)) == "shape" and
    shape(property.value) != null) or
  (target == "edge" and lower(string(property.name)) == "routing" and
    route(property.value) != null)

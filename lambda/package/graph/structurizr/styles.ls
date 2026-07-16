// Safe Structurizr style lowering shared by validation and view projection.

import graph_style: lambda.package.graph.style

pub fn declaration(target, property) {
  let name = lower(string(property.name));
  let value = string(property.value);
  let color = graph_style.safe_color(value);
  let numeric = graph_style.unsigned_number_text(value, true);
  let opacity = graph_style.parse("opacity:" ++ value).opacity;
  if (name == "background" and target == "node" and color != null) "fill:" ++ color
  else if (name == "color" and color != null)
    (if (target == "node") "color:" else "stroke:") ++ color
  else if (name == "stroke" and target == "node" and color != null) "stroke:" ++ color
  else if ((name == "strokewidth" or name == "thickness") and numeric != null)
    "stroke-width:" ++ numeric
  else if (name == "fontsize" and target == "node" and numeric != null)
    "font-size:" ++ numeric
  else if ((name == "width" or name == "height") and target == "node" and numeric != null)
    name ++ ":" ++ numeric
  else if (name == "opacity" and opacity != null) "opacity:" ++ string(opacity)
  else if (name == "dashed" and target == "edge" and lower(value) == "true")
    "stroke-dasharray:6 4"
  else ""
}

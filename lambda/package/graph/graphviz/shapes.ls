// Graphviz node-shape aliases lowered to renderer geometry roles.

pub fn role(raw) {
  let shape = lower(string(if (raw == null) "ellipse" else raw));
  if (contains(["box", "rect", "rectangle", "square", "polygon"], shape)) "box"
  else if (shape == "oval") "ellipse"
  else if (shape == "triangle") "tri"
  else if (shape == "invtriangle") "flip-tri"
  else if (shape == "parallelogram") "lean-r"
  else if (shape == "trapezium") "trapezoid"
  else if (shape == "invtrapezium") "trapezoid-alt"
  else if (shape == "plaintext" or shape == "plain" or shape == "none") "text"
  else if (shape == "record") "box"
  else if (shape == "mrecord") "rounded"
  else shape
}

pub fn family(raw) {
  let shape = role(raw);
  if (contains(["circle", "doublecircle", "ellipse"], shape)) "ellipse"
  else if (contains(["diamond", "tri", "flip-tri", "hexagon", "octagon",
      "trapezoid", "trapezoid-alt", "lean-r", "lean-l", "house", "invhouse"], shape))
    "polygon"
  else if (shape == "text") "text"
  else "box"
}

pub fn source_name(raw) => lower(string(if (raw == null) "ellipse" else raw))

// Graphviz node-shape aliases lowered to renderer geometry roles.

pub fn role(raw) {
  let shape = lower(string(if (raw == null) "ellipse" else raw));
  if (contains(["box", "rect", "rectangle", "square", "msquare"], shape)) "box"
  else if (contains(["oval", "egg"], shape)) "ellipse"
  else if (shape == "point") "circle"
  else if (shape == "triangle") "tri"
  else if (shape == "invtriangle") "flip-tri"
  else if (shape == "parallelogram") "lean-r"
  else if (shape == "trapezium") "trapezoid"
  else if (shape == "invtrapezium") "trapezoid-alt"
  else if (shape == "doubleoctagon" or shape == "tripleoctagon") "octagon"
  else if (shape == "mdiamond") "diamond"
  else if (shape == "mcircle") "circle"
  else if (shape == "note") "doc"
  else if (shape == "tab") "tag-rect"
  else if (shape == "folder") "notch-pent"
  else if (shape == "box3d") "subroutine"
  else if (shape == "component") "win-pane"
  else if (shape == "underline") "underline"
  else if (shape == "promoter" or shape == "rpromoter") "asymmetric"
  else if (shape == "lpromoter") "asymmetric-left"
  else if (shape == "cds" or shape == "rarrow") "tag-rect"
  else if (shape == "larrow") "tag-rect-left"
  else if (shape == "terminator") "lin-rect"
  else if (shape == "utr") "box"
  else if (shape == "primersite") "lean-r"
  else if (contains(["restrictionsite", "noverhang", "proteasesite"], shape)) "notch-rect"
  else if (shape == "fivepoverhang") "notch-rect"
  else if (shape == "threepoverhang") "notch-rect-right"
  else if (shape == "assembly") "hourglass"
  else if (shape == "signature") "doc"
  else if (shape == "insulator") "doublecircle"
  else if (shape == "ribosite") "circle"
  else if (shape == "rnastab" or shape == "proteinstab") "stadium"
  else if (shape == "plaintext" or shape == "plain" or shape == "none") "text"
  else if (shape == "record") "box"
  else if (shape == "mrecord") "rounded"
  else shape
}

pub fn family(raw) {
  let shape = role(raw);
  if (contains(["circle", "doublecircle", "ellipse"], shape)) "ellipse"
  else if (contains(["diamond", "tri", "flip-tri", "hexagon", "octagon",
      "trapezoid", "trapezoid-alt", "lean-r", "lean-l", "house", "invhouse",
      "polygon", "pentagon", "septagon", "star", "asymmetric",
      "asymmetric-left", "tag-rect", "tag-rect-left", "notch-rect",
      "notch-rect-right", "hourglass"], shape))
    "polygon"
  else if (shape == "text") "text"
  else "box"
}

pub fn source_name(raw) => lower(string(if (raw == null) "ellipse" else raw))

pub fn default_sides(raw) {
  let shape = source_name(raw);
  if (shape == "polygon") 4
  else if (shape == "pentagon") 5
  else if (shape == "septagon") 7
  else null
}

pub fn default_peripheries(raw) {
  let shape = source_name(raw);
  if (shape == "doublecircle" or shape == "doubleoctagon") 2
  else if (shape == "tripleoctagon") 3
  else null
}

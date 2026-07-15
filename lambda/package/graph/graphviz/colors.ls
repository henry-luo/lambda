// Graphviz color schemes and weighted color-list lowering.

import graph_style: lambda.package.graph.style

let PALETTES = {
  accent8: ["#7fc97f", "#beaed4", "#fdc086", "#ffff99", "#386cb0", "#f0027f", "#bf5b17", "#666666"],
  dark28: ["#1b9e77", "#d95f02", "#7570b3", "#e7298a", "#66a61e", "#e6ab02", "#a6761d", "#666666"],
  paired12: ["#a6cee3", "#1f78b4", "#b2df8a", "#33a02c", "#fb9a99", "#e31a1c", "#fdbf6f", "#ff7f00", "#cab2d6", "#6a3d9a", "#ffff99", "#b15928"],
  set19: ["#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00", "#ffff33", "#a65628", "#f781bf", "#999999"],
  set28: ["#66c2a5", "#fc8d62", "#8da0cb", "#e78ac3", "#a6d854", "#ffd92f", "#e5c494", "#b3b3b3"],
  set312: ["#8dd3c7", "#ffffb3", "#bebada", "#fb8072", "#80b1d3", "#fdb462", "#b3de69", "#fccde5", "#d9d9d9", "#bc80bd", "#ccebc5", "#ffed6f"]
}

fn digits(text, i) {
  if (i >= len(text)) len(text) > 0
  else if (contains("0123456789", slice(text, i, i + 1))) digits(text, i + 1)
  else false
}

fn palette(name) {
  let found = PALETTES[lower(string(name))];
  if (found is error) null else found
}

pub fn supported_scheme(name) {
  let value = lower(trim(string(if (name != null) name else "")));
  value == "" or value == "svg" or value == "x11" or palette(value) != null
}

fn indexed_color(scheme, raw) {
  let colors = palette(scheme);
  let text = trim(string(raw));
  if (colors == null or not digits(text, 0)) null
  else {
    let index = int(text) - 1;
    if (index >= 0 and index < len(colors)) colors[index] else null
  }
}

fn resolve_color(raw, scheme) {
  let text = trim(string(raw));
  let qualified = if (starts_with(text, "/")) split(text, "/") else [];
  if (len(qualified) == 3)
    if (lower(qualified[1]) == "svg" or lower(qualified[1]) == "x11")
      graph_style.safe_color(qualified[2])
    else indexed_color(qualified[1], qualified[2])
  else {
    let indexed = indexed_color(scheme, text);
    if (indexed != null) indexed else graph_style.safe_color(text)
  }
}

fn weight_text(raw) {
  let text = trim(string(raw));
  let numeric = graph_style.unsigned_number_text(text, false);
  if (numeric == null) null
  else {
    let value = float(numeric);
    if (value >= 0.0 and value <= 1.0) numeric else null
  }
}

fn resolve_stop(raw, scheme) {
  let parts = split(string(raw), ";");
  let color = resolve_color(parts[0], scheme);
  let weight = if (len(parts) == 2) weight_text(parts[1]) else null;
  if (color == null or len(parts) > 2 or (len(parts) == 2 and weight == null)) null
  else color ++ (if (weight != null) ";" ++ weight else "")
}

pub fn resolve(raw, scheme = null) {
  if (raw == null) null
  else {
    let stops = [for (entry in split(string(raw), ":")) resolve_stop(entry, scheme)];
    if (len(stops) == 0 or len([for (entry in stops where entry == null) entry]) > 0) null
    else join(stops, ":")
  }
}

fn stop(raw) {
  let parts = split(string(raw), ";");
  {color: graph_style.safe_color(parts[0]),
    weight: if (len(parts) == 2) float(parts[1]) else null}
}

fn normalized_weights(stops) {
  let explicit = sum([for (entry in stops where entry.weight != null) entry.weight]);
  let missing = len([for (entry in stops where entry.weight == null) entry]);
  let share = if (missing > 0) max([0.0, 1.0 - explicit]) / missing else 0.0;
  [for (i, entry in stops) if (entry.weight != null)
      entry.weight + (if (missing == 0 and i == len(stops) - 1)
        max([0.0, 1.0 - explicit]) else 0.0)
    else if (missing > 0) share
    else if (i == len(stops) - 1) max([0.0, 1.0 - explicit]) else 0.0]
}

fn percent(value) => string(value * 100.0) ++ "%"

fn hard_stops(stops, weights) => [
  for (i, entry in stops,
    let start = sum(slice(weights, 0, i)),
    let end = start + weights[i])
    entry.color ++ " " ++ percent(start) ++ " " ++ percent(end)
]

pub fn background(raw, style, angle, fallback) {
  let stops = if (raw == null) [] else [for (entry in split(string(raw), ":")) stop(entry)];
  let invalid = len([for (entry in stops where entry.color == null) entry]) > 0;
  let kind = lower(string(if (style != null) style else ""));
  if (len(stops) < 2 or invalid) fallback
  else if (contains(kind, "striped"))
    "linear-gradient(" ++ string(angle) ++ "deg," ++
      join(hard_stops(stops, normalized_weights(stops)), ",") ++ ")"
  else if (contains(kind, "wedged"))
    "conic-gradient(" ++ join(hard_stops(stops, normalized_weights(stops)), ",") ++ ")"
  else {
    let colors = join([for (entry in stops) entry.color], ",");
    if (contains(kind, "radial")) "radial-gradient(" ++ colors ++ ")"
    else "linear-gradient(" ++ string(angle) ++ "deg," ++ colors ++ ")"
  }
}

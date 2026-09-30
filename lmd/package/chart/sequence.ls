// Mermaid sequence timeline rendering for the chart-owned diagram family.

import model: lambda.graph.model

fn canonical_chart(source) {
  // The input parser uses a source graph envelope; chart rendering consumes
  // its ordered statements without running graph ranking or edge routing.
  let attrs = {*:map(source), 'ir-stage': "canonical"};
  <chart *:attrs,
    for (child in model.child_items(source)) child
  >
}

fn metrics() => {
  first_x: 86, column_gap: 183, box_width: 137, box_height: 72,
  top_y: 27, text_color: "#29263e", line_color: "#29263e",
  colors: [
    {fill: "#fff7ff", stroke: "#ec64ff"},
    {fill: "#f1fffc", stroke: "#15d8c0"},
    {fill: "#fff8f1", stroke: "#ff8b2b"}
  ]
}

fn sequence_font() => "Trebuchet MS, Verdana, Arial, sans-serif"

fn participant_x(nodes, id, style) {
  let matches = [for (i, node in nodes where string(node.id) == string(id)) i];
  style.first_x + (if (len(matches) > 0) matches[0] else 0) * style.column_gap
}

fn note_lines(value) => split(replace(replace(string(value), "<br/>", "\n"),
  "<br>", "\n"), "\n")

fn note_height(annotation) => max([56, 20 + 18 * len(note_lines(annotation.label))])

fn place_events_at(events, index, cursor, previous_message, positions) {
  if (index >= len(events)) {items: positions, cursor: cursor}
  else {
    let event = events[index];
    let tag = model.tag(event);
    let block_start = tag == "sequence-block" and event.phase == "start";
    let block_end = tag == "sequence-block" and event.phase == "end";
    let self_message = tag == "edge" and event.from == event.to;
    let y = if (block_start and previous_message != null) previous_message + 8
      else if (block_end) max([cursor,
        if (previous_message != null) previous_message + 54 else cursor])
      else cursor;
    let next_cursor = if (block_start) max([cursor, y + 88])
      else if (block_end) y + 8
      else if (tag == "annotation") y + note_height(event) + 45
      else if (tag == "edge") y + (if (self_message) 50 else 46)
      else cursor;
    place_events_at(events, index + 1, next_cursor,
      if (tag == "edge") y else previous_message,
      [*positions, {value: event, y: y}])
  }
}

fn loop_end(events, index, depth) {
  if (index >= len(events)) len(events) - 1
  else {
    let event = events[index];
    if (model.tag(event) == "sequence-block" and event.phase == "start")
      loop_end(events, index + 1, depth + 1)
    else if (model.tag(event) == "sequence-block" and event.phase == "end")
      (if (depth == 1) index else loop_end(events, index + 1, depth - 1))
    else loop_end(events, index + 1, depth)
  }
}

fn participant_box(node, index, x, y, style) {
  let color = style.colors[index % len(style.colors)];
  let label = string(model.label_source(node, node.id));
  [
    <rect x: x - style.box_width / 2 + 2, y: y + 3,
      width: style.box_width, height: style.box_height, rx: 6,
      fill: "#d8d8d8", opacity: 0.55>,
    <rect x: x - style.box_width / 2, y: y,
      width: style.box_width, height: style.box_height, rx: 6,
      fill: color.fill, stroke: color.stroke, 'stroke-width': 1.7>,
    <text x: x, y: y + 41, 'text-anchor': "middle",
      'font-family': sequence_font(), 'font-size': 15,
      fill: style.text_color, label>
  ]
}

fn participant(node, index, nodes, bottom_y, style) {
  let x = participant_x(nodes, node.id, style);
  [
    <line x1: x, y1: style.top_y + style.box_height, x2: x, y2: bottom_y,
      stroke: style.line_color, 'stroke-width': 1.5>,
    *participant_box(node, index, x, style.top_y, style),
    *participant_box(node, index, x, bottom_y, style)
  ]
}

fn message(edge, nodes, y, style) {
  let x1 = participant_x(nodes, edge.from, style);
  let x2 = participant_x(nodes, edge.to, style);
  let dashed = string(edge.relation) == "-->>" or string(edge.relation) == "-->";
  let dash = if (dashed) "3 3" else null;
  let label = string(model.label_source(edge, ""));
  if (x1 == x2) [
    <path d: "M " ++ string(x1) ++ " " ++ string(y - 11) ++
      " C " ++ string(x1 + 58) ++ " " ++ string(y - 20) ++
      " " ++ string(x1 + 58) ++ " " ++ string(y + 20) ++
      " " ++ string(x1) ++ " " ++ string(y + 11),
      fill: "none", stroke: style.line_color, 'stroke-width': 1.5,
      'stroke-dasharray': dash>,
    <path d: "M " ++ string(x1) ++ " " ++ string(y + 11) ++
      " l 9 -4 v 8 z", fill: style.line_color>,
    <text x: max([12, x1 - 99]), y: y - 28,
      'font-family': sequence_font(), 'font-size': 14,
      fill: style.text_color, label>
  ] else {
    let tip = if (x2 > x1) x2 - 4 else x2 + 4;
    let wing = if (x2 > x1) -9 else 9;
    [
      <line x1: x1, y1: y, x2: tip, y2: y,
        stroke: style.line_color, 'stroke-width': 1.5,
        'stroke-dasharray': dash>,
      <path d: "M " ++ string(tip) ++ " " ++ string(y) ++
        " l " ++ string(wing) ++ " -5 v 10 z", fill: style.line_color>,
      <text x: (x1 + x2) / 2, y: y - 11, 'text-anchor': "middle",
        'font-family': sequence_font(), 'font-size': 14,
        fill: style.text_color, label>
    ]
  }
}

fn note_width(annotation) {
  let lengths = [for (line in note_lines(annotation.label)) len(line)];
  max([141, 8 + (if (len(lengths) > 0) max(lengths) else 0) * 7.5])
}

fn note(annotation, nodes, y, style) {
  let owner_x = participant_x(nodes, annotation["owner-id"], style);
  let side = string(annotation.kind);
  let width = note_width(annotation);
  let x = if (side == "note-left") owner_x - width - 22 else owner_x + 22;
  let lines = note_lines(annotation.label);
  let height = note_height(annotation);
  [
    <rect x: x, y: y, width: width, height: height,
      fill: "#fff9da", stroke: "#ffd21a", 'stroke-width': 1.1>,
    for (i, line in lines)
      <text x: x + width / 2, y: y + 23 + i * 18,
        'text-anchor': "middle", 'font-family': sequence_font(),
        'font-size': 14, fill: style.text_color, line>
  ]
}

fn loop_box(positions, index, nodes, style) {
  let events = [for (entry in positions) entry.value];
  let end = loop_end(events, index + 1, 1);
  let members = [for (j, entry in positions,
    id in if (model.tag(entry.value) == "edge")
      [entry.value.from, entry.value.to]
      else if (model.tag(entry.value) == "annotation")
        [entry.value["owner-id"]] else []
    where j > index and j < end) participant_x(nodes, id, style)];
  let centers = if (len(members) > 0) members else [style.first_x];
  let left = max([14, min(centers) - 104]);
  let right = max(centers) + 105;
  let y = positions[index].y;
  let height = positions[end].y - y + 1;
  [
    <rect x: left, y: y, width: right - left, height: height,
      fill: "none", stroke: style.line_color, 'stroke-width': 1.4,
      'stroke-dasharray': "2 2">,
    <rect x: left, y: y + 1, width: 46, height: 27,
      fill: "#fff", stroke: style.line_color, 'stroke-width': 1.2>,
    <text x: left + 7, y: y + 20, 'font-family': sequence_font(),
      'font-size': 14, fill: style.text_color, "loop">,
    <text x: left + 76, y: y + 18, 'font-family': sequence_font(),
      'font-size': 14, fill: style.text_color,
      "[" ++ string(positions[index].value.label) ++ "]">
  ]
}

pub fn to_html(graph, opts = null) {
  let chart = canonical_chart(graph);
  let nodes = model.nodes(chart);
  // Events retain source order even when participants are declared among messages.
  let events = [for (child in model.element_children(chart)
    where contains(["edge", "annotation", "sequence-block"], model.tag(child))) child];
  let base_style = metrics();
  let left_notes = [for (event in events
    where model.tag(event) == "annotation" and event.kind == "note-left")
    participant_x(nodes, event["owner-id"], base_style) - note_width(event) - 22];
  let shift = if (len(left_notes) > 0) max([0, 14 - min(left_notes)]) else 0;
  let style = {*:base_style, first_x: base_style.first_x + shift};
  let right_notes = [for (event in events
    where model.tag(event) == "annotation" and event.kind == "note-right")
    participant_x(nodes, event["owner-id"], style) + note_width(event) + 47];
  let width = max([320, style.first_x + (len(nodes) - 1) * style.column_gap + 188,
    *right_notes]);
  let placed = place_events_at(events, 0, 143, null, []);
  let positions = placed.items;
  let final_event = if (len(positions) > 0) positions[len(positions) - 1] else null;
  let bottom_y = if (final_event == null) 150
    else if (model.tag(final_event.value) == "edge") final_event.y + 18
    else placed.cursor + 20;
  let height = bottom_y + 88;
  <div class: "lambda-sequence", role: "img",
      'aria-label': model.title(chart),
      style: "display:block;width:" ++ string(width) ++ "px;min-height:" ++
        string(height) ++ "px;background:#fff;",
    <svg xmlns: "http://www.w3.org/2000/svg", width: width, height: height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
      for (i, entry in positions,
        item in if (model.tag(entry.value) == "sequence-block" and
          entry.value.phase == "start") loop_box(positions, i, nodes, style)
          else []) item
      for (i, node in nodes,
        item in participant(node, i, nodes, bottom_y, style)) item
      for (entry in positions,
        item in if (model.tag(entry.value) == "edge")
          message(entry.value, nodes, entry.y, style)
          else if (model.tag(entry.value) == "annotation")
            note(entry.value, nodes, entry.y, style)
          else []) item
    >
  >
}

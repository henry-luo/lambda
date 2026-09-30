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

fn metrics(nodes) {
  // Participant boxes shrink with a wider cast while retaining readable gaps.
  let count = max([1, len(nodes)]);
  let box_width = max([90, min([137, 450 / count])]);
  {first_x: box_width / 2 + 20,
  column_gap: max([box_width + 25, min([183, 675 / count])]),
  box_width: box_width, box_height: max([45, min([72, 225 / count])]),
  top_y: max([14, min([27, 75 / count])]),
  font_size: max([10, min([14, 50 / count])]),
  text_color: "#29263e", line_color: "#29263e",
  colors: [
    {fill: "#fff7ff", stroke: "#ec64ff"},
    {fill: "#f1fffc", stroke: "#15d8c0"},
    {fill: "#fff8f1", stroke: "#ff8b2b"},
    {fill: "#f0fdff", stroke: "#16c9e7"},
    {fill: "#f2fff5", stroke: "#65df96"}
  ]
  }
}

fn sequence_font() => "Trebuchet MS, Verdana, Arial, sans-serif"

fn participant_x(nodes, id, style) {
  let matches = [for (i, node in nodes where string(node.id) == string(id)) i];
  style.first_x + (if (len(matches) > 0) matches[0] else 0) * style.column_gap
}

fn note_lines(value) => split(replace(replace(string(value), "<br/>", "\n"),
  "<br>", "\n"), "\n")

fn note_height(annotation) => if (annotation.kind == "note-over")
  max([24, 6 + 18 * len(note_lines(annotation.label))])
  else max([56, 20 + 18 * len(note_lines(annotation.label))])

fn place_events_at(events, index, cursor, previous_message, positions, step) {
  if (index >= len(events)) {items: positions, cursor: cursor}
  else {
    let event = events[index];
    let tag = model.tag(event);
    let block_start = tag == "sequence-block" and event.phase == "start";
    let block_end = tag == "sequence-block" and event.phase == "end";
    let block_branch = tag == "sequence-block" and event.phase == "branch";
    let after_end = len(positions) > 0 and
      model.tag(positions[len(positions) - 1].value) == "sequence-block" and
      positions[len(positions) - 1].value.phase == "end";
    let over_note = tag == "annotation" and event.kind == "note-over";
    let self_message = tag == "edge" and event.from == event.to;
    let y = if (block_start and previous_message != null) previous_message + 8
      else if (block_end and event.kind != "loop")
        if (after_end) cursor
        else if (previous_message != null) previous_message + 6 else cursor
      else if (block_end) max([cursor,
        if (previous_message != null) previous_message + step else cursor])
      else if (block_branch and previous_message != null) previous_message + 9
      else if (over_note and previous_message != null) previous_message + 7
      else cursor;
    let next_cursor = if (block_start) max([cursor, y + 2 * step])
      else if (block_end) y + (if (event.kind == "loop") 8 else 6)
      else if (block_branch) max([cursor, y + step + 18])
      else if (over_note) y + note_height(event) + 28
      else if (tag == "annotation") y + note_height(event) + step
      else if (tag == "edge") y + (if (self_message) step + 4 else step)
      else cursor;
    place_events_at(events, index + 1, next_cursor,
      if (tag == "edge") y else previous_message,
      [*positions, {value: event, y: y}], step)
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
    <text x: x, y: y + style.box_height / 2 + 5, 'text-anchor': "middle",
      'font-family': sequence_font(), 'font-size': style.font_size + 1,
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
  let dashed = contains(["-->>", "-->", "--)"], string(edge.relation));
  let open_arrow = contains(["-)", "--)"], string(edge.relation));
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
      'font-family': sequence_font(), 'font-size': style.font_size,
      fill: style.text_color, label>
  ] else {
    let tip = if (x2 > x1) x2 - 4 else x2 + 4;
    let wing = if (x2 > x1) -9 else 9;
    [
      <line x1: x1, y1: y, x2: tip, y2: y,
        stroke: style.line_color, 'stroke-width': 1.5,
        'stroke-dasharray': dash>,
      <path d: "M " ++ string(tip + wing) ++ " " ++ string(y - 5) ++
        " L " ++ string(tip) ++ " " ++ string(y) ++
        " L " ++ string(tip + wing) ++ " " ++ string(y + 5) ++
        (if (open_arrow) "" else " z"),
        fill: if (open_arrow) "none" else style.line_color,
        stroke: if (open_arrow) style.line_color else null,
        'stroke-width': if (open_arrow) 1.2 else null>,
      <text x: (x1 + x2) / 2, y: y - 11, 'text-anchor': "middle",
        'font-family': sequence_font(), 'font-size': style.font_size,
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
  let over = side == "note-over";
  let to_id = model.optional(annotation, "to-id");
  let to_x = if (over) participant_x(nodes,
    if (to_id != null) to_id else annotation["owner-id"], style)
    else owner_x;
  let width = note_width(annotation);
  let x = if (over) min([owner_x, to_x]) - 15
    else if (side == "note-left") owner_x - width - 22 else owner_x + 22;
  let actual_width = if (over) max([width, abs(to_x - owner_x) + 30]) else width;
  let lines = note_lines(annotation.label);
  let height = note_height(annotation);
  [
    <rect x: x, y: y, width: actual_width, height: height,
      fill: if (over) "#fff2a0" else "#fff9da",
      stroke: "#ffd21a", 'stroke-width': 1.1>,
    for (i, line in lines)
      <text x: x + actual_width / 2,
        y: y + (if (over) 16 + i * 18 else 23 + i * 18),
        'text-anchor': "middle", 'font-family': sequence_font(),
        'font-size': style.font_size, fill: style.text_color, line>
  ]
}

fn block_depth(positions, index) {
  let preceding = [for (j, entry in positions where j < index and
    model.tag(entry.value) == "sequence-block") entry.value.phase];
  len([for (phase in preceding where phase == "start") phase]) -
    len([for (phase in preceding where phase == "end") phase])
}

fn block_box(positions, index, nodes, style) {
  let events = [for (entry in positions) entry.value];
  let end = loop_end(events, index + 1, 1);
  let kind = string(positions[index].value.kind);
  let members = [for (j, entry in positions,
    id in if (model.tag(entry.value) == "edge")
      [entry.value.from, entry.value.to]
      else if (model.tag(entry.value) == "annotation")
        [entry.value["owner-id"],
          if (model.optional(entry.value, "to-id") != null)
            entry.value["to-id"] else entry.value["owner-id"]]
      else []
    where j > index and j < end) participant_x(nodes, id, style)];
  let centers = if (len(members) > 0) members else [style.first_x];
  let margin = if (kind == "loop") 104 else 20;
  let inset = if (kind == "loop") 0 else block_depth(positions, index) * 13;
  let left = max([14, min(centers) - margin + inset]);
  let right = max(centers) + margin - inset;
  let y = positions[index].y;
  let height = positions[end].y - y + 1;
  let branches = [for (j, entry in positions where j > index and j < end and
    model.tag(entry.value) == "sequence-block" and
    entry.value.phase == "branch" and
    block_depth(positions, j) == block_depth(positions, index) + 1) entry];
  [
    <rect x: left, y: y, width: right - left, height: height,
      fill: "none", stroke: style.line_color, 'stroke-width': 1.4,
      'stroke-dasharray': "2 2">,
    <rect x: left, y: y + 1, width: 46, height: 21,
      fill: "#fff", stroke: style.line_color, 'stroke-width': 1.2>,
    <text x: left + 7, y: y + 16, 'font-family': sequence_font(),
      'font-size': style.font_size, fill: style.text_color, kind>,
    <text x: if (kind == "loop") left + 76 else (left + right) / 2,
      y: y + 15, 'font-family': sequence_font(),
      'font-size': style.font_size, fill: style.text_color,
      "[" ++ string(positions[index].value.label) ++ "]">,
    for (branch in branches, item in [
        <line x1: left, y1: branch.y, x2: right, y2: branch.y,
          stroke: style.line_color, 'stroke-width': 1.2,
          'stroke-dasharray': "2 2">,
        <text x: (left + right) / 2, y: branch.y + 14,
          'font-family': sequence_font(), 'font-size': style.font_size,
          fill: style.text_color,
          "[" ++ string(branch.value.label) ++ "]">
      ]) item
  ]
}

fn activation_end(positions, index, actor, depth) {
  // match a sender's close event after any nested activation of that actor.
  if (index >= len(positions)) null
  else {
    let edge = positions[index].value;
    let activation = if (model.tag(edge) == "edge")
      model.optional(edge, "activation") else null;
    let opens = activation == "start" and edge.to == actor;
    let closes = activation == "end" and edge.from == actor;
    if (closes and depth == 1) positions[index].y
    else activation_end(positions, index + 1, actor,
      depth + (if (opens) 1 else if (closes) -1 else 0))
  }
}

fn activation_bar(positions, index, nodes, bottom_y, style) {
  let edge = positions[index].value;
  let actor = edge.to;
  let x = participant_x(nodes, actor, style);
  let found = activation_end(positions, index + 1, actor, 1);
  let finish = if (found != null) found else bottom_y;
  <rect x: x - 3, y: positions[index].y, width: 6,
    height: max([0, finish - positions[index].y]),
    fill: "#fff", stroke: style.line_color, 'stroke-width': 0.8>
}

pub fn to_html(graph, opts = null) {
  let chart = canonical_chart(graph);
  let nodes = model.nodes(chart);
  // Events retain source order even when participants are declared among messages.
  let events = [for (child in model.element_children(chart)
    where contains(["edge", "annotation", "sequence-block"], model.tag(child))) child];
  let base_style = metrics(nodes);
  let left_notes = [for (event in events
    where model.tag(event) == "annotation" and event.kind == "note-left")
    participant_x(nodes, event["owner-id"], base_style) - note_width(event) - 22];
  let shift = if (len(left_notes) > 0) max([0, 14 - min(left_notes)]) else 0;
  let style = {*:base_style, first_x: base_style.first_x + shift};
  let right_notes = [for (event in events
    where model.tag(event) == "annotation" and event.kind == "note-right")
    participant_x(nodes, event["owner-id"], style) + note_width(event) + 47];
  let width = max([640, style.first_x + (len(nodes) - 1) * style.column_gap +
    style.box_width / 2 + 33,
    *right_notes]);
  let step = min([46, max([27, 360 / max([1, len(events)])])]);
  let first_over = len(events) > 0 and model.tag(events[0]) == "annotation" and
    events[0].kind == "note-over";
  let first_y = style.top_y + style.box_height + (if (first_over) 6 else 44);
  let placed = place_events_at(events, 0, first_y, null, [], step);
  let positions = placed.items;
  let final_event = if (len(positions) > 0) positions[len(positions) - 1] else null;
  let bottom_y = if (final_event == null) 150
    else if (model.tag(final_event.value) == "edge") final_event.y + 18
    else if (model.tag(final_event.value) == "sequence-block" and
      final_event.value.phase == "end") placed.cursor + 5
    else placed.cursor + 20;
  let height = bottom_y + style.box_height + 20;
  <div class: "lambda-sequence", role: "img",
      'aria-label': model.title(chart),
      style: "display:block;width:" ++ string(width) ++ "px;min-height:" ++
        string(height) ++ "px;background:#fff;",
    <svg xmlns: "http://www.w3.org/2000/svg", width: width, height: height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
      for (i, entry in positions,
        item in if (model.tag(entry.value) == "sequence-block" and
          entry.value.phase == "start") block_box(positions, i, nodes, style)
          else []) item
      for (i, node in nodes,
        item in participant(node, i, nodes, bottom_y, style)) item
      for (i, entry in positions,
        item in if (model.tag(entry.value) == "edge" and
          model.optional(entry.value, "activation") == "start")
          [activation_bar(positions, i, nodes, bottom_y, style)]
          else []) item
      for (entry in positions,
        item in if (model.tag(entry.value) == "edge")
          message(entry.value, nodes, entry.y, style)
          else if (model.tag(entry.value) == "annotation")
            note(entry.value, nodes, entry.y, style)
          else []) item
    >
  >
}

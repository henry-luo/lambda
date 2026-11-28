import layout: lambda.graph.layout
import model: lambda.graph.model
import transform: lambda.graph.transform

fn by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

fn placement_by_index(placements, index) {
  let matches = [for (placement in placements where placement.index == index) placement];
  if (len(matches) > 0) matches[0] else null
}

fn separated(a, a_width, a_height, b, b_width, b_height) =>
  a.x + a_width <= b.x or b.x + b_width <= a.x or
    a.y + a_height <= b.y or b.y + b_height <= a.y

let grouped = layout.compute({
  nodes: [
    {id: "inside-a", width: 40, height: 30, group: "cluster"},
    {id: "outside", width: 40, height: 30},
    {id: "inside-b", width: 40, height: 30, group: "cluster"}
  ],
  edges: [],
  clusters: [{id: "cluster", padding: 16}],
  direction: "TB"
})

let routed = layout.compute({
  nodes: [
    {id: "a", width: 40, height: 30},
    {id: "blocker", width: 80, height: 50},
    {id: "c", width: 40, height: 30}
  ],
  edges: [
    {id: "ab", from: "a", to: "blocker"},
    {id: "ac", from: "a", to: "c", min_length: 2}
  ],
  direction: "TB"
})
let ac = by_id(routed.edges, "ac")

// A bypass must use the endpoints' sides without changing explicit compass ports.
fn bypass_check(direction, explicit_ports = false, wide_endpoints = false) any^ {
  let result = layout.compute({
    nodes: [{id:"a",width:if (wide_endpoints) 220 else 140,height:if (wide_endpoints) 180 else 60},
      {id:"b",width:150,height:80}, {id:"c",width:100,height:60}],
    edges: [{id:"ab",from:"a",to:"b"},
      {id:"ac",from:"a",to:"c",from_compass:if (explicit_ports) "s" else null,
        to_compass:if (explicit_ports) "n" else null}, {id:"bc",from:"b",to:"c"}],
    direction:direction
  });
  let edge = by_id(result.edges, "ac");
  let from = by_id(result.nodes, "a");
  let to = by_id(result.nodes, "c");
  let first = edge.points[0];
  let end_point = edge.points[len(edge.points)-1];
  let vertical = direction in ["TB", "BT"];
  len(edge.points) == 4 and (if (explicit_ports)
    first.y == from.y + from.height / 2 and end_point.y == to.y - to.height / 2
    else if (vertical)
      abs(first.y - from.y) < from.height / 2 and abs(end_point.y - to.y) < to.height / 2 and
        abs(first.x - from.x) == from.width / 2 and abs(end_point.x - to.x) == to.width / 2
    else abs(first.x - from.x) < from.width / 2 and abs(end_point.x - to.x) < to.width / 2 and
      abs(first.y - from.y) == from.height / 2 and abs(end_point.y - to.y) == to.height / 2)
}

// Return lanes should stay on the source branch's clear side in either orientation.
fn feedback_check(direction, return_from_c) any^ {
  let from_id = if (return_from_c) "c" else "d";
  let other_id = if (return_from_c) "d" else "c";
  let result = layout.compute({
    nodes: [{id:"a",width:60,height:40}, {id:"b",width:152,height:58,shape:"diamond"},
      {id:"c",width:70,height:40}, {id:"d",width:72,height:40}, {id:"e",width:56,height:40}],
    edges: [{id:"ab",from:"a",to:"b"}, {id:"bc",from:"b",to:"c"},
      {id:"bd",from:"b",to:"d"}, {id:"return",from:from_id,to:"a"},
      {id:"end",from:other_id,to:"e"}], direction:direction
  });
  let edge = by_id(result.edges, "return");
  let from = by_id(result.nodes, from_id);
  let other = by_id(result.nodes, other_id);
  let vertical = direction in ["TB", "BT"];
  let cross = if (vertical) from.x else from.y;
  let other_cross = if (vertical) other.x else other.y;
  let lane = if (vertical) edge.points[1].x else edge.points[1].y;
  let source_edge = if (vertical) edge.points[0].x else edge.points[0].y;
  len(edge.points) == 4 and (if (cross > other_cross) lane > source_edge and source_edge > cross
    else lane < source_edge and source_edge < cross)
}

// Curved return lanes must leave and enter the node with diagonal tangents.
fn curved_feedback_check(direction, reversed) any^ {
  let result = layout.compute({nodes:[{id:"a",width:80,height:80}, {id:"b",width:80,height:80}],
    edges:[{id:"forward",from:"a",to:"b"},
      {id:"back0",from:"b",to:"a"}, {id:"back1",from:"b",to:"a"}],
    direction:direction, route_mode:"curved"});
  let edge = by_id(result.edges, if (reversed) "back1" else "back0");
  let points = edge.points;
  len(points) == 4 and abs(points[1].x - points[0].x) > 0.001 and
    abs(points[1].y - points[0].y) > 0.001 and
    abs(points[3].x - points[2].x) > 0.001 and abs(points[3].y - points[2].y) > 0.001
}

// Fractional measurements previously let an equal-length detour replace a clear direct edge.
fn clear_fractional_edge() any^ {
  let result = layout.compute({direction:"LR",route_mode:"curved",
    node_sep:80.55,rank_sep:107.4,edge_sep:13.425,
    nodes:[{id:"Client",width:137.51132202148438,height:61.7550048828125},
      {id:"API",width:158.48284912109375,height:61.7550048828125},
      {id:"Auth",width:158.7044677734375,height:61.7550048828125},
      {id:"Data",width:158.6754150390625,height:61.7550048828125},
      {id:"DB",width:129.1820068359375,height:77.864990234375},
      {id:"Cache",width:157.2257080078125,height:77.864990234375}],
    edges:[{id:"client",from:"Client",to:"API"}, {id:"auth",from:"API",to:"Auth"},
      {id:"data",from:"API",to:"Data"}, {id:"db",from:"Data",to:"DB"},
      {id:"cache",from:"Data",to:"Cache"}, {id:"auth-db",from:"Auth",to:"DB"}]});
  let points = by_id(result.edges, "auth").points;
  len(points) == 2 and points[0].x < points[1].x and points[0].y > points[1].y
}

// A clear curved bypass should enter and leave its endpoints diagonally in every orientation.
fn curved_forward_bypass(direction) any^ {
  let result = layout.compute({nodes:[{id:"a",width:140,height:60},
    {id:"b",width:150,height:80}, {id:"c",width:100,height:60}],
    edges:[{id:"ab",from:"a",to:"b"}, {id:"ac",from:"a",to:"c"},
      {id:"bc",from:"b",to:"c"}], direction:direction,route_mode:"curved",rank_sep:200});
  let points = by_id(result.edges, "ac").points;
  len(points) == 4 and abs(points[1].x - points[0].x) > 0.001 and
    abs(points[1].y - points[0].y) > 0.001 and
    abs(points[3].x - points[2].x) > 0.001 and abs(points[3].y - points[2].y) > 0.001
}

// Cluster endpoints constrain every descendant and attach at the visible boundary.
fn cluster_endpoint_check(direction) any^ {
  let result = layout.compute({nodes:[
    {id:"p1",width:40,height:40}, {id:"p2",width:40,height:40,group:"g"},
    {id:"p25",width:40,height:40,group:"nested"}, {id:"p3",width:40,height:40,group:"g"},
    {id:"p4",width:40,height:40}, {id:"p6",width:40,height:40}, {id:"p5",width:40,height:40}],
    clusters:[{id:"g"}, {id:"nested",parent:"g"}],
    edges:[{id:"in",from:"p1",to:"p2",head_cluster:"g"},
      {id:"p2-p4",from:"p2",to:"p4"}, {id:"p3-p6",from:"p3",to:"p6"},
      {id:"out",from:"p3",to:"p5",tail_cluster:"g"}],
    direction:direction, compound:true, route_mode:"curved"});
  let p1 = by_id(result.nodes, "p1");
  let members = [for (id in ["p2", "p25", "p3"]) by_id(result.nodes, id)];
  let targets = [for (id in ["p4", "p6", "p5"]) by_id(result.nodes, id)];
  let cluster = by_id(result.clusters, "g");
  let incoming = by_id(result.edges, "in");
  let outgoing = by_id(result.edges, "out");
  let head = incoming.points[len(incoming.points)-1];
  let tail = outgoing.points[0];
  let vertical = direction in ["TB", "BT"];
  let center = if (vertical) cluster.x + cluster.width / 2.0 else cluster.y + cluster.height / 2.0;
  let p5 = by_id(result.nodes, "p5");
  let p6 = by_id(result.nodes, "p6");
  let p5_cross = if (vertical) p5.x else p5.y;
  let p6_cross = if (vertical) p6.x else p6.y;
  let boundary = if (direction == "TB") abs(head.y - cluster.y) < 0.001 and
      abs(tail.y - cluster.y - cluster.height) < 0.001
    else if (direction == "BT") abs(head.y - cluster.y - cluster.height) < 0.001 and
      abs(tail.y - cluster.y) < 0.001
    else if (direction == "LR") abs(head.x - cluster.x) < 0.001 and
      abs(tail.x - cluster.x - cluster.width) < 0.001
    else abs(head.x - cluster.x - cluster.width) < 0.001 and abs(tail.x - cluster.x) < 0.001;
  boundary and not (false in [for (member in members) p1.rank < member.rank]) and
    not (false in [for (target in targets, member in members) target.rank > member.rank]) and
    abs(p5_cross - center) < abs(p6_cross - center)
}

fn source_cluster_check() any^ {
  let graph = input("test/lambda/graph/upstream/mermaid/subgraph-variation.mmd",
    {type:"graph", flavor:"mermaid"})^;
  let html = transform.to_html(graph)^;
  let children = [for (i, child in model.element_children(html),
    let tag = string(model.tag(child))) {tag:tag, index:i,
      width:if (tag == "node") 40 else if (tag == "cluster-label") 50 else 0,
      height:if (tag == "node") 40 else if (tag == "cluster-label") 20 else 0,
      attrs:map(child)}];
  let result = layout.from_velmts({attrs:map(html)}, children, null);
  let cluster = by_id(result.clusters, "P1.5");
  let p1 = by_id(result.nodes, "P1");
  let p2 = by_id(result.nodes, "P2");
  let p3 = by_id(result.nodes, "P3");
  let p4 = by_id(result.nodes, "P4");
  let p5 = by_id(result.nodes, "P5");
  let p6 = by_id(result.nodes, "P6");
  let incoming = by_id(result.edges, "e0");
  let outgoing = by_id(result.edges, "e3");
  (p1.y + p1.height / 2 < cluster.y and p2.y > cluster.y and p3.y > cluster.y and
    p4.y - p4.height / 2 > cluster.y + cluster.height and
    p5.y - p5.height / 2 > cluster.y + cluster.height and
    p6.y - p6.height / 2 > cluster.y + cluster.height and
    incoming.head_cluster == "P1.5" and outgoing.tail_cluster == "P1.5" and
    abs(incoming.points[len(incoming.points)-1].y - cluster.y) < 0.001 and
    abs(outgoing.points[0].y - cluster.y - cluster.height) < 0.001)
}

// Cycles must not split a compound endpoint's members across the drawing.
fn nested_cluster_cycle_check() any^ {
  let graph = input("test/lambda/graph/upstream/mermaid/nested-sb-edges-in-out.mmd",
    {type:"graph", flavor:"mermaid"})^;
  let html = transform.to_html(graph)^;
  let children = [for (i, child in model.element_children(html), let tag = string(model.tag(child)))
    {tag:tag, index:i, width:if (tag == "node") 60 else if (tag == "cluster-label") 60 else 0,
      height:if (tag == "node") 40 else if (tag == "cluster-label") 20 else 0, attrs:map(child)}];
  let result = layout.from_velmts({attrs:map(html)}, children, null)^;
  let router = by_id(result.nodes, "routeur");
  let nat = by_id(result.nodes, "nat");
  let internet = by_id(result.nodes, "internet");
  let project = by_id(result.clusters, "project");
  let members = [for (id in ["compute1", "lb1", "compute2", "lb2"]) by_id(result.nodes, id)];
  let boundary_edges = [for (id in ["e1", "e2", "e3", "e4"]) by_id(result.edges, id)];
  let boundary_ok = [for (edge in boundary_edges,
    let cluster = by_id(result.clusters, if (edge.head_cluster != null) edge.head_cluster else edge.tail_cluster),
    let point = if (edge.head_cluster != null) edge.points[len(edge.points)-1] else edge.points[0])
    if (edge.head_cluster != null) abs(point.x - cluster.x) < 0.001
    else abs(point.x - cluster.x - cluster.width) < 0.001];
  (internet.x + internet.width / 2 < project.x and router.x < nat.x and
    not (false in [for (node in members) router.x < node.x and node.x < nat.x]) and
    not (false in boundary_ok))
}

// The cluster frame must contain every point of a member's self-loop.
fn cluster_self_loop_check() any^ {
  let graph = input("test/lambda/graph/upstream/mermaid/subgraph-labels-2.mmd",
    {type:"graph", flavor:"mermaid"})^;
  let html = transform.to_html(graph)^;
  let children = [for (i, child in model.element_children(html), let tag = string(model.tag(child)))
    {tag:tag, index:i, width:if (tag == "node") 60 else if (tag == "cluster-label") 50 else 0,
      height:if (tag == "node") 40 else if (tag == "cluster-label") 20 else 0, attrs:map(child)}];
  let result = layout.from_velmts({attrs:map(html)}, children, null)^;
  let cluster = by_id(result.clusters, "one");
  let loops = [for (edge in ((result.edges) ^ { [] }) where edge.from == edge.to) edge];
  len(loops) == 1 and not (false in [for (point in ((loops[0].points) ^ { [] }))
    point.x >= cluster.x and point.x <= cluster.x + cluster.width and
      point.y >= cluster.y and point.y <= cluster.y + cluster.height])
}

// Marked fan-in and fan-out edges must meet the shared node separately and head-on.
fn marker_fan_check(direction, at_start) any^ {
  let result = layout.compute({
    nodes: [{id:"a",width:180,height:180}, {id:"b",width:60,height:60},
      {id:"c",width:60,height:60}, {id:"d",width:60,height:60}],
    edges: [for (peer in ["b", "c", "d"])
      {id:peer, from:if (at_start) "a" else peer, to:if (at_start) peer else "a",
        arrow_start:at_start, arrow_end:not at_start, marker_start:if (at_start) "empty" else "none",
        marker_end:if (at_start) "none" else "empty"}], direction:direction, route_mode:"orthogonal"
  });
  let vertical = direction in ["TB", "BT"];
  let endpoints = [for (edge in ((result.edges) ^ { [] }))
    if (at_start) edge.points[0] else edge.points[len(edge.points)-1]];
  let tangent_ok = [for (edge in ((result.edges) ^ { [] }),
    let terminal = if (at_start) edge.points[0] else edge.points[len(edge.points)-1],
    let next = if (at_start) edge.points[1] else edge.points[len(edge.points)-2])
    if (vertical) abs(terminal.x - next.x) < 0.001 else abs(terminal.y - next.y) < 0.001];
  let cross = [for (point in endpoints) if (vertical) point.x else point.y];
  not (false in tangent_ok) and abs(cross[0] - cross[1]) >= 15.0 and
    abs(cross[0] - cross[2]) >= 15.0 and abs(cross[1] - cross[2]) >= 15.0
}

// Forward edges from different ranks still need separate marked attachments.
fn mixed_rank_fan_check(direction, mode) any^ {
  let result = layout.compute({
    nodes: [{id:"a",width:50,height:50}, {id:"b",width:50,height:50},
      {id:"c",width:50,height:50}, {id:"target",width:50,height:50}],
    edges: [{id:"ab",from:"a",to:"b"}, {id:"bc",from:"b",to:"c"},
      {id:"at",from:"a",to:"target"}, {id:"bt",from:"b",to:"target"},
      {id:"ct",from:"c",to:"target"}], direction:direction, route_mode:mode
  });
  let points = [for (id in ["at", "bt", "ct"], let edge = by_id(result.edges, id))
    edge.points[len(edge.points) - 1]];
  len([for (i, point in points, j in 0 to 2
    where ((j > i and abs(point.x - points[j].x) < 1.0 and
      abs(point.y - points[j].y) < 1.0) ^ { false })) point]) == 0
}

let labels = layout.from_velmts(
  {attrs: {'data-direction': "LR", 'data-edge-sep': "12"}},
  [
    {tag: "node", index: 0, width: 60, height: 40,
      attrs: {'data-node-id': "a"}},
    {tag: "node", index: 1, width: 60, height: 40,
      attrs: {'data-node-id': "b"}},
    {tag: "edge-label", index: 2, width: 70, height: 20,
      attrs: {'data-edge-id': "e1"}},
    {tag: "edge-label", index: 3, width: 70, height: 20,
      attrs: {'data-edge-id': "e2"}},
    {tag: "edge", index: 4, width: 0, height: 0,
      attrs: {'data-edge-id': "e1", 'data-from': "a", 'data-to': "b"}},
    {tag: "edge", index: 5, width: 0, height: 0,
      attrs: {'data-edge-id': "e2", 'data-from': "a", 'data-to': "b"}}
  ], null)
let label1 = placement_by_index(labels.placements, 2)
let label2 = placement_by_index(labels.placements, 3)

{
  grouped: [for (node in ((grouped.layers[0].nodes) ^ { [] })) node.id],
  cluster_separated: grouped.clusters[0].x + grouped.clusters[0].width <=
    by_id(grouped.nodes, "outside").x - by_id(grouped.nodes, "outside").width / 2.0,
  obstacle_detour: len(ac.points) >= 4,
  bypass_endpoints: [for (direction in ["TB", "BT", "LR", "RL"]) bypass_check(direction)],
  wide_bypass_endpoints: [for (direction in ["TB", "BT", "LR", "RL"]) bypass_check(direction, false, true)],
  explicit_endpoints: bypass_check("TB", true),
  feedback_clear_side: [for (direction in ["TB", "BT", "LR", "RL"], mirrored in [false, true])
    feedback_check(direction, mirrored)],
  curved_feedback_tangents: [for (direction in ["TB", "BT", "LR", "RL"], reversed in [false, true])
    curved_feedback_check(direction, reversed)],
  clear_fractional_edge: clear_fractional_edge(),
  curved_forward_tangents: [for (direction in ["TB", "BT", "LR", "RL"]) curved_forward_bypass(direction)],
  cluster_endpoints: [for (direction in ["TB", "BT", "LR", "RL"]) cluster_endpoint_check(direction)],
  source_cluster: source_cluster_check(),
  nested_cluster_cycle: nested_cluster_cycle_check(),
  cluster_self_loop: cluster_self_loop_check(),
  marked_fans: [for (direction in ["TB", "BT", "LR", "RL"], at_start in [false, true])
    marker_fan_check(direction, at_start)],
  mixed_rank_fans: [for (direction in ["TB", "BT", "LR", "RL"], mode in ["curved", "polyline", "orthogonal"])
    mixed_rank_fan_check(direction, mode)],
  label_positions: [[label1.x, label1.y], [label2.x, label2.y]],
  labels_separated: separated(label1, 70, 20, label2, 70, 20),
  labels_contained: label1.x >= 0 and label1.y >= 0 and label2.x >= 0 and label2.y >= 0 and
    label1.x + 70 <= labels.width and label2.x + 70 <= labels.width and
    label1.y + 20 <= labels.height and label2.y + 20 <= labels.height
}

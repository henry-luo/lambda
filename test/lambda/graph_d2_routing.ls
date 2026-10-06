import layout: lambda.graph.layout
import model: lambda.graph.model
import normalize: lambda.graph.normalize
import transform: lambda.graph.transform

fn by_id(items, id) => [for (item in items where item.id == id) item][0]

fn side_terminal(point, bend, node, vertical) {
  if (vertical)
    abs(point.y - node.y) < 0.001 and
      abs(point.x - (node.x + node.width / 2.0)) < 0.001 and
      abs(bend.y - point.y) < 0.001 and bend.x > point.x
  else
    abs(point.x - node.x) < 0.001 and
      abs(point.y - (node.y + node.height / 2.0)) < 0.001 and
      abs(bend.x - point.x) < 0.001 and bend.y > point.y
}

fn return_checks(result, vertical) => [
  for (edge in result.edges where edge.id == "e2" or edge.id == "e3") {
    let points = edge.points;
    let final_index = len(points) - 1;
    {
      from: edge.from,
      to: edge.to,
      source_side: side_terminal(points[0], points[1],
        by_id(result.nodes, edge.from), vertical),
      target_side: side_terminal(points[final_index], points[final_index - 1],
        by_id(result.nodes, edge.to), vertical),
      orthogonal: all([for (i in 1 to final_index)
        abs(points[i].x - points[i - 1].x) < 0.001 or
          abs(points[i].y - points[i - 1].y) < 0.001])
    }
  }
]

let source = (input("test/input/simple_diagram.d2",
  {type: "graph", flavor: "d2"})) ^ { null }
let canonical = normalize.normalize(source)
let nodes = [for (node in model.nodes(canonical.graph))
  {id: node.id, width: 100, height: 40, shape: node.shape}]
let edges = model.edges(canonical.graph)
let html = transform.to_html(source)
let database_html = [for (node in model.nodes(html)
  where node["data-node-id"] == "database") node][0]
let rim = model.element_children(database_html)[0]
let shape_variants = transform.to_html({nodes: [
  {id: "lined", label: "Lined", shape: "lin-cyl"},
  {id: "fixed", label: "Fixed", shape: "cylinder", 'fixed-shape': true,
    width: 100, height: 60, stroke: "#ff0000", 'stroke-width': 2},
  {id: "borderless", label: "Borderless", shape: "cylinder", peripheries: 0}
]})
let variant_nodes = model.nodes(shape_variants)
let fixed_shape = model.element_children(variant_nodes[1])[0]
let directions = [for (direction in ["TB", "BT", "LR", "RL"]) {
  let result = layout.compute({nodes: nodes, edges: edges},
    {direction: direction, rank_sep: 80, edge_sep: 12});
  {direction: direction,
    returns: return_checks(result, direction == "TB" or direction == "BT")}
}]

// return lanes must still honor explicit ports and compass endpoints.
let ported = layout.compute({
  nodes: [
    {id: "a", width: 100, height: 40},
    {id: "b", width: 100, height: 40,
      ports: [{id: "out", side: "east", offset: 0.25}]}
  ],
  edges: [
    {id: "ab", from: "a", to: "b"},
    {id: "ba", from: "b", to: "a", from_port: "out", to_compass: "e"}
  ]
})
let back = by_id(ported.edges, "ba")
let a = by_id(ported.nodes, "a")
let b = by_id(ported.nodes, "b")
let finish = back.points[len(back.points) - 1]

{
  counts: [len(nodes), len(edges)],
  shapes: [for (node in nodes) node.shape],
  valid: canonical.valid,
  labels: [for (edge in edges) model.label_source(edge)],
  html_counts: [len(model.nodes(html)), len(model.edges(html))],
  cylinder_rim: [
    model.tag(rim) == "node-rim" and rim["aria-hidden"] == "true",
    contains(string(rim.style), "border:inherit;border-radius:50%"),
    contains(string(database_html.style), "padding:22px 14px 10px"),
    database_html[1] == "database",
    model.tag(model.element_children(variant_nodes[0])[0]) == "node-rim",
    model.tag(model.element_children(fixed_shape)[0]) == "node-rim" and
      contains(string(fixed_shape.style), "border-color:#ff0000;border-width:2px"),
    len(model.element_children(variant_nodes[2])) == 0
  ],
  directions: directions,
  explicit_endpoints: [
    abs(back.points[0].x - (b.x + b.width / 2.0)) < 0.001 and
      abs(back.points[0].y - (b.y - b.height / 4.0)) < 0.001,
    abs(finish.x - (a.x + a.width / 2.0)) < 0.001 and
      abs(finish.y - a.y) < 0.001
  ]
}

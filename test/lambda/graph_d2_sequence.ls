import model: lambda.graph.model
import normalize: lambda.graph.normalize
import transform: lambda.graph.transform

fn timeline(graph) any^ {
  let html = transform.to_html(graph)
  let canvas = model.element_children(html)[0]
  let items = model.element_children(canvas);
  {canvas:canvas, labels:[for (item in items where model.tag(item)^ == "text") item],
    lifelines:[for (item in items where model.tag(item)^ == "line") item],
    routes:[for (item in items where model.tag(item)^ == "path" and item.fill == "none") item],
    arrows:[for (item in items where model.tag(item)^ == "path" and item.fill != "none") item]}
}

let source = input("test/input/seq_diagram.d2")^
let canonical = normalize.normalize(source)
let diagram = timeline(source)^
let explicit = parse("shape: sequence_diagram\nGateway: Public API; Worker: Queue Worker; Client: Browser\nWorker <- Client: Unicast\nGateway -- Worker: Plain\nGateway <-> Client: Duplex\nClient -> Client: Loop\n", {type:"graph", flavor:"d2"})^
let declared = timeline(explicit)^
let svg = transform.render_svg(source, 1050, 640);
{
  kind: source.kind,
  valid: canonical.valid,
  participants:[for (node in model.nodes(canonical.graph)^) node.id],
  messages:[for (edge in model.edges(canonical.graph)^) [edge.from, edge.to]],
  viewer_order:[for (label in diagram.labels) label[0]],
  geometry:[
    len(diagram.lifelines) == 3,
    all([for (line in diagram.lifelines) (
      line.x1 == line.x2 and line.y1 < line.y2 and line["stroke-dasharray"] != null)]),
    diagram.lifelines[0].x1 < diagram.lifelines[1].x1 and diagram.lifelines[1].x1 < diagram.lifelines[2].x1,
    diagram.labels[3].y < diagram.labels[4].y and diagram.labels[4].y < diagram.labels[5].y,
    contains(diagram.routes[2].d, " Q "),
    diagram.lifelines[2].x1 - diagram.lifelines[1].x1 > diagram.lifelines[1].x1 - diagram.lifelines[0].x1,
    diagram.canvas.width > diagram.lifelines[2].x1
  ],
  explicit_actors:[for (label in take(declared.labels, 3)) label[0]],
  operator_endpoints:[for (edge in model.edges(normalize.normalize(explicit).graph)^) [edge.from, edge.to]],
  operator_arrows:len(declared.arrows),
  rendered:contains(svg, "Never lend your car") and contains(svg, "internal debate ensues")
}

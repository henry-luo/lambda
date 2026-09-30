import model: lambda.graph.model
import normalize: lambda.graph.normalize
import transform: lambda.graph.transform

let graph = (input("test/lambda/graph/mermaid/sequence_diagram2.mmd",
  {type: "graph", flavor: "mermaid"})) ^ { null }
let html = transform.to_html(graph)
let svg_element = model.element_children(html)[0]
let shapes = model.element_children(svg_element)
let events = [for (child in model.element_children(graph)
  where contains(["edge", "annotation", "sequence-block"], model.tag(child))) child]
let edges = [for (event in events where model.tag(event) == "edge") event]
let blocks = [for (event in events where model.tag(event) == "sequence-block")
  string(event.kind) ++ ":" ++ string(event.phase)]
let guards = [for (event in events where model.tag(event) == "sequence-block" and
  event.phase != "end") string(event.label)]
let notes = [for (event in events where model.tag(event) == "annotation")
  [string(event.kind), string(event["owner-id"]),
    string(model.optional(event, "to-id"))]]
let labels = [for (edge in edges) string(edge.label)]
let activations = [for (edge in edges where
  model.optional(edge, "activation") != null)
  string(edge.from) ++ ":" ++ string(edge.to) ++ ":" ++
    string(edge.activation)]
let frame_rects = [for (shape in shapes where model.tag(shape) == "rect" and
  model.optional(shape, "stroke-dasharray") != null) shape]
let activation_rects = [for (shape in shapes where model.tag(shape) == "rect" and
  shape.width == 6) shape]
let open_arrows = [for (shape in shapes where model.tag(shape) == "path" and
  model.optional(shape, "fill") == "none") shape]
let svg = transform.render_svg(graph, 1000, 800)

{
  valid: normalize.normalize(graph).valid,
  participants: [for (node in model.nodes(graph)) string(node.id)],
  counts: [len(edges), len(notes), len(blocks), len(frame_rects),
    len(activation_rects), len(open_arrows)],
  blocks: blocks,
  guards: guards,
  notes: notes,
  activations: activations,
  messages: labels,
  rendered: [for (label in ["Logs in using credentials",
    "Successfully logged in", "Submit new post",
    "Send mail to blog subscribers", "Successfully posted",
    "The user must be logged in to submit blog posts",
    "When the user is authenticated, they can now submit new posts"])
    contains(svg, label)]
}

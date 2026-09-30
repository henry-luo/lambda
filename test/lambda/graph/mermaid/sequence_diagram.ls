import model: lambda.graph.model
import normalize: lambda.graph.normalize
import transform: lambda.graph.transform

let graph = (input("test/lambda/graph/mermaid/sequence_diagram.mmd",
  {type: "graph", flavor: "mermaid"})) ^ { null }
let normalized = normalize.normalize(graph)
let html = transform.to_html(graph)
let svg_element = model.element_children(html)[0]
let shapes = model.element_children(svg_element)
let svg = transform.render_svg(graph, 1000, 800)
let events = [for (child in model.element_children(graph)
  where contains(["edge", "annotation", "sequence-block"], model.tag(child)))
  model.tag(child)]
let participant_boxes = [for (shape in shapes
  where model.tag(shape) == "rect" and contains([
    "#ec64ff", "#15d8c0", "#ff8b2b"], model.optional(shape, "stroke"))) shape]
let lifelines = [for (shape in shapes
  where model.tag(shape) == "line" and shape.y1 != shape.y2) shape]
let loops = [for (shape in shapes
  where model.tag(shape) == "rect" and
    model.optional(shape, "stroke-dasharray") != null and
    shape.width < svg_element.width / 2) shape]
let self_messages = [for (shape in shapes
  where model.tag(shape) == "path" and contains(string(shape.d), " C ")) shape]
let notes = [for (shape in shapes
  where model.tag(shape) == "rect" and shape.stroke == "#ffd21a") shape]

{
  valid: normalized.valid,
  participants: [for (node in model.nodes(graph)) string(node.id)],
  events: events,
  visual: [len(participant_boxes),
    len([for (box in participant_boxes where box.y > 100) box]),
    len(lifelines), len(loops), len(self_messages), len(notes)],
  rendered: [contains(svg, "Alice"), contains(svg, "Bob"),
    contains(svg, "John"), contains(svg, "HealthCheck"),
    contains(svg, "Rational thoughts"), contains(svg, "prevail..."),
    contains(svg, "Jolly good!")]
}

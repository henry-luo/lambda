import transform: lambda.package.graph.transform

let source^err = input("test/input/simple_flowchart.mmd",
  {type: "graph", flavor: "mermaid"})
let graph = transform.to_html(source, {theme: "nord"})
let nodes = [for (i in 0 to (len(graph) - 1), let child = graph[i]
  where string(name(child)) == "node") child]
let edges = [for (i in 0 to (len(graph) - 1), let child = graph[i]
  where string(name(child)) == "edge") child]

{
  tag: name(graph),
  class: graph.class,
  theme: graph["data-theme"],
  direction: graph["data-direction"],
  counts: [len(nodes), len(edges)],
  first_node: [nodes[0]["data-node-id"], nodes[0][0]],
  first_edge: [edges[0]["data-from"], edges[0]["data-to"],
    edges[0]["data-directed"], edges[0]["data-arrow-end"]]
}

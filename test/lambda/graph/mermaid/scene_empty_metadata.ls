import scene: lambda.graph.scene
import model: lambda.graph.model

// empty serialized optional fields must not become graph relation identifiers.
let source = "<svg><g data-graph-role='graph' data-width='120' data-height='80'>"
  ++ "<g data-graph-role='cluster' data-cluster-id='c' data-parent-cluster-id='' data-width='100' data-height='60'/>"
  ++ "<g data-graph-role='node' data-node-id='a' data-subgraph-id='' data-shape-family='' data-fill='' data-opacity='0' data-width='10' data-height='10'/>"
  ++ "<g data-graph-role='node' data-node-id='b' data-subgraph-id='c' data-width='10' data-height='10'/>"
  ++ "<g data-graph-role='edge' data-edge-id='e' data-from='a' data-to='b' data-from-port='' data-to-port='p' data-tail-cluster='' data-head-cluster='c'/>"
  ++ "</g></svg>"
let result = scene.from_svg(source)
let nodes = [for (entry in model.element_children(result) where model.tag(entry) == "node") entry]
let clusters = [for (entry in model.element_children(result) where model.tag(entry) == "cluster") entry]
let edges = [for (entry in model.element_children(result) where model.tag(entry) == "edge") entry];

[result.direction, nodes[0].group, nodes[0]["shape-family"], nodes[0].fill,
  nodes[0].opacity, nodes[1].group, clusters[0].parent, edges[0]["from-port"],
  edges[0]["to-port"], edges[0]["tail-cluster"], edges[0]["head-cluster"]]

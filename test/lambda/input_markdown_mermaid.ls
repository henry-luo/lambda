import graph_doc: lambda.graph.document

// The same source-preserving fence feeds both native and editor presentation.
let markdown = input("test/input/markdown_extensions.md", 'markdown')^
let fence = markdown?<code>
let diagram = graph_doc.from_mermaid(content(fence)[0])^;

name(diagram);
diagram["data-direction"];
[for (node in content(diagram) where name(node) == 'node') node["data-node-id"]];
[for (edge in content(diagram) where name(edge) == 'edge') [edge["data-from"], edge["data-to"]]];
count(markdown?<math>) == 2

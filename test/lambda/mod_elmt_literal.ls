// helper module for elmt_literal_module.ls: computed-key / spread element
// literals defined in a module other than the running script
pub fn spread_graph(graph) {
  let attrs = {*:map(graph), 'ir-stage': "canonical"};
  <graph *:attrs, for (child in graph) child>
}
pub fn keyed_node(graph, key) {
  <node kind:"n", *:map(graph), [key]:42, "tail">
}
pub fn keyed_map(source, key) {
  {*:source, [key]:true}
}

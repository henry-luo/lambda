// Semantic HTML transform and Radiant custom-layout installation.

import radiant;
import graph_layout: .layout
import normalize: .normalize
import html: .transform.html
import paint: .transform.paint

fn lambda_graph_layout(parent, children, ctx) {
  let result = graph_layout.from_velmts(parent, children, ctx);
  let edge_color = if (parent.attrs != null and parent.attrs["data-edge-color"] != null)
    string(parent.attrs["data-edge-color"]) else "#59636e";
  {
    width: result.width,
    height: result.height,
    placements: result.placements,
    paint_layers: paint.layers(result, {edge_color: edge_color})
  }
}

pub fn install() => radiant.register_layout("lambda-graph", lambda_graph_layout)

pub fn to_html(graph, opts = null) => html.to_html(normalize.normalize(graph).graph, opts)

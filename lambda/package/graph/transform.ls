// Semantic HTML transform and Radiant custom-layout installation.

import radiant;
import graph_layout: .layout
import normalize: .normalize
import scene: .scene
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

pub fn render_svg(graph, width: int, height: int, opts = null) {
  // the transform module owns the Radiant import, avoiding duplicate retained
  // module registrations when callers use the complete graph render pipeline.
  radiant.render_svg(format(to_html(graph, opts), 'html'), width, height)
}

pub fn scene_from_svg(svg) => scene.from_svg(svg)

pub fn render_scene(graph, width: int, height: int, opts = null) {
  let svg = render_svg(graph, width, height, opts);
  {svg: svg, scene: scene.from_svg(svg)}
}

pub fn compare_scenes(actual, expected, policy = null) =>
  scene.compare_scenes(actual, expected, policy)

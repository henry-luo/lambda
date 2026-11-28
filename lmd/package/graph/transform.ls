// Semantic HTML transform and automatically resolved Radiant layout export.

import radiant
import graph_layout: .layout
import normalize: .normalize
import scene: .scene
import html: .transform.html
import paint: .transform.paint
import sequence: lambda.chart.sequence

pub fn layout(parent, children, ctx) {
  let result = graph_layout.from_velmts(parent, children, ctx);
  let attrs = parent.attrs;
  let color = if (attrs == null) null else attrs["data-edge-color"];
  let edge_color = if (color == null) "#59636e" else string(color);
  let flavor = if (attrs == null) null else attrs["data-graph-flavor"];
  let zoom = if (attrs == null) null else attrs["data-graph-zoom"];
  {
    width: result.width,
    height: result.height,
    placements: result.placements,
    paint_layers: paint.layers(result, {edge_color: edge_color, flavor: flavor,
      edge_width_scale: if (zoom == null) 1.0 else float(zoom)})
  }
}

pub fn to_html(graph, opts = null) any^ {
  let canonical = normalize.canonicalize(graph);
  if (canonical is element and canonical.flavor in ["mermaid", "d2"] and
      canonical.kind == "sequence") sequence.to_html(canonical, opts)
  else html.to_html(canonical, opts)
}

pub fn render_svg(graph, width: int, height: int, opts = null) any^ {
  // the transform module owns the Radiant import, avoiding duplicate retained
  // module registrations when callers use the complete graph render pipeline.
  radiant.render_svg(format(to_html(graph, opts), 'html'), width, height)
}

pub fn scene_from_svg(svg) any^ => scene.from_svg(svg)

pub fn render_scene(graph, width: int, height: int, opts = null) {
  let svg = render_svg(graph, width, height, opts);
  {svg: svg, scene: scene.from_svg(svg)}
}

pub fn compare_scenes(actual, expected, policy = null) any^ =>
  scene.compare_scenes(actual, expected, policy)

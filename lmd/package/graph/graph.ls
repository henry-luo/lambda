// Compatibility facade. New code should import graph.layout or graph.transform.

import graph_layout: .layout

pub fn make_options() any^ => graph_layout.make_options()

pub fn layout(input, opts = null) any^ => graph_layout.compute(input, opts)

pub fn layout_custom(parent, children, ctx, opts = null) any^ =>
  graph_layout.layout_custom(parent, children, ctx, opts)

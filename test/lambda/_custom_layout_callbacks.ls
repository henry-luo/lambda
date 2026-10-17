// Native boundary tests supply this module through the internal package bridge.
import radiant

pub fn layout(parent, children, ctx) {
  if (ctx.layout_name == "bfc_smoke") {
    placements: [for (child in children) {child: child, x: 0, y: 0}]
  } else if (ctx.layout_name == "flow_smoke") {
    baseline: 12,
    placements: [for (child in children) {
      child: child,
      x: child.index * 50 + (if (ctx.child_available_width_definite and
        ctx.child_available_width_source == "available") -10 else -20),
      y: child.index * 30 + (if (ctx.child_available_height_definite and
        ctx.child_available_height_source == "available") 0 else 1000),
      z: child.index
    }]
  } else {
    let child = children[0];
    let attrs = child.attrs;
    {
      width: child.width, height: child.height,
      placements: [for (value in children) {
        child_index: value.index, x: value.width, y: value.height
      }],
      paint_layers: [{z: 0, content: <g 'data-projection-tag': child.tag,
        'data-projection-role': attrs["data-graph-role"],
        'data-projection-id': attrs["data-node-id"]>}]
    }
  }
}

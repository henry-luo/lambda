// compatibility exports; the drawing package owns the implementation.
import core: lambda.editor.draw.routing

pub fn resolve_endpoint_from(doc, connector) any^ => core.resolve_endpoint_from(doc,connector)
pub fn resolve_endpoint_to(doc, connector) any^ => core.resolve_endpoint_to(doc,connector)
pub fn route_straight(a, b) any^ => core.route_straight(a,b)
pub fn route_orthogonal(a, b, a_bbox, b_bbox) any^ => core.route_orthogonal(a,b,a_bbox,b_bbox)
pub fn route_curved(a, b, a_bbox, b_bbox) any^ => core.route_curved(a,b,a_bbox,b_bbox)
pub fn compute_route(connector, doc) any^ => core.compute_route(connector,doc)
pub fn route_to_svg_path(points) any^ => core.route_to_svg_path(points)
pub fn curved_corner(prev, cur, next, radius) any^ => core.curved_corner(prev,cur,next,radius)
pub fn route_to_curved_points(points, radius, steps = 8) any^ => core.route_to_curved_points(points,radius,steps)
pub fn route_to_curved_svg_path(points, radius) any^ => core.route_to_curved_svg_path(points,radius)
pub fn is_point_on_route(p, route, tolerance) any^ => core.is_point_on_route(p,route,tolerance)

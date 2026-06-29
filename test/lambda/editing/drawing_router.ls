// Stage-4 connector routing (mod_router).
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_geom
import lambda.package.editor.mod_router

let empty_doc = node_attrs('drawing', [], [])

// 1. straight routing between free points
let cs = node_attrs('connector', [{name:'id',value:"C1"},{name:'routing',value:'straight'},
  {name:'from-x',value:0.0},{name:'from-y',value:0.0},{name:'to-x',value:100.0},{name:'to-y',value:50.0}], [])
let rs = compute_route(cs, empty_doc)
"straight_len:"; len(rs)
"straight_d:"; route_to_svg_path(rs) == "M 0 0 L 100 50"

// 2. orthogonal L-route between free points (horizontal-first)
let co = node_attrs('connector', [{name:'id',value:"C2"},{name:'routing',value:'orthogonal'},
  {name:'from-x',value:0.0},{name:'from-y',value:0.0},{name:'to-x',value:100.0},{name:'to-y',value:50.0}], [])
let ro = compute_route(co, empty_doc)
"orth_len:"; len(ro)
"orth_corner_x:"; ro[1].x
"orth_corner_y:"; ro[1].y

// 3. default routing is orthogonal when unspecified
let cd = node_attrs('connector', [{name:'id',value:"C2b"},
  {name:'from-x',value:0.0},{name:'from-y',value:0.0},{name:'to-x',value:40.0},{name:'to-y',value:30.0}], [])
"default_orth_len:"; len(compute_route(cd, empty_doc))

// 4. shape-anchored route (closest-edge fallback = bbox center)
let s1 = node_attrs('shape',[{name:'id',value:"S1"},{name:'kind',value:'rect'},{name:'x',value:0.0},{name:'y',value:0.0},{name:'width',value:40.0},{name:'height',value:40.0}],[])
let s2 = node_attrs('shape',[{name:'id',value:"S2"},{name:'kind',value:'rect'},{name:'x',value:200.0},{name:'y',value:100.0},{name:'width',value:40.0},{name:'height',value:40.0}],[])
let ca = node_attrs('connector',[{name:'id',value:"C3"},{name:'routing',value:'orthogonal'},{name:'from-shape',value:"S1"},{name:'to-shape',value:"S2"}],[])
let draw = node_attrs('drawing',[{name:'id',value:"D1"}],[node_attrs('layer',[{name:'id',value:"L1"}],[s1,s2,ca])])
let fa = resolve_endpoint_from(draw, ca)
"anchor_center_x:"; fa.point.x
"anchor_center_y:"; fa.point.y
"anchor_has_bbox:"; fa.bbox != null
let ra = compute_route(ca, draw)
"anchored_len_ok:"; (len(ra) >= 2)
"anchored_start_x:"; ra[0].x

// 5. port anchor (normalized coords)
let sp = node_attrs('shape',[{name:'id',value:"SP"},{name:'kind',value:'rect'},{name:'x',value:0.0},{name:'y',value:0.0},{name:'width',value:100.0},{name:'height',value:100.0},
  {name:'ports',value:[{id:"p1",x:1.0,y:0.5}]}],[])
let cp = node_attrs('connector',[{name:'id',value:"CP"},{name:'from-shape',value:"SP"},{name:'from-port',value:"p1"},{name:'to-x',value:200.0},{name:'to-y',value:50.0}],[])
let dp = node_attrs('drawing',[{name:'id',value:"DP"}],[node_attrs('layer',[{name:'id',value:"LP"}],[sp,cp])])
let pa = resolve_endpoint_from(dp, cp)
"port_x:"; pa.point.x
"port_y:"; pa.point.y

// 6. waypoint pinning forces the route through the given point
let cw = node_attrs('connector',[{name:'id',value:"C4"},{name:'routing',value:'straight'},
  {name:'from-x',value:0.0},{name:'from-y',value:0.0},{name:'to-x',value:100.0},{name:'to-y',value:0.0},
  {name:'waypoints',value:[{x:50.0,y:30.0}]}],[])
let rw = compute_route(cw, empty_doc)
"wp_len:"; len(rw)
"wp_mid_x:"; rw[1].x
"wp_mid_y:"; rw[1].y

// 7. curved render path
"curved_short:"; route_to_curved_svg_path([v2(0.0,0.0),v2(10.0,0.0)], 8.0) == "M 0 0 L 10 0"
"curved_has_Q:"; (index_of(route_to_curved_svg_path(ro, 8.0), "Q") >= 0)

// 8. point-on-route hit test
"on_route:"; is_point_on_route(v2(50.0,0.0), [v2(0.0,0.0),v2(100.0,0.0)], 2.0)
"off_route:"; is_point_on_route(v2(50.0,50.0), [v2(0.0,0.0),v2(100.0,0.0)], 2.0)

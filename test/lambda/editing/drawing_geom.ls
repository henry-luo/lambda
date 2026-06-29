// Stage-4 geometry + shape helpers + geometric hit-test (mod_geom).
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_geom

// 1. pure geometry
"rect_contains_in:"; rect_contains(rect(0.0,0.0,10.0,10.0), v2(5.0,5.0))
"rect_contains_out:"; rect_contains(rect(0.0,0.0,10.0,10.0), v2(15.0,5.0))
"rect_center_x:"; rect_center(rect(0.0,0.0,10.0,20.0)).x
"rect_center_y:"; rect_center(rect(0.0,0.0,10.0,20.0)).y
"rect_intersects:"; rect_intersects(rect(0.0,0.0,10.0,10.0), rect(5.0,5.0,10.0,10.0))
"rect_disjoint:"; rect_intersects(rect(0.0,0.0,5.0,5.0), rect(20.0,20.0,5.0,5.0))
"ellipse_in:"; point_in_ellipse(v2(5.0,5.0), rect(0.0,0.0,10.0,10.0))
"ellipse_corner_out:"; point_in_ellipse(v2(0.0,0.0), rect(0.0,0.0,10.0,10.0))
"seg_dist:"; point_to_segment_dist(v2(0.0,5.0), v2(0.0,0.0), v2(10.0,0.0))
"polyline_d:"; polyline_to_svg_d([v2(0.0,0.0), v2(10.0,0.0), v2(10.0,10.0)]) == "M 0 0 L 10 0 L 10 10"

let square = [v2(0.0,0.0), v2(10.0,0.0), v2(10.0,10.0), v2(0.0,10.0)]
"polygon_in:"; point_in_polygon(v2(5.0,5.0), square)
"polygon_out:"; point_in_polygon(v2(15.0,5.0), square)

// rotation: rotate (1,0) by 90deg around origin -> (0,1)
let rp = rotate_point(v2(1.0,0.0), v2(0.0,0.0), 90.0)
"rotate_x_near0:"; (abs(rp.x) < 0.0001)
"rotate_y_near1:"; (abs(rp.y - 1.0) < 0.0001)

// 2. shape attribute helpers
let s1 = node_attrs('shape', [{name:'id',value:"S1"},{name:'kind',value:'rect'},
  {name:'x',value:50.0},{name:'y',value:50.0},{name:'width',value:100.0},{name:'height',value:80.0}], [])
"shape_kind:"; get_shape_kind(s1)
"shape_id:"; get_shape_id(s1) == "S1"
"shape_bbox_w:"; get_shape_bbox(s1).width
"shape_attr_num:"; get_shape_attr_number(s1, 'x', 0.0)
"shape_attr_missing:"; get_shape_attr_number(s1, 'rotate', 7.0)

"parse_points_n:"; len(parse_points("0,0 10,0 10,10"))
"parse_points_x:"; parse_points("3,4 5,6")[1].x
let lep = get_line_endpoints(node_attrs('shape',[{name:'kind',value:'line'},{name:'x',value:0.0},{name:'y',value:0.0},{name:'width',value:10.0},{name:'height',value:20.0}],[]))
"line_end_bx:"; lep.b.x
"line_end_by:"; lep.b.y

// 3. per-shape hit-test (with rotation)
"hit_rect_in:"; hit_test_shape(s1, v2(100.0, 90.0))
"hit_rect_out:"; hit_test_shape(s1, v2(200.0, 90.0))
let sl = node_attrs('shape',[{name:'id',value:"LN"},{name:'kind',value:'line'},{name:'x',value:0.0},{name:'y',value:0.0},{name:'width',value:100.0},{name:'height',value:0.0}],[])
"hit_line_on:"; hit_test_shape(sl, v2(50.0, 1.0))
"hit_line_off:"; hit_test_shape(sl, v2(50.0, 20.0))

// 4. drawing-tree hit-test with z-order + find_shape_by_id
let s2 = node_attrs('shape',[{name:'id',value:"S2"},{name:'kind',value:'rect'},{name:'x',value:80.0},{name:'y',value:60.0},{name:'width',value:100.0},{name:'height',value:100.0}],[])
let layer = node_attrs('layer', [{name:'id',value:"L1"}], [s1, s2])
let drawing = node_attrs('drawing', [{name:'id',value:"D1"}], [layer])
// point inside both S1 and S2 overlap -> topmost (S2, later in z-order) wins
let h = hit_test_drawing(drawing, v2(100.0, 90.0))
"hit_kind:"; h.kind
"hit_topmost_id:"; h.shape_id == "S2"
"hit_path_len:"; len(h.path)
let miss = hit_test_drawing(drawing, v2(5.0, 5.0))
"hit_miss:"; miss.kind
// hidden layer is skipped
let hidden = node_attrs('layer', [{name:'id',value:"L1"},{name:'visible',value:false}], [s1, s2])
let drawing2 = node_attrs('drawing', [{name:'id',value:"D2"}], [hidden])
"hit_hidden_layer:"; hit_test_drawing(drawing2, v2(100.0, 90.0)).kind

"find_tag:"; find_shape_by_id(drawing, "S2").shape.tag
"find_path_len:"; len(find_shape_by_id(drawing, "S2").path)
"find_none:"; find_shape_by_id(drawing, "ZZ") == null

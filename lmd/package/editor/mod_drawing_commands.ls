// compatibility exports; the drawing package owns the implementation.
import core: lambda.editor.draw.commands

pub fn cmd_insert_shape(st, args) => core.cmd_insert_shape(st,args)
pub fn cmd_move_shapes(st, args) => core.cmd_move_shapes(st,args)
pub fn cmd_resize_shape(st, args) => core.cmd_resize_shape(st,args)
pub fn cmd_rotate_shape(st, shape_path, angle_deg) => core.cmd_rotate_shape(st,shape_path,angle_deg)
pub fn cmd_set_shape_attr(st, shape_path, name, value) => core.cmd_set_shape_attr(st,shape_path,name,value)
pub fn cmd_delete_shapes(st, shape_paths) => core.cmd_delete_shapes(st,shape_paths)
pub fn cmd_bring_to_front(st, shape_path) => core.cmd_bring_to_front(st,shape_path)
pub fn cmd_send_to_back(st, shape_path) => core.cmd_send_to_back(st,shape_path)
pub fn make_rect_shape(id, geom, style) => core.make_rect_shape(id,geom,style)
pub fn make_ellipse_shape(id, geom, style) => core.make_ellipse_shape(id,geom,style)
pub fn make_line_shape(id, geom, style) => core.make_line_shape(id,geom,style)
pub fn make_connector(id, args) => core.make_connector(id,args)

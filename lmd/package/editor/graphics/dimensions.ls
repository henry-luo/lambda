// Existing office insertion bounds are a selectable profile, not a drawing-engine limit.
import geometry: lambda.editor.draw.geometry

pub let office = {min_width:16,max_width:650,min_height:16,max_height:2000,min_rotation:-180,max_rotation:180}
pub fn valid(p,limits) any^ => geometry.finite_number(p.width) and p.width>=limits.min_width and p.width<=limits.max_width and
    geometry.finite_number(p.height) and p.height>=limits.min_height and p.height<=limits.max_height and
    geometry.finite_number(p.rotation) and p.rotation>=limits.min_rotation and p.rotation<=limits.max_rotation

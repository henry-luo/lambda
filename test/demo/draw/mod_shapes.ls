// compatibility exports; the drawing package owns the implementation.
import core: lambda.editor.draw.shapes

pub let catalog = core.catalog
pub let kinds = core.kinds
pub fn title(kind) => core.title(kind)
pub fn outline(kind,b) => core.outline(kind,b)
pub fn point_list(points) => core.point_list(points)
pub fn graphic(kind,b,style) => core.graphic(kind,b,style)
pub fn detail(kind,b,style) => core.detail(kind,b,style)
pub fn anchor(kind,b,direction) => core.anchor(kind,b,direction)

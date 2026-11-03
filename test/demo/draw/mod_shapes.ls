// compatibility exports; the drawing package owns the implementation.
import core: lambda.editor.draw.shapes

pub let catalog = core.catalog
pub let kinds = core.kinds
pub fn title(kind) any^ => core.title(kind)
pub fn outline(kind,b) any^ => core.outline(kind,b)
pub fn point_list(points) any^ => core.point_list(points)
pub fn graphic(kind,b,style) any^ => core.graphic(kind,b,style)
pub fn detail(kind,b,style) any^ => core.detail(kind,b,style)
pub fn anchor(kind,b,direction) any^ => core.anchor(kind,b,direction)

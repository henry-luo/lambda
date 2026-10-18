// compatibility exports; the drawing package owns the implementation.
import core: lambda.editor.draw.paint

pub fn stroke(node) => core.stroke(node)
pub fn pattern(node) => core.pattern(node)
pub fn graphic(node) => core.graphic(node)
pub fn marker(node,side) => core.marker(node,side)
pub fn arrow_preview(kind,side,target) => core.arrow_preview(kind,side,target)
pub fn text(node,b,label,edge=false) => core.text(node,b,label,edge)

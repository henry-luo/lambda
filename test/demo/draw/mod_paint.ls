// compatibility exports; the drawing package owns the implementation.
import core: lambda.editor.draw.paint

pub fn stroke(node) any^ => core.stroke(node)
pub fn pattern(node) any^ => core.pattern(node)
pub fn graphic(node) any^ => core.graphic(node)
pub fn marker(node,side) any^ => core.marker(node,side)
pub fn arrow_preview(kind,side,target) any^ => core.arrow_preview(kind,side,target)
pub fn text(node,b,label,edge=false) any^ => core.text(node,b,label,edge)

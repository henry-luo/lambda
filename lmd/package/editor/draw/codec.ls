// v1-v3 remain the demo profile; v4 preserves normalized, serializable drawing data.
import core: .core
import model: .model
import doc: lambda.editor.mod_doc
import step: lambda.editor.mod_step
fn restore_ports(ports) => [for (p in ports) if (p.direction == null) p else {*:p,direction:if (p.direction is symbol) p.direction else symbol(p.direction)}]
fn restore_attr(a) => if (contains(["kind","routing"],a.name)) if (a.value is symbol) a.value else symbol(a.value)
    else if (a.name == "ports") restore_ports(a.value)
    else if (a.name == "descriptor" and a.value.ports != null) {*:a.value,ports:restore_ports(a.value.ports)}
    else a.value
fn restore_node(n) any^ {
    let checked = if (not (n is map) or not (n.attrs is array) or not (n.content is array) or not (n.tag is string or n.tag is symbol))
        raise error("Draw: malformed archive node")
    let checked_attrs = if (not all([for (a in n.attrs) a is map and (a.name is string or a.name is symbol) and core.serializable(a.value)]))
        raise error("Draw: malformed archive attributes");
    doc.node_attrs(symbol(n.tag),[for (a in n.attrs) {name:symbol(a.name),
        value:restore_attr({*:a,name:string(a.name)})}],
        [for (child in n.content) restore_node(child)^])
}
pub fn decode(value,options=null) any^ {
    if (value.version == 4) core.checked(restore_node(value.drawing)^)^
    else model.restore(if (value.drawing.tag is symbol) parse(format(value,'json'),'json')^ else value)^
}
pub fn encode(drawing,options=null) any^ {
    let checked = core.checked(drawing)^
    let checked_version = if (options.version != null and not contains([3,4],options.version)) raise error("Draw: unsupported archive version");
    if (options.version == 3) (
        let legacy = model.drawing([for (node in model.objects(drawing)) {*:node,attrs:[for (a in node.attrs)
            if (a.name == 'rotate') {*:a,name:'rotation'} else a]}]),
        let representable = model.page(drawing) == {width:model.width,height:model.height} and
            all([for (node in model.objects(legacy)) model.valid_node(node,[for (n in core.vertices(drawing)) model.id(n)],false)]),
        if (not representable) raise error("Draw: this drawing cannot be represented by archive version 3")
        else model.archive(legacy))
    else {version:4,drawing:drawing}
}

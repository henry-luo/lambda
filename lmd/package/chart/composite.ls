// Factories expand once into values; a returned composite cannot recursively expand itself.
import parse: .parse
import util: .util
import dataflow:.dataflow
import animation:.animation

fn part_overrides(raw,parts) {
    let node=if (raw is element) parse.parse_top(raw) else raw;
    let own=parts[node.part];
    if (raw is element and name(raw)=='svg') raw else {*:node,
        encoding:{*:parse.attributes(node.encoding),*:parse.attributes(own.encoding)},
        mark:if (node.mark!=null) {*:node.mark,*:parse.attributes(own)} else null,
        layer:if (node.layer!=null) [for (child in node.layer) part_overrides(child,parts)] else null,
        children:if (node.children!=null) [for (child in node.children) part_overrides(child,parts)] else null}
}

pub fn present(raw) {
    let spec=if (raw is element) parse.parse_top(raw) else raw;
    not (raw is element and name(raw)=='svg') and (spec.mark.kind=="composite" or
        any([for (child in [*spec.children,*spec.layer,spec.template] where child!=null) present(child)]))
}
pub fn materialize(raw,values,parent=null) {
    if (raw is element and name(raw)=='svg') raw else {
        let spec=if (raw is element) parse.parse_top(raw) else raw;
        let resolved=dataflow.resolve(spec,parent,values);
        let context={*:spec,*:resolved,transform:null,data_source:null};
        let expanded=if (spec.facet!=null or spec.repeat_row!=null or spec.repeat_column!=null) context else expand(context);
        let failure=util.first_error([resolved.data,expanded]);
        // Expansion sees the partition's data; ordinary descendants keep inheriting that partition.
        let result=if (spec.mark.kind=="composite" and spec.facet==null) expanded else spec;
        let descendants=if (spec.mark.kind=="composite" and spec.facet==null) {*:expanded,*:dataflow.resolve(expanded,parent,values)} else context;
        if (failure is error) failure else {*:result,
            _factory_catalogue:if (spec.facet!=null) [for (i,rows in dataflow.facet_plan(context).data)
                {*:materialize({*:spec,facet:null,data:rows,data_source:null,transform:null},values,parent),
                    _cell_key:dataflow.facet_plan(context).keys[i]}]
                else if (spec.repeat_row!=null or spec.repeat_column!=null)
                    [for (row in (if (spec.repeat_row!=null) spec.repeat_row else [""]))
                     for (column in (if (spec.repeat_column!=null) spec.repeat_column else [""]))
                        {*:materialize(parse.substitute(spec.template,row,column),values,context),_cell_key:[row,column]}] else null,
            children:if (result.children!=null) [for (child in result.children) materialize(child,values,descendants)] else null,
            layer:if (result.layer!=null) [for (child in result.layer) materialize(child,values,descendants)] else null,
            template:if (result.template!=null and spec.repeat_row==null and spec.repeat_column==null)
                materialize(result.template,values,context) else result.template}
    }
}
fn recursive(spec) {
    let value = if (spec is element) parse.parse_top(spec) else spec;
    value.mark.kind == "composite" or any([for (child in [*value.layer, *value.children, value.template]
        where child != null) recursive(child)])
}

pub fn expand(spec) {
    if (spec.mark.kind != "composite") spec
    else if (not (spec.mark.expand is fn)) error("chart: composite mark requires a pure expand function")
    else {
        let expanded = spec.mark.expand(spec.data, if (spec.mark.options != null) spec.mark.options else {});
        let parsed = if (expanded is map or expanded is element) part_overrides(expanded,spec.mark.parts) else expanded;
        if (expanded is error) expanded
        else if (not (parsed is map) or (parsed.mark == null and parsed.layer == null))
            error("chart: composite factory must return a chart or layer composition")
        else if (recursive(parsed)) error("chart: recursive composite expansion is not supported")
        else {*:spec, mark: null, *:parsed, data: if (parsed.data != null) parsed.data else spec.data,
            interaction:{*:parse.attributes(spec.interaction),*:parse.attributes(spec.mark.interaction),*:parse.attributes(parsed.interaction)},
            state:{*:parse.attributes(spec.state),*:parse.attributes(spec.mark.state),*:parse.attributes(parsed.state)},
            animate:animation.merge(animation.merge(spec.animate,spec.mark.animate),parsed.animate),
            _composite: true}
    }
}

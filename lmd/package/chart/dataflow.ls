// Primary data resolution is shared by ordinary views and pure composite expansion.
import parse:.parse
import source:.source
import transform:.transform
import parameter:.parameter
import geometry:.geometry
import util:.util
pub fn resolve(spec,parent,values) {
    let datasets={*:parse.attributes(parent.datasets),*:parse.attributes(spec.datasets)};
    let own=spec.data!=null or spec.data_source.values!=null or spec.data_source.name!=null or spec.data_source.url!=null;
    let resolved=if (parent==null or own) source.resolve(spec.data,spec.data_source,datasets) else parent.data;
    let graph=if (resolved.nodes!=null) resolved else if (spec._graph!=null) spec._graph else parent._graph;
    let raw=if (resolved is error) resolved else if (resolved.nodes!=null) resolved.nodes else
        if (contains(["geoshape","geo"],spec.mark.kind) or contains(["FeatureCollection","Feature"],resolved.type)) geometry.records(resolved) else resolved;
    {datasets:datasets,_graph:graph,data:if (raw is error) raw else if (not (raw is array)) error("chart: data must be an array")
        else transform.apply_transforms(raw,parameter.transforms(spec.transform,values),datasets)}
}

pub fn facet_plan(spec) {
    let facet = spec.facet;
    let row_field = if (facet.row is string) facet.row else facet.row.field;
    let column_field = if (facet.column is string) facet.column else facet.column.field;
    let grid = row_field != null or column_field != null;
    let rows = if (row_field != null) util.unique_vals(spec.data |> ~[row_field]) else [null];
    let columns = if (column_field != null) util.unique_vals(spec.data |> ~[column_field]) else [null];
    let keys = if (grid) [for (row in rows) for (column in columns) [row, column]] else util.unique_vals(spec.data |> ~[facet.field]);
    {keys:keys,columns: if (grid) max([1, len(columns)]) else if (facet.columns != null) facet.columns else 3,
        headers: [for (key in keys) if (grid) join([for (value in key where value != null) string(value)], " / ") else string(key)],
        data: [for (key in keys) if (grid) (spec.data |: (row_field == null or ~[row_field] == key[0]) and
            (column_field == null or ~[column_field] == key[1])) else (spec.data |: ~[facet.field] == key)]}
}

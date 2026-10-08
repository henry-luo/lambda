// Table reshaping reuses the chart's grouping, aggregate, frame, and stack contracts.
import records: .records
import util: .util
import window: .window
import stack: .stack
import parse: .parse

fn group_fields(step) => if (step.groupby != null) step.groupby else []
fn valid_groups(groups) => groups is array and len(groups |: not (~ is string)) == 0
fn names(value, defaults, suffix = null) => if (value is string) [value, value ++ suffix]
    else if (value != null) value else defaults
fn valid_names(value) => value is array and len(value) == 2 and value[0] is string and value[1] is string

pub fn pivot(data, step) {
    let field = if (step.pivot != null) step.pivot else step.field;
    let groups = group_fields(step);
    let op = if (step.op != null) step.op else "sum";
    let limit = if (step.limit != null) step.limit else 0;
    let operation = records.aggregate([], op, step.value);
    if (not (field is string) or not (step.value is string) or not valid_groups(groups) or
        not util.finite_number(limit) or limit < 0 or floor(limit) != limit)
        error("chart: pivot requires field/value, groupby fields, and a nonnegative integer limit")
    else if (operation is error) operation
    else {
        let columns = sort(util.unique_vals([for (row in data) string(row[field])]));
        let selected = if (limit > 0) take(columns, int(limit)) else columns;
        [for (partition in records.group_by(data, groups)) {
            *:records.group_fields(partition.rows[0], groups),
            *:map([for (column in selected) for (item in [column,
                records.aggregate(partition.rows |: string(~[field]) == column, op, step.value)]) item])
        }]
    }
}

pub fn stack_rows(data, step) {
    let field = if (step.stack != null) step.stack else step.field;
    let groups = group_fields(step);
    let output = names(step.as, ["y0", "y1"], "_end");
    let mode = if (step.offset != null) step.offset else "zero";
    if (not (field is string) or not valid_groups(groups) or not valid_names(output) or
        not contains(["zero", "center", "normalize"], mode)) error("chart: invalid stack transform options")
    else if (len(data |: not util.finite_number(~[field])) > 0) error("chart: stack requires finite numeric values")
    else {
        // Private wrapper fields keep user _y0/_y1 and sorting attributes intact.
        let indexed = [for (index, row in data) {index: index, row: row, value: row[field], partition: records.key(row, groups)}];
        let ordered = records.sort_rows(indexed, step.sort, (item) => item.row) |> ~.index;
        let result = stack.apply_stack(indexed, "value", "index", "partition", mode, ordered);
        if (result is error) result else if (len(result |: not util.finite_number(~._y0) or not util.finite_number(~._y1)) > 0)
            error("chart: stack transform extent is not finite")
        else [for (item in result) records.add_field(records.add_field(item.row, output[0], item._y0), output[1], item._y1)]
    }
}

pub fn quantile(data, step) {
    let field = if (step.quantile != null) step.quantile else step.field;
    let groups = group_fields(step);
    let output = names(step.as, ["prob", "value"]);
    let spacing = if (step.step != null) step.step else 0.01;
    let probabilities = if (step.probs != null) step.probs
        else if (util.finite_number(spacing) and spacing > 0 and spacing <= 1) util.number_sequence(spacing / 2.0, 1.0, spacing)
        else error("chart: quantile step must lie in (0,1]");
    if (probabilities is error) probabilities
    else if (not (field is string) or not valid_groups(groups) or not valid_names(output) or
        not (probabilities is array) or len(probabilities |: not util.finite_number(~) or ~ <= 0 or ~ >= 1) > 0)
        error("chart: quantile requires field/groupby/output names and probabilities in (0,1)")
    else [for (partition in records.group_by(data, groups),
        let values = [for (row in partition.rows where util.finite_number(row[field])) row[field]] where len(values) > 0)
        for (probability in probabilities) {
            *:records.group_fields(partition.rows[0], groups),
            *:map([output[0], probability, output[1], math.quantile(values, probability)])
        }]
}

fn impute_keys(data, step) {
    let values = step.keyvals;
    let extra = if (values is map) util.number_sequence(
        if (values.start != null) values.start else 0, values.stop,
        if (values.step != null) values.step else if (values.stop < (if (values.start != null) values.start else 0)) -1 else 1)
        else if (values == null) [] else values;
    if (extra is error) extra else if (not (extra is array)) error("chart: impute keyvals must be an array or sequence")
    else util.unique_vals([for (row in data) row[step.key], *extra])
}

pub fn impute(data, step) {
    let field = if (step.impute != null) step.impute else step.field;
    let groups = group_fields(step);
    let method = if (step.method != null) step.method else "value";
    let frame = if (step.frame != null) step.frame else [null, null];
    let keys = impute_keys(data, step);
    if (keys is error) keys
    else if (not (field is string) or not (step.key is string) or not valid_groups(groups) or
        not contains(["value", "mean", "median", "max", "min"], method) or not window.valid_frame(frame))
        error("chart: invalid impute field, key, groupby, method, or frame")
    else if (len(groups) == 0 and step.keyvals == null) data
    else {
        let partitions = records.group_by([for (index, row in data) {index: index, row: row,
            group: records.key(row, groups)}], ["group"]);
        let result = [for (partition_index, partition in partitions,
            let present = partition.rows |> ~.row[step.key],
            let dense = [*partition.rows, for (index, key in keys where not (key in present)) {
                index: len(data) + partition_index * len(keys) + index,
                row: records.add_field(records.group_fields(partition.rows[0].row, groups), step.key, key)}],
            let ordered = records.sort_rows(dense, {field: step.key}, (item) => item.row))
            for (index, item in ordered,
                let bounds = window.frame_bounds(frame, index, len(ordered)),
                let neighbors = slice(ordered, bounds[0], max([bounds[0], bounds[1] + 1])) |> ~.row,
                let replacement = if (method == "value") (if (parse.has_attribute(step, "value")) step.value else 0)
                    else records.aggregate(neighbors, method, field))
                {index: item.index, row: if (item.row[field] != null) item.row else records.add_field(item.row, field, replacement)}];
        records.sort_rows(result, {field: "index"}) |> ~.row
    }
}

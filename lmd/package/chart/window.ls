// Ordered window calculations retain source order while evaluating each partition.
import records: .records
import parse: .parse
import util: .util

fn operations(step) {
    if (step.window != null) step.window
    else if (step.ops != null) [for (index, op in step.ops)
        {op: op, field: step.fields[index], as: step.as[index], param: step.params[index]}]
    else if (step is element) [for (child in content(step)) parse.attributes(child)]
    else []
}

fn valid_offset(value) => value == null or (util.finite_number(value) and floor(value) == value)

pub fn valid_frame(frame) => frame is array and len(frame) == 2 and
    valid_offset(frame[0]) and valid_offset(frame[1]) and
    (frame[0] == null or frame[1] == null or frame[0] <= frame[1])

pub fn frame_bounds(frame, index, count) => [
    if (frame[0] == null) 0 else max([0, index + int(frame[0])]),
    if (frame[1] == null) count - 1 else min([count - 1, index + int(frame[1])])]

fn validate_operation(entry) {
    let op = entry.op;
    if (not contains(["row_number", "rank", "dense_rank", "percent_rank", "cume_dist", "ntile",
        "lag", "lead", "first_value", "last_value", "nth_value", "count", "sum", "mean", "average",
        "min", "max", "median", "q1", "q3", "variance", "stdev", "distinct", "valid", "missing"], op))
        error("chart: unsupported window operation " ++ string(op))
    else if (contains(["lag", "lead", "ntile", "nth_value"], op) and
        (not valid_offset(entry.param) or (entry.param != null and entry.param < 0) or
            (op == "ntile" and (entry.param == null or entry.param < 1))))
        error("chart: invalid window parameter for " ++ op)
    else if (not contains(["row_number", "rank", "dense_rank", "percent_rank", "cume_dist", "ntile", "count"], op)
        and entry.field == null) error("chart: window operation requires a field: " ++ op)
    else null
}

pub fn evaluate(data, step) {
    let ops = operations(step);
    let frame = if (step.frame != null) step.frame else [null, 0];
    let groups = if (step.groupby != null) step.groupby else [];
    let definitions = records.sort_definitions(step.sort);
    let failure = util.first_error([for (op in ops) validate_operation(op)]);
    if (failure is error) failure
    else if (not (groups is array) or not valid_frame(frame))
        error("chart: invalid window groupby or frame")
    else {
        // Wrappers avoid overwriting user fields and restore order across interleaved partitions.
        let indexed = [for (index, row in data) {index: index, row: row,
            group: records.key(row, groups)}];
        let partitions = records.group_by(indexed, ["group"]);
        // Each row and frame boundary remains its own anchor even for nonreflexive sort keys (S5.1.2).
        let calculated = [for (partition in partitions,
            let ordered = records.sort_rows(partition.rows, definitions, (item) => item.row))
            for (index, item in ordered,
                let ties = if (len(definitions) == 0) [index]
                    else [for (i, candidate in ordered
                        where i == index or records.sort_key(candidate.row, definitions) == records.sort_key(item.row, definitions)) i],
                let frame_range = frame_bounds(frame, index, len(ordered)),
                let bounds = expand_frame(ordered, definitions, frame_range[0], frame_range[1], step.ignore_peers == true),
                let rows = [for (i in bounds[0] to bounds[1] where i >= 0 and i < len(ordered)) ordered[i].row])
                {index: item.index, row: add_results(item.row, ordered, index, ties, rows, definitions, ops, 0)}];
        records.sort_rows(calculated, {field: "index"}) |> ~.row
    }
}

fn expand_frame(ordered, definitions, start, end, ignore) {
    if (ignore or len(definitions) == 0 or start > end) [start, end]
    else {
        let first_key = records.sort_key(ordered[start].row, definitions);
        let last_key = records.sort_key(ordered[end].row, definitions);
        [min([for (i, item in ordered where i == start or records.sort_key(item.row, definitions) == first_key) i]),
            max([for (i, item in ordered where i == end or records.sort_key(item.row, definitions) == last_key) i])]
    }
}

fn add_results(row, ordered, index, ties, frame, definitions, ops, offset) {
    if (offset >= len(ops)) row
    else {
        let entry = ops[offset];
        let output = if (entry.as != null) entry.as else entry.op ++ (if (entry.field != null) "_" ++ entry.field else "");
        let value = calculate(ordered, index, ties, frame, definitions, entry);
        add_results(records.add_field(row, output, value), ordered, index, ties, frame, definitions, ops, offset + 1)
    }
}

fn calculate(ordered, index, ties, frame, definitions, entry) {
    let op = entry.op;
    let n = len(ordered);
    let param = if (entry.param != null) int(entry.param) else 1;
    let rank = min(ties) + 1;
    if (op == "row_number") index + 1
    else if (op == "rank") rank
    else if (op == "dense_rank") (
        if (len(definitions) == 0) index + 1 else
        len(util.unique_vals([for (i in 0 to index) records.sort_key(ordered[i].row, definitions)])))
    else if (op == "percent_rank") (if (n < 2) 0.0 else float(rank - 1) / float(n - 1))
    else if (op == "cume_dist") float(max(ties) + 1) / float(n)
    else if (op == "ntile") int(floor(float(param) * float(rank - 1) / float(n))) + 1
    else if (op == "lag" or op == "lead") (
        let target = index + (if (op == "lead") param else -param),
        if (target < 0 or target >= n) null else ordered[target].row[entry.field])
    else if (op == "first_value") frame[0][entry.field]
    else if (op == "last_value") frame[len(frame) - 1][entry.field]
    else if (op == "nth_value") frame[param][entry.field]
    else records.aggregate(frame, op, entry.field)
}

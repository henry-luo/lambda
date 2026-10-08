// Shared record operations keep transforms, partitions, and sort ties consistent.
import parse: .parse
import util: .util

pub fn add_field(row, field, value) {
    map([for (key, item in parse.attributes(row) where string(key) != field)
        for (pair in [string(key), item]) pair, field, value])
}

pub fn key(row, fields) => [for (field in fields) row[field]]

pub fn group_by(data, fields = []) {
    partition_rows(data, fields, 0, [])
}

fn partition_rows(data, fields, index, partitions) {
    if (index >= len(data)) partitions
    else {
        let value = key(data[index], fields);
        let hits = [for (i, partition in partitions where partition.key == value) i];
        // Insert each row directly: poison keys are nonreflexive (S5.1.2) and cannot be recovered by filtering on equality.
        let updated = if (len(hits) == 0) [*partitions, {key: value, rows: [data[index]]}]
            else [for (i, partition in partitions) if (i == hits[0])
                {*:partition, rows: [*partition.rows, data[index]]} else partition];
        partition_rows(data, fields, index + 1, updated)
    }
}

pub fn group_fields(row, fields) => map([for (field in fields) for (item in [field, row[field]]) item])

pub fn sort_definitions(options) {
    if (options is array) options
    else if (options.field is array) [for (index, field in options.field)
        {field: field, order: if (options.order is array) options.order[index] else options.order}]
    else if (options.field != null) [options]
    else []
}

// Stable sorts from the last key to the first implement mixed-direction ordering (S6.2.3).
pub fn sort_rows(data, options, project = null) {
    sort_keys(data, sort_definitions(options), len(sort_definitions(options)) - 1, project)
}

fn sort_keys(data, keys, index, project) {
    if (index < 0) data
    else sort_keys(sort(data, {
        by: (row) => (if (project is fn) project(row) else row)[keys[index].field],
        dir: if (keys[index].order == "descending") "desc" else "asc"
    }), keys, index - 1, project)
}

pub fn sort_key(row, definitions) => [for (entry in definitions) row[entry.field]]

pub fn aggregate(data, op, field) {
    let values = [for (row in data where util.finite_number(row[field])) float(row[field])];
    if (op == "count") len(data)
    else if (op == "valid") len(values)
    else if (op == "missing") len(data) - len(values)
    else if (op == "distinct") len(util.unique_vals(data |> ~[field]))
    else if (op == "sum") sum(values)
    else if (not contains(["mean", "average", "median", "min", "max", "q1", "q3", "stdev", "variance"], op))
        error("chart: unsupported aggregate " ++ string(op))
    else if (len(values) == 0) null
    else if (op == "mean" or op == "average") avg(values)
    else if (op == "median") math.median(values)
    else if (op == "min") min(values)
    else if (op == "max") max(values)
    else if (op == "q1") math.quantile(values, 0.25)
    else if (op == "q3") math.quantile(values, 0.75)
    else if (op == "stdev") math.sqrt(math.variance(values))
    else math.variance(values)
}

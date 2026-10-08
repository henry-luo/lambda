// chart/stack.ls — Stable, sign-aware stacking for categorical bar and area series.

import util: .util
import records: .records

// Explicit orders may be partial; unlisted observed series retain first appearance.
fn complete_order(values, requested) => util.unique_vals([
    for (value in requested where value in values) value,
    for (value in values) value])

fn inside_out(summaries, index, bottom, top, bottom_sum, top_sum) {
    if (index >= len(summaries)) [*reverse(bottom), *top]
    else {
        let series = summaries[index];
        if (top_sum < bottom_sum) inside_out(summaries, index + 1, bottom,
            [*top, series.group], bottom_sum, top_sum + series.total)
        else inside_out(summaries, index + 1, [*bottom, series.group], top,
            bottom_sum + series.total, top_sum)
    }
}

fn series_order(data, value_field, group_field, position_field, order, positions) {
    let groups = util.unique_vals(data |> ~[group_field]);
    if (order is array) complete_order(groups, order)
    else if (order == null or order == "none") groups
    else if (order == "reverse") reverse(groups)
    else if (order == "inside_out" or order == "ascending" or order == "descending") {
        let vectors = [for (partition in util.group_by(data, group_field))
            {group: partition.key, values: [for (position in positions)
                sum([for (row in partition.items where row[position_field] == position) float(row[value_field])])]}];
        // Rescale only overflowing totals so ordinary equal sums retain their exact sort ties.
        let magnitude = if (len(vectors |: not util.finite_number(sum(~.values))) > 0)
            max([1.0, for (row in data) abs(float(row[value_field]))]) else 1.0;
        let summaries = [for (series in vectors) {group: series.group,
            total: sum(series.values |> ~ / magnitude), peak: util.find_index(series.values, max(series.values))}];
        if (order == "inside_out") inside_out(sort(summaries, {by: (s) => s.peak}), 0, [], [], 0.0, 0.0)
        else {
            let ascending = sort(summaries, {by: (s) => s.total}) |> ~.group;
            // descending reverses the complete order, including equal totals (S6.2.3).
            if (order == "descending") reverse(ascending) else ascending
        }
    } else error("chart: unsupported stack_order " ++ string(order))
}

// Missing samples have zero thickness; duplicate samples add, retaining the first record's attributes.
fn dense_samples(data, value_field, group_field, position_field, positions) {
    [for (partition in util.group_by(data, group_field), position in positions) (
        let peers = partition.items |: ~[position_field] == position,
        let base = if (len(peers) > 0) peers[0] else records.group_fields(partition.items[0], [group_field]),
        records.add_field(records.add_field(base, position_field, position), value_field,
            sum([for (peer in peers) float(peer[value_field])])))]
}

// The weighted slope uses normalized weights to avoid multiplying two large data values.
fn wiggle_slope(previous, current, index, prefix, weighted, total) {
    if (index >= len(current)) weighted
    else {
        let delta = current[index] - previous[index];
        wiggle_slope(previous, current, index + 1, prefix + delta,
            weighted + (prefix + delta / 2.0) * (current[index] / total), total)
    }
}

fn wiggle_offsets(columns, index, offsets) {
    if (index >= len(columns)) offsets
    else {
        let total = sum(columns[index]);
        let slope = if (total > 0) wiggle_slope(columns[index - 1], columns[index], 0, 0.0, 0.0, total) else 0.0;
        wiggle_offsets(columns, index + 1, [*offsets, offsets[index - 1] - slope])
    }
}

fn wiggle_stack(data, value_field, group_field, position_field, order, position_order) {
    let invalid = [for (row in data where not util.finite_number(row[value_field]) or row[value_field] < 0 or
        row[group_field] == null or row[group_field] != row[group_field] or
        not (row[position_field] is string or row[position_field] is datetime or util.finite_number(row[position_field]))) row];
    if (len(invalid) > 0) error("chart: wiggle requires nonnegative finite values and valid series/positions")
    else {
        let values = util.unique_vals(data |> ~[position_field]);
        let positions = if (position_order is array) complete_order(values, position_order) else sort(values);
        let samples = dense_samples(data, value_field, group_field, position_field, positions);
        let groups = series_order(samples, value_field, group_field, position_field,
            if (order == null) "inside_out" else order, positions);
        if (groups is error) groups else if (len(positions) == 0) [] else {
            let partitions = util.group_by(samples, group_field);
            let keys = partitions |> ~.key;
            let ranked = [for (group in groups) partitions[util.find_index(keys, group)].items];
            let columns = [for (column, position in positions) [for (series in ranked) series[column][value_field]]];
            let totals = columns |> sum(~);
            if (len(totals |: not util.finite_number(~)) > 0) error("chart: wiggle stack total is not finite")
            else {
                let offsets = wiggle_offsets(columns, 1, [0.0]);
                let result = [for (partition in partitions) for (column, row in partition.items) (
                    let group = util.find_index(groups, partition.key),
                    let start = offsets[column] + sum(slice(columns[column], 0, group)),
                    {*:row, _y0: start, _y1: start + row[value_field]})];
                if (len(result |: not util.finite_number(~._y0) or not util.finite_number(~._y1)) > 0)
                    error("chart: wiggle stack extent is not finite") else result
            }
        }
    }
}

pub fn apply_stack(data, value_field, group_field, position_field, mode, order = null, position_order = null) {
    if (mode == "wiggle") wiggle_stack(data, value_field, group_field, position_field, order, position_order)
    else {
    let groups = series_order(data, value_field, group_field, position_field, order,
        util.unique_vals(data |> ~[position_field]));
    if (groups is error) groups else {
    let stacked = [for (index, row in data) (
        let group_index = util.find_index(groups, row[group_field]),
        let value = float(row[value_field]),
        // Positive and negative contributions accumulate on opposite sides of zero.
        let start = sum([for (previous_index, previous in data,
            let previous_group = util.find_index(groups, previous[group_field]),
            let previous_value = float(previous[value_field])
            where previous[position_field] == row[position_field] and
                (previous_group < group_index or (previous_group == group_index and previous_index < index)) and
                (previous_value < 0) == (value < 0)) previous_value]),
        {*:row, _y0: start, _y1: start + value}
    )];
    [for (row in stacked) (
        let peers = stacked |: ~[position_field] == row[position_field],
        let extent = [min([0.0, for (peer in peers) peer._y0, for (peer in peers) peer._y1]),
            max([0.0, for (peer in peers) peer._y0, for (peer in peers) peer._y1])],
        let span = extent[1] - extent[0],
        let offset = (extent[0] + extent[1]) / 2.0,
        if (mode == "normalize") {*:row, _y0: if (span > 0) row._y0 / span else 0.0,
            _y1: if (span > 0) row._y1 / span else 0.0}
        else if (mode == "center") {*:row, _y0: row._y0 - offset, _y1: row._y1 - offset}
        else row
    )]
    }
    }
}

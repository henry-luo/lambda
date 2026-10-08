// chart/stack.ls — Stable, sign-aware stacking for categorical bar and area series.

import util: .util

pub fn apply_stack(data, value_field, group_field, position_field, mode, order = null) {
    let groups = if (order is array) order else util.unique_vals(data |> ~[group_field]);
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

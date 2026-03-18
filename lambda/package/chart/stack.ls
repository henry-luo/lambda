// chart/stack.ls — Stacking engine for the chart library
// Computes cumulative y0/y1 offsets for stacked bar and area charts.

import util: .util

// Apply stacking to data
// Returns data with added _y0 and _y1 fields
// mode: "zero" | "normalize" | "center"
pub fn apply_stack(data, y_field, group_field, x_field, mode) {
    let groups = util.unique_vals(data | ~[group_field])
    let stacked = for (d in data) (
        let g_idx = find_index(groups, d[group_field]),
        let y0 = sum_preceding(data, x_field, d[x_field], group_field, groups, y_field, g_idx),
        let y1 = y0 + float(d[y_field]),
        add_stack_fields(d, y0, y1)
    )
    if (mode == "normalize") normalize_stack(stacked, x_field)
    else if (mode == "center") center_stack(stacked, x_field)
    else stacked
}

fn find_index(arr, val) {
    let matches = (for (i in 0 to (len(arr) - 1))
        if (arr[i] == val) i else null) that (~ != null)
    if (len(matches) > 0) matches[0] else 0
}

fn sum_preceding(data, x_field, x_val, group_field, groups, y_field, g_idx) {
    if (g_idx <= 0) 0.0
    else sum(for (gi in 0 to (g_idx - 1))
        get_group_y(data, x_field, x_val, group_field, groups[gi], y_field))
}

fn get_group_y(data, x_field, x_val, group_field, group_val, y_field) {
    let by_x = data that (~[x_field] == x_val)
    let matches = by_x that (~[group_field] == group_val)
    if (len(matches) > 0) float(matches[0][y_field]) else 0.0
}

fn add_stack_fields(row, y0, y1) {
    let extra = {_y0: y0, _y1: y1}
    {*:row, *:extra}
}

fn get_x_max_y1(stacked, x_field, x_val) {
    let by_x = stacked that (~[x_field] == x_val)
    let y1_vals = by_x | float(~["_y1"])
    if (len(y1_vals) > 0) max(y1_vals) else 0.0
}

fn normalize_stack(stacked, x_field) {
    for (d in stacked) (
        let total = get_x_max_y1(stacked, x_field, d[x_field]),
        let ny0 = if (total > 0.0) float(d["_y0"]) / total else 0.0,
        let ny1 = if (total > 0.0) float(d["_y1"]) / total else 0.0,
        let extra = {_y0: ny0, _y1: ny1},
        {*:d, *:extra}
    )
}

fn center_stack(stacked, x_field) {
    for (d in stacked) (
        let total = get_x_max_y1(stacked, x_field, d[x_field]),
        let offset = total / 2.0,
        let extra = {_y0: float(d["_y0"]) - offset, _y1: float(d["_y1"]) - offset},
        {*:d, *:extra}
    )
}

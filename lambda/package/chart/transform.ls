// chart/transform.ls — Data transform operations for the chart library
// Applied in order before encoding: filter, aggregate, calculate, bin, sort, fold, flatten

import util: .lambda.package.chart.util

// ============================================================
// Public API: Apply a sequence of transforms to data
// ============================================================

pub fn apply_transforms(data, transform_el) {
    if (not transform_el) data
    else (let n = len(transform_el),
          if (n == 0) data
          else apply_transform_list(data, transform_el, 0, n))
}

// recursive helper: apply transforms one at a time
fn apply_transform_list(data, transforms, index: int, count: int) {
    if (index >= count) data
    else (let t = transforms[index],
          let tag = name(t),
          let result = if (tag == 'filter') apply_filter(data, t)
              else if (tag == 'sort') apply_sort(data, t)
              else if (tag == 'aggregate') apply_aggregate(data, t)
              else if (tag == 'calculate') apply_calculate(data, t)
              else if (tag == 'bin') apply_bin(data, t)
              else if (tag == 'fold') apply_fold(data, t)
              else if (tag == 'flatten') apply_flatten(data, t)
              else data,
          apply_transform_list(result, transforms, index + 1, count))
}

// ============================================================
// Filter transform
// ============================================================

fn apply_filter(data, filter_el) {
    let field = filter_el.field;
    let op = filter_el.op;
    let value = filter_el.value;

    if field and op and value != null {
        (if (op == "==") (data that ~[field] == value)
         else if (op == "!=") (data that ~[field] != value)
         else if (op == ">") (data that float(~[field]) > float(value))
         else if (op == ">=") (data that float(~[field]) >= float(value))
         else if (op == "<") (data that float(~[field]) < float(value))
         else if (op == "<=") (data that float(~[field]) <= float(value))
         else data)
    } else {
        data
    }
}

// ============================================================
// Sort transform
// ============================================================

fn apply_sort(data, sort_el) {
    let field_name = sort_el.field;
    let order = if (sort_el.order) sort_el.order else "ascending";
    if order == "descending" {
        for (d in data order by d[field_name] desc) d
    } else {
        for (d in data order by d[field_name]) d
    }
}

// ============================================================
// Aggregate helper (public for external use)
// ============================================================

pub fn compute_agg(data, op, field) {
    if (op == "count") len(data)
    else if (op == "sum") sum(data | float(~[field]))
    else if (op == "mean" or op == "average") avg(data | float(~[field]))
    else if (op == "median") median(data | float(~[field]))
    else if (op == "min") min(data | float(~[field]))
    else if (op == "max") max(data | float(~[field]))
    else if (op == "distinct") len(util.unique_vals(data | ~[field]))
    else if (op == "q1") quantile(data | float(~[field]), 0.25)
    else if (op == "q3") quantile(data | float(~[field]), 0.75)
    else if (op == "stdev") sqrt(variance(data | float(~[field])))
    else if (op == "variance") variance(data | float(~[field]))
    else 0
}

// ============================================================
// Aggregate transform
// ============================================================

fn apply_aggregate(data, agg_el) {
    let n = len(agg_el);
    let group_fields = for (i in 0 to (n - 1),
                            let child = agg_el[i]
                            where child and name(child) == 'group') child.field;
    let agg_specs = for (i in 0 to (n - 1),
                         let child = agg_el[i]
                         where child and name(child) == 'agg')
                     {op: child.op, field: child.field, as: child.as};
    do_aggregate(data, group_fields, agg_specs)
}

fn do_aggregate(data, group_fields, agg_specs) {
    if len(group_fields) == 0 {
        [build_agg_row(data, [], agg_specs)]
    } else {
        let gkeys = util.unique_vals(data | group_key_str(~, group_fields));
        for (gk in gkeys) (
            let items = data that group_key_str(~, group_fields) == gk,
            build_agg_row(items, group_fields, agg_specs)
        )
    }
}

fn group_key_str(row, fields) {
    let parts = for (f in fields) string(row[f]);
    str_join(parts, "|||")
}

fn build_agg_row(items, group_fields, agg_specs) {
    let group_pairs = for (f in group_fields) for (x in [f, items[0][f]]) x;
    let agg_pairs = for (spec in agg_specs)
        for (x in [spec.as, float(compute_agg(items, spec.op, spec.field))]) x;
    map([*group_pairs, *agg_pairs])
}

// ============================================================
// Calculate transform
// ============================================================

fn apply_calculate(data, calc_el) {
    let as_field = calc_el.as;
    let op = calc_el.op;
    let field1 = calc_el.field1;
    let field2 = calc_el.field2;
    let field = if (calc_el.field) calc_el.field else field1;
    if (not as_field) data
    else for (d in data) add_field(d, as_field, calc_value(d, op, field, field2))
}

fn calc_value(d, op, field, field2) {
    if (not op or op == "copy") d[field]
    else if (op == "string") string(d[field])
    else if (op == "float") float(d[field])
    else if (op == "int") int(d[field])
    else if (op == "+") float(d[field]) + float(if (field2) d[field2] else 0)
    else if (op == "-") float(d[field]) - float(if (field2) d[field2] else 0)
    else if (op == "*") float(d[field]) * float(if (field2) d[field2] else 1)
    else if (op == "/") float(d[field]) / float(if (field2) d[field2] else 1)
    else null
}

// ============================================================
// Bin transform
// ============================================================

fn apply_bin(data, bin_el) {
    let field = bin_el.field;
    let as_field = if (bin_el.as) bin_el.as else field ++ "_bin";
    let maxbins = if (bin_el.maxbins) bin_el.maxbins else 10;
    let step_override = bin_el.step;
    if not field or len(data) == 0 {
        data
    } else {
        let values = data | float(~[field]);
        let vmin = min(values);
        let vmax = max(values);
        let range_span = vmax - vmin;
        let step = if (step_override) float(step_override)
                   else util.nice_num(range_span / float(maxbins), true);
        let as_end = as_field ++ "_end";
        for (d in data) (
            let v = float(d[field]),
            let bin_start = floor(v / step) * step,
            let bin_end = bin_start + step,
            add_field(add_field(d, as_field, bin_start), as_end, bin_end)
        )
    }
}

// ============================================================
// Fold transform (unpivot wide to long)
// ============================================================

fn apply_fold(data, fold_el) {
    let fields = fold_el.fields;
    let as_names = if (fold_el.as) fold_el.as else ["key", "value"];
    if not fields or len(fields) == 0 {
        data
    } else {
        let key_name = as_names[0];
        let val_name = if (len(as_names) > 1) as_names[1] else "value";
        for (d in data) for (f in fields)
            add_field(add_field(d, key_name, f), val_name, d[f])
    }
}

// ============================================================
// Flatten transform
// ============================================================

fn apply_flatten(data, flat_el) {
    let fields = flat_el.fields;
    let as_names = flat_el.as;
    if not fields or len(fields) == 0 {
        data
    } else if len(fields) == 1 {
        let src_field = fields[0];
        let dst_field = if (as_names and len(as_names) > 0) as_names[0] else src_field;
        for (d in data) for (val in d[src_field])
            add_field(d, dst_field, val)
    } else {
        data
    }
}

// ============================================================
// Helper: add a field to a map
// ============================================================

fn add_field(row, field_name, value) {
    let existing_pairs = for (k, v at row) for (x in [k, v]) x;
    map([*existing_pairs, field_name, value])
}

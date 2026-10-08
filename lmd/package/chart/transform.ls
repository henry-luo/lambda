// chart/transform.ls — Data transform operations for the chart library
// Applied in order before encoding: filter, aggregate, calculate, bin, sort, fold, flatten

import util: .util
import parse: .parse

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
    if (data is error or index >= count) data
    else (let t = transforms[index],
          let tag = if (t is element) string(name(t)) else t.type,
          let result = if (t is error) t
              else if (tag == "filter") apply_filter(data, t)
              else if (tag == "sort") apply_sort(data, t)
              else if (tag == "aggregate") apply_aggregate(data, t)
              else if (tag == "calculate") apply_calculate(data, t)
              else if (tag == "bin") apply_bin(data, t)
              else if (tag == "fold") apply_fold(data, t)
              else if (tag == "flatten") apply_flatten(data, t)
              else error("chart: unsupported transform " ++ string(tag)),
          apply_transform_list(result, transforms, index + 1, count))
}

// ============================================================
// Filter transform
// ============================================================

fn apply_filter(data, filter_el) {
    data |: parse.test_predicate(filter_el, ~)
}

// ============================================================
// Sort transform
// ============================================================

fn apply_sort(data, sort_el) {
    let field_name = sort_el.field;
    let order = if (sort_el.order) sort_el.order else "ascending";
    if order == "descending" {
        [for (d in data order by d[field_name] desc) d]
    } else {
        [for (d in data order by d[field_name]) d]
    }
}

// ============================================================
// Aggregate helper (public for external use)
// ============================================================

pub fn compute_agg(data, op, field) {
    if (op == "count") len(data)
    else if (op == "sum") sum(data |> float(~[field]))
    else if (op == "mean" or op == "average") avg(data |> float(~[field]))
    else if (op == "median") math.median(data |> float(~[field]))
    else if (op == "min") min(data |> float(~[field]))
    else if (op == "max") max(data |> float(~[field]))
    else if (op == "distinct") len(util.unique_vals(data |> ~[field]))
    else if (op == "q1") math.quantile(data |> float(~[field]), 0.25)
    else if (op == "q3") math.quantile(data |> float(~[field]), 0.75)
    else if (op == "stdev") math.sqrt(math.variance(data |> float(~[field])))
    else if (op == "variance") math.variance(data |> float(~[field]))
    else 0
}

// ============================================================
// Aggregate transform
// ============================================================

fn apply_aggregate(data, agg_el) {
    let n = len(agg_el);
    let group_fields = [for (i in 0 to (n - 1),
                            let child = agg_el[i]
                            where child and name(child) == 'group') child.field];
    let agg_specs = [for (i in 0 to (n - 1),
                         let child = agg_el[i]
                         where child and name(child) == 'agg')
                     {op: child.op, field: child.field, as: child.as}];
    do_aggregate(data, group_fields, agg_specs)
}

fn do_aggregate(data, group_fields, agg_specs) {
    if len(group_fields) == 0 {
        [build_agg_row(data, [], agg_specs)]
    } else {
        let gkeys = util.unique_vals(data |> group_key(~, group_fields));
        [for (gk in gkeys) (
            let items = data |: group_key(~, group_fields) == gk,
            build_agg_row(items, group_fields, agg_specs)
        )]
    }
}

fn group_key(row, fields) => [for (field in fields) row[field]]

fn build_agg_row(items, group_fields, agg_specs) {
    let group_pairs = [for (f in group_fields) for (x in [f, items[0][f]]) x];
    let agg_pairs = [for (spec in agg_specs)
        for (x in [spec.as, float(compute_agg(items, spec.op, spec.field))]) x];
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
    else [for (d in data) add_field(d, as_field,
        if (calc_el.expression is fn) calc_el.expression(d) else calc_value(d, op, field, field2))]
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
        let values = data |> float(~[field]);
        let vmin = min(values);
        let vmax = max(values);
        let range_span = vmax - vmin;
        let step = if (step_override != null) float(step_override)
                   else if (range_span == 0) 1.0 else util.nice_num(range_span / float(maxbins), true);
        let as_end = if (bin_el.as_end != null) bin_el.as_end else as_field ++ "_end";
        if (step <= 0 or maxbins <= 0) error("chart: bin step and maxbins must be positive")
        else [for (d in data) (
            let v = float(d[field]),
            let bin_start = floor(v / step) * step,
            let bin_end = bin_start + step,
            add_field(add_field(d, as_field, bin_start), as_end, bin_end)
        )]
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
        [for (d in data) for (f in fields)
            add_field(add_field(d, key_name, f), val_name, d[f])]
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
    } else {
        // Zip parallel arrays to the longest length, padding shorter fields with null.
        [for (row in data,
              let count = max([for (field in fields) len(row[field])]))
            for (index in 0 to (count - 1))
                flatten_fields(row, row, fields, as_names, index, 0)]
    }
}

fn flatten_fields(source, row, fields, as_names, index, field_index) {
    if (field_index >= len(fields)) row
    else flatten_fields(source, add_field(row,
        if (as_names != null and field_index < len(as_names)) as_names[field_index] else fields[field_index],
        source[fields[field_index]][index]), fields, as_names, index, field_index + 1)
}

// ============================================================
// Helper: add a field to a map
// ============================================================

pub fn add_field(row, field_name, value) {
    let existing_pairs = [for (k, v in parse.attributes(row) where string(k) != field_name) for (x in [string(k), v]) x];
    map([*existing_pairs, field_name, value])
}

// Encoding shorthand is normalized before any renderer or composition derives its scales.
pub fn prepare_encoding(data, encoding, partition_fields = []) {
    let enc = if (encoding != null) encoding else {};
    let binned_data = apply_transforms(data, [for (key, channel in enc where channel.bin and channel.field)
        <bin field: channel.field, as: channel.field ++ "_bin",
            maxbins: if (channel.bin.maxbins != null) channel.bin.maxbins else 10,
            step: channel.bin.step>]);
    let histogram = enc.x.bin and enc.y.aggregate == "count";
    let binned_enc = map([for (key, channel in enc) for (item in [string(key),
        if (channel.bin and channel.field) {*:channel, field: channel.field ++ "_bin", bin: null,
            dtype: if (histogram and string(key) == "x") "ordinal" else channel.dtype}
        else channel]) item]);
    let aggregates = [for (key, channel in binned_enc where channel.aggregate != null)
        {channel: string(key), field: channel.field, op: channel.aggregate,
            as: if (channel.aggregate == "count") "_count" else channel.field ++ "_" ++ channel.aggregate}];
    let groups = util.unique_vals([*partition_fields, for (key, channel in binned_enc
        where channel.field != null and channel.aggregate == null) channel.field]);
    let summarized = if (binned_data is error) binned_data
        else if (len(aggregates) > 0) do_aggregate(binned_data, groups, aggregates) else binned_data;
    let normalized = map([for (key, channel in binned_enc,
        let matches = [for (spec in aggregates where spec.channel == string(key)) spec])
        for (item in [string(key), if (len(matches) > 0)
            {*:channel, field: matches[0].as, dtype: "quantitative", aggregate: null} else channel]) item]);
    {data: summarized, encoding: normalized}
}

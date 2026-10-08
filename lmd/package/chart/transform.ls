// chart/transform.ls — Data transform operations for the chart library
// Applied in source order before encoding, including analytical and join operations.

import util: .util
import parse: .parse
import records: .records
import window: .window
import source: .source
import statistics: .statistics
import calendar: .calendar

// ============================================================
// Public API: Apply a sequence of transforms to data
// ============================================================

pub fn apply_transforms(data, transform_el, datasets = null) {
    if (not transform_el) data
    else (let steps = if (transform_el is element) content(transform_el) else transform_el,
          let n = len(steps),
          if (n == 0) data
          else apply_transform_list(data, steps, 0, n, datasets))
}

// recursive helper: apply transforms one at a time
fn apply_transform_list(data, transforms, index: int, count: int, datasets) {
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
              else if (tag == "window") window.evaluate(data, t)
              else if (tag == "lookup") apply_lookup(data, t, datasets)
              else if (tag == "density" or tag == "kde") statistics.density(data, t)
              else if (tag == "regression") statistics.regression(data, t)
              else if (tag == "loess") statistics.loess(data, t)
              else if (tag == "timeunit") apply_timeunit(data, t)
              else error("chart: unsupported transform " ++ string(tag)),
          apply_transform_list(result, transforms, index + 1, count, datasets))
}

fn apply_timeunit(data, step) {
    if (step.field == null or step.as == null) error("chart: timeunit requires field and as")
    else {
        let values = [for (row in data) calendar.time_unit(row[step.field],
            if (step.unit != null) step.unit else step.time_unit, if (step.timezone != null) step.timezone else 0)];
        let failure = util.first_error(values);
        if (failure is error) failure else [for (index, row in data) records.add_field(row, step.as, values[index])]
    }
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
    records.sort_rows(data, if (sort_el.sort != null) sort_el.sort else sort_el)
}

// ============================================================
// Aggregate helper (public for external use)
// ============================================================

pub fn compute_agg(data, op, field) {
    records.aggregate(data, op, field)
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
    let summaries = if len(group_fields) == 0 {
        [build_agg_row(data, [], agg_specs)]
    } else {
        [for (partition in records.group_by(data, group_fields))
            build_agg_row(partition.rows, group_fields, agg_specs)]
    };
    let failure = util.first_error(summaries);
    if (failure is error) failure else summaries
}

fn build_agg_row(items, group_fields, agg_specs) {
    let values = [for (spec in agg_specs) compute_agg(items, spec.op, spec.field)];
    let failure = util.first_error(values);
    if (failure is error) failure
    else {*:records.group_fields(items[0], group_fields),
        *:map([for (index, spec in agg_specs) for (x in [spec.as, values[index]]) x])}
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
    records.add_field(row, field_name, value)
}

fn apply_lookup(data, step, datasets) {
    let from = step.from;
    let foreign = source.resolve(null, if (from.data != null) from.data else from, datasets);
    let field = if (step.lookup != null) step.lookup else step.field;
    let key = if (from.key != null) from.key else step.key;
    let fields = if (from.fields != null) from.fields else step.fields;
    let names = if (step.as is string) [step.as] else if (step.as != null) step.as else fields;
    if (foreign is error) foreign
    else if (not (foreign is array) or field == null or key == null or
        (fields == null and (names == null or len(names) != 1)) or
        (fields != null and (not (fields is array) or len(names) != len(fields))))
        error("chart: lookup requires primary/foreign keys, array data, and matching output fields")
    else [for (row in data,
        let matches = foreign |: ~[key] == row[field],
        let found = matches[0])
        lookup_fields(row, found, fields, names, step.default, 0)]
}

fn lookup_fields(row, found, fields, names, fallback, index) {
    if (index >= len(names)) row
    else lookup_fields(add_field(row, names[index],
        if (found == null) fallback else if (fields == null) found else found[fields[index]]),
        found, fields, names, fallback, index + 1)
}

// Encoding shorthand is normalized before any renderer or composition derives its scales.
pub fn prepare_encoding(data, encoding, partition_fields = []) {
    let temporal_data = apply_transforms(data, [for (key, channel in encoding where channel.time_unit != null and channel.field != null)
        {type: "timeunit", field: channel.field, unit: channel.time_unit, as: channel.field ++ "_time_" ++ string(key),
            timezone: if (channel.scale.timezone != null) channel.scale.timezone else 0}]);
    let temporal_encoding = map([for (key, channel in encoding) for (item in [string(key),
        if (channel.time_unit != null and channel.field != null) {*:channel,
            field: channel.field ++ "_time_" ++ string(key), time_unit: null, _temporal: true,
            format: if (channel.format != null) channel.format else calendar.unit_format(channel.time_unit),
            scale: if (parse.option_enabled(channel, "scale")) {*:parse.attributes(channel.scale),
                timezone: calendar.unit_zone(channel.time_unit, if (channel.scale.timezone != null) channel.scale.timezone else 0)} else channel.scale}
        else channel]) item]);
    // Aggregate sort fields may disappear during encoding aggregation; resolve their order first.
    let enc = map([for (key, channel in temporal_encoding) for (item in [string(key),
        if (channel.field != null and channel.sort is map)
            {*:channel, sort: records.categories(temporal_data, channel.field, channel.sort)} else channel]) item]);
    let sort_error = util.first_error([for (key, channel in enc) channel.sort]);
    let binned_data = apply_transforms(temporal_data, [for (key, channel in enc where channel.bin and channel.field)
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
    let summarized = if (temporal_data is error) temporal_data else if (sort_error is error) sort_error else if (binned_data is error) binned_data
        else if (len(aggregates) > 0) do_aggregate(binned_data, groups, aggregates) else binned_data;
    let normalized = map([for (key, channel in binned_enc,
        let matches = [for (spec in aggregates where spec.channel == string(key)) spec])
        for (item in [string(key), if (len(matches) > 0)
            {*:channel, field: matches[0].as, dtype: "quantitative", aggregate: null} else channel]) item]);
    {data: summarized, encoding: normalized}
}

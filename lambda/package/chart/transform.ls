// chart/transform.ls — Data transform operations for the chart library
// Applied in order before encoding: filter, aggregate, calculate, bin, sort, fold, flatten
// NOTE: calculate, aggregate, bin, fold, flatten require computed key support
//       which Lambda does not yet have. These return data unchanged for now.

import util: .lambda.package.chart.util

// ============================================================
// Apply a sequence of transforms to data
// ============================================================

// apply all transforms from a <transform> element to data
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
              else data,
          apply_transform_list(result, transforms, index + 1, count))
}

// ============================================================
// Filter transform
// ============================================================

// filter data rows where the expression evaluates to true
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
        sort(data, (a, b) => if (a[field_name] > b[field_name]) -1 else if (a[field_name] < b[field_name]) 1 else 0)
    } else {
        sort(data, (a, b) => if (a[field_name] < b[field_name]) -1 else if (a[field_name] > b[field_name]) 1 else 0)
    }
}

// ============================================================
// Aggregate helper
// ============================================================

// compute a single aggregation value from data
pub fn compute_agg(data, op: string, field) {
    if (op == "count") len(data)
    else if (op == "sum") sum(data | float(~[field]))
    else if (op == "mean") avg(data | float(~[field]))
    else if (op == "median") median(data | float(~[field]))
    else if (op == "min") min(data | float(~[field]))
    else if (op == "max") max(data | float(~[field]))
    else if (op == "distinct") len(util.unique_vals(data | ~[field]))
    else 0
}

// Stable sorting evaluates CSL keys once, with absent values after present values.
import c: .common
import e: .eval
import names: .names

fn key_value(node, context) {
    if (node.macro != null) lower(e.render(<text macro: node.macro>, context).text)
    else {
        let value = e.variable(node.variable, context)
        if (value == null) null
        else if (value is map and value["date-parts"] != null) {
            let parts = value["date-parts"][0]
            join([for (part in parts) {
                let digits = string(part)
                join([for (i in 1 to max([0, 6 - len(digits)])) "0"], "") ++ digits
            }], "-")
        } else if (value is array or value is list)
            lower(names.render(<names variable: node.variable, <name>>, value, context).text)
        else {
            let numeric = int(value) ^ { null }
            if (numeric != null) numeric else lower(c.text(value))
        }
    }
}

fn comparison(left, right, keys, index) {
    if (index >= len(keys)) 0
    else {
        let a = left.keys[index]
        let b = right.keys[index]
        if (a == b) comparison(left, right, keys, index + 1)
        else if (a == null or a == "") 1
        else if (b == null or b == "") -1
        else {
            let less = if (a is int and b is int) a < b else string(a) < string(b)
            let direction = if (keys[index].sort == "descending") -1 else 1;
            (if (less) -1 else 1) * direction
        }
    }
}

fn merge(left, right, keys, li, ri, result) {
    if (li >= len(left)) result ++ slice(right, ri, len(right))
    else if (ri >= len(right)) result ++ slice(left, li, len(left))
    else if (comparison(left[li], right[ri], keys, 0) <= 0)
        merge(left, right, keys, li + 1, ri, result ++ [left[li]])
    else merge(left, right, keys, li, ri + 1, result ++ [right[ri]])
}

fn merge_sort(values, keys) {
    if (len(values) < 2) values
    else {
        let middle = int(floor(len(values) / 2))
        merge(merge_sort(slice(values, 0, middle), keys),
            merge_sort(slice(values, middle, len(values)), keys), keys, 0, 0, [])
    }
}

pub fn references(references, node, style, locales) {
    let keys = c.children(c.child(node, "sort"), "key")
    if (len(keys) == 0) references
    else {
        let decorated = [for (reference in references) {
            let context = {*:e.context(style, locales, reference), sorting: true}
            {reference: reference, keys: [for (key in keys) key_value(key,
                {*:context, options: {*:context.options, initialize: "false",
                    'et-al-min': key["names-min"], 'et-al-use-first': key["names-use-first"],
                    'et-al-use-last': key["names-use-last"]}})]}
        }];
        [for (part in merge_sort(decorated, keys)) part.reference]
    }
}

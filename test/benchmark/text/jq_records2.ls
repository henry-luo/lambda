// Text benchmark jq_records, typed: jq/records.jq written as a typed Lambda
// query over the
// same jq/orders.json. Each binding of round_digest is the same computation
// as in the filter, keeping jq's object key order and its left-to-right float
// sums (vibe/impl/Lambda_Impl_Jq_Tests.md §5.6).
import ~~.jq_query_typed

let rounds: int = 15

// map(select(.status != "pending") | .user.region //= "unknown"
//   | .total = (total | cents) | .skus = ([.items[].sku] | unique)
//   | del(.note) | with_entries(select(.value != null)))
pn transform_order(o: map) map {
    var user: map = o.user
    user["region"] = jq_alt(user["region"], "unknown")
    // def total: reduce .items[] as $it (0; . + $it.qty * $it.price)
    // jq numbers are doubles, so the float sums start from 0.0
    var total: float = 0.0
    var i: int = 0
    while (i < len(o.items)) {
        total = total + o.items[i].qty * o.items[i].price
        i = i + 1
    }
    let skus: string[] = jq_unique_strings([for (it in o.items) string(it.sku)])
    // .total and .skus land after the input keys; then note and nulls go
    var r: map = {}
    for (k, v at o) {
        let key: string = string(k)
        let value = if (key == "user") user else v
        if (key != "note" and value != null) { r[key] = value }
    }
    r["total"] = int(floor(total * 100))
    r["skus"] = skus
    r
}

// group_by(.user.region) | map({region, n, revenue, top, paid}) | map(digest) | add
fn region_digest(region: string, grp) int {
    let revenue: int = sum([for (o in grp) o.total])
    let top: int = sort(grp, (o) => [0 - o.total, o.id])[0].id
    let paid: int = len([for (o in grp where o.status == "paid") o])
    len(grp) * 7 + revenue + top * 3 + paid + len(region)
}

// reduce (.[] | .items[]) as $it ({}; .[$it.sku] += $it.qty) | to_entries
// | sort_by([0 - .value, .key]) | .[:5] | from_entries | to_entries
// | map(.value + (.key | explode | add)) | add
pn top_skus_digest(orders: array) int {
    var counts: map = {}
    var i: int = 0
    while (i < len(orders)) {
        let items = orders[i].items
        var j: int = 0
        while (j < len(items)) {
            let sku: string = items[j].sku
            counts[sku] = jq_alt(counts[sku], 0) + items[j].qty
            j = j + 1
        }
        i = i + 1
    }
    let entries = [for (k, v at counts) {key: string(k), value: v}]
    let ranked: array = sort(entries, (e) => [0 - e.value, e.key]) ^ { [] }
    let top5: array = jq_slice_array(ranked, null, 5)
    var top_map: map = {}
    var t: int = 0
    while (t < len(top5)) {
        top_map[top5[t].key] = top5[t].value
        t = t + 1
    }
    sum([for (k, v at top_map) v + jq_codepoint_sum(string(k))])
}

// [foreach .[] as $o (0; . + $o.total; . % 1000003)] | last
pn running_last(orders: array) int {
    var acc: int = 0
    var outs: int[] = []
    var i: int = 0
    while (i < len(orders)) {
        acc = acc + orders[i].total
        outs.push(acc % 1000003)
        i = i + 1
    }
    outs[len(outs) - 1]
}

// (.. | objects | select(has("qty")) | .qty) |= . * 2
pn double_qty(v) {
    if (v is array) {
        var out: array = []
        var i: int = 0
        while (i < len(v)) {
            out.push(double_qty(v[i]))
            i = i + 1
        }
        return out
    }
    if (v is map) {
        var r: map = {}
        let keys: string[] = [for (k at v) string(k)]
        var j: int = 0
        while (j < len(keys)) {
            let child = double_qty(v[keys[j]])
            r[keys[j]] = if (keys[j] == "qty") child * 2 else child
            j = j + 1
        }
        return r
    }
    v
}

// [.. | numbers], in jq's pre-order
pn collect_numbers(v, var out: array) {
    if (v is int or v is float) {
        out.push(v)
        return null
    }
    if (v is array) {
        var i: int = 0
        while (i < len(v)) {
            collect_numbers(v[i], out)
            i = i + 1
        }
        return null
    }
    if (v is map) {
        let keys: string[] = [for (k at v) string(k)]
        var j: int = 0
        while (j < len(keys)) {
            collect_numbers(v[keys[j]], out)
            j = j + 1
        }
    }
    return null
}

// add over numbers: left to right, as jq sums the array
pn sum_in_order(numbers: array) float {
    var total: float = 0.0
    var i: int = 0
    while (i < len(numbers)) {
        total = total + numbers[i]
        i = i + 1
    }
    total
}

fn tag_pair(tags: array) string => jq_alt(jq_at(tags, 0), "-") ++ "/" ++ jq_alt(jq_at(tags, 1), "-")

pn round_digest(orders_in: array, r: int) int {
    let orders: array = [for (o in orders_in where o.status != "pending") transform_order(o)]
    let by_region: int = sum([for (o in orders group by o.user.region as region into g order by g.region)
        region_digest(g.region, content(g))])
    let top_skus: int = top_skus_digest(orders)
    let running: int = running_last(orders)
    let csvlen: int = len(join([for (o in orders) string(o.id) ++ "," ++ o.user.name ++ "," ++ string(o.total)], "\n"))
    var paths: array = []
    jq_collect_paths(jq_slice_array(orders, null, 200), [], true, paths)
    let npaths: int = len(paths)
    var numbers: array = []
    collect_numbers(double_qty(jq_slice_array(orders, null, 200)), numbers)
    let doubled: int = int(floor(sum_in_order(numbers)))
    let tagpairs: int = len(jq_unique_strings([for (o in orders) tag_pair(o.user.tags)]))
    let splits: int = sum([for (o in orders) len(split(upper(o.user.name), "R"))])
    // tojson | fromjson (Lambda's JSON writer has no compact mode, so the text
    // is indented; the parsed values are the same)
    let roundtrip: int = sum([for (o in jq_slice_array(orders, null, 300)) (parse(format(o, 'json'), 'json') ^ { null }).id])
    let parsed: int = sum([for (o in orders) jq_tonumber_or(jq_slice_string(o.user.name, 4, null), 0)])
    // {} | bump(.a) | bump(.a) | bump(.b) | .a * 10 + .b
    var c: map = {}
    c["a"] = jq_alt(c["a"], 0) + 1
    c["a"] = jq_alt(c["a"], 0) + 1
    c["b"] = jq_alt(c["b"], 0) + 1
    let counts: int = c["a"] * 10 + c["b"];
    (by_region + top_skus + running + csvlen + npaths + doubled + tagpairs +
        splits + roundtrip + parsed + counts + r) % jq_modulus
}

pn main() {
    let orders_in: array = input("test/benchmark/text/jq/orders.json", 'json')^
    let t0 = clock()
    var checksum: int = 0
    var r: int = 0
    while (r < rounds) {
        checksum = (checksum * 31 + round_digest(orders_in, r)) % jq_modulus
        r = r + 1
    }
    let t1 = clock()
    if (checksum == 878885883) {
        print("jq_records: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("jq_records: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((t1 - t0) * 1000.0) ++ "\n")
}

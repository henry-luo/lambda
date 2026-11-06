// D4.3.1/S12.2.1v3: inferred shapes are observations, never write contracts.
pn replace(var value, next) { value = next }
pn add(var group, amount) { group.total = group.total + amount }
pn main() {
    var table = {keys: null, vals: null}
    table.keys = [1, 2]
    table.vals = ["a", "b"]
    print([table.keys, table.vals]); print("\n")
    var value = null
    value = {total: 3}
    add(value, 4)^
    print(value.total); print("\n")
    var groups = {api: {total: 1}, worker: {total: 2}}
    add(groups.api, 5)^
    add(groups.worker, 7)^
    print([groups.api.total, groups.worker.total]); print("\n")
    var scalar = 1
    replace(scalar, "text")^
    print([scalar is string, scalar]); print("\n")
    var dynamic = {}
    dynamic["a"] = [3, 4]
    dynamic['b'] = 5
    print([dynamic.a, dynamic.b]); print("\n")
}

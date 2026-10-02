include("../common.jl")

# chained buckets and rehashing retain hash-map.js's actual one-bucket start.
mutable struct MapEntry
    key::Int
    value::Int
    next::Union{Nothing,MapEntry}
end
mutable struct BenchmarkMap
    buckets::Vector{Union{Nothing,MapEntry}}
    size::Int
    threshold::Int
end
BenchmarkMap() = BenchmarkMap([nothing], 0, 0)
function map_put!(table, key, value)
    at = (key & (length(table.buckets) - 1)) + 1
    entry = table.buckets[at]
    while entry !== nothing
        if entry.key == key; entry.value = value; return; end
        entry = entry.next
    end
    table.buckets[at] = MapEntry(key, value, table.buckets[at])
    table.size += 1
    if table.size > table.threshold
        grown = Vector{Union{Nothing,MapEntry}}(nothing, length(table.buckets) * 2)
        for head in table.buckets
            entry = head
            while entry !== nothing
                next = entry.next
                at = (entry.key & (length(grown) - 1)) + 1
                entry.next = grown[at]; grown[at] = entry; entry = next
            end
        end
        table.buckets = grown
        table.threshold = div(length(grown) * 3, 4)
    end
end
function map_get(table, key)
    entry = table.buckets[(key & (length(table.buckets) - 1)) + 1]
    while entry !== nothing
        entry.key == key && return entry.value
        entry = entry.next
    end
    error("missing key: $key")
end
run_benchmark(()->nothing, (io, state)->begin
    count, table = 90000, BenchmarkMap()
    for key in 0:count-1; map_put!(table, key, 42); end
    total = 0
    for _ in 1:5, key in 0:count-1; total += map_get(table, key); end
    key_total = value_total = 0
    for head in table.buckets
        entry = head
        while entry !== nothing
            key_total += entry.key; value_total += entry.value; entry = entry.next
        end
    end
    return table.size == count && total == 42 * count * 5 &&
        key_total == div(count * (count - 1), 2) && value_total == 42 * count
end, identity, result->nothing)

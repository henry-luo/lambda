'=== empty aggregates ==='
avg([]) == null
min([]) == null
max([]) == null
sum([])
math.prod([])

'=== strict null propagation ==='
sum([1, null, 2]) == null
avg([1, null, 3]) == null
min([1, null, 3]) == null
max([1, null, 3]) == null
math.prod([2, null, 4]) == null
math.median([1, null, 3]) == null
math.variance([1, null, 3]) == null
math.deviation([1, null, 3]) == null
math.quantile([1, null, 3], 0.5) == null

'=== skip null option ==='
avg([1, null, 3], skip_null: true)
math.mean([1, null, 3], skip_null: true)
math.median([1, null, 3], skip_null: true)
math.variance([1, null, 3], skip_null: true)
math.deviation([1, null, 3], skip_null: true)
math.quantile([1, null, 3], 0.5, skip_null: true)
avg([null], skip_null: true) == null

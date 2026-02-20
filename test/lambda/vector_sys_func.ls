// Vector System Functions - Comprehensive Test Suite
// Tests all vector functions with various input types: array, list, range

// ============================================================
// AGGREGATION FUNCTIONS
// ============================================================

'sum'
sum([1, 2, 3, 4, 5])
sum([1.5, 2.5, 3.5])
sum(1 to 5)

'avg'
avg([1, 2, 3, 4, 5])
avg([1.0, 2.0, 3.0])
avg(1 to 10)

'min'
min([5, 2, 8, 1, 9])
min([3.5, 1.2, 4.8])
min(5 to 10)

'max'
max([5, 2, 8, 1, 9])
max([3.5, 1.2, 4.8])
max(5 to 10)

'len'
len([1, 2, 3, 4, 5])
len([])
len(1 to 100)

'prod'
prod([2, 3, 4])
prod([1.5, 2.0, 3.0])
prod(1 to 5)

// ============================================================
// STATISTICAL FUNCTIONS
// ============================================================

'mean'
mean([1, 2, 3, 4, 5])
mean([10.0, 20.0, 30.0])
mean(1 to 9)

'median'
median([1, 3, 5, 7, 9])
median([1, 2, 3, 4])
median([5.0, 1.0, 9.0, 3.0, 7.0])

'variance'
variance([2, 4, 4, 4, 5, 5, 7, 9])
variance([1, 2, 3])
variance(1 to 5)

'deviation'
deviation([2, 4, 4, 4, 5, 5, 7, 9])
deviation([1, 2, 3])
deviation(1 to 5)

'quantile'
quantile([1, 2, 3, 4, 5], 0.0)
quantile([1, 2, 3, 4, 5], 0.25)
quantile([1, 2, 3, 4, 5], 0.5)
quantile([1, 2, 3, 4, 5], 0.75)
quantile([1, 2, 3, 4, 5], 1.0)
quantile(1 to 100, 0.5)

// ============================================================
// ELEMENT-WISE MATH FUNCTIONS
// ============================================================

'abs'
abs([-1, -2, 3, -4, 5])
abs([-1.5, 2.5, -3.5])

'sqrt'
sqrt([1, 4, 9, 16, 25])
sqrt([2.0, 8.0, 18.0])
sqrt(4)

'log'
log([1, 2.718281828, 7.389056099])
log(1)

'log10'
log10([1, 10, 100, 1000])
log10(100)

'exp'
exp([0, 1, 2])
exp(0)

'sin'
sin([0, 1.5707963268, 3.1415926536])
sin(0)

'cos'
cos([0, 1.5707963268, 3.1415926536])
cos(0)

'tan'
tan([0, 0.7853981634])
tan(0)

'sign'
sign([-5, 0, 3, -1, 7])
sign([-2.5, 0.0, 1.5])
sign(0)

'floor'
floor([1.2, 2.7, -1.5, -2.8])
floor(3.9)

'ceil'
ceil([1.2, 2.7, -1.5, -2.8])
ceil(3.1)

'round'
round([1.4, 1.5, 1.6, 2.5])
round(3.7)

// ============================================================
// VECTOR CONSTRUCTION FUNCTIONS
// ============================================================

'fill'
fill(5, 0)
fill(3, 1.5)
fill(4, 42)

'range with step'
range(0, 10, 2)
range(1, 10, 3)
range(10, 0, -2)
range(0.0, 1.0, 0.25)

// ============================================================
// VECTOR MANIPULATION FUNCTIONS
// ============================================================

'reverse'
{r: reverse([1, 2, 3, 4, 5])}
{r: reverse([1.5, 2.5, 3.5])}
{r: reverse(1 to 5)}

'sort (ascending)'
sort([5, 2, 8, 1, 9, 3])
sort([3.5, 1.2, 4.8, 2.1])
sort(5 to 1)

'sort (descending)'
sort([1, 5, 3, 2, 4], "desc")
sort([1.2, 3.4, 2.3], "desc")
sort(1 to 5, "descending")

'unique'
{r: unique([1, 2, 2, 3, 3, 3, 4])}
{r: unique([1.5, 2.5, 1.5, 3.5, 2.5])}
{r: unique(["a", "b", "a", "c", "b", "c"])}
{r: unique(["hello", "world", "hello", "foo", "world"])}
'unique (list input - spreadable)'
{r: unique((1, 2, 1, 3, 2))}
unique(("a", "b", "a"))
'unique (array input - non-spreadable)'
{r: unique([1, 2, 1, 3, 2])}

'concat'
{r: concat([1, 2, 3], [4, 5, 6])}
concat([1.0, 2.0], [3.0, 4.0])
{r: concat(1 to 3, 4 to 6)}

'slice'
{r: slice([1, 2, 3, 4, 5], 1, 4)}
{r: slice([10, 20, 30, 40, 50], 0, 3)}

'take'
{r: take([1, 2, 3, 4, 5], 3)}
take([10.0, 20.0, 30.0, 40.0], 2)
{r: take(1 to 10, 5)}

'drop'
{r: drop([1, 2, 3, 4, 5], 2)}
drop([10.0, 20.0, 30.0, 40.0], 1)
{r: drop(1 to 10, 7)}

// ============================================================
// REDUCTION FUNCTIONS
// ============================================================

// NOTE: all() and any() are not yet registered in mir.c

'argmin'
argmin([5, 2, 8, 1, 9])
argmin([3.5, 1.2, 4.8])
argmin(5 to 10)

'argmax'
argmax([5, 2, 8, 1, 9])
argmax([3.5, 1.2, 4.8])
argmax(5 to 10)

'cumsum'
cumsum([1, 2, 3, 4, 5])
cumsum([1.0, 2.0, 3.0])
cumsum(1 to 5)

'cumprod'
cumprod([1, 2, 3, 4])
cumprod([2.0, 2.0, 2.0])
cumprod(1 to 5)

'dot'
dot([1, 2, 3], [4, 5, 6])
dot([1.0, 0.0], [0.0, 1.0])

'norm'
norm([3, 4])
norm([1, 2, 2])
norm([1.0, 1.0, 1.0, 1.0])

'zip'
{r: zip([1, 2, 3], [4, 5, 6])}
{r: zip([1.0, 2.0], [3.0, 4.0])}
{r: zip(1 to 3, 4 to 6)}

// ============================================================
// ARITHMETIC OPERATIONS
// ============================================================

'scalar + vector'
1 + [2, 3, 4]
10 - [1, 2, 3]
3 * [1, 2, 3]
12 / [2, 3, 4]
2 ** [1, 2, 3]
10 % [3, 4, 6]

'vector + scalar'
[10, 20, 30] - 5
[10, 20, 30] / 5
[2, 3, 4] ** 2

'vector + vector'
[1, 2, 3] + [4, 5, 6]
[10, 20, 30] - [1, 2, 3]
[2, 3, 4] * [1, 2, 3]
[12, 15, 18] / [3, 5, 6]
[2, 3, 4] ** [1, 2, 3]

'range arithmetic'
{r: (1 to 5) + 10}
{r: (1 to 5) * 2}
{r: (1 to 5) + (6 to 10)}

// ============================================================
// EDGE CASES
// ============================================================

'empty array handling'
sum([])
len([])
{r: reverse([])}
sort([])

'single element'
sum([42])
mean([42])
sort([42])
{r: reverse([42])}

'chained operations'
sum([1, 2, 3, 4, 5] * 2)
{r: sort(concat([5, 3, 1], [6, 4, 2]))}
mean(take([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 5))

'ALL TESTS COMPLETE'

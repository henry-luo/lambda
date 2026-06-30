// Test 2-argument slice(vec, start) shorthand for slice-to-end.

"=== strings ==="
{r: slice("hello", 2)}
{r: slice("héllo", 1)}
{r: "hello".slice(2)}
{r: "hello" | slice(2)}

"=== arrays ==="
{r: slice([1, 2, 3, 4], 2)}
{r: slice([1, 2, 3, 4], -2)}
{n: len(slice([1, 2, 3, 4], 99))}
{r: [1, 2, 3, 4].slice(1)}
{r: [1, 2, 3, 4] | slice(3)}

"=== null ==="
{r: slice(null, 1)}

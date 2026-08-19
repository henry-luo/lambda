let first = for (x in [1, 2, 3, 4, 5] limit 3) x
let skipped = for (x in [1, 2, 3, 4, 5] offset 2) x
let window = for (x in [1, 2, 3, 4, 5, 6, 7] limit 3 offset 2) x
let tail = for (x in [1, 2, 3, 4, 5] limit last 2) x

{
  first: first,
  skipped: skipped,
  window: window,
  tail: tail
}

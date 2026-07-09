'=== keyword comparisons ==='
[1, 2, 3] gt 1
[1, 2, 3] lt [2, 2, 2]
[1, 2, 3] ge [1, 3, 3]
[1, 2, 3] le 2

'=== equality masks ==='
[1, 2, 3] eq [1, 0, 3]
[1, 2, 3] ne [1, 0, 3]
[1.0, nan] eq [1.0, nan]
[1.0, nan] ne [1.0, nan]

'=== scalar keywords ==='
2 gt 1
2 eq 2
2 ne 2

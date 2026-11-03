// S16.10.4: grouping ends the row binding's scope.
[for (x in [1, 2] group by x as value into g) x]

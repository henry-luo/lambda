let rows = [{home: 1}, {home: 2}]
;(rows |> ~.home) == [1, 2]
len(rows |: ~.home == 1 and ~proc.pid > 0) == 1
len(rows |> {value: ~.home, pid: ~proc.pid}) == 2

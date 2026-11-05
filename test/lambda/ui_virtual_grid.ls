import grid: lambda.ui.core.virtual_grid

let a = grid.axis(10, 27, [{index:5,size:15},{index:0,size:39},{index:2,size:40}])
let exact = grid.window(a, 39, 27)^
let partial = grid.window(a, 38.5, 1)^
let padded = grid.window(a, 39, 27, 1)^
let hidden = grid.window(a, 39, 0, 2)^
let big = grid.axis(1000000, 27)
let far = grid.window(big, 26999900, 54, 2)^
let empty = grid.window(grid.axis(0, 27), 0, 100, 2)^;
[
    grid.extent(a)^ == 283,
    [for (i in [0,1,2,3,5,6,10]) grid.offset(a,i)^] == [0,39,66,106,160,175,283],
    exact.first == 1 and exact.last == 2 and exact.before == 39 and exact.after == 217,
    partial.first == 0 and partial.last == 2,
    padded.first == 0 and padded.last == 3 and padded.after == 177,
    grid.covers(padded,exact),
    grid.covers(padded,partial),
    not grid.covers(exact,partial),
    not grid.covers(padded,hidden),
    grid.covers(hidden,empty),
    not grid.covers(hidden,exact),
    hidden.first == 0 and hidden.last == 0 and hidden.after == 283,
    empty.first == 0 and empty.last == 0 and empty.after == 0,
    grid.extent(big)^ == 27000000,
    far.first == 999994 and far.last == 1000000,
    grid.reveal(a, 8, 0, 54)^ == 202,
    grid.reveal(a, 1, 39, 54)^ == 39,
    grid.reveal(a, 0, 39, 54)^ == 0,
    grid.bounds({row:8,col:2,anchor:{row:4,col:6}})=={top:4,bottom:8,left:2,right:6},
    grid.selected({row:8,col:2,anchor:{row:4,col:6}},5,4) and not grid.selected({row:8,col:2,anchor:{row:4,col:6}},3,4),
    grid.hit(a,grid.axis(10,100),250,67)^=={row:2,col:2},
    grid.hit(grid.axis(0,27),grid.axis(1,100),0,0)^==null
]

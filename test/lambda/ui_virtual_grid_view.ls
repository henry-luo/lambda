import grid_view: lambda.ui.core.virtual_grid_view

let rows={first:100,last:102,before:2700,after:2000000}
let columns={first:40,last:43,before:4960,after:90000}
let body=grid_view.body(rows,columns,(r,cells) => <tr id:string(r),<th string(r)>; *cells>,
    (r,c) => <td id:string(r) ++ ":" ++ string(c)>)^
let rendered=content(body)
let first=content(rendered[1])
let second=content(rendered[2])
let hidden=grid_view.body({first:0,last:0,before:0,after:100},columns,
    (r,cells) => <tr <th string(r)>; *cells>,(r,c) => <td "unexpected">)^
let narrow=grid_view.body(rows,{first:0,last:0,before:0,after:200},
    (r,cells) => <tr <th string(r)>; *cells>,(r,c) => <td "unexpected">)^;
[
    name(body)=='tbody' and len(rendered)==4,
    rendered[0].class=="grid-spacer" and rendered[3].class=="grid-spacer",
    rendered[1].id=="100" and rendered[2].id=="101",
    len(first)==6 and len(second)==6,
    first[1].class=="grid-gap" and first[5].class=="grid-gap",
    [for (cell in slice(first,2,5)^) cell.id]==["100:40","100:41","100:42"],
    [for (cell in slice(second,2,5)^) cell.id]==["101:40","101:41","101:42"],
    len(content(hidden))==1 and content(hidden)[0].class=="grid-spacer",
    len(content(content(narrow)[1]))==2 and content(content(narrow)[1])[1].class=="grid-gap"
]

import m: ~~.mod_model
import f: ~~.mod_formula
import files: ~~.mod_io
import window: ~~.mod_window
import grid: lambda.ui.core.virtual_grid

let book=files.read("test/demo/sheet/tests/fixtures/large.sheet.json")^
let initial=m.initial(book)
let all=m.select(m.select(initial,0,0),99999,999,true)
let changed=m.set_value(all,"=1+2")^
let selected=m.select(m.select(changed,0,0),99999,999,true)
let undone=m.history(selected)^
let redone=m.history(undone,true)^
let cached={*:initial,geometry_key:window.key(initial),geometry:window.build(initial)^}
let scrolled={*:cached,viewport:{x:100.0,y:200.0,width:800.0,height:600.0}}
let resized=m.resize(cached,"column",0,300)^
let zoomed={*:cached,zoom:1.5};
[
    m.current(initial).rows==100000 and m.current(initial).cols==1000,
    f.address(99999,999)=="ALL100000",
    m.stats(all)=={count:2,sum:0,average:0},
    m.stats(selected)=={count:2,sum:3,average:3},
    m.stats(undone)=={count:2,sum:0,average:0},
    m.stats(redone)=={count:2,sum:3,average:3},
    m.stats(m.select(redone,0,0))=={count:1,sum:0,average:0},
    match (m.copy_text(all) ^ { ^ }) { case error: true default: false },
    match (m.format_cells(all,"bold",true) ^ { ^ }) { case error: true default: false },
    match (m.sort_rows(all) ^ { ^ }) { case error: true default: false },
    match (files.csv(book.sheets[0]) ^ { ^ }) { case error: true default: false },
    window.axes(scrolled)^==cached.geometry,
    grid.offset(window.axes(resized)^.columns,1)^==300,
    grid.offset(window.axes(zoomed)^.columns,1)^==330
]

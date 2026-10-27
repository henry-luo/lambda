import m: ~~.mod_model
import f: ~~.mod_formula
import io: ~~.mod_io
import v: ~~.mod_view

let book=m.sample()
io.restore(parse(format(book,'json')^,'json')^)^==book
io.restore({}) ^ {true} ~ {false}
io.restore({*:book,version:2}) ^ {true} ~ {false}
io.restore({*:book,sheets:[]}) ^ {true} ~ {false}
io.restore({*:book,sheets:[{*:m.empty_sheet(),rows:0}]}) ^ {true} ~ {false}
io.restore({*:book,sheets:[{*:m.empty_sheet(),cells:[{row:-1,col:0,raw:"a",style:{}}]}]}) ^ {true} ~ {false}
io.restore({*:book,sheets:[{*:m.empty_sheet(),cells:[{row:0,col:0,raw:"a",style:{}},{row:0,col:0,raw:"b",style:{}}]}]}) ^ {true} ~ {false}
let st=m.paste(m.select(m.initial(m.blank()),0,0),"hello, world\tHe said \"yes\"\n=2+3\tline")
io.csv(m.current(st))=="\"hello, world\",\"He said \"\"yes\"\"\"\r\n\"5\",\"line\"\r\n"
v.formatted(131966.666666,{format:"currency"})=="$131966.67"
v.formatted(0,{format:"number"})=="0.00"
v.formatted(-1234.5,{format:"currency"})=="$-1234.50"
v.formatted(0.94,{format:"percent"})=="94.0%"
v.formatted(140500,{})=="140500"
let zero=m.set_value(m.select(st,2,0),"=0")
f.display(m.current(zero),2,0)==0
let sorted=m.sort_rows(m.select(m.select(m.paste(m.select(m.initial(m.blank()),0,0),"2\tb\n1\ta"),0,0),1,1,true))
f.cell(m.current(sorted),0,0).raw=="1" and f.cell(m.current(sorted),0,1).raw=="a"
f.cell(m.current(m.sort_rows(sorted,true)),0,0).raw=="2"

let precise=m.set_value(m.select(m.initial(m.blank()),0,0),"1000000.125")
io.csv(m.current(precise))=="\"1000000.125\"\r\n"
io.restore({*:book,sheets:[{*:m.empty_sheet(),cells:[{row:0,col:0,raw:"a",style:{size:"bad"}}]}]}) ^ {true} ~ {false}

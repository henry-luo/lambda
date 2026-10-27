import m: ~~.mod_model
import files: ~~.mod_io
import sizing: ~~.mod_resize

let st=m.initial()
m.dimension(m.current(st),"column",0)==220
m.dimension(m.current(st),"column",1)==150
m.dimension(m.current(st),"column",2)==124
m.dimension(m.current(st),"row",0)==39 and m.dimension(m.current(st),"row",5)==27
let wide=m.resize(st,"column",2,204)^
m.dimension(m.current(wide),"column",2)==204
wide.book.sheets[1]==st.book.sheets[1]
wide.row==st.row and wide.col==st.col and wide.anchor==st.anchor
len(wide.undo)==1 and wide.undo[0]==st.book and len(wide.redo)==0
let tall=m.resize(wide,"row",5,57.5)^
m.dimension(m.current(tall),"row",5)==57.5
m.dimension(m.current(tall),"column",2)==204
m.history(tall).book==wide.book
m.history(m.history(tall),true).book==tall.book
m.resize(wide,"column",2,204)^.book==wide.book
len(m.resize(wide,"column",2,204)^.undo)==len(wide.undo)
let fixed=m.resize(st,"row",5,27)^
m.current(fixed).row_heights['5']==27 and len(fixed.undo)==1
m.resize(fixed,"row",5,27)^==fixed
files.restore(parse(format(tall.book,'json')^,'json')^)^==tall.book
files.restore(st.book)^==st.book
m.resize(st,"column",2,39) ^ {true} ~ {false}
m.resize(st,"row",5,26) ^ {true} ~ {false}
m.resize(st,"column",20,100) ^ {true} ~ {false}
m.resize(st,"column",-1,100) ^ {true} ~ {false}
m.resize(st,"depth",0,100) ^ {true} ~ {false}
m.resize(st,"row",0,nan) ^ {true} ~ {false}
m.resize(st,"row",0,inf) ^ {true} ~ {false}
let pending={*:st,zoom:1.5,resize_drag:{axis:"column",index:2,original:124,value:124,start:100,pointer:1}}
let stationary={*:pending,resize_drag:{*:pending.resize_drag,original:124.045,value:124.045}}
sizing.move(stationary,100,0).resize_drag.value==124.045
let preview=sizing.move(pending,190,0)
m.used_dimension(preview,"column",2)==184
preview.book==st.book and preview.undo==st.undo
m.dimension(m.current(preview),"column",2)==124
m.used_dimension(preview,"column",3)==124
m.used_dimension(sizing.move(preview,-10000,0),"column",2)==40
m.used_dimension(sizing.move(preview,10000,0),"column",2)==1000
let pending_row={*:st,resize_drag:{axis:"row",index:5,original:27,value:27,start:100,pointer:1}}
m.used_dimension(sizing.move(pending_row,0,130),"row",5)==57
m.used_dimension(sizing.move(pending_row,0,-10000),"row",5)==27
let invalid=(fields)=>{*:st.book,sheets:[{*:m.current(st),*:fields}]}
files.restore(invalid({column_widths:{'2':10}})) ^ {true} ~ {false}
files.restore(invalid({row_heights:{'5':-1}})) ^ {true} ~ {false}
files.restore(invalid({column_widths:{'20':100}})) ^ {true} ~ {false}
files.restore(invalid({row_heights:{'05':40}})) ^ {true} ~ {false}
files.restore(invalid({row_heights:[40]})) ^ {true} ~ {false}

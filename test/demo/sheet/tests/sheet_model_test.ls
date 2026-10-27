import m: ~~.mod_model
import f: ~~.mod_formula

f.address(0,0)=="A1" and f.address(19,27)=="AB20"
f.position("$AB$20")^=={row:19,col:27}
f.position("A0") ^ {true} ~ {false}
let sample=m.initial()
f.display(m.current(sample),4,5)==133000
f.display(m.current(sample),10,5)==395900
let edited=m.set_value(m.select(sample,4,2),"50000")
f.display(m.current(edited),4,5)==140500
f.display(m.current(edited),10,5)==403400
m.history(edited).book==sample.book
m.history(m.history(edited),true).book==edited.book
let st=m.initial(m.blank())
let data=m.paste(m.select(st,0,0),"10\t20\n30\t40")
let result=(expr)=>f.display(m.current(m.set_value(m.select(data,2,0),expr)),2,0)
result("=1+2*3")==7
result("=(1+2)*3")==9
result("=2^3^2")==512
result("=50%*20")==10
result("=SUM(A1:B2)")==100
result("=AVERAGE(A1:B2)")==25
result("=MIN(A1:B2)+MAX(A1:B2)")==50
result("=COUNT(A1:B2)")==4
result("=SUM(A1, B1, 5)")==35
result("=A1/$B$1")==0.5
result("=1/0")=="#DIV/0!"
result("=A3")=="#CYCLE!"
result("=SUM(A1:A3)")=="#CYCLE!"
result("=Z100")=="#REF!"
result("=BOGUS(1)")=="#NAME?"
result("=1+2 junk")=="#VALUE!"
result("=SUM(A1:)")=="#REF!"
let selection=m.select(m.select(data,0,0),1,1,true)
m.copy_text(selection)=="10\t20\n30\t40"
m.stats(selection)=={count:4,sum:100,average:25}
let styled=m.format_cells(selection,"bold",true)
all([for (c in m.current(styled).cells) c.style.bold==true])
all([for (c in m.current(m.clear(selection)).cells) c.raw==""])
f.display(m.current(m.autosum(m.select(data,2,0))),2,0)==40
let added=m.add_sheet(data)
len(added.book.sheets)==2 and added.sheet==1
m.current(added).title=="Sheet2" and len(m.current(added).cells)==0
m.current(m.history(added)).cells==data.book.sheets[0].cells

// a dependency cycle can span several cells, and AutoSum stops at a gap.
let cycle=m.paste(m.select(st,0,0),"=B1\t=A1")
f.display(m.current(cycle),0,0)=="#CYCLE!"
let gap=m.paste(m.select(st,0,0),"100\n\n10\n20")
f.cell(m.current(m.autosum(m.select(gap,4,0))),4,0).raw=="=SUM(A3:A4)"
let mixed=m.set_value(m.select(st,0,0),"'0123")
f.display(m.current(mixed),0,0)=="0123"
result("=10^1000")=="#NUM!"

// committing an unchanged native field only closes its draft, without creating history.
let unchanged=m.set_value({*:sample,editing:true},m.active(sample).raw)
unchanged.book==sample.book and unchanged.undo==sample.undo and not unchanged.editing
m.set_value(m.select(st,0,0),"").book==st.book

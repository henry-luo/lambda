// Spills are derived grid values, not stored cells.
import m: ~~.mod_model
import f: ~~.mod_formula
let start=m.initial(m.blank())
let filled=m.set_value(m.select(start,0,0),"=SEQUENCE(2,2)")^
let dependent=m.set_value(m.select(filled,0,3),"=SUM(A1#)")^
let indirect=m.set_value(m.select(dependent,1,3),"=B1+B2")^
let blocked=m.set_value(m.select(start,0,1),"occupied")^
let collision=m.set_value(m.select(blocked,0,0),"=SEQUENCE(2,2)")^
let cleared=m.set_value(m.select(filled,0,0),"1")^
let cross_book={*:m.blank(),sheets:[{*:m.empty_sheet("Source"),cells:[{row:0,col:0,raw:"=SEQUENCE(2,2)"}]},
    {*:m.empty_sheet("Target"),cells:[{row:0,col:0,raw:"=SUM(Source!A1#)"},{row:1,col:0,raw:"=Source!B2"}]}]}
let cross=m.activate(m.initial(cross_book)^,1)
let protected=m.clear(m.select(filled,1,1))
let pasted=m.paste(m.select(filled,0,1),"99")
let padded=m.set_value(m.select(start,0,0),"=TEXTSPLIT(\"a,b;c\",\",\",\";\")")^
let padded_ref=m.set_value(m.select(padded,3,0),"=B2")^
let cyc=m.set_value(m.select(start,0,0),"=SEQUENCE(1,2,B1+1)")^
let edited=m.set_value(m.select(filled,1,1),"99") ^ {^};
[
 filled.cache.values.A1==1,filled.cache.values.B1==2,filled.cache.values.A2==3,filled.cache.values.B2==4,
 len(m.current(filled).cells)==1,
 dependent.cache.values.D1==10,
 indirect.cache.values.D2==6,
 collision.cache.values.A1=="#SPILL!",collision.cache.values.B1=="occupied",
 cleared.cache.values.B1==null,cleared.cache.values.B2==null,
 edited is error,
 cross.cache.values.A1==10,cross.cache.values.A2==4,
 protected.book==filled.book,protected.status=="Cannot change part of a spilled array",
 pasted.book==filled.book,pasted.status=="Cannot change part of a spilled array",
 padded.cache.values.B2=="#N/A",padded_ref.cache.values.A4=="#N/A",
 cyc.cache.values.A1=="#CYCLE!",cyc.cache.values.B1==null
]

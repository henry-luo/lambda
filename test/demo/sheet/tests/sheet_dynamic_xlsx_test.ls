// XLSX dynamic-array cached children remain derived from their formula anchor.
import files: ~~.mod_io
import m: ~~.mod_model
let imported=files.import_xlsx("test/input/xlsx/dynamic-arrays.xlsx")^
let restored=files.restore(imported.book)^
let st=m.initial(restored)^
let scoped=m.activate(st,1)^
let updated=m.set_value(m.select(st,0,0),"=SEQUENCE(2,2,2)")^;
[
 len(restored.defined_names)==2,
 restored.defined_names[0].name=="DOUBLE",restored.defined_names[1].scope==1,
 scoped.cache.values.A1==18,
 st.cache.values.A1==1,st.cache.values.B1==2,st.cache.values.A2==3,st.cache.values.B2==4,
 st.cache.values.D1==12,
 updated.cache.values.B1==3,updated.cache.values.B2==5,
 updated.cache.values.D1==12,
 contains(files.csv(m.indexed(updated))^,"\"3\""),
 contains(files.csv(m.indexed(updated))^,"\"4\",\"5\"")
]

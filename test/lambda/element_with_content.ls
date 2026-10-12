// Pure reconstruction uses the ordinary element contract and ownership (D3.4.5/D4.5.2).
import radiant
import svg: 'http://www.w3.org/2000/svg'
import xlink: 'http://www.w3.org/1999/xlink'

type Panel { variant: "history" | "archive", hint: string?, any* }
type TextOnly { string* }
fn rebuild(index) {
    let source=<Panel variant:"history",hint:null, "original">
    let result=radiant.with_content(source,["new",null," text ",<em index:index,"rich">]);
    {source:source,result:result,index:index}
}
let rows=[for (index in 0 to 99) rebuild(index)]
let qualified=<svg.a xlink.href:"https://example.org/history",id:"qualified",<svg.text "old">>
let copied=radiant.with_content(qualified,[<svg.text "new">])
let normalized=radiant.with_content(<p "old">,["a",null,"", "b",b'\x01',b'\x02']);
[
    ["retained nominal identity",all([for (row in rows) row.result is Panel])],
    ["retained union and nullable attributes",all([for (row in rows) row.result.variant=="history" and row.result.hint==null])],
    ["normalized strings and rich children",all([for (row in rows) content(row.result)[0]=="new text " and content(row.result)[1].index==row.index])],
    ["immutable source content",all([for (row in rows) content(row.source)==["original"]])],
    ["qualified tag retained",name(copied)==name(qualified)],
    ["qualified attribute retained",copied.xlink.href=="https://example.org/history" and copied.id=="qualified"],
    ["qualified child retained",content(copied)[0]==<svg.text "new">],
    ["canonical merging",content(normalized)==["ab",b'\x0102']],
    ["declared content rejection",radiant.with_content(<TextOnly "old">,[<em "new">]) is error]
]

// D5.3.3: every active Cartesian source remains rooted while its body allocates.
fn combine(a,b) => {sum:a+b}
fn rows(outer,inner) => [for (a in outer,b in inner,c in b.values)
    {*:c,result:combine(a.value,c.value),outer:a}]
let actual=rows([{value:10},{value:20}],[{values:[{value:1},{value:2}]}]);
[len(actual)==4,[for (x in actual) x.result.sum]==[11,12,21,22]]

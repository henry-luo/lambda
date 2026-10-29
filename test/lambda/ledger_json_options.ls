let data = {a: [1, 2], b: {c: true}, text: "a b\nc"}
let compact = format(data, {type: 'json', compact: true})
let zero = format(data, {type: 'json', indent: 0})
let wide = format({a: [1]}, {type: 'json', indent: 4});
[compact, compact == zero, wide, format([], {type:'json',compact:true}),
 format({}, {type:'json',compact:true})]

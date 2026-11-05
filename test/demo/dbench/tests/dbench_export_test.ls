import x: ~~.mod_export

let columns=[["same","integer"],["same","string"],["bytes","binary"]]
let rows=[[1,"",b'\x00ff'],[2,"a,\"b\"\n",null]]
let encoded=x.encode(columns,rows,"json")^
let decoded=parse(encoded,'json')^
let csv=x.encode(columns,rows,"csv")^
let st={columns:columns,rows:rows,row:1,col:1,anchor:{row:0,col:1}}
let selected=x.data(st,"selection")^;
[
    decoded.columns==columns and decoded.rows==[[1,"","AP8="],[2,"a,\"b\"\n",null]],
    csv=="\"same\",\"same\",\"bytes\"\r\n1,\"\",\"base64:AP8=\"\r\n2,\"a,\"\"b\"\"\n\",\r\n",
    selected.columns==[["same","string"]] and selected.rows==[[""],["a,\"b\"\n"]],
    match (x.encode(columns,rows,"other") ^ {^}) {case error: true default: false}
]

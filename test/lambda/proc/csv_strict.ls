// Strict CSV rejects lossy rows while preserving legacy reader behavior.
pn show(value) { print(value);print("\n") }
pn rejected(text) {
    let result: any^ = parse(text,{type:'csv',flavor:'strict'})
    return result is error
}
pn main() {
    let strict={type:'csv',flavor:'strict'}
    let valid=parse("date,reference,description,direction,amount\r\n2024-02-29,R1,\"Service, \"\"quoted\"\"\r\nnext\",deposit,12.34\r\n",strict)^
    show(len(valid)==1 and valid[0].date=="2024-02-29" and valid[0].description=="Service, \"quoted\"\r\nnext" and valid[0].amount=="12.34")^
    let empties=parse("a,b,c\n1,,\n",strict)^
    show(len(empties)==1 and empties[0].a=="1" and empties[0].b==null and empties[0].c==null)^
    let unicode=parse("a,b\n你好,café\n",strict)^
    show(unicode[0].a=="你好" and unicode[0].b=="café")^
    show(rejected("") and rejected("\na,b\n1,2\n") and rejected("a,a\n1,2\n") and rejected("a,\n1,2\n"))^
    show(rejected("a,b\n1,2,3\n") and rejected("a,b\n1\n") and rejected("a,b\n1,2,\n"))^
    show(rejected("a,b\n1,\"unclosed\n") and rejected("a,b\n1,\"good\"bad\n") and rejected("a,b\n1,bad\"quote\n"))^
    show(rejected("a,b\n1," ++ chr(0) ++ "2\n"))^
    let alias=parse("a,b\n1,2\n",'csv-strict')^
    let invalid_alias: any^ = parse("a,b\n1,2,3\n",'csv-strict')
    show(alias[0].a=="1" and invalid_alias is error)^
    let blank=parse("a,b\n\n1,2\n\n",strict)^
    show(len(blank)==1 and blank[0].b=="2")^
    let legacy=parse("a,b\n1,2,3\n",'csv')^
    show(len(legacy)==1 and legacy[0].a=="1" and legacy[0].b=="2")^
    let path="temp/csv-strict-" ++ (uuid()^) ++ ".csv"
    output("a,b\n1,2\n",path,'text')^
    let from_file=input(path,strict)^
    show(len(from_file)==1 and from_file[0].a=="1" and from_file[0].b=="2")^
    output(b'\x612C620A31FF2C320A',path)^
    let invalid_utf8: any^ = input(path,strict)
    show(invalid_utf8 is error)^
    io.delete(path)^
}

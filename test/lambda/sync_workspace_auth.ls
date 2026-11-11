// signatures use the public RFC 8032 section 7.1 test seed, not a user key.
import w: lambda.sync.workspace
import p: lambda.sync.protocol
import a: lambda.sync.auth
import k: lambda.sync.codec
let key = b'\xD75A980182B10AB7D54BFED3C964073A0EE172F3DAA62325AF021A68F707511A'
let base = w.start("signed-workspace")^
let changed = w.transact(base,"alice",[
    {kind:'create',type:'dir',parent:w.root,name:"docs",as:"d"},
    {kind:'create',type:'file',parent:"d",name:"a.txt",content:w.content("A")^.reference}
])^.endpoint
let packet = p.next(changed,w.have(base)^)^
let frames = p.frames(changed.replica,w.model,4096)^
let grants = [{actor:"alice",document:"signed-workspace",profile:'workspace',epoch:0,schema:w.model,
    public_key:key,permissions:['write','checkpoint']}]
let tx = {id:packet.body.id,kind:'transaction',actor:"alice",signature:b'\x20244F34623E3F0C83262D73F7903CE228706BFFD0798F69BA23137229DD4DCC4836C440DC153825F9711436E324BBE697947E0031F8A90849DE7CAF2E85E902'}
let cp = {id:frames[0].body.checkpoint,kind:'checkpoint',actor:"alice",signature:b'\x2949A55DC2498E91C003EE4775FBD56C749FAEF56A43DD4A8041E4E0D7F20646E2EE3CD9A41858417F9E2E02B1680319B521C1B84BD19B5A8D11B7DBA3DECD08'}
let received = p.accept(base,k.encode(packet)^,a.admission(packet,[tx],grants)^)^
fn recover(endpoint, frames, proof, grants) any^ {
    var result = endpoint
    var i = len(frames)-1
    while (i >= 0) {
        result = p.accept(result,k.encode(frames[i])^,a.admission(frames[i],[proof],grants)^)^
        i = i-1
    }
    return result
}
let restored = recover(base,frames,cp,grants)^;
all([
    w.path(received.replica,["alice",2])^ == "docs/a.txt",
    w.path(restored.replica,["alice",2])^ == "docs/a.txt",
    a.admission(packet,[tx],[{*:grants[0],profile:'mark'}]) ^ { ^.message == "sync auth: missing or ambiguous grant" },
    a.admission({*:packet,document:"other"},[tx],[{*:grants[0],document:"other"}]) ^ { ^.message == "sync auth: invalid signature" },
    a.admission({*:packet,body:{*:packet.body,operations:[]}},[tx],grants) ^ { ^.message == "sync auth: invalid signature" },
    len(restored.replica.atoms) == 2
])

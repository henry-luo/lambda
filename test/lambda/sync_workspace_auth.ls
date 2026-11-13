// deterministic P-256 proofs; RFC 6979 scalar is public test data.
import crypto: lambda.io.crypto
import w: lambda.sync.workspace
import p: lambda.sync.protocol
import a: lambda.sync.auth
import k: lambda.sync.codec
let seed = b'c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721'
let key = crypto.public(seed,'ecdsa-sha2-nistp256')^
let base = w.start("signed-workspace")^
let changed = w.transact(base,"alice",[
    {kind:'create',type:'dir',parent:w.root,name:"docs",as:"d"},
    {kind:'create',type:'file',parent:"d",name:"a.txt",content:w.content("A")^.reference}
])^.endpoint
let packet = p.next(changed,w.have(base)^)^
let frames = p.frames(changed.replica,w.model,4096)^
let grants = [{actor:"alice",document:"signed-workspace",profile:'workspace',epoch:0,schema:w.model,
    public_key:key,permissions:['write','checkpoint']}]
let tx = {id:packet.body.id,kind:'transaction',actor:"alice",signature:crypto.sign(seed,a.transaction_bytes(packet)^)^}
let cp = {id:frames[0].body.checkpoint,kind:'checkpoint',actor:"alice",signature:crypto.sign(seed,a.checkpoint_bytes(frames[0],frames[0].body.checkpoint)^)^}
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

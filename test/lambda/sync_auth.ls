// deterministic P-256 proofs; RFC 6979 scalar is public test data.
import crypto: lambda.io.crypto
import c: lambda.sync.core
import t: lambda.sync.text
import p: lambda.sync.protocol
import a: lambda.sync.auth
import k: lambda.sync.codec
let seed = b'c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721'
let key = crypto.public(seed,'ecdsa-sha2-nistp256')^
let changed = p.local_change(p.start(t.empty("signed"))^,"alice",[{kind:'text',start:0,count:0,value:"A"}])^.endpoint
let packet = p.next(changed,p.have(t.empty("signed"))^,128,8192)^
let frame = p.frames(changed.replica,p.default_schema,2048)^[0]
let update = p.reply(changed.replica,p.have(t.empty("signed"))^,p.default_schema,128,8192)^
let grants = [{actor:"alice",document:"signed",profile:'text',epoch:0,schema:p.default_schema,
    public_key:key,permissions:['write','checkpoint']}]
let tx = {id:packet.body.id,kind:'transaction',actor:"alice",signature:crypto.sign(seed,a.transaction_bytes(packet)^)^}
let op = {id:update.body.operations[0].id,kind:'operation',actor:"alice",signature:crypto.sign(seed,a.operation_bytes(update.body.operations[0])^)^}
let cp = {id:frame.body.checkpoint,kind:'checkpoint',actor:"alice",signature:crypto.sign(seed,a.checkpoint_bytes(frame,frame.body.checkpoint)^)^}
let admitted = a.admission(packet,[tx],grants)^
let received = p.accept(p.start(t.empty("signed"))^,k.encode(packet)^,admitted)^;
all([t.read(received.replica)^ == "A",
 a.admission(update,[op],grants)^.actors == ["alice"],
 a.admission(frame,[cp],grants)^.checkpoints == [frame.body.checkpoint],
 a.admission(packet,[tx],[]) ^ { ^.message == "sync auth: missing or ambiguous grant" },
 a.admission(packet,[],grants) ^ { ^.message == "sync auth: missing proof" },
 a.admission({*:packet,body:{*:packet.body,frontier:[]}},[tx],grants) ^ { ^.message == "sync auth: invalid signature" },
 a.admission({*:frame,document:"other"},[cp],[{*:grants[0],document:"other"}]) ^ { ^.message == "sync auth: invalid signature" }])

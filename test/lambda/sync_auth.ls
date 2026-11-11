// Ed25519 proofs generated with the public RFC 8032 section 7.1 test seed.
import c: lambda.sync.core
import t: lambda.sync.text
import p: lambda.sync.protocol
import a: lambda.sync.auth
import k: lambda.sync.codec
let key = b'\xD75A980182B10AB7D54BFED3C964073A0EE172F3DAA62325AF021A68F707511A'
let changed = p.local_change(p.start(t.empty("signed"))^,"alice",[{kind:'text',start:0,count:0,value:"A"}])^.endpoint
let packet = p.next(changed,p.have(t.empty("signed"))^,128,8192)^
let frame = p.frames(changed.replica,p.default_schema,2048)^[0]
let update = p.reply(changed.replica,p.have(t.empty("signed"))^,p.default_schema,128,8192)^
let grants = [{actor:"alice",document:"signed",profile:'text',epoch:0,schema:p.default_schema,
    public_key:key,permissions:['write','checkpoint']}]
let tx = {id:packet.body.id,kind:'transaction',actor:"alice",signature:b'\x4388F5A0FF0A8783F1F91AE1709117B3472795C58DCB43390826AAEFE9BDF0AAEA95818FA4E2EC7AE5131F63173DC1CD783DBD271ADB91AD767C3251F5760F01'}
let op = {id:update.body.operations[0].id,kind:'operation',actor:"alice",signature:b'\xA324151146975A4D1878EADB4F6CCAF4B49E36319EB50BD89CC036564BCBADB11DFA08D054C2E3E4121FE340A340B8EF5774EF2C152503541BB554502E38000A'}
let cp = {id:frame.body.checkpoint,kind:'checkpoint',actor:"alice",signature:b'\xBB1C19B929063DD61CD871193C0C138F97B83890464B13BA796DD5C3E15DBCE273F8D2D303228C6EB3CE891E1CD274B5DC28343F83509D4F4F2165439B059003'}
let admitted = a.admission(packet,[tx],grants)^
let received = p.accept(p.start(t.empty("signed"))^,k.encode(packet)^,admitted)^;
all([t.read(received.replica)^ == "A",
 a.admission(update,[op],grants)^.actors == ["alice"],
 a.admission(frame,[cp],grants)^.checkpoints == [frame.body.checkpoint],
 a.admission(packet,[tx],[]) ^ { ^.message == "sync auth: missing or ambiguous grant" },
 a.admission(packet,[],grants) ^ { ^.message == "sync auth: missing proof" },
 a.admission({*:packet,body:{*:packet.body,frontier:[]}},[tx],grants) ^ { ^.message == "sync auth: invalid signature" },
 a.admission({*:frame,document:"other"},[cp],[{*:grants[0],document:"other"}]) ^ { ^.message == "sync auth: invalid signature" }])

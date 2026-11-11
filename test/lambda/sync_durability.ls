import c: lambda.sync.core
import t: lambda.sync.text
import m: lambda.sync.mark
import b: lambda.sync.blob
import p: lambda.sync.protocol
import s: lambda.sync.session
import store: lambda.sync.store
import k: lambda.sync.codec

let bytes = b'\x010203'
let ref = b.reference(hash("manifest-a",'sha256')^,3,hash(bytes,'sha256')^)^
let ref_b = b.reference(hash("manifest-b",'sha256')^,1,hash(b'\x04','sha256')^)^
let empty_ref = b.reference(hash("empty-manifest",'sha256')^,0,hash(null,'sha256')^)^
let base = m.from_value("blobs","seed",{caption:"photo"})^
let a = m.set_blob(base,"alice","image",ref)^
let other = m.set_blob(base,"bob","image",ref_b)^
let conflict = c.merge(a,other)^
let endpoint = p.start(a)^
let captured = c.snapshot(a)^
let cloud = p.record_durable(endpoint,'cloud',captured,[ref.manifest])^
let opened = s.start(a)^
let bundle = store.seal(cloud,opened)^
let restored = store.load(bundle,"fresh-writer")^
let new_epoch = {*:t.from_text("epoch","new","current")^,epoch:1,log:[]}
let old = p.start(t.from_text("epoch","old","offline")^)^
// all operation headers must agree with their epoch, so use an empty new epoch.
let current = {*:t.empty("epoch"),epoch:1}
let bootstrapped = p.bootstrap(old,current,k.digest(current)^)^;
all([b.verify(ref,bytes)^,not b.verify(ref,b'\x00')^,b.verify(empty_ref,null)^,not b.verify(ref,null)^,
 len(b.required(conflict)^) == 2,len(m.conflicts(conflict)^) == 1,
 p.durable(endpoint,'cloud',a.clock) ^ { ^.message == "sync protocol: referenced blobs are not durable" },
 p.status(cloud).cloud,p.status(restored.endpoint).cloud,
 restored.session == opened,restored.actor == "fresh-writer",
 store.load(bundle,"alice") ^ { ^.message == "sync store: restoration requires a fresh writer actor" },
 len(bootstrapped.recoveries) == 1,t.read(bootstrapped.recoveries[0].replica)^ == "offline",
 t.read(bootstrapped.replica)^ == "",
 p.reply(current,p.have(old.replica)^)^.kind == 'bootstrap_required'])

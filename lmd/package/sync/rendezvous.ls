// Advisory discovery never grants membership; signed routing is also the AEAD context.
import schema: .cloud_schema
import identity: .peer_identity
import codec: .codec
import v: .schema
import crypto: lambda.io.crypto

pub let domain = "lambda-sync-rendezvous-1"
pub fn route(body) => {version:body.version,space:body.space,actor:body.actor,target:body.target,
    kind:body.kind,sequence:body.sequence,issued:body.issued,expires:body.expires,session:body.session}
pub fn header(body, space, at) any^ {
    v.fields(body,["version","space","actor","target","kind","sequence","issued","expires","session","box"])^
    if (body.version != 1 or body.space != space or not schema.id(body.actor) or
        not contains(["presence","offer","answer"],body.kind)^ or
        (body.kind == "presence" and body.target != null) or
        (body.kind != "presence" and (not schema.id(body.target) or body.target == body.actor)) or
        not schema.id(body.session) or not v.natural(body.sequence)^ or body.sequence < 1 or
        not v.natural(body.issued)^ or not v.natural(body.expires)^ or body.expires <= body.issued or
        body.expires-body.issued > 120000 or body.issued > at+5000 or body.expires <= at or
        not (body.box is binary) or len(body.box) > 65536)
        raise error("sync rendezvous: invalid/expired record")
    return true
}
pub fn check(record, device, space, at) any^ {
    let body = schema.verify(record,domain,device.public_key)^
    header(body,space,at)^
    if (body.actor != device.id) raise error("sync rendezvous: author mismatch")
    return body
}
fn context(body) any^ => codec.encode([domain,route(body)])^
pub pn create(credentials, kind, target, sequence, session, payload, at, ttl=60000) any^ {
    if (target != null) identity.member(credentials,target)^
    let body = {version:1,space:credentials.space,actor:credentials.device.id,target:target,
        kind:kind,sequence:sequence,session:session,issued:at,expires:at+ttl}
    let encoded = codec.encode(payload)^
    if (len(binary(encoded)^) > 60000) raise error("sync rendezvous: payload budget")
    let box = crypto.seal(credentials.secret,encoded,context(body)^)^
    let complete = {*:body,box:box}
    header(complete,credentials.space,at)^
    return schema.signed(credentials.seed,domain,complete)^
}
pub fn decode(credentials, record, kind, target, at, session=null) any^ {
    let device = identity.member(credentials,record.body.actor)^
    let body = check(record,device,credentials.space,at)^
    if (body.kind != kind or body.target != target or (session != null and body.session != session))
        raise error("sync rendezvous: route/session mismatch")
    return {body:body,payload:codec.decode(crypto.open(credentials.secret,body.box,context(body)^)^,60000)^}
}
pub fn lan_name(credentials, actor) any^ => slice(schema.digest([domain,credentials.space,credentials.secret,actor])^,0,48)^

// Cloud metadata is signed independently of storage credentials and encryption keys.
import codec: .codec
import identity: .peer_identity
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata
import v: .schema
import core: .core

pub fn digest(value) any^ => hex.hex(codec.digest(value)^)^
pub fn id(value) => value is string and len(value) == 64 and hex.hex_valid(value) and lower(value) == value
pub fn prefix(space) any^ => "spaces/" ++ hex.hex(hash(space,'sha256')^)^ ++ "/"
pub fn data_key(key) bool^ => key is string and
    (starts_with(key,"objects/") or starts_with(key,"bundles/")) and id(slice(key,index_of(key,"/")+1,len(key)))
pub fn readable(key) bool^ => data_key(key)^ or (key is string and
    (starts_with(key,"receipts/") or starts_with(key,"publications/")) and id(slice(key,index_of(key,"/")+1,len(key))))
pub fn signed(seed, domain, body) any^ => {body:body,signature:crypto.sign(seed,codec.encode([domain,body])^)^}
pub fn verify(envelope, domain, public_key) any^ {
    v.fields(envelope,["body","signature"])^
    if (not verify_signature(public_key,codec.encode([domain,envelope.body])^,envelope.signature,'ed25519')^)
        raise error("sync cloud: invalid signature")
    return envelope.body
}
pub fn control(value, expected) any^ {
    let body = value.body
    v.fields(body,["version","space","owner","generation","members"])^
    identity.validate_device(body.owner)^
    verify(value,"lambda-cloud-control-1",body.owner.public_key)^
    if (body.version != 1 or not v.actor(body.space)^ or body.owner.id != expected or
        not v.natural(body.generation)^ or body.generation < 1 or not (body.members is array) or
        len(body.members) < 1 or len(body.members) > 64) raise error("sync cloud: invalid control record")
    for (entry in body.members) {
        v.fields(entry,["device","active"])^
        identity.validate_device(entry.device)^
        if (not (entry.active is bool)) raise error("sync cloud: invalid member state")
    }
    if (len(unique([for (entry in body.members) entry.device.id])) != len(body.members) or
        len([for (entry in body.members where entry.device.id == expected and entry.active) entry]) != 1)
        raise error("sync cloud: duplicate/missing authority")
    return body
}
pub fn member(control, actor, active=true) any^ {
    let found = [for (entry in control.members where entry.device.id == actor and (not active or entry.active)) entry.device]
    if (len(found) != 1) raise error("sync cloud: device is not authorized")
    return found[0]
}
pub fn descriptor(value) any^ {
    v.fields(value,["key","bytes","checksum"])^
    if (not data_key(value.key)^ or not v.natural(value.bytes)^ or value.bytes < 1 or value.bytes > 8388608 or
        not (value.checksum is string) or len(value.checksum) != 44 or slice(value.checksum,43,44) != "=" or
        not all([for (ch in split(slice(value.checksum,0,43),"")^)
            contains("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",ch)^])) raise error("sync cloud: invalid object descriptor")
    return true
}
pub fn publication(value, control) any^ {
    let body = value.body
    v.fields(body,["version","space","actor","sequence","previous","clock","bundle","inventory","dependencies","receipts"])^
    let device = member(control,body.actor,false)^
    verify(value,"lambda-cloud-publication-1",device.public_key)^
    if (body.version != 1 or body.space != control.space or not v.natural(body.sequence)^ or body.sequence < 1 or
        (body.previous != null and not id(body.previous)) or not id(body.dependencies) or not id(body.inventory) or
        not (body.receipts is array) or len(body.receipts) < 1 or len(body.receipts) > 128 or
        not all([for (item in body.receipts) id(item)]) or len(unique(body.receipts)) != len(body.receipts))
        raise error("sync cloud: invalid publication")
    v.clock(body.clock)^
    descriptor(body.bundle)^
    if (not starts_with(body.bundle.key,"bundles/")) raise error("sync cloud: expected checkpoint bundle")
    return body
}
pub fn request(credentials, kind, body, issued, nonce) any^ {
    return signed(credentials.seed,"lambda-cloud-request-1",{version:1,space:credentials.space,
        actor:credentials.device.id,issued:issued,nonce:nonce,kind:kind,body:body})^
}
pub fn authenticate(request, control, milliseconds) any^ {
    let body = request.body
    v.fields(body,["version","space","actor","issued","nonce","kind","body"])^
    let device = member(control,body.actor)^
    verify(request,"lambda-cloud-request-1",device.public_key)^
    if (body.version != 1 or body.space != control.space or not v.natural(body.issued)^ or
        abs(milliseconds-body.issued) > 300000 or not id(body.nonce) or not (body.kind is string))
        raise error("sync cloud: invalid/expired request")
    return body
}
pub fn dependency_digest(keys) any^ => digest(sort(unique(keys)))^

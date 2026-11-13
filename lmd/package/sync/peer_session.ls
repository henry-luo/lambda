// signed ephemeral handshake and strictly ordered encrypted records over an arbitrary byte transport.
import crypto: lambda.io.crypto
import identity: .peer_identity
import codec: .codec
import v: .schema

pub let protocol = "lambda-peer-lan-1"
pub let burst_protocol = "lambda-peer-lan-burst-1"
pub let max_frame = 262144
pub pn start(credentials, role, profile=protocol) any^ {
    if (role != 'client' and role != 'server') raise error("sync peer: invalid handshake role")
    let secret = crypto.key()^
    if (not contains([protocol,burst_protocol],profile)^) raise error("sync peer: invalid session profile")
    let hello = {protocol:profile,space:credentials.space,device:credentials.device.id,role:role,
        ephemeral:crypto.public(secret,'x25519')^,nonce:crypto.key()^}
    return {secret:secret,hello:hello}
}
pub fn transcript(credentials, own, remote) any^ {
    v.fields(remote,["protocol","space","device","role","ephemeral","nonce"])^
    if (remote.protocol != own.hello.protocol or remote.space != credentials.space or remote.role == own.hello.role or
        (remote.role != 'client' and remote.role != 'server') or not identity.bytes32(remote.ephemeral) or
        not identity.bytes32(remote.nonce)) raise error("sync peer: incompatible handshake")
    identity.member(credentials,remote.device)^
    return codec.encode([own.hello.protocol,if (own.hello.role == 'client') own.hello else remote,
        if (own.hello.role == 'server') own.hello else remote])^
}
pub fn proof(credentials, own, remote) any^ => crypto.sign(credentials.seed,
    codec.encode(["lambda-peer-handshake-1",own.hello.role,transcript(credentials,own,remote)^])^)^
pub fn finish(credentials, own, remote, signature) any^ {
    let text = transcript(credentials,own,remote)^
    let peer = identity.member(credentials,remote.device)^
    if (not verify_signature(peer.public_key,codec.encode(["lambda-peer-handshake-1",remote.role,text])^,
        signature,'ed25519')^) raise error("sync peer: handshake signature rejected")
    let salt = hash(text,'sha256')^
    let shared = crypto.agree(own.secret,remote.ephemeral)^
    let client = identity.derive(shared,salt,own.hello.protocol ++ ":client")^
    let server = identity.derive(shared,salt,own.hello.protocol ++ ":server")^
    return {id:salt,peer:peer,send_key:if (own.hello.role == 'client') client else server,
        receive_key:if (own.hello.role == 'client') server else client,
        role:own.hello.role,sent:0,received:0,
        *:if (own.hello.protocol == protocol) ({}) else {profile:own.hello.protocol}}
}
fn context(session, role, sequence) any^ => codec.encode([
    if (session.profile == null) protocol else session.profile,session.id,role,sequence])^
pub pn seal(session, payload) any^ {
    let bytes = codec.encode(payload)^
    if (len(binary(bytes))^ > max_frame-32 or session.sent >= 1000000)
        raise error("sync peer: session frame/budget limit")
    let encrypted = crypto.seal(session.send_key,bytes,context(session,session.role,session.sent)^)^
    return {session:{*:session,sent:session.sent+1},bytes:encrypted}
}
pub fn unseal(session, bytes) any^ {
    if (not (bytes is binary) or len(bytes) > max_frame or session.received >= 1000000)
        raise error("sync peer: invalid session frame")
    let role = if (session.role == 'client') 'server' else 'client'
    let plaintext = crypto.open(session.receive_key,bytes,context(session,role,session.received)^)^
    return {session:{*:session,received:session.received+1},value:codec.decode(plaintext,max_frame)^}
}

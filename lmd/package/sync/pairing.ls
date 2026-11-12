// Finite demo enrollment: a public account hint selects peers; a one-use code grants access.
import identity: .peer_identity
import codec: .codec
import schema: .schema
import hex: lambda.security.tuf.metadata
import crypto: lambda.io.crypto
import ws: lambda.io.websocket
import discovery: lambda.io.discovery
import connectivity: .connectivity

pub let protocol = "lambda-sync-pair-1"
pub fn user(value) string^ {
    if (not (value is string) or len(trim(value)) == 0 or len(trim(value)) > 128)
        raise error("Enter a user ID of 1 to 128 characters.")
    return trim(value)
}
pub fn account(value) string^ => hex.hex(hash(codec.encode([protocol,user(value)^])^,'sha256')^)^
pub fn space(value) string^ => "demo-" ++ account(value)^
pub fn code(value) string^ {
    if (not (value is string)) raise error("Enter the pairing code shown on the other device.")
    let normalized = lower(replace(replace(trim(value),"-","")," ",""))
    if (len(normalized) != 20 or len(hex.unhex(normalized)^)^ != 10)
        raise error("Enter all five groups of the pairing code.")
    return normalized
}
pub fn display(value) string^ {
    let normalized = code(value)^
    return join([for (i in 0 to 4) slice(normalized,i*4,i*4+4)],"-")
}
fn key(value) any^ => crypto.mac(hash(hex.unhex(code(value)^)^,'sha256')^,protocol)^
fn alive(ticket, at) any^ {
    if (at < ticket.created or at >= ticket.deadline) raise error("Pairing code expired. Create a new code.")
    return true
}
pub pn create(value, at, lifetime_ms=120000) any^ {
    if (not schema.natural(at)^ or not schema.natural(lifetime_ms)^ or lifetime_ms < 1 or lifetime_ms > 120000)
        raise error("sync pairing: lifetime bounds")
    return {account:account(value)^,code:slice(hex.hex(crypto.key()^)^,0,20),created:at,deadline:at+lifetime_ms}
}
pub fn prefix(value) string^ => "pair-" ++ slice(account(value)^,0,16) ++ "-"
pub fn name(value, device) string^ => prefix(value)^ ++ slice(identity.validate_device(device)^.id,0,32)
pub pn hello(value, device) any^ { return {protocol:protocol,account:account(value)^,
    device:identity.validate_device(device)^,nonce:crypto.key()^} }
fn validate_hello(value, expected) any^ {
    schema.fields(value,["protocol","account","device","nonce"])^
    if (value.protocol != protocol or value.account != expected or not identity.bytes32(value.nonce)^)
        raise error("sync pairing: different user or invalid request")
    identity.validate_device(value.device)^
    return true
}
fn context(hello, challenge) any^ => codec.encode([protocol,hello.account,hello.device,hello.nonce,
    challenge.device,challenge.nonce])^
pn proof(value, transcript, role) any^ { return crypto.seal(key(value)^,codec.encode(role)^,transcript)^ }
fn verify(value, transcript, box, role) any^ {
    if (codec.decode(crypto.open(key(value)^,box,transcript)^,128)^ != role)
        raise error("sync pairing: incorrect proof role")
    return true
}
pub pn challenge(ticket, device, request, at) any^ {
    alive(ticket,at)^
    validate_hello(request,ticket.account)^
    identity.validate_device(device)^
    if (request.device.id == device.id) raise error("sync pairing: cannot link a device to itself")
    let selected = {device:device,nonce:crypto.key()^}
    return {*:selected,proof:proof(ticket.code,context(request,selected)^,"server")^}
}
pub pn confirm(value, supplied_code, request, response) any^ {
    validate_hello(request,account(value)^)^
    schema.fields(response,["device","nonce","proof"])^
    identity.validate_device(response.device)^
    if (not identity.bytes32(response.nonce)^ or response.device.id == request.device.id)
        raise error("sync pairing: invalid challenge")
    let transcript = context(request,response)^
    verify(supplied_code,transcript,response.proof,"server")^
    return {proof:proof(supplied_code,transcript,"client")^,expected:response.device.id}
}
pub fn admit(ticket, request, challenge, response, at) any^ {
    alive(ticket,at)^
    validate_hello(request,ticket.account)^
    schema.fields(response,["proof"])^
    verify(ticket.code,context(request,challenge)^,response.proof,"client")^
    return request.device
}
pub pn send(socket, value) any^ {
    let bytes = binary(codec.encode(value)^)^
    if (len(bytes)^ > 16384) raise error("sync pairing: frame budget")
    ws.send(socket,bytes)^
}
pub pn receive(socket, deadline) any^ {
    let remaining = deadline-now().unix
    if (remaining <= 0) raise error("Pairing code expired. Create a new code.")
    let frame = ws.receive(socket,min(5000,remaining))^
    if (frame.type != "binary") raise error("sync pairing: expected a binary message")
    return codec.decode(frame.data,16384)^
}
pub pn listen() any^ { return ws.listen(0,{bind:"0.0.0.0",protocols:[protocol],max_message_bytes:16384})^ }
pub pn connect(url, timeout_ms=5000) any^ {
    return ws.open(url,{protocols:[protocol],max_message_bytes:16384,timeout_ms:timeout_ms})^
}
pub pn discover(value, timeout_ms=1000) any^ {
    let selected = prefix(value)^
    let hints = discovery.browse(timeout_ms)^
    return unique([for (hint in hints where starts_with(hint.name,selected)^ and connectivity.hint_url(hint)^ != null)
        {name:hint.name,url:connectivity.hint_url(hint)^}])
}

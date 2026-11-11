// explicit device pins and encrypted enrollment; host grants remain authoritative (D11.2.3).
import crypto: lambda.io.crypto
import sqlite: lambda.io.sqlite
import hex: lambda.security.tuf.metadata
import codec: .codec
import v: .schema

pub fn bytes32(value) => value is binary and len(value) == 32
fn descriptor_bytes(key, exchange) any^ => codec.encode(["lambda-peer-device-1",key,exchange])^
pub fn descriptor(seed, exchange_seed) any^ {
    let key = crypto.public(seed,'ed25519')^
    let exchange = crypto.public(exchange_seed,'x25519')^
    return {version:1,id:hex.hex(hash(key,'sha256')^)^,public_key:key,exchange_key:exchange,
        signature:crypto.sign(seed,descriptor_bytes(key,exchange)^)^}
}
pub fn validate_device(device) any^ {
    v.fields(device,["version","id","public_key","exchange_key","signature"])^
    if (device.version != 1 or not bytes32(device.public_key) or not bytes32(device.exchange_key) or
        device.id != hex.hex(hash(device.public_key,'sha256')^)^ or
        not verify_signature(device.public_key,descriptor_bytes(device.public_key,device.exchange_key)^,
            device.signature,'ed25519')^) raise error("sync peer: invalid device descriptor")
    return device
}
pub pn create(space, secret) any^ {
    if (not v.actor(space)^ or not bytes32(secret)) raise error("sync peer: invalid space/key")
    let seed = crypto.key()^
    let exchange = crypto.key()^
    return {version:1,space:space,secret:secret,seed:seed,exchange_seed:exchange,
        device:descriptor(seed,exchange)^,members:[]}
}
pub fn validate(credentials) any^ {
    v.fields(credentials,["version","space","secret","seed","exchange_seed","device","members"])^
    if (credentials.version != 1 or not v.actor(credentials.space)^ or not bytes32(credentials.secret) or
        not (credentials.members is array) or len(credentials.members) > 64 or
        descriptor(credentials.seed,credentials.exchange_seed)^ != credentials.device)
        raise error("sync peer: invalid credentials")
    for (member in credentials.members) { validate_device(member)^ }
    if (len(unique([for (member in credentials.members) member.id])) != len(credentials.members))
        raise error("sync peer: duplicate member")
    return true
}
pub fn member(credentials, id) any^ {
    let found = [for (entry in credentials.members where entry.id == id) entry]
    if (len(found) != 1) raise error("sync peer: device is not authorized")
    return found[0]
}
pub fn trust(credentials, device) any^ {
    validate_device(device)^
    if (device.id == credentials.device.id) raise error("sync peer: cannot pair with self")
    return {*:credentials,members:[*[for (entry in credentials.members where entry.id != device.id) entry],device]}
}
pub fn revoke(credentials, id) => {*:credentials,members:[for (entry in credentials.members where entry.id != id) entry]}

// RFC 5869 extract/one-block expand; caller supplies a SHA-256 salt and distinct context.
pub fn derive(shared, salt, info) any^ {
    let extracted = crypto.mac(salt,shared)^
    return crypto.mac(extracted,binary(info)^ ++ b'01')^
}
fn invitation_context(sender, recipient, ephemeral) any^ =>
    codec.encode(["lambda-peer-invitation-1",sender,recipient,ephemeral])^
pub pn invite(credentials, target) any^ {
    validate(credentials)^
    validate_device(target)^
    let ephemeral = crypto.key()^
    let public_key = crypto.public(ephemeral,'x25519')^
    let context = invitation_context(credentials.device,target.id,public_key)^
    let key = derive(crypto.agree(ephemeral,target.exchange_key)^,hash(context,'sha256')^,context)^
    let box = crypto.seal(key,codec.encode({space:credentials.space,secret:credentials.secret})^,context)^
    return {version:1,sender:credentials.device,recipient:target.id,ephemeral:public_key,box:box,
        signature:crypto.sign(credentials.seed,codec.encode([context,box])^)^}
}
pub fn enroll(credentials, offer, expected_sender) any^ {
    v.fields(offer,["version","sender","recipient","ephemeral","box","signature"])^
    validate_device(offer.sender)^
    if (offer.version != 1 or offer.sender.id != expected_sender or offer.recipient != credentials.device.id or
        not bytes32(offer.ephemeral)) raise error("sync peer: invitation pin/recipient mismatch")
    let context = invitation_context(offer.sender,offer.recipient,offer.ephemeral)^
    if (not verify_signature(offer.sender.public_key,codec.encode([context,offer.box])^,offer.signature,'ed25519')^)
        raise error("sync peer: invalid invitation signature")
    let key = derive(crypto.agree(credentials.exchange_seed,offer.ephemeral)^,hash(context,'sha256')^,context)^
    let payload = codec.decode(crypto.open(key,offer.box,context)^,65536)^
    v.fields(payload,["space","secret"])^
    if (not v.actor(payload.space)^ or not bytes32(payload.secret)) raise error("sync peer: invalid enrollment")
    return trust({*:credentials,space:payload.space,secret:payload.secret},offer.sender)^
}

pn store_credentials(tx, wrapping_key, credentials) any^ {
    validate(credentials)^
    let context = "lambda-peer-credentials-1"
    let box = crypto.seal(wrapping_key,codec.encode(credentials)^,context)^
    sqlite.execute(tx,"INSERT OR REPLACE INTO credentials VALUES (1, ?)",[box])^
    return credentials.device
}
pn read_credentials(tx, wrapping_key) any^ {
    let rows = sqlite.query(tx,"SELECT box FROM credentials WHERE id = 1")^
    if (len(rows) != 1) raise error("sync peer: missing credentials")
    let credentials = codec.decode(crypto.open(wrapping_key,rows[0][0],"lambda-peer-credentials-1")^,131072)^
    validate(credentials)^
    return credentials
}
// creation never overwrites an existing device; updates read under the same writer transaction.
pub pn save(directory, wrapping_key, credentials) any^ {
    open tx = (sqlite.create(directory ++ "/device.sqlite")^) {
        sqlite.execute(tx,"CREATE TABLE credentials (id INTEGER PRIMARY KEY CHECK (id = 1), box BLOB NOT NULL)")^
        return store_credentials(tx,wrapping_key,credentials)^
    }
}
pub pn load(directory, wrapping_key) any^ {
    open tx = (sqlite.existing(directory ++ "/device.sqlite")^) { return read_credentials(tx,wrapping_key)^ }
}
pub pn update(directory, wrapping_key, operation) any^ {
    open tx = (sqlite.existing(directory ++ "/device.sqlite")^) {
        let current = read_credentials(tx,wrapping_key)^
        let next = operation(current)^
        store_credentials(tx,wrapping_key,next)^
        return next
    }
}

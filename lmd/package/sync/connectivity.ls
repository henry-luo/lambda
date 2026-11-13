// Finite HTTP rendezvous and private LAN hints; application handshakes still authenticate peers.
import rv: .rendezvous
import transport: .cloud_transport
import schema: .cloud_schema
import identity: .peer_identity
import discovery: lambda.io.discovery
import rtc: lambda.io.rtc
import crypto: lambda.io.crypto
import v: .schema

pub pn capabilities() any^ { return rtc.capabilities()^ }
pub pn advertise(credentials, port) any^ { return discovery.advertise(rv.lan_name(credentials,credentials.device.id)^,port)^ }
pub pn withdraw(handle) any^ { return discovery.close(handle)^ }
pub pn publish(config, credentials, kind, target, session, payload) any^ {
    let current = transport.call(config,credentials,"rendezvous_current",{kind:kind,target:target},2)^
    let record = rv.create(credentials,kind,target,
        if (current.record == null) 1 else current.record.body.sequence+1,session,payload,now().unix)^
    let result = transport.call(config,credentials,"rendezvous_put",{record:record,expected:current.etag},2)^
    if (not result.written) raise error("sync connectivity: concurrent signaling writer")
    return record
}
pub pn query(config, credentials, kind, actors) any^ {
    if (len(actors)^ > 8) raise error("sync connectivity: signaling page budget")
    let records = transport.call(config,credentials,"rendezvous_get",{kind:kind,actors:actors},2)^
    var result = []
    for (record in records) {
        if (not contains(actors,record.body.actor)^) raise error("sync connectivity: unrequested signal author")
        result = [*result,rv.decode(credentials,record,kind,
            if (kind == "presence") null else credentials.device.id,now().unix)^]
    }
    return result
}
// DNS-SD is untrusted input; only a bounded DNS host name can enter a URL.
pub fn hint_url(hint) any^ {
    if (not (hint.port is int) or hint.port < 1 or hint.port > 65535 or
        not (hint.host is string) or len(hint.host)^ == 0 or len(hint.host)^ > 255 or
        not all([for (ch in split(hint.host,"")^) contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-",ch)^])) { return null }
    return "ws://" ++ hint.host ++ ":" ++ string(hint.port) ++ "/sync"
}
pub pn discover(credentials, timeout_ms=1000) any^ {
    if (len(credentials.members)^ == 0) { return [] }
    let hints = discovery.browse(timeout_ms,{names:[for (member in credentials.members) rv.lan_name(credentials,member.id)^]})^
    var result = []
    for (member in credentials.members) {
        let name = rv.lan_name(credentials,member.id)^
        for (hint in hints where hint.name == name and hint_url(hint)^ != null) {
            result = [*result,{peer:member.id,url:hint_url(hint)^}]
        }
    }
    return unique(result)
}
pub pn session_id() any^ { return schema.digest(crypto.key()^)^ }
pub fn description(payload) any^ {
    v.fields(payload,["protocol","sdp","instance","target_instance","offer"])^
    if (payload.protocol != "lambda-peer-lan-1" or not (payload.sdp is string) or len(payload.sdp)^ > 60000 or
        not schema.id(payload.instance) or not schema.id(payload.target_instance) or
        (payload.offer != null and not schema.id(payload.offer)))
        raise error("sync connectivity: incompatible channel offer")
    return payload.sdp
}
pub fn offer(remote, instance) any^ {
    if (remote.payload.target_instance != instance or remote.payload.offer != null)
        raise error("sync connectivity: stale process offer")
    return description(remote.payload)^
}
pub fn answer(remote, pending) any^ {
    if (remote.body.session != pending.session or remote.body.actor != pending.peer or
        remote.payload.instance != pending.remote_instance or remote.payload.target_instance != pending.instance or
        remote.payload.offer != pending.offer) raise error("sync connectivity: answer binding mismatch")
    return description(remote.payload)^
}

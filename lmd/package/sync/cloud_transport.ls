// Finite signed control requests; bulk ciphertext uses scoped expiring storage URLs.
import schema: .cloud_schema
import codec: .codec
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata
import s3: .s3
import service: .cloud_function

fn endpoint(url) any^ {
    if (not (url is string) or not (starts_with(url,"https://") or starts_with(url,"http://127.0.0.1:")) or
        any([for (c in ["@","#","\r","\n"]) contains(url,c)])) raise error("sync cloud: HTTPS endpoint required")
    return true
}
pub pn call(config, credentials, kind, body, timeout_seconds=30) any^ {
    if (config.storage == null) endpoint(config.url)^
    if (not (timeout_seconds is int) or timeout_seconds < 1 or timeout_seconds > 30)
        raise error("sync cloud: invalid request timeout")
    let signed = schema.request(credentials,kind,body,now().unix,hex.hex(crypto.key()^)^)^
    // A trusted local host may invoke the same stateless handler without an HTTP listener.
    if (config.storage != null) { return service.handle(s3.adapter(config.storage),signed,now().unix)^ }
    let response = io.fetch(config.url,{method:"POST",body:codec.encode(signed)^,
        headers:{'content-type':"application/json"},timeout:timeout_seconds,max_bytes:1048576,follow_redirects:false})^
    if (not response.ok) raise error("sync cloud: function request rejected")
    let reply = codec.decode(response.body,1048576)^
    if (reply.ok != true) raise error("sync cloud: function request rejected")
    return reply.body
}
pub pn transfer(capability, method, bytes=null) any^ {
    endpoint(capability.url)^
    return io.fetch(capability.url,{method:method,body:bytes,headers:capability.headers,
        timeout:30,max_bytes:33554432,follow_redirects:false})^
}
// The local service already owns the storage credential; avoid exposing signed URLs to host logs.
pn transfer_object(config, credentials, offer, method, bytes=null) any^ {
    if (config.storage == null) { return transfer(offer.capability,method,bytes)^ }
    return s3.request(config.storage,method,schema.prefix(credentials.space)^ ++ offer.key,
        bytes,offer.capability.headers)^
}
pub pn read(config, credentials, key) any^ {
    let offer = call(config,credentials,"read",{keys:[key]})^[0]
    if (offer.key != key) raise error("sync cloud: read descriptor mismatch")
    let response = transfer_object(config,credentials,offer,"GET")^
    if (not response.ok) raise error("sync cloud: missing/unavailable object")
    return response.body
}
fn read_request(keys, etags) => {keys:keys,*:if (etags == null) {} else {etags:etags}}
pub pn read_many(config, credentials, keys, etags=null) any^ {
    if (len(keys)^ == 0) { return [] }
    let offers = call(config,credentials,"read",read_request(keys,etags)^)^
    if ([for (offer in offers) offer.key] != keys) raise error("sync cloud: read descriptors mismatch")
    var records = []
    for (offer in offers) {
        let response = transfer_object(config,credentials,offer,"GET")^
        if (not response.ok) raise error("sync cloud: unavailable object")
        records = [*records,{key:offer.key,bytes:response.body}]
    }
    return records
}
pub fn descriptor(key, bytes) any^ => {key:key,bytes:len(binary(bytes)^),checksum:s3.checksum(bytes)^}
// Reserve a bounded group together; physical uploads remain independently retryable.
pub pn ensure_many(config, credentials, items) any^ {
    if (not (items is array) or len(items)^ < 1 or len(items)^ > 64)
        raise error("sync cloud: invalid upload batch")
    let offers = call(config,credentials,"upload",{objects:[for (item in items) item.descriptor]})^
    if (not (offers is array) or len(offers)^ != len(items)^) raise error("sync cloud: incomplete upload offers")
    var results = []
    var index = 0
    while (index < len(items)^) {
        let item = items[index]
        let offered = offers[index]
        if (offered.key != item.descriptor.key) raise error("sync cloud: upload descriptor mismatch")
        var stored = null
        if (offered.exists) { stored = {uploaded:false,existing:transfer_object(config,credentials,offered,"GET")^} }
        else {
            let result = transfer_object(config,credentials,offered,"PUT",item.bytes)^
            if (result.status == 409 or result.status == 412) {
                stored = {uploaded:false,existing:{ok:true,body:read(config,credentials,item.descriptor.key)^}}
            } else {
                if (not result.ok) raise error("sync cloud: object upload failed")
                stored = {uploaded:true,existing:null}
            }
        }
        results = [*results,stored]
        index = index+1
    }
    return results
}
pub pn ensure(config, credentials, descriptor, bytes) any^ {
    return ensure_many(config,credentials,[{descriptor:descriptor,bytes:bytes}])^[0]
}

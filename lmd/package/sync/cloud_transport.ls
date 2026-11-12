// Finite signed control requests; bulk ciphertext uses scoped expiring storage URLs.
import schema: .cloud_schema
import codec: .codec
import crypto: lambda.io.crypto
import hex: lambda.security.tuf.metadata
import s3: .s3

fn endpoint(url) any^ {
    if (not (url is string) or not (starts_with(url,"https://") or starts_with(url,"http://127.0.0.1:")) or
        any([for (c in ["@","#","\r","\n"]) contains(url,c)])) raise error("sync cloud: HTTPS endpoint required")
    return true
}
pub pn call(config, credentials, kind, body) any^ {
    endpoint(config.url)^
    let signed = schema.request(credentials,kind,body,now().unix,hex.hex(crypto.key()^)^)^
    let response = io.fetch(config.url,{method:"POST",body:codec.encode(signed)^,
        headers:{'content-type':"application/json"},timeout:30,max_bytes:1048576,follow_redirects:false})^
    if (not response.ok) raise error("sync cloud: function request rejected")
    let reply = codec.decode(response.body,1048576)^
    if (reply.ok != true) raise error("sync cloud: function request rejected")
    return reply.body
}
pub pn transfer(capability, method, bytes=null) any^ {
    endpoint(capability.url)^
    return io.fetch(capability.url,{method:method,body:bytes,headers:capability.headers,
        timeout:30,max_bytes:8388608,follow_redirects:false})^
}
pub pn read(config, credentials, key) any^ {
    let offer = call(config,credentials,"read",{keys:[key]})^[0]
    if (offer.key != key) raise error("sync cloud: read descriptor mismatch")
    let response = transfer(offer.capability,"GET")^
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
        let response = transfer(offer.capability,"GET")^
        if (not response.ok) raise error("sync cloud: unavailable object")
        records = [*records,{key:offer.key,bytes:response.body}]
    }
    return records
}
pub fn descriptor(key, bytes) any^ => {key:key,bytes:len(binary(bytes)^),checksum:s3.checksum(bytes)^}
pub pn ensure(config, credentials, descriptor, bytes) any^ {
    let offered = call(config,credentials,"upload",{objects:[descriptor]})^[0]
    if (offered.key != descriptor.key) raise error("sync cloud: upload descriptor mismatch")
    if (offered.exists) { return {uploaded:false,existing:transfer(offered.capability,"GET")^} }
    let result = transfer(offered.capability,"PUT",bytes)^
    if (result.status == 409 or result.status == 412) {
        return {uploaded:false,existing:{ok:true,body:read(config,credentials,descriptor.key)^}}
    }
    if (not result.ok) raise error("sync cloud: object upload failed")
    return {uploaded:true,existing:null}
}

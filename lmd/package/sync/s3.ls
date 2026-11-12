// S3 is one storage adapter; version tokens stay opaque and redirects stay disabled.
import hex: lambda.security.tuf.metadata

pub fn hmac(key, message) any^ {
    let raw = binary(key)^
    let reduced = if (len(raw) > 64) hash(raw,'sha256')^ else raw
    let inner = binary([for (i in 0 to 63) bxor(if (i < len(reduced)) int(reduced[i]) else 0,54)])^
    let outer = binary([for (i in 0 to 63) bxor(if (i < len(reduced)) int(reduced[i]) else 0,92)])^
    return hash(outer ++ hash(inner ++ binary(message)^,'sha256')^,'sha256')^
}
pub fn stamp(milliseconds) any^ => datetime(milliseconds)^.format("YYYYMMDD") ++ "T" ++
    datetime(milliseconds)^.format("hhmmss") ++ "Z"
fn safe(text, alphabet) bool^ => text is string and len(text) > 0 and
    all([for (ch in split(text,"")^) contains(alphabet,ch)^])
pub fn validate(config) any^ {
    let endpoint = config.endpoint
    if (not (endpoint is string) or
        not (starts_with(endpoint,"https://") or starts_with(endpoint,"http://127.0.0.1:")) or
        contains(slice(endpoint,index_of(endpoint,"://")+3,len(endpoint)),"/") or
        any([for (c in ["@","?","#","\r","\n"]) contains(endpoint,c)]) or
        not safe(config.bucket,"abcdefghijklmnopqrstuvwxyz0123456789-.")^ or
        not safe(config.region,"abcdefghijklmnopqrstuvwxyz0123456789-")^ or
        not (config.access_key is string) or not (config.secret_key is string))
        raise error("sync s3: invalid endpoint or signing configuration")
    return true
}
fn signing_key(config, daystamp) any^ => hmac(hmac(hmac(hmac("AWS4" ++ config.secret_key,daystamp)^,
    config.region)^,"s3")^,"aws4_request")^
fn scope(config, daystamp) => daystamp ++ "/" ++ config.region ++ "/s3/aws4_request"
fn query_string(query) => join([for (key in sort([for (k at query) k])) url_encode(key) ++ "=" ++ url_encode(query[key])],"&")
fn canonical_headers(headers) => join([for (key in sort([for (k at headers) k])) key ++ ":" ++ headers[key] ++ "\n"],"")
fn signature(config, method, path, query, headers, timestamp, payload) any^ {
    let names = join(sort([for (k at headers) k]),";")
    let canonical = method ++ "\n" ++ path ++ "\n" ++ query_string(query) ++ "\n" ++
        canonical_headers(headers) ++ "\n" ++ names ++ "\n" ++ payload
    let daystamp = slice(timestamp,0,8)
    let message = "AWS4-HMAC-SHA256\n" ++ timestamp ++ "\n" ++ scope(config,daystamp) ++ "\n" ++
        hex.hex(hash(canonical,'sha256')^)^
    return hex.hex(hmac(signing_key(config,daystamp)^,message)^)^
}
pub fn signed(config, method, path, query, headers, timestamp, payload) any^ {
    let signed_headers = {*:headers,'x-amz-date':timestamp,'x-amz-content-sha256':payload,
        *:if (config.session_token != null) {'x-amz-security-token':config.session_token} else {}}
    let authorization = "AWS4-HMAC-SHA256 Credential=" ++ config.access_key ++ "/" ++ scope(config,slice(timestamp,0,8)) ++
        ", SignedHeaders=" ++ join(sort([for (k at signed_headers) k]),";") ++ ", Signature=" ++
        signature(config,method,path,query,signed_headers,timestamp,payload)^
    return {*:signed_headers,authorization:authorization}
}
fn address(config, key) any^ {
    validate(config)^
    if (not safe(key,"abcdefghijklmnopqrstuvwxyz0123456789/-")^ or
        starts_with(key,"/") or ends_with(key,"/") or contains(key,"//"))
        raise error("sync s3: invalid repository key")
    return {host:slice(config.endpoint,index_of(config.endpoint,"://")+3,len(config.endpoint)),
        path:"/" ++ config.bucket ++ "/" ++ key}
}
pub pn request(config, method, key, bytes=null, conditions={}) any^ {
    let target = address(config,key)^
    let headers = signed(config,method,target.path,{},
        {host:target.host,*:conditions},stamp(now().unix)^,hex.hex(hash(bytes,'sha256')^)^)^
    return io.fetch(config.endpoint ++ target.path,{method:method,body:bytes,
        headers:headers,timeout:30,max_bytes:8388608,follow_redirects:false})^
}
fn result(response) any^ {
    if (response.status == 404) { return null }
    if (not response.ok or response.headers.etag == null) raise error("sync s3: storage request rejected")
    return {body:response.body,etag:response.headers.etag,bytes:response.bytes,headers:response.headers}
}
pub pn get(config, key) any^ { return result(request(config,"GET",key)^)^ }
pub pn head(config, key) any^ { return result(request(config,"HEAD",key)^)^ }
pub pn write(config, key, bytes, expected=null) any^ {
    let conditions = if (expected == null) {'if-none-match':"*"} else {'if-match':expected}
    let response = request(config,"PUT",key,bytes,conditions)^
    if (response.status == 409 or response.status == 412) { return {written:false} }
    if (not response.ok or response.headers.etag == null) raise error("sync s3: conditional write rejected")
    return {written:true,etag:response.headers.etag}
}
pub fn checksum(bytes) any^ {
    let alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    let data = hash(bytes,'sha256')^
    return join([for (i in range(0,32,3)) {
        let a = int(data[i])
        let b = if (i+1 < 32) int(data[i+1]) else 0
        let c = if (i+2 < 32) int(data[i+2]) else 0;
        alphabet[int(a/4)] ++ alphabet[(a%4)*16+int(b/16)] ++
        (if (i+1 < 32) alphabet[(b%16)*4+int(c/64)] else "=") ++
        (if (i+2 < 32) alphabet[c%64] else "=")
    }],"")
}
pub fn presign(config, method, key, headers, timestamp) any^ {
    let target = address(config,key)^
    let amztime = timestamp
    let all_headers = {host:target.host,*:headers}
    let query = {'X-Amz-Algorithm':"AWS4-HMAC-SHA256",'X-Amz-Credential':config.access_key ++ "/" ++ scope(config,slice(amztime,0,8)),
        'X-Amz-Date':amztime,'X-Amz-Expires':"60",'X-Amz-SignedHeaders':join(sort([for (k at all_headers) k]),";"),
        *:if (config.session_token != null) {'X-Amz-Security-Token':config.session_token} else {}}
    let sig = signature(config,method,target.path,query,all_headers,amztime,"UNSIGNED-PAYLOAD")^
    return {url:config.endpoint ++ target.path ++ "?" ++ query_string({*:query,'X-Amz-Signature':sig}),headers:headers}
}
pub pn erase(config, key) any^ {
    let response = request(config,"DELETE",key)^
    if (not response.ok and response.status != 404) raise error("sync s3: delete rejected")
    return true
}
// consumers depend on this small contract, rather than S3 request conventions.
pub fn adapter(config) { return {
    erase:pn (key)=> { return erase(config,key)^ },
    get:pn (key)=> { return get(config,key)^ },
    head:pn (key)=> { return head(config,key)^ },
    write:pn (key,bytes,expected)=> { return write(config,key,bytes,expected)^ },
    capability:pn (method,key,headers)=> { return presign(config,method,key,headers,stamp(now().unix)^)^ }
} }

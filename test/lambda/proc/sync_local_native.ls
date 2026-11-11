import crypto: lambda.io.crypto
import disk: lambda.io.disk

pn main() {
    let root = "temp/sync-local-native"
    io.delete(root) ^ { null }
    io.mkdir(root)^
    let secret = crypto.key()^
    let payload = binary([0,255,1,2])^
    let sealed = crypto.seal(secret,payload,"context")^
    disk.install(root,"a.bin",payload,null)^
    let captured = disk.capture(root,"a.bin",100)^
    let listed = disk.entries(root,"",10)^
    let original = disk.inspect(root,"a.bin")^
    disk.install(root,"a.bin",binary("changed")^,original.token)^
    let stale = disk.install(root,"a.bin",payload,original.token) ^ { true }
    disk.directory(root,"nested/empty")^
    let empty = disk.entries(root,"nested/empty",10)^
    let bad_context = crypto.open(secret,sealed,"other") ^ { true }
    let zero_key = binary([for (i in 1 to 32) 0])^
    // NIST AES-256-GCM: zero key/nonce, one zero block, empty AAD.
    let known = b'\x4c474331000000000000000000000000d0d1c8a799996bf0265b98b5d48ab919cea7403d4d606b6e074ec5d3baf39d18'
    let decoded = crypto.open(zero_key,known,binary([])^)^
    let current = disk.inspect(root,"a.bin")^
    disk.install(root,"zero.bin",null,null)^
    let zero = disk.capture(root,"zero.bin",0)^
    let empty_box = crypto.seal(secret,null,"empty")^
    let rfc_key = binary([for (i in 1 to 32) if (i <= 20) 11 else 0])^
    let mac = crypto.mac(rfc_key,"Hi There")^
    let truncated = crypto.open(secret,take(sealed,31)^,"context") ^ { true }
    let modified = binary([for (i in 0 to (len(sealed)-1)) if (i == 32) bxor(sealed[i],1)^ else sealed[i]])^
    let tampered = crypto.open(secret,modified,"context") ^ { true }
    let wrong_key = crypto.open(crypto.key()^,sealed,"context") ^ { true }
    disk.erase(root,"a.bin",current.token)^;
    [len(secret) == 32,crypto.open(secret,sealed,"context")^ == payload,
        sealed != crypto.seal(secret,payload,"context")^,bad_context,
        captured.bytes == payload,listed[0].identity == captured.identity,stale,
        len(empty) == 0,disk.inspect(root,"a.bin")^ == null,
        decoded == binary([for (i in 1 to 16) 0])^,
        len(crypto.mac(secret,payload)^) == 32,zero.bytes == null and zero.size == 0,
        crypto.open(secret,empty_box,"empty")^ == null,
        mac == b'\xb0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7',truncated,tampered,wrong_key]
}

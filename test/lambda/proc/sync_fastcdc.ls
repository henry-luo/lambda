import chunks: lambda.sync.chunks
import crypto: lambda.io.crypto
pn main() {
    let config = {algorithm:chunks.default_profile.algorithm,minimum:512,target:1024,maximum:8192}
    var seed = crypto.key()^
    while (len(seed)^ < 524288) { seed = seed ++ seed }
    let bytes = slice(crypto.seal(crypto.key()^,seed,"chunk-test")^,0,524288)^
    let whole = chunks.split(bytes,config)^
    var accumulator = chunks.start(config)^
    var streamed = []
    var offset = 0
    while (offset < len(bytes)^) {
        let fed = chunks.feed(accumulator,slice(bytes,offset,offset+317)^)^
        streamed = [*streamed,*fed.pieces]; accumulator = fed.state; offset = offset+317
    }
    let final = chunks.feed(accumulator,null,true)^
    streamed = [*streamed,*final.pieces]
    let legacy = chunks.legacy_profile
    let old = chunks.split(bytes,legacy)^
    let average = len(bytes)^ / len(whole)^
    let inserted = slice(bytes,0,12345)^ ++ crypto.key()^ ++ slice(bytes,12345)^
    let changed = chunks.split(inserted,config)^
    let hashes = [for (piece in whole) hash(piece,'sha256')^]
    let reused = len([for (piece in changed where contains(hashes,hash(piece,'sha256')^)^) piece])^
    let invalid = chunks.profile({*:config,maximum:8388609}) ^ { ^ }
    return [chunks.default_profile.minimum == 524288,chunks.default_profile.target == 1048576,
        chunks.default_profile.maximum == 8388608,streamed == whole,
        all([for (piece in take(whole,len(whole)^-1)^) len(piece)^ >= config.minimum]),
        all([for (piece in whole) len(piece)^ <= config.maximum]),average > 700 and average < 1600,
        reused > len(whole)^*0.9,len(old)^ > len(whole)^/8,invalid is error]
}

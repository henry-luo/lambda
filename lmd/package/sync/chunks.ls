// Gear32 content-defined boundaries; the profile fixes table generation and sizes.
pub let default_profile = {algorithm:"lambda-gear32-1",minimum:1024,target:4096,maximum:16384}

fn table_word(index) any^ {
    let bytes = hash("lambda-gear32-1:" ++ string(index),'sha256')^
    return bytes[0]*16777216+bytes[1]*65536+bytes[2]*256+bytes[3]
}
let gear = [for (index in 0 to 255) table_word(index)^]

pub fn profile(value) any^ {
    if (not (value is map) or value.algorithm != default_profile.algorithm or
        not (value.minimum is int) or not (value.target is int) or not (value.maximum is int) or
        value.minimum < 16 or value.minimum > value.target or value.target > value.maximum or
        value.maximum > 1048576 or band(value.target,value.target-1)^ != 0)
        raise error("sync chunks: invalid profile")
    return value
}

pub fn start(config=default_profile) any^ {
    profile(config)^
    return {profile:config,tail:binary([])^,fingerprint:0}
}
pub fn feed(acc, bytes, final=false) any^ {
    let config = profile(acc.profile)^
    if ((bytes != null and not (bytes is binary)) or
        (acc.tail != null and not (acc.tail is binary)) or len(acc.tail)^ >= config.maximum or
        not (acc.fingerprint is int) or acc.fingerprint < 0 or acc.fingerprint >= 4294967296)
        raise error("sync chunks: invalid stream state or binary")
    let combined = acc.tail ++ bytes
    var result = []
    var start = 0
    var index = len(acc.tail)^
    var fingerprint = acc.fingerprint
    while (index < len(combined)^) {
        fingerprint = (fingerprint*2+gear[combined[index]]) % 4294967296
        index = index+1
        let count = index-start
        if (count >= config.maximum or (count >= config.minimum and band(fingerprint,config.target-1)^ == 0)) {
            result = [*result,slice(combined,start,index)^]
            start = index
            fingerprint = 0
        }
    }
    var tail = slice(combined,start,len(combined)^)^
    if (final and len(tail)^ > 0) { result = [*result,tail]; tail = binary([])^; fingerprint = 0 }
    return {pieces:result,state:{profile:config,tail:tail,fingerprint:fingerprint}}
}
pub fn split(bytes, config=default_profile) any^ => feed(start(config)^,bytes,true)^.pieces

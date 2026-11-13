import discovery: lambda.io.discovery
import rtc: lambda.io.rtc
pn main() {
    let rejected = [for (options in [{names:["one"],prefix:"two"},
        {names:[for (i in 1 to 65) string(i)]},{names:[1]},
        {prefix:"this-prefix-is-deliberately-longer-than-the-sixty-three-byte-DNS-label-limit"}]) {
        let result = discovery.browse(100,options) ^ { ^ }
        result is error
    }]
    let deadline = discovery.browse(0,{names:["one"]}) ^ { ^ }
    if (not rtc.capabilities()^.bonjour) {
        // Unsupported hosts must fail closed; discovery qualification is macOS native.
        let unavailable = discovery.browse(100,{names:["one"]}) ^ { ^ }
        return [*rejected,deadline is error,unavailable is error]
    }
    let prefix = "test-sync-" ++ uuid()^
    let name = prefix ++ "-a"
    let other = "other-" ++ uuid()^
    let a = discovery.advertise(name,12345)^
    let b = discovery.advertise(other,12346)^
    let exact = discovery.browse(1000,{names:[name]})^
    let matching = discovery.browse(1000,{prefix:prefix})^
    let empty = discovery.browse(100,{names:[]})^
    discovery.close(a)^
    discovery.close(b)^
    return [*rejected,deadline is error,
        len(exact)^ > 0 and all([for (hint in exact) hint.name == name]) and
        len(matching)^ > 0 and all([for (hint in matching) starts_with(hint.name,prefix)^]) and len(empty)^ == 0]
}

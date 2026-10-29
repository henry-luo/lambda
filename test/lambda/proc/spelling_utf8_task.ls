// S2.5.8 / S13.3.1v2: decoding a worker response cannot recurse once per character.
import utf8: lambda.io.utf8

pn decoded(bytes) { return utf8.decode(bytes)^ }
pn main() {
    let text = join([for (i in 0 to 255) "abc😀café漢字"],"")
    let handle = start(decoded,[binary(text)])
    var result = null
    wait(handle) ^ { result = ^ } ~ { result = ~ }
    return [result==text,len(result)==2560]
}

// Captured publication paths share one checked resolver; callers own diagnostics.
import encoding: lambda.dom.urlencode

pub fn component(value) => replace(encoding.form_encode(value),"+","%20")
// Canonical member names are decoded data; encode each segment before emitting a URL.
pub fn url(path,fragment = null) => join([for (part in split(path,"/")) component(part)],"/") ++
    (if (fragment == null) "" else "#" ++ component(fragment))

fn hex_byte(value, index, failure) int^ {
    let hi = index_of("0123456789abcdef",lower(slice(value,index+1,index+2)))
    let lo = index_of("0123456789abcdef",lower(slice(value,index+2,index+3)));
    if (slice(value,index,index+1) != "%" or index+2 >= len(value) or hi == null or lo == null)
        raise failure("invalid-target","Invalid percent escape")
    else hi*16+lo
}
// Decode UTF-8 URI escapes once, before checking traversal and package separators.
fn decode(value, failure, index = 0, result = "", separators = false) string^ {
    if (index >= len(value)) result else if (slice(value,index,index+1) != "%")
        decode(value,failure,index+1,result ++ slice(value,index,index+1),separators)
    else {
        let lead = hex_byte(value,index,failure)^
        let byte_count = if (lead < 128) 1 else if (lead >= 194 and lead <= 223) 2
            else if (lead >= 224 and lead <= 239) 3 else if (lead >= 240 and lead <= 244) 4 else 0
        let bytes = [for (i in 1 to (byte_count-1)) hex_byte(value,index+i*3,failure)^]
        let cp = if (byte_count == 1) lead else if (byte_count == 2) (lead-192)*64+bytes[0]-128
            else if (byte_count == 3) (lead-224)*4096+(bytes[0]-128)*64+bytes[1]-128
            else if (byte_count == 4) (lead-240)*262144+(bytes[0]-128)*4096+(bytes[1]-128)*64+bytes[2]-128 else -1;
        if (byte_count == 0 or any([for (b in bytes) b < 128 or b > 191]) or
            (byte_count == 2 and cp < 128) or (byte_count == 3 and cp < 2048) or (byte_count == 4 and cp < 65536) or
            cp > 1114111 or (cp >= 55296 and cp <= 57343) or cp < 32 or cp == 127 or (not separators and (cp == 47 or cp == 92)))
            raise failure("invalid-target","Invalid encoded package path")
        else decode(value,failure,index+byte_count*3,result ++ chr(cp),separators)
    }
}
fn clean(parts, failure, index = 0, result = []) string^ {
    if (index >= len(parts)) join(result,"/") else if (parts[index] == "" or parts[index] == ".")
        clean(parts,failure,index+1,result)
    else if (parts[index] == "..") {
        if (len(result) == 0) raise failure("invalid-target","Relationship target escapes package root")
        else clean(parts,failure,index+1,slice(result,0,len(result)-1))
    } else clean(parts,failure,index+1,result ++ [parts[index]])
}
pub fn fragment(value,failure) string^ {
    if (any([for (ch in split(value,"")) ord(ch) < 32 or ord(ch) == 127]))
        raise failure("invalid-target","Invalid fragment characters")
    else decode(value,failure,0,"",true)^
}
pub fn target(owner, value, failure) string^ {
    if (not (value is string) or value == "" or starts_with(value,"//") or
        any([for (ch in ["\\",":","?","#"]) contains(value,ch)]) or
        any([for (ch in split(value,"")) ord(ch) < 32 or ord(ch) == 127]))
        raise failure("invalid-target","Invalid internal relationship target",owner)
    else {
        let decoded = decode(value,failure)^
        let base = if (starts_with(decoded,"/")) "" else dirname(owner)
        let normalized = clean(split(base ++ decoded,"/"),failure)^;
        if (normalized == "") raise failure("invalid-target","Empty internal relationship target",owner)
        else normalized
    }
}
pub fn dirname(path) {
    let parts = split(path,"/");
    if (len(parts) < 2) "" else join(slice(parts,0,len(parts)-1),"/") ++ "/"
}

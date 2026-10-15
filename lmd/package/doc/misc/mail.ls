// RFC 5322/MIME serialization is pure; raw source and reconstructed MIME are explicit.
import c: .common
fn headers(value) array^ {
    if (value == null) []
    else if (value is array) [for (entry in value) {
        if (not (entry is map) or entry.name == null or entry.value == null) raise c.fail("header entry requires name and value")
        else {name:lower(c.token(entry.name,"header name","_")^),value:entry.value}
    }]
    else if (value is map) [for (name,value in value,item in if (lower(string(name)) in ["from","to","cc","bcc","reply-to","sender"]) [value] else c.sequence(value))
        {name:lower(c.token(name,"header name","_")^),value:item}]
    else raise c.fail("headers must be a map or ordered entry array")
}
fn encoded_words(value) {
    let folded = c.fold_line(value,45)
    let chunks = split(slice(folded,0,len(folded)-2),"\r\n ");
    join([for (chunk in chunks) "=?UTF-8?B?" ++ c.base64(chunk) ++ "?="]," ")
}
fn header_text(value) string^ {
    let valid_type = if (not (value is string or value is symbol or value is int)) raise c.fail("header value must be text")
    let text = string(value);
    if (any([for (ch in split(text,"")) (ord(ch) < 32 and ord(ch) != 9) or ord(ch) == 127])) raise c.fail("header value contains a control character")
    else text
}
fn mailbox(value) string^ {
    if (value is map) {
        let address = c.single_line(value.address,"email address")^
        let display = c.get(value,"name","");
        if (contains(address,"<") or contains(address,">") or contains(address," ") or contains(address,"\t") or not contains(address,"@")) raise c.fail("invalid email address")
        else if (display == "") address
        else {
            let label = c.single_line(display,"display name")^;
            (if (any([for (ch in split(label,"")) ord(ch) > 127])) encoded_words(label)
                else "\"" ++ replace(replace(label,"\\","\\\\"),"\"","\\\"") ++ "\"") ++ " <" ++ address ++ ">"
        }
    } else header_text(value)^
}
fn header_value(name,value) string^ {
    let address_field = name in ["from","to","cc","bcc","reply-to","sender","return-path"]
    let text = if (address_field) join([for (item in c.sequence(value)) mailbox(item)^],", ")
        else if (value is array or value is list) join([for (item in value) header_text(item)^]," ")
        else header_text(value)^
    let non_ascii = any([for (ch in split(text,"")) ord(ch) > 127]);
    if (non_ascii and name in ["subject","comments","content-description"]) encoded_words(text)
    else if (non_ascii) raise c.fail("non-ASCII structured header requires an encoded display name or an ASCII wire value")
    else text
}
fn header(entry) string^ {
    let name = c.token(entry.name,"header name","_")^
    let value = header_value(lower(name),entry.value)^
    let words = split(value," ")
    let ends = if (len(words) == 0) [] else math.cumsum([for (word in words) c.byte_count(word)+1])
    let early_fold = c.byte_count(name)+2+c.byte_count(c.get(words,0,"")) > 78
    let offsets = [for (i,word in words) ends[i]-c.byte_count(word)-1+(if (early_fold) 1 else c.byte_count(name)+2)]
    let folded = name ++ (if (early_fold) ":\r\n " else ": ") ++ join([for (i,word in words)
        (if (i == 0) "" else if (floor(offsets[i]/70) != floor(offsets[i-1]/70)) "\r\n " else " ") ++ word],"");
    if (any([for (line in split(folded,"\r\n")) c.byte_count(line) > 998])) raise c.fail("email header has an unbreakable line longer than 998 octets")
    else folded ++ "\r\n"
}
fn header_lines(values) string^ => join([for (entry in values) header(entry)^],"")
fn find(values,name,fallback = null) {
    let matches = [for (entry in values where lower(entry.name) == name) entry.value];
    if (len(matches) == 0) fallback else matches[len(matches)-1]
}
fn hex_byte(value) {
    let alphabet = "0123456789ABCDEF";
    "=" ++ slice(alphabet,int(floor(value/16)),int(floor(value/16))+1) ++ slice(alphabet,value%16,value%16+1)
}
fn quoted_printable(value) => join([for (line in split(c.newline(value),"\n")) {
    let bytes = binary(line);
    join([for (start in range(0,len(bytes),25)) join([for (i in range(start,min(len(bytes),start+25),1)) hex_byte(int(bytes[i]))],"")],"=\r\n")
}],"\r\n")
fn transfer(content,encoding) string^ {
    if (encoding == "base64") c.base64_lines(content)
    else if (encoding == "quoted-printable" and content is string) quoted_printable(content)
    else if (encoding in ["7bit","8bit"] and content is string) {
        let wire = c.crlf(content);
        if (contains(wire,chr(0)) or (encoding == "7bit" and any([for (ch in split(wire,"")) ord(ch) > 127])) or
            any([for (line in split(wire,"\r\n")) c.byte_count(line) > 998])) raise c.fail("body is incompatible with " ++ encoding)
        else wire
    } else raise c.fail("unsupported MIME transfer encoding " ++ string(encoding))
}
fn boundary(parts,index = 0) string^ {
    let candidate = "=_Lambda_MIME_" ++ string(index);
    if (index > 128) raise c.fail("cannot choose a noncolliding MIME boundary")
    else if (any([for (part in parts) contains(part,candidate)])) boundary(parts,index+1)^
    else candidate
}
fn entity(value,depth) map^ {
    let valid_depth = if (depth > 64) raise c.fail("MIME nesting exceeds 64")
    let part = if (value is map) value else {content:value}
    let retained = headers(part.headers)^
    let content = c.get(part,"content",c.get(part,"body",""))
    let content_type = c.single_line(c.get(part,"content_type",c.get(part,"type",find(retained,"content-type",
        if (content is element) "text/html; charset=UTF-8" else if (content is binary) "application/octet-stream" else "text/plain; charset=UTF-8"))),"content type")^
    let others = [for (entry in retained where not (entry.name in ["content-type","content-transfer-encoding","mime-version"]) and
        not (entry.name == "content-disposition" and part.filename != null) and not (entry.name == "content-id" and part.content_id != null)) entry]
    let issues = c.unknown(part,["content","body","content_type","type","encoding","headers","parts","filename","content_id","disposition"],"MIME part field");
    if (part.parts != null) {
        let valid_parts = if (not (part.parts is array) or len(part.parts) == 0) raise c.fail("multipart requires a nonempty parts array")
        let parts = [for (child in part.parts) entity(child,depth+1)^]
        let serialized = [for (child in parts) header_lines(child.headers)^ ++ "\r\n" ++ child.body]
        let delimiter = boundary(serialized)^
        let subtype = if (starts_with(lower(content_type),"multipart/")) split(content_type,";")[0] else "multipart/mixed"
        let mime_headers = [*others,{name:"content-type",value:subtype ++ "; boundary=\"" ++ delimiter ++ "\""}]
        let body = join([for (child in serialized) "--" ++ delimiter ++ "\r\n" ++ child ++ (if (ends_with(child,"\r\n")) "" else "\r\n")],"") ++ "--" ++ delimiter ++ "--\r\n";
        {headers:mime_headers,body:body,diagnostics:issues ++ [for (child in parts,issue in child.diagnostics) issue]}
    } else {
        let encoding = lower(c.get(part,"encoding","base64"))
        let content_text = if (content is element) lambda.sys.format(content,"html")^ else content;
        let valid_content = if (not (content_text is string or content_text is binary)) raise c.fail("MIME content must be string, binary, or HTML element")
        else if (starts_with(lower(content_type),"multipart/")) raise c.fail("multipart content requires parts")
        else if (content_text is string and contains(lower(content_type),"charset=") and not contains(lower(content_type),"utf-8") and not contains(lower(content_type),"us-ascii"))
            raise c.fail("MIME text serialization currently requires UTF-8 or US-ASCII")
        else if (content_text is string and contains(lower(content_type),"us-ascii") and any([for (ch in split(content_text,"")) ord(ch) > 127]))
            raise c.fail("non-ASCII content cannot declare US-ASCII")
        let filename = if (part.filename == null) null else c.single_line(part.filename,"attachment filename")^
        let disposition = c.single_line(c.get(part,"disposition",if (filename == null) "inline" else "attachment"),"content disposition")^
        let file_headers = if (filename == null) [] else [{name:"content-disposition",value:disposition ++ "; filename*=UTF-8''" ++ url_encode(filename)}]
        let id_headers = if (part.content_id == null) [] else [{name:"content-id",value:c.single_line(part.content_id,"content ID")^}];
        {headers:[*others,{name:"content-type",value:content_type},{name:"content-transfer-encoding",value:encoding},*file_headers,*id_headers],
            body:transfer(if (content_text is string and starts_with(lower(content_type),"text/")) c.crlf(content_text) else content_text,encoding)^,diagnostics:issues}
    }
}
fn url_encode(value) {
    let bytes = binary(value);
    join([for (i in range(0,len(bytes),1),let byte = int(bytes[i])) if ((byte >= 65 and byte <= 90) or (byte >= 97 and byte <= 122) or (byte >= 48 and byte <= 57) or byte in [45,46,95,126]) chr(int(byte))
        else "%" ++ slice(hex_byte(int(byte)),1)],"")
}
pub fn render(value) map^ {
    let valid_message = if (not (value is map)) raise c.fail("EML requires a message map")
    let retained = headers(c.get(value,"header_entries",c.get(value,"headers",{})))^
    let top = [for (field,name in {from:"from",to:"to",cc:"cc",bcc:"bcc",reply_to:"reply-to",subject:"subject",date:"date",message_id:"message-id"}
        where value[field] != null and find(retained,name) == null) {name:name,value:value[field]}]
    let all_headers = [*retained,*top]
    let body = c.get(value,"body","")
    let attachments = c.sequence(value.attachments)
    let original_type = find(all_headers,"content-type","")
    let replaced_multipart = value.raw_body == null and body is element and starts_with(lower(original_type),"multipart/")
    let raw = value.raw_body != null or (body is string and find(all_headers,"content-transfer-encoding") != null and len(attachments) == 0)
    let mime = if (raw) {
        let valid_attachments = if (len(attachments) > 0) raise c.fail("remove raw_body before adding attachments")
        let source = c.get(value,"raw_body",body);
        if (not (source is string)) raise c.fail("raw_body must be a string")
        else {headers:[],body:c.crlf(source),diagnostics:[]}
    } else if (len(attachments) > 0) entity({type:"multipart/mixed",parts:[body,*attachments]},0)^
    else if (body is map) entity(body,0)^
    else entity({content:body,content_type:if (body is element) "text/html; charset=UTF-8" else
        find(all_headers,"content-type",if (body is binary) "application/octet-stream" else "text/plain; charset=UTF-8")},0)^
    let selected = if (raw) all_headers else [for (entry in all_headers where not (entry.name in ["content-type","content-transfer-encoding","mime-version"])) entry]
    let output_headers = [*selected,*if (raw or find(selected,"mime-version") != null) [] else [{name:"mime-version",value:"1.0"}],*mime.headers]
    let issues = mime.diagnostics ++ [for (name in ["from","date"] where find(output_headers,name) == null) c.issue("missing-header",value,"Email requires " ++ name)] ++
        (if (replaced_multipart) [c.issue("mime-source-unavailable",value,"Rebuilding the display HTML does not retain the original multipart attachments")] else []) ++
        c.unknown(value,["headers","header_entries","body","raw_body","attachments","from","to","cc","bcc","reply_to","subject","date","message_id"],"email field");
    c.result(header_lines(output_headers)^ ++ "\r\n" ++ mime.body ++ (if (ends_with(mime.body,"\r\n")) "" else "\r\n"),issues)
}

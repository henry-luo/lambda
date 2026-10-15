import math
import c: .common
import g: .grammar

fn line_kind(record) {
    let kind = c.kind(record);
    if (kind == "Added" or kind == "NewPath") "add"
    else if (kind == "Removed" or kind == "OldPath") "remove"
    else if (kind == "Context") "context" else null
}
fn old_step(record) => if (line_kind(record) in ["context","remove"]) 1 else 0
fn new_step(record) => if (line_kind(record) in ["context","add"]) 1 else 0
fn no_newline(record) => c.kind(record) == "NoNewline"
fn payload_end(source,record) => record.end - (if (source[record.end - 1] == 10) 1 else 0)

fn hunk(source,records,at,limit) map^ {
    let header = records[at]
    let old_start = c.natural(header.old.line,header.start)^
    let new_start = c.natural(header.new.line,header.start)^
    let old_count = c.count_of(header.old)^
    let new_count = c.count_of(header.new)^
    let nonempty = c.require(old_count + new_count > 0 and (old_count == 0 or old_start > 0) and
        (new_count == 0 or new_start > 0),"invalid hunk range",header.start)^
    let body = slice(records,at + 1,limit)
    let old = math.cumsum([for (record in body) old_step(record)])
    let new = math.cumsum([for (record in body) new_step(record)])
    let ends = [for (i,record in body where old[i] == old_count and new[i] == new_count and line_kind(record) != null) i + 1]
    let complete = c.require(len(ends) > 0,"hunk line counts do not match its header",header.start)^
    let consumed_count = ends[0] + (if (no_newline(body[ends[0]])) 1 else 0)
    let consumed = take(body,consumed_count)
    let valid = c.require(all([for (i,record in consumed)
        line_kind(record) != null or (no_newline(record) and i > 0 and line_kind(consumed[i - 1]) != null)]),
        "unexpected record inside hunk",header.start)^
    let lines = [for (i,record in consumed where line_kind(record) != null) {
        let kind = line_kind(record)
        let missing = no_newline(consumed[i + 1])
        let final = c.require(not missing or ((old_step(record) == 0 or old[i] == old_count) and
            (new_step(record) == 0 or new[i] == new_count)),"newline marker precedes more source lines",record.start)^
        let terminated = c.require(source[record.end - 1] == 10,"unterminated patch payload",record.start)^;
        {kind:kind,old_line:if (old_step(record) == 1) old_start + old[i] - 1 else null,
         new_line:if (new_step(record) == 1) new_start + new[i] - 1 else null,
         start:record.start + 1,end:payload_end(source,record),
         old_no_newline:missing and old_step(record) == 1,new_no_newline:missing and new_step(record) == 1}
    }];
    // hunk titles use the same explicit invalid-byte display as payload rows.
    {record:at,record_end:at + consumed_count + 1,start:header.start,end:consumed[consumed_count - 1].end,
     old_start:old_start,old_count:old_count,new_start:new_start,new_count:new_count,
     header:c.display(slice(source,header.start,payload_end(source,header)))^,lines:lines}
}

fn escaped(value,offset) binary^ {
    let bytes = binary(value)
    let length = len(bytes)
    let controls = {a:7,b:8,t:9,n:10,v:11,f:12,r:13}
    let text = c.text(value)^
    let octal = length == 3
    let valid = c.require(if (octal) all([for (b in bytes) b >= 48 and b <= 55])
        else length == 1 and (text in ["\\","\""] or controls[text] != null),"invalid quoted-path escape",offset)^
    let byte = if (octal) (bytes[0] - 48) * 64 + (bytes[1] - 48) * 8 + bytes[2] - 48
        else if (controls[text] != null) controls[text] else bytes[0]
    let bounded = c.require(byte > 0 and byte <= 255,"invalid path byte",offset)^;
    binary([byte])^
}
pub fn path_bytes(value,offset = null) binary^ {
    let raw = binary(value)
    let valid = c.require(len(raw) > 0 and not (0 in raw),"empty path or embedded NUL",offset)^;
    if (raw[0] != 34) raw
    else {
        let tree = lambda.sys.parse(raw,g.paths,'Quoted')^
        let parts = [for (part in content(tree)) if (c.kind(part) == "Run") binary(part.value)
            else escaped(part.value,offset)^];
        let decoded = reduce(parts,(a,b) => a ++ b)
        let nonempty = c.require(len(decoded) > 0,"empty quoted path",offset)^;
        decoded
    }
}
fn named_path(value,strip,offset) map^ {
    let raw = path_bytes(value,offset)^
    let path = if (raw == binary("/dev/null")) null else if (strip and
        (slice(raw,0,2) == binary("a/") or slice(raw,0,2) == binary("b/"))) slice(raw,2,len(raw)) else raw;
    if (path == null) {raw:null,display:null} else {raw:path,display:c.display(path)^}
}
fn matching_pair(raw,i) => if (slice(raw,0,2) == binary("a/") and slice(raw,i + 1,i + 3) == binary("b/") and
    slice(raw,2,i) == slice(raw,i + 3,len(raw))) {before:slice(raw,0,i),after:slice(raw,i + 1,len(raw))} else null
fn header_paths(header) map^ {
    let raw = binary(header.paths)
    let equal_pairs = [for (i,b in raw where b == 32)
        for (candidate in [matching_pair(raw,i)] where candidate != null) candidate]
    let pair = if (len(equal_pairs) == 1) null else lambda.sys.parse(raw,g.paths,{spans:true}) ^ { null }
    let valid = c.require(pair != null or len(equal_pairs) == 1,"ambiguous Git header paths",header.start)^;
    if (pair != null) {before:slice(raw,pair.before.start,pair.before.end),after:slice(raw,pair.after.start,pair.after.end)}
    else equal_pairs[0]
}
fn inside_hunk(hunks,positions,index) {
    let i = c.lower_bound(positions,index + 1) - 1;
    i >= 0 and index < hunks[i].record_end
}
fn metadata(records,kind) => [for (record in records where c.kind(record) == "Metadata" and c.text(record.kind)^ == kind) record]
fn meta_value(records,kind) {
    let matches = metadata(records,kind);
    if (len(matches) == 0) null else matches[0].value
}
fn path_record(record,strip,timestamp) {
    let raw = binary(record.path)
    let tabs = if (timestamp) [for (i,b in raw where b == 9) i] else []
    let value = if (len(tabs) > 0) take(raw,tabs[0]) else raw;
    named_path(value,strip,record.start)^
}
fn file(source,records,at,end,hunks,hunk_positions,git) map^ {
    let first_hunk = c.lower_bound(hunk_positions,at)
    let final_hunk = c.lower_bound(hunk_positions,end)
    let own_hunks = slice(hunks,first_hunk,final_hunk)
    let first_body = if (len(own_hunks) > 0) own_hunks[0].record else end
    let headers = slice(records,at,first_body)
    let old_paths = [for (record in headers where c.kind(record) == "OldPath") record]
    let new_paths = [for (record in headers where c.kind(record) == "NewPath") record]
    let rename_from = c.get({value:meta_value(headers,"rename from ")},"value",meta_value(headers,"copy from "))
    let rename_to = c.get({value:meta_value(headers,"rename to ")},"value",meta_value(headers,"copy to "))
    let from_header = if (git and len(old_paths) == 0 and rename_from == null) header_paths(records[at])^ else null
    let old = if (rename_from != null) named_path(rename_from,false,records[at].start)^
        else if (len(old_paths) == 1) path_record(old_paths[0],git,not git)^
        else if (from_header != null) named_path(from_header.before,true,records[at].start)^ else {raw:null,display:null}
    let new = if (rename_to != null) named_path(rename_to,false,records[at].start)^
        else if (len(new_paths) == 1) path_record(new_paths[0],git,not git)^
        else if (from_header != null) named_path(from_header.after,true,records[at].start)^ else {raw:null,display:null}
    let new_file = meta_value(headers,"new file mode ") != null or (len(old_paths) == 1 and old.raw == null)
    let deleted = meta_value(headers,"deleted file mode ") != null or (len(new_paths) == 1 and new.raw == null)
    let binary_records = [for (record in headers where c.kind(record) == "BinaryHeader") record]
    let binary_file = len(binary_records) > 0
    // inspect only this file's records when locating an opaque binary body.
    let binary_bodies = [for (i in at to end - 1 where
        c.kind(records[i]) == "BinaryHeader" and records[i].patch != null) i]
    let binary_start = if (len(binary_bodies) > 0) binary_bodies[0] else end
    let bad = [for (index in at to end - 1 where not inside_hunk(hunks,hunk_positions,index))
        for (record in [records[index]] where not (c.kind(record) in ["GitHeader","OldPath","NewPath","Metadata","BinaryHeader","Empty"]) and
            not (binary_file and index > binary_start)) record.start]
    let header_valid = c.require(len(old_paths) <= 1 and len(new_paths) <= 1 and
        (len(own_hunks) == 0 or (len(old_paths) == 1 and len(new_paths) == 1)),"missing or repeated file paths",records[at].start)^
    let records_valid = c.require(len(bad) == 0,"unexpected record outside a hunk",if (len(bad) > 0) bad[0] else null)^
    let ordered = c.require(all([for (i,h in own_hunks where i > 0)
        h.old_start >= own_hunks[i - 1].old_start + own_hunks[i - 1].old_count and
        h.new_start >= own_hunks[i - 1].new_start + own_hunks[i - 1].new_count]),"overlapping or out-of-order hunks",records[at].start)^
    let index_value = meta_value(headers,"index ")
    let index_parts = if (index_value == null) [] else split(c.text(index_value)^," ")
    let modes = if (len(index_parts) > 1) index_parts[1] else null
    let old_mode = c.get({value:meta_value(headers,"old mode ")},"value",c.get({value:meta_value(headers,"deleted file mode ")},"value",modes))
    let new_mode = c.get({value:meta_value(headers,"new mode ")},"value",c.get({value:meta_value(headers,"new file mode ")},"value",modes))
    let kind = if (new_file) "added" else if (deleted) "deleted" else if (meta_value(headers,"rename from ") != null) "renamed"
        else if (meta_value(headers,"copy from ") != null) "copied" else if (len(own_hunks) == 0 and old_mode != new_mode) "mode" else "modified";
    {id:at,start:records[at].start,end:records[end - 1].end,
     old_path:if (new_file) null else old.raw,new_path:if (deleted) null else new.raw,
     old_name:if (new_file) null else old.display,new_name:if (deleted) null else new.display,
     kind:kind,binary:binary_file,submodule:c.text(old_mode)^ == "160000" or c.text(new_mode)^ == "160000",
     old_mode:if (old_mode == null) null else c.text(old_mode)^,new_mode:if (new_mode == null) null else c.text(new_mode)^,
     index:if (index_value == null) null else c.text(index_value)^,hunks:own_hunks,
     added:sum([for (h in own_hunks,line in h.lines where line.kind == "add") 1]),
     removed:sum([for (h in own_hunks,line in h.lines where line.kind == "remove") 1]),loaded:true}
}

pub fn parse(source,options = null) map^ {
    let opts = c.options(options,["max_bytes","max_records","profile"])^
    let input_valid = c.require(source == null or source is string or source is binary,"source must be text or bytes")^
    let bytes = if (len(source) == 0) null else binary(source)
    let maximum = c.get(opts,"max_bytes",16777216)
    let limits = c.require(maximum is int and maximum > 0 and len(bytes) <= maximum,"patch exceeds max_bytes")^
    let tree = lambda.sys.parse(if (bytes == null) "" else bytes,g.patch,{spans:true})^
    let records = content(tree)
    let record_limit = c.get(opts,"max_records",200000)
    let bounded = c.require(record_limit is int and record_limit > 0 and len(records) <= record_limit,"patch exceeds max_records")^
    let combined = c.require(not any([for (r in records) c.kind(r) == "Combined"]),"combined merge patches are unsupported")^
    let positions = [for (i,r in records where c.kind(r) == "HunkHeader") i]
    let git_headers = [for (i,r in records where c.kind(r) == "GitHeader") i]
    let git = len(git_headers) > 0
    let profile = c.text(c.get(opts,"profile","auto"))^
    let profile_ok = c.require(profile in ["auto","git","unified"] and
        (profile == "auto" or len(records) == 0 or git == (profile == "git")),"patch profile does not match source")^
    let hunks = [for (i,pos in positions) {
        let next_hunk = if (i + 1 < len(positions)) positions[i + 1] else len(records)
        let next_file = c.lower_bound(git_headers,pos + 1)
        let limit = min(next_hunk,if (next_file < len(git_headers)) git_headers[next_file] else len(records));
        hunk(bytes,records,pos,limit)^
    }]
    let starts = if (git) git_headers else [for (i,r in records where c.kind(r) == "OldPath" and
        c.kind(records[i + 1]) == "NewPath" and not inside_hunk(hunks,positions,i)) i]
    let first = if (len(starts) > 0) starts[0] else len(records)
    let prefix = c.require(all([for (r in take(records,first)) c.kind(r) == "Empty"]),"expected a file header")^
    let no_orphan = c.require(all([for (p in positions) len(starts) > 0 and p >= starts[0]]),"hunk has no file header")^
    let files = [for (i,position in starts) {*:file(bytes,records,position,
        if (i + 1 < len(starts)) starts[i + 1] else len(records),hunks,positions,git)^,id:i}];
    {kind:"diff",origin:"patch",profile:if (git) "git" else "unified",source:bytes,files:files,complete:true,
     added:sum([for (f in files) f.added]),removed:sum([for (f in files) f.removed]),diagnostics:[]}
}

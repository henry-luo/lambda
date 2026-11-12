// immutable ancestry pages contain small headers; each endpoint is an independent compact payload.
import repo: .repository
import session: .session
import core: .core
import protocol: .protocol
import codec: .codec
import v: .schema

pub fn roots(record) => if (record.history == null) [] else record.history
pub fn retired(current) => if (current.retention == null) [] else current.retention.versions
pub pn headers(tx, keys, roots) any^ {
    var pending = roots
    var seen = []
    var entries = []
    var referenced = []
    while (len(pending)^ > 0) {
        let id = pending[0]
        pending = slice(pending,1)^
        if (contains(seen,id)^) { continue }
        if (len(seen)^ >= 65536) raise error("sync history: traversal budget exceeded")
        let page = repo.get(tx,keys,id,"history")^
        v.fields(page,["version","document","previous","entries"])^
        if (page.version != 1 or not v.actor(page.document)^ or not (page.previous is array) or
            len(page.previous)^ > 64 or not (page.entries is array) or len(page.entries)^ > 16)
            raise error("sync history: invalid ancestry page")
        for (header in page.entries) {
            v.fields(header,["id","group","object","parents","frontier","checkpoint","raw_hash","saved_at"] )^
            if (header.group != page.document or not (header.saved_at is number) or header.saved_at < 0)
                raise error("sync history: invalid endpoint header")
            v.clock(header.frontier)^
            let old = v.lookup(entries,header.id)^
            if (old != null and old != header) raise error("sync history: conflicting endpoint binding")
            if (old == null) { entries = [*entries,header] }
        }
        seen = [*seen,id]
        referenced = [*referenced,*page.previous]
        pending = [*pending,*page.previous]
    }
    return {entries:sort(entries,(item)=>item.id)^,pages:seen,referenced:unique(referenced)}
}
pub pn endpoint(tx, keys, header) any^ {
    let saved = repo.get(tx,keys,header.object,"version")^
    session.validate_version(saved)^
    if (saved.id != header.id or saved.parents != header.parents or saved.frontier != header.frontier or
        codec.digest(protocol.checkpoint(saved.replica))^ != header.checkpoint or
        hash(saved.raw,'sha256')^ != header.raw_hash or
        header.saved_at != (if (saved.save_metadata == null) 0 else saved.save_metadata.at))
        raise error("sync history: endpoint binding mismatch")
    return saved
}
pub pn expand(tx, keys, record, expired=[]) any^ {
    if (record.history == null) { return record }
    let catalog = headers(tx,keys,record.history)^
    let versions = [for (header in catalog.entries where not contains(expired,header.id)^) endpoint(tx,keys,header)^]
    return {*:record,session:{*:record.session,versions:versions,saved_heads:session.heads({versions:catalog.entries})^}}
}
pub pn flush(tx, keys, record, milliseconds) any^ {
    let catalog = headers(tx,keys,roots(record))^
    var additions = []
    for (saved in record.session.versions) {
        if (v.lookup(catalog.entries,saved.id)^ == null) {
            session.validate_version(saved)^
            let value = {*:saved,replica:core.compact(saved.replica)}
            let id = repo.store_object(tx,keys,"version",value)^
            additions = [*additions,{id:saved.id,group:record.id,object:id,parents:saved.parents,
                frontier:saved.frontier,checkpoint:codec.digest(protocol.checkpoint(saved.replica))^,raw_hash:hash(saved.raw,'sha256')^,
                saved_at:if (saved.save_metadata == null) 0 else saved.save_metadata.at}]
        }
    }
    var pages = [for (id in roots(record) where not contains(catalog.referenced,id)^) id]
    while (len(pages)^ > 64) {
        var level = []
        for (offset in range(0,len(pages)^,64)) {
            level = [*level,repo.store_object(tx,keys,"history",{version:1,document:record.id,
                previous:slice(pages,offset,offset+64)^,entries:[]})^]
        }
        pages = level
    }
    if (len(additions)^ == 0 and len(pages)^ > 1) {
        pages = [repo.store_object(tx,keys,"history",{version:1,document:record.id,previous:sort(pages)^,entries:[]})^]
    }
    for (offset in range(0,len(additions)^,16)) {
        let page = {version:1,document:record.id,previous:pages,entries:slice(additions,offset,offset+16)^}
        pages = [repo.store_object(tx,keys,"history",page)^]
    }
    return {*:record,history:pages,session:{*:record.session,saved_heads:session.heads({versions:[*catalog.entries,*additions]})^}}
}
pub fn pack(record) => {*:record,session:{*:record.session,versions:[]}}
pub pn expand_public(tx, keys, value, expired=[]) any^ {
    if (value.version == 1) { return value }
    v.fields(value,["version","id","entry","format","origin","live","history"])^
    if (value.version != 2) raise error("sync history: unsupported document root")
    let catalog = headers(tx,keys,value.history)^
    if (not all([for (header in catalog.entries) header.group == value.id]))
        raise error("sync history: wrong document pages")
    return {*:value,versions:[for (header in catalog.entries where not contains(expired,header.id)^) endpoint(tx,keys,header)^]}
}
pub pn page(tx, keys, record, expired=[], cursor=0, limit=32) any^ {
    if (not v.natural(cursor)^ or not v.natural(limit)^ or limit < 1 or limit > 128)
        raise error("sync history: invalid page request")
    let entries = sort(headers(tx,keys,roots(record))^.entries,(item)=>[item.saved_at,item.id])^
    let page = slice(entries,cursor,cursor+limit)^
    return {versions:[for (header in page) {*:header,availability:if (contains(expired,header.id)^) 'retired' else 'retained'}],
        next:if (cursor+len(page)^ < len(entries)^) cursor+len(page)^ else null,root:codec.digest([roots(record),sort(expired)^])^}
}

// pure persistence format. The host atomically stores bytes before marking durability.
import c: .core
import v: .schema
import codec: .codec
import p: .protocol
import s: .session

fn validate(endpoint, session) any^ {
    p.validate_endpoint(endpoint)^
    var i = 0
    if (session != null) {
        c.validate(session.live)^
        if (c.compact(session.live) != c.compact(endpoint.replica)) raise error("sync store: endpoint/session mismatch")
        s.validate_version(session.base)^
        i = 0
        while (i < len(session.versions)^) { s.validate_version(session.versions[i])^; i = i+1 }
    }
    return true
}

pub fn seal(endpoint, session=null) any^ {
    validate(endpoint,session)^
    let data = {endpoint:endpoint,session:session}
    return codec.encode({format:"lambda-sync-bundle",version:1,digest:codec.digest(data)^,data:data})^
}

pub fn load(encoded, actor) any^ {
    let bundle = codec.decode(encoded)^
    v.fields(bundle,["format","version","digest","data"])^
    if (bundle.format != "lambda-sync-bundle" or bundle.version != 1 or bundle.digest != codec.digest(bundle.data)^)
        raise error("sync store: corrupt bundle")
    let endpoint = bundle.data.endpoint
    validate(endpoint,bundle.data.session)^
    // cloned/rolled-back stores cannot safely resume the same counter stream.
    if (not p.fresh_writer(endpoint,actor)^)
        raise error("sync store: restoration requires a fresh writer actor")
    return {endpoint:endpoint,session:bundle.data.session,actor:actor}
}

// Fixed slots bound storage by the membership limit; expired records retain sequence fences.
import rv: .rendezvous
import schema: .cloud_schema
import codec: .codec
import v: .schema

fn key(kind, actor, target) any^ {
    if (not contains(["presence","offer","answer"],kind)^ or not schema.id(actor) or
        (kind == "presence" and target != null) or (kind != "presence" and not schema.id(target)))
        raise error("sync rendezvous: invalid slot")
    return "rendezvous/" ++ kind ++ "/" ++ actor ++ (if (target == null) "" else "/" ++ target)
}
pn current(store, prefix, kind, actor, target) any^ {
    let found = store.get(prefix ++ key(kind,actor,target)^)^
    return if (found == null) {record:null,etag:null} else {record:codec.decode(found.body,131072)^,etag:found.etag}
}
pub pn handle(store, prefix, control, envelope, at) any^ {
    let body = envelope.body
    if (envelope.kind == "rendezvous_current") {
        v.fields(body,["kind","target"])^
        if (body.target != null) schema.member(control,body.target)^
        return current(store,prefix,body.kind,envelope.actor,body.target)^
    }
    if (envelope.kind == "rendezvous_put") {
        v.fields(body,["record","expected"])^
        let encoded = codec.encode(body.record)^
        if (len(binary(encoded)^) > 122880) raise error("sync rendezvous: record budget")
        let value = rv.check(body.record,schema.member(control,envelope.actor)^,control.space,at)^
        if (value.target != null) schema.member(control,value.target)^
        let captured = current(store,prefix,value.kind,envelope.actor,value.target)^
        if (captured.record == body.record) { return {written:true,etag:captured.etag} }
        if (body.expected != captured.etag or value.sequence !=
            (if (captured.record == null) 1 else captured.record.body.sequence+1)) { return {written:false} }
        return store.write(prefix ++ key(value.kind,envelope.actor,value.target)^,encoded,captured.etag)^
    }
    if (envelope.kind == "rendezvous_get") {
        v.fields(body,["kind","actors"])^
        // eight 120 KiB records leave room inside the function's 1 MiB response envelope.
        if (not (body.actors is array) or len(body.actors)^ > 8 or len(unique(body.actors))^ != len(body.actors)^)
            raise error("sync rendezvous: query budget")
        var result = []
        for (actor in body.actors) {
            let device = schema.member(control,actor)^
            let captured = current(store,prefix,body.kind,actor,if (body.kind == "presence") null else envelope.actor)^
            if (captured.record != null and captured.record.body.expires > at) {
                rv.check(captured.record,device,control.space,at)^
                result = [*result,captured.record]
            }
        }
        return result
    }
    raise error("sync rendezvous: unknown action")
}

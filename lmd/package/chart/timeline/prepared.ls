// Immutable inputs can reuse normalization without exposing reference identity (S5.1.4v2).
import resource: .resource
import schema: .schema

pub fn record(raw, snapshot, normalized: map | error) => {raw:raw,entry:snapshot.resources.source,normalized:normalized,
    fallback:if (normalized is error) schema.validate(raw) else normalized}

pub fn source(raw, snapshot = null, previous = null) {
    if (previous!=null and previous.raw==raw and previous.entry==snapshot.resources.source) previous
    else record(raw,snapshot,resource.normalize(raw,snapshot))
}

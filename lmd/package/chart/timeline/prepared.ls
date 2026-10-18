// Immutable inputs can reuse normalization without exposing reference identity (S5.1.4v2).
import resource: .resource
import schema: .schema
import layout: .layout
import index: .index

// Source indices survive window/selection changes; filters invalidate only visibility.
pub fn record(raw, snapshot, normalized: map | error, previous = null) {
    // S5.1.5: an error-bearing comparison is a cache miss, handled explicitly.
    let same=previous!=null and (normalized==previous.normalized) ^ { false };
    let base=if (same) previous.source_normalized else normalized;
    let source_index=if (same) previous.normalized._timeline.index else if (base is error) null else index.build(base);
    let key=if (base is error) null else layout.visibility_key(base,snapshot);
    let reused=same and key==previous.normalized._timeline.key;
    let shown=if (base is error) null else if (reused) previous.normalized._timeline.visible else layout.visible(base,snapshot);
    let visible_index=if (shown is error or base is error) null else if (reused) previous.normalized._timeline.visible_index
        else if (len(shown.events)==len(base.events)) {*:source_index,events:shown.events}
        else index.build({*:base,events:shown.events});
    let prepared=if (base is error) base else {*:base,_timeline:{index:source_index,key:key,visible:shown,visible_index:visible_index,
        domain:if (same) previous.normalized._timeline.domain else layout.extent(base)}};
    if (reused and previous.raw==raw and previous.entry==snapshot.resources.source) previous else {raw:raw,entry:snapshot.resources.source,source_normalized:base,normalized:prepared,
        normalizations:counters(previous,"normalizations")+(if (same) 0 else 1),
        visibility_builds:counters(previous,"visibility_builds")+(if (base is error or reused) 0 else 1),
        fallback:if (base is error) schema.validate(raw) else prepared}
}
fn counters(previous, key) => if (previous[key]!=null) previous[key] else 0

pub fn geometry_key(spec, viewport, snapshot) => {spec:spec,viewport:viewport,window:snapshot.window,expanded:snapshot.expanded_events,
        clusters:snapshot.expanded_clusters,collapsed:snapshot.collapsed_groups,query:snapshot.query,
        filters:snapshot.filters,theme:snapshot.theme,view:snapshot.view,resources:snapshot.resources,draft:snapshot.draft}
pub fn geometry(spec, viewport, snapshot, previous = null) {
    let key=geometry_key(spec,viewport,snapshot);
    if (previous!=null and previous.key==key) previous else {key:key,plan:layout.normalized(spec,viewport,snapshot)}
}

pub fn source(raw, snapshot = null, previous = null) {
    if (previous!=null and previous.raw==raw and previous.entry==snapshot.resources.source)
        if (previous.normalized is error or layout.visibility_key(previous.normalized,snapshot)==previous.normalized._timeline.key) previous
        else record(raw,snapshot,previous.normalized,previous)
    else record(raw,snapshot,resource.normalize(raw,snapshot),previous)
}

// historical timelines; the seven legacy playback exports remain forwarding aliases.
import playback: .playback
import schema: .timeline.schema
import present: .timeline.present
import geometry: .timeline.layout
import vector: .timeline.svg
import reducer: .timeline.state
import live: .timeline.events
import adapters: .timeline.adapters
import story: .timeline.story
import media: .timeline.media
import resource: .timeline.resource
import exports: .timeline.export
import editing: .timeline.edit
import bookmarks: .timeline.bookmark
import prepared: .timeline.prepared
import motion: .timeline.motion

pub fn validate(spec) => schema.validate(spec)
pub fn render(spec, viewport = null, snapshot = null) {
    let normalized = prepared.source(spec,snapshot).normalized;
    if (normalized is error) normalized else present.html(normalized,viewport,snapshot)
}
pub fn render_spec(spec, viewport = null, snapshot = null) => render(spec,viewport,snapshot)
pub fn initial_state(spec) => reducer.initial_state(spec)
pub fn update(current, action, context) => reducer.update(current,action,context)
pub fn apply_edit(spec, request) => editing.apply_edit(spec,request)
pub fn bookmark(spec, id) {
    let normalized=schema.validate(spec);
    if (normalized is error) normalized else bookmarks.address(normalized,id)
}
pub fn convert(data, format, options = {}) => adapters.convert(data,format,options)
pub fn capabilities(host = "native") => media.capabilities(host)
pub fn render_frame(spec, frame, viewport = null, snapshot = null) {
    let normalized=prepared.source(spec,snapshot).normalized;
    let elapsed=if (frame is number) frame else frame.time_ms;
    let frozen=if (normalized is error) normalized else story.sample(story.plan(normalized,snapshot),elapsed);
    let sampled_state={*:snapshot_map(snapshot),*:frozen,story_frame:frozen,sample_time_ms:elapsed};
    let target=if (frame.previous!=null and not (normalized is error)) geometry.normalized(normalized,viewport,sampled_state) else null;
    let sampled=if (target!=null and not (target is error)) motion.sample(frame.previous,
        {plan:target,svg:vector.render(target,sampled_state,{unsupported:"omit",cards:normalized.view=="story"})},elapsed,normalized) else target;
    if (frozen is error) frozen else if (sampled is error) sampled else present.html(normalized,viewport,sampled_state,sampled)
}
fn snapshot_map(value) => if (value is map) value else {}
pub fn layout(spec, viewport, snapshot = null, resources = {}) {
    let normalized=prepared.source(spec,snapshot).normalized;
    if (normalized is error) normalized else geometry.normalized(normalized,viewport,snapshot,resources)
}
pub fn render_svg(spec, viewport, snapshot = null, options = {}) {
    let plan=layout(spec,viewport,snapshot);
    if (plan is error) plan else vector.render(plan,snapshot,options)
}

// compatibility boundary for a skin that supplies an existing list root.
pub fn render_cards(spec, snapshot = null) {
    let normalized = schema.validate(spec);
    if (normalized is error) normalized else present.cards(normalized,snapshot)
}

pub fn model(spec, viewport = null, options = {}) {
    let validated=schema.validate(spec);
    if (validated is error) validated else <timeline.chart spec:spec,viewport:viewport,options:options,
        prepared:prepared.record(spec,null,validated)>
}
pub fn interactive(spec, viewport = null, options = {}) => apply(model(spec,viewport,options))
pub pn load(request, options = {}) { resource.load(request,options) }
pub fn export_data(spec, format = "json", options = {}, snapshot = null) => exports.data(spec,format,options,snapshot)
pub fn export_document(spec, format = "html", viewport = null, options = {}, snapshot = null) => exports.document(spec,format,viewport,options,snapshot)
pub pn save(spec, destination, format = null, viewport = null, options = {}, snapshot = null) {
    exports.save(spec,destination,format,viewport,options,snapshot)
}
// the authored source is stable; mutable view state belongs to its live instance.
view <timeline.chart> state live_state:{interaction:null,token:0} {
    live.presentation(~,live_state)
}
on render(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on timeline_frame(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on click(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on dblclick(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on keydown(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on input(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on change(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on wheel(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on pointerdown(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on pointermove(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on pointerup(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on pointercancel(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on lostpointercapture(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on timeline_action(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on hashchange(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }
on popstate(evt) { live_state=live.handle(~,live_state,evt); live.deliver(live_state,evt) }

// deprecated animation API: implementation lives exclusively in chart.playback.
pub fn configure(spec) => playback.configure(spec)
pub fn inherit(base, keyframe) => playback.inherit(base,keyframe)
pub fn target(spec) => playback.target(spec)
pub fn position(plan, elapsed) => playback.position(plan,elapsed)
pub fn segment(spec, elapsed, exact = false) => playback.segment(spec,elapsed,exact)
pub fn initial(spec) => playback.initial(spec)
pub fn command(current, action, now, plan) => playback.command(current,action,now,plan)

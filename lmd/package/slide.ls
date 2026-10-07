// lambda.slide: pure compilation/sampling, with live orchestration in Lambda.
import compiler: lambda.slide.normalize
import renderer: lambda.slide.html
import sampler: lambda.slide.sample
import playback: lambda.slide.player
import live: lambda.slide.live
import paragraph: lambda.slide.paragraphs

pub fn compile(deck, options = {}) map^ => compiler.compile(deck, options)^
pub fn snapshot(deck, address = {}, options = {}) element^ {
    let plan = compiler.compile(deck, options)^;
    let at = sampler.address(plan, address)^
    renderer.document(plan, renderer.stage(plan, at, options))
}
pub fn slides(deck, options = {}) array^ {
    let plan = compiler.compile(deck, options)^;
    [for (i, scene in plan.slides) renderer.stage(plan, sampler.address(plan, {slide: i, cue: 'final'})^,
        {*: options, instance: "slide" ++ string(i)})]
}
pub fn initial_state(plan, options = {}) => playback.initial_state(plan, options)
pub fn reduce(plan, ps, event) map^ => playback.reduce(plan, ps, event)^
pub fn sample(plan, ps) array^ => playback.sample(plan, ps)^
pub fn needs_frame(ps) bool => playback.needs_frame(ps)
pub fn page(deck, options = {}) element^ {
    let plan = compiler.compile(deck, options)^;
    live.document_tree(plan, options)
}
pub fn player(deck, options) element^ {
    let required_instance = if (options.instance == null) raise error("slide: embedded players require an explicit instance ID")
    let plan = compiler.compile(deck, options)^;
    live.player_tree(plan, options)
}
pub fn paragraphs(id, content_blocks, options = {}) array^ => paragraph.build(id, content_blocks, options)^

pub fn diagnostic(plan, ps, frame_token = 0) map {
    let scene = plan.slides[ps.slide]
    let cue = if (ps.cue >= 0) scene.cues[ps.cue] else null
    {deck: plan.id, slide: scene.id, cue: cue.id, time_ms: ps.time_ms, phase: ps.phase,
        paused: ps.paused, generation: ps.generation, frame_token: frame_token,
        active_tracks: if (ps.phase == 'cue') [for (tr in cue.tracks where ps.time_ms >= tr.begin_ms and
            ps.time_ms < tr.begin_ms + tr.duration_ms * tr.repeat) {target: tr.target, channel: tr.channel}] else []}
}

pub fn handout(deck, options = {}) element^ {
    let plan = compiler.compile(deck, options)^;
    renderer.document(plan, <main class: "slide-handout",
        *[for (i, scene in plan.slides) <article style: "break-after:page;", *[
            renderer.stage(plan, sampler.address(plan, {slide: i, cue: 'final'})^, {*: options, instance: "handout" ++ string(i)}),
            <aside class: "slide-speaker-notes", *[for (note in scene.notes) for (child in content(note)) child]>]
        >]
    >)
}

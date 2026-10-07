import slide: lambda.slide
import northstar: ~~.~~.demo.slides.northstar_deck

// check intermediate mark geometry as well as completion and reduced-motion snapping.
let plan = slide.compile(northstar.deck)^
fn visuals(index, at_ms) array^ {
    let ps = slide.reduce(plan, slide.initial_state(plan),
        {command: 'seek', address: {slide: index, cue: 0, time_ms: at_ms}})^;
    slide.sample(plan, ps)^
}
fn mark(index, at_ms, id) map^ => [for (v in visuals(index, at_ms)^ where v.id == id) v][0]

let pie_start = mark(3, 0, "market-sector-0")^
let pie_middle = visuals(3, 400)^
let pie_end = visuals(3, 868)^
let line_start = mark(4, 0, "trend-series")^
let line_middle = mark(4, 500, "trend-series")^
let bars_middle = [for (i in [0,4]) mark(9, 500, "revenue-bar-" ++ string(i))^]
let progress_middle = [for (i in [0,1]) mark(8, 500, "progress-bar-" ++ string(i))^];
[
    len(plan.slides) == 18,
    pie_start.visible == 0 and pie_start.opacity == 0,
    [for (v in pie_middle where v.id == "market-sector-0") v.opacity][0] == 1 and
        [for (v in pie_middle where v.id == "market-sector-23") v.opacity][0] == 0,
    all([for (v in pie_end where starts_with(v.id, "market-sector-")) v.opacity == 1]),
    line_start.visible == 0 and line_start.clip == 1,
    line_middle.visible == 1 and line_middle.clip > 0 and line_middle.clip < 1,
    mark(4, 1140, "trend-series")^.clip == 0,
    bars_middle[0].clip > 0 and bars_middle[0].clip < bars_middle[1].clip and bars_middle[1].clip < 1,
    all([for (v in visuals(9, 1180)^ where starts_with(v.id, "revenue-bar-")) v.clip == 0]),
    progress_middle[0].clip > 0 and progress_middle[0].clip < progress_middle[1].clip and progress_middle[1].clip < 1,
    all([for (v in visuals(15, 1220)^ where starts_with(v.id, "progress-value-")) v.opacity == 1]),
    all([for (index in [3,4,8,9,15]) {
        let ps = slide.reduce(plan, slide.initial_state(plan, {reduced_motion: true}), {command: 'jump', slide: index})^;
        not slide.needs_frame(ps) and all([for (v in slide.sample(plan, ps)^ where starts_with(v.id, "market-") or starts_with(v.id, "trend-") or
            starts_with(v.id, "revenue-") or starts_with(v.id, "progress-")) v.visible == 1 and v.clip == 0])
    }])
]

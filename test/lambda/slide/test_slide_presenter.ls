import slide: lambda.slide
import presenter: lambda.slide.presenter

let plan = slide.compile(<presentation <slide id: "start"> <slide id: "end">>)^
let initial = presenter.initial_state()
let opened = presenter.reduce(initial, "console", 1000.0)^
let paused = presenter.reduce(opened, "timer-toggle", 6500.0)^
let resumed = presenter.reduce(paused, "timer-toggle", 20000.0)^
let reset = presenter.reduce(resumed, "timer-reset", 23000.0)^
let hidden = presenter.reduce(resumed, "console", 23000.0)^
let escaped = presenter.reduce({*: opened, overview: true, blackout: true, pointer: true}, "escape", 3000.0)^;
[
    not initial.running and not initial.console,
    opened.running and opened.console,
    presenter.elapsed(opened, 6500.0) == 5500.0,
    presenter.clock(opened, 6500.0) == "00:00:05",
    presenter.elapsed(paused, 20000.0) == 5500.0,
    presenter.elapsed(resumed, 23000.0) == 8500.0,
    presenter.clock(reset, 24000.0) == "00:00:01",
    not hidden.console and hidden.running and presenter.elapsed(hidden, 23000.0) == 8500.0,
    presenter.clock({*: initial, elapsed_ms: 3661000.0}, 0.0) == "01:01:01",
    not escaped.overview and not escaped.blackout and not escaped.pointer and escaped.console,
    not presenter.reduce(initial, "toolbar", 0.0)^.toolbar,
    presenter.destination(plan, "2")^ == 1,
    presenter.destination(plan, "start")^ == 0,
    contains(presenter.destination(plan, "999") ^ { ^.message }, "slide number or ID"),
    contains(presenter.reduce(initial, "unknown", 0.0) ^ { ^.message }, "unknown tool"),
    contains(presenter.reduce(initial, "console", nan) ^ { ^.message }, "invalid timer time")
]

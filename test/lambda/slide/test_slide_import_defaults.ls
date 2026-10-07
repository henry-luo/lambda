import helper: ~~.fixtures.slide_import_defaults
import slide: lambda.slide

let deck = <presentation <slide <text "Defaults">>>
let plan = slide.compile(deck)^;
[
    helper.options() == {answer: 42},
    helper.options(null) == null,
    helper.skipped(c: 7) == [10, 20, 7],
    helper.optional() == null,
    plan.width == 1280.0 and plan.height == 720.0,
    name(slide.snapshot(deck)^) == 'html'
]

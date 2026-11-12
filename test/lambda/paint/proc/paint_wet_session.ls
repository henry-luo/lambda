// Interactive watercolor frames, simulation and history (PT10, S9.1.2, D7.6.4).
import paint: lambda.paint
import wc: lambda.procart.watercolor

pn main() {
    let brush = paint.brush({size:8.0, hardness:1.0, amount:0.5})^
    let base = paint.session(40, 32, {medium:'watercolor'})^
    var shown = base
    let blank = paint.frame(shown)^
    let selected = paint.select_wet(shown, 1, 0.45)^
    var session = paint.begin(selected, 7, brush, [0.2,0.3,0.8,1.0])^
    let before = session
    let feed = paint.feed(session, 7, [[8.5,12.5,1.0],[24.5,12.5,1.0]])^
    let frame = paint.frame(session)^
    let done = paint.end(session, 7)^
    var undone = paint.undo(done)^
    let undo_frame = paint.frame(undone)^
    var redone = paint.redo(undone)^
    let redo_frame = paint.frame(redone)^
    let pre_step = redone
    let stepped = paint.step_wet(redone, {tilt:0.0, evaporation:0.99, edges:false, drying:true}, 2)^
    let step_frame = paint.frame(redone)^
    let before_dry = paint.flattened(redone)^
    let report = paint.dry(redone)^
    let after_dry = paint.flattened(redone)^
    let before_clear = redone
    let cleared = paint.clear(redone)^
    let restored = paint.undo(cleared)^
    var cancelled = paint.begin(done, 2, brush, [0.1,0.1,0.1,1.0])^
    paint.feed(cancelled, 2, [[12.5,18.5,1.0],[20.5,18.5,1.0]])^
    let cancel = paint.cancel(cancelled, 2)^
    let save = paint.painting_document(done) ^ { ^ }
    let invalid = paint.select_wet(done, 3, 0.45) ^ { ^ }
    let invalid_pigments = paint.session(40, 32, {medium:'watercolor', pigments:[{K:[1.0,1.0,1.0], S:[1.0,1.0,1.0]}]}) ^ { ^ }
    let invalid_light = paint.with_lighting(done, true) ^ { ^ }
    let invalid_initial_light = paint.session(40, 32, {medium:'watercolor', lighting:true}) ^ { ^ }
    return {initial:blank.rect == [0,0,40,32], feed:feed.rect != null and frame.rect != null,
        snapshot:before.wet == base.wet, changed:frame.pixels != null and done.wet != base.wet,
        undo:undone.wet == base.wet and undo_frame.pixels != null,
        redo:redone.wet != base.wet and redo_frame.pixels != null,
        simulated:stepped.work > 0 and step_frame.rect != null and pre_step.wet != redone.wet,
        dry:report.rect == [0,0,40,32] and before_dry == after_dry,
        clear:cleared.wet == base.wet and restored.wet == before_clear.wet,
        cancel:cancel.wet == done.wet, invalid:save is error and invalid is error and invalid_pigments is error and
            invalid_light is error and invalid_initial_light is error}
}

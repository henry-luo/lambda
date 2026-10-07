// lambda.slide.present: `lambda view deck.slides` presents a Mark deck. The
// document transform passes the parsed <presentation> and its source path.
import c: .common
import compiler: .normalize
import live: .live
import paths: lambda.edit.session

// presentation attributes double as presenter defaults; the deck ID names the player
fn presenter_options(deck, options) {
    let id = c.as_text(deck.id)
    {instance: if (c.instance_id(id)) id else "slides",
        *: if (deck.autostart != null) {autostart: deck.autostart} else {},
        *: if (deck.reduced_motion != null) {reduced_motion: deck.reduced_motion} else {},
        *: if (deck.autoplay_dwell_ms != null) {autoplay_dwell_ms: deck.autoplay_dwell_ms} else {},
        // relative images and fonts resolve against the deck file, not the working directory
        *: if (deck.base_uri == null and options.source_path != null)
            {base_uri: paths.dirname(options.source_path) ++ "/"} else {}}
}

pub fn present(deck, options = {}) element^ {
    let opts = presenter_options(deck, options)
    let plan = compiler.compile(deck, opts)^;
    live.document_tree(plan, opts)
}

import c: .common
import scene: .scene
import timeline: .timeline
import morph: .morph

pub fn compile(deck, options = {}) map^ {
    let checked_options = if (not (options is map)) raise c.fail("options", "expected map")
    let checked_options_attrs = c.attributes(options, ["instance", "width", "height", "base_uri", "reduced_motion", "autostart", "autoplay_dwell_ms"], "options")^;
    let checked_options_numbers = c.numbers(options, ["width", "height", "autoplay_dwell_ms"], true, "options")^;
    let checked_instance = if (options.instance != null and not c.instance_id(options.instance)) raise c.fail("options", "invalid instance ID")
    let checked_base = if (options.base_uri != null and not (options.base_uri is string)) raise c.fail("options", "base_uri must be string")
    let checked_flags = if (any([for (key in ["reduced_motion", "autostart"] where options[key] != null) not (options[key] is bool)]))
        raise c.fail("options", "player flags must be bool")
    let guard_1 = if (not (deck is element) or name(deck) != 'presentation') raise c.fail("deck", "expected presentation element")
    let checked_1 = c.attributes(deck, ["id", "title", "width", "height", "theme", "base_uri"], "deck")^;
    let checked_2 = c.numbers(deck, ["width", "height"], true, "deck")^;
    let checked_id = if (deck.id != null and not c.text_id(deck.id)) raise c.fail("deck", "ID must be nonempty text")
    let checked_text = if (any([for (key in ["title", "base_uri"] where deck[key] != null) not (deck[key] is string)]))
        raise c.fail("deck", "title and base_uri must be string")
    let width = c.num(deck, "width", 1280.0)
    let height = c.num(deck, "height", 720.0)
    let theme = c.value(deck.theme, 'light')
    let guard_2 = if (not contains(['light', 'dark'], theme)) raise c.fail("deck", "unsupported theme")
    let invalid = [for (child in content(deck) where not (child is element) or name(child) != 'slide') child]
    let guard_3 = if (len(invalid) > 0) raise c.fail("deck", "unsupported presentation child")
    let slides = [for (i, node in c.children(deck, 'slide')) timeline.compile(scene.build(node, i, width, height, theme)^)^]
    let guard_4 = if (len(slides) == 0) raise c.fail("deck", "presentation has no slides")
    let checked_3 = c.check_unique(slides, "deck")^;
    let prepared = [for (i, scene in slides) if (i > 0 and scene.transition == 'morph')
        {*: scene, morph: morph.compile(slides[i - 1], scene, theme)^} else scene]
    {id: c.as_text(c.value(deck.id, "deck")), title: c.as_text(c.value(deck.title, "Presentation")),
        width: width, height: height, theme: theme, slides: prepared,
        base_uri: c.value(deck.base_uri, options.base_uri), autoplay_dwell_ms: c.value(options.autoplay_dwell_ms, 3000.0)}
}

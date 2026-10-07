import c: .common

// Explicit paragraph boxes provide stable build targets without layout measurement.
pub fn build(id, paragraphs, options = {}) array^ {
    let checked_id = if (not c.text_id(id)) raise c.fail("paragraphs", "expected nonempty ID")
    let checked_items = if (not (paragraphs is array) or len(paragraphs) == 0) raise c.fail("paragraphs", "expected nonempty content array")
    let checked_options = if (not (options is map)) raise c.fail("paragraphs", "expected options map")
    let checked_attrs = c.attributes(options, ["x", "y", "width", "line_height", "font_size", "color", "duration", "kind", "easing"], "paragraphs")^;
    let checked_numbers = c.numbers(options, ["width", "line_height", "font_size"], true, "paragraphs")^;
    let checked_positions = c.numbers(options, ["x", "y", "duration"], false, "paragraphs")^;
    let checked_duration = if (options.duration != null and options.duration < 0.0) raise c.fail("paragraphs", "duration must be nonnegative")
    let prefix = c.as_text(id)
    let height = c.value(options.line_height, 90.0)
    let texts = [for (i, paragraph in paragraphs) <text id: prefix ++ "-p" ++ string(i),
        x: 0.0, y: i * height, width: c.value(options.width, 1000.0), height: height,
        font_size: c.value(options.font_size, 32.0), color: c.value(options.color, "#111827"), paragraph>]
    let cues = [for (i, paragraph in paragraphs) <cue id: prefix ++ "-cue" ++ string(i),
        <effect target: prefix ++ "-p" ++ string(i), kind: c.value(options.kind, 'fade-in'),
            duration: c.value(options.duration, 300.0), easing: c.value(options.easing, 'linear')>>];
    [<group id: prefix, x: c.value(options.x, 60.0), y: c.value(options.y, 140.0),
        width: c.value(options.width, 1000.0), height: height * len(paragraphs), *texts>, *cues]
}

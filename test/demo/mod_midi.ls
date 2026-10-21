// MIDI performance events for dom.midi_open, shared by the demos that compose a soundtrack as code
// (cityscape, photo-album). Each event is {seconds, status, a, b, order}; `performance` sorts them.

// times snap to whole milliseconds: 27.0 + 2 * 0.1 must equal 27.2, or a tie sorted by
// `order` could step backwards and the native player rejects the stream.
pub fn at(t) => round(t * 1000.0) / 1000.0
// `order` keeps same-time events valid: programs, then controllers, then note-offs, then note-ons.
pub fn on(t, ch, key, vel) map => {seconds: at(t), status: 144 + ch, a: key, b: vel, order: 3}
pub fn off(t, ch, key) map => {seconds: at(t), status: 128 + ch, a: key, b: 0, order: 2}
pub fn cc(t, ch, control, value) map => {seconds: at(t), status: 176 + ch, a: control, b: value, order: 1}
pub fn program(t, ch, preset) map => {seconds: at(t), status: 192 + ch, a: preset, b: 0, order: 0}

// the map dom.midi_open plays: the native player requires non-decreasing times; `order` breaks ties.
pub fn performance_of(events, seconds) map {
    let sorted = sort(events, (e) => e.seconds + e.order * 0.000001);
    {kind: "music-performance", seconds: seconds,
     events: [for (e in sorted) {seconds: e.seconds, status: e.status, a: e.a, b: e.b}]}
}

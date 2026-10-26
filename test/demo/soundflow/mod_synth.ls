// Soundflow's sound engine: three instruments synthesized in Lambda, encoded as
// 16-bit mono WAV for dom.audio_open, and a loop mixdown for export.
// SoundBow kept its engine in Pure Data apart from the drawing; this module is
// that engine, and knows nothing of strokes or strings.

pub let RATE = 24000

fn le16(v) => [v % 256, (v div 256) % 256]
fn le32(v) => [v % 256, (v div 256) % 256, (v div 65536) % 256, (v div 16777216) % 256]
pub fn hz(note) => 440.0 * math.pow(2.0, (note - 69) / 12.0)

// Karplus–Strong: a burst of noise recirculating through a damped two-point
// average. The read point is fractional, so high strings stay in tune.
pn harp(f) {
    let n = int(RATE * 2.2)
    let delay = RATE / f - 0.5
    let whole = int(floor(delay))
    let frac = delay - whole
    // each recirculation loses rho: about 60 dB over 3 s, less for high strings
    let rho = math.pow(0.001, 1.0 / (f * max(1.4, 3.6 - f / 500.0)))
    var out: float[] = fill(n, 0.0)
    var seed = 7919 + int(f)
    var prev = 0.0
    var i = 0
    while (i < whole + 3) {
        seed = (seed * 16807) % 2147483647
        // a softened burst: a finger, not a pick
        let noise = seed / 1073741823.5 - 1.0
        out[i] = 0.5 * (noise + prev)
        prev = noise
        i = i + 1
    }
    while (i < n) {
        out[i] = rho * 0.5 * ((1.0 - frac) * out[i - whole] + out[i - whole - 1] + frac * out[i - whole - 2])
        i = i + 1
    }
    return out
}

// a sum of decaying sine partials, each run by the two-term recurrence
// y[n] = 2cos(w)·y[n-1] − y[n-2] instead of a sin() per sample
pn additive(f, seconds, partials, attack) {
    let n = int(RATE * seconds)
    var out: float[] = fill(n, 0.0)
    for p in partials {
        let w = 2.0 * math.pi * f * p.ratio / RATE
        // a partial above Nyquist would fold back as noise
        if (w < 0.9 * math.pi) {
            let k = 2.0 * math.cos(w)
            let fall = math.exp(-p.decay / RATE)
            var y1 = 0.0
            var y2 = -math.sin(w)
            var g = p.gain
            var i = 0
            while (i < n) {
                out[i] = out[i] + g * y1
                let y = k * y1 - y2
                y2 = y1
                y1 = y
                g = g * fall
                i = i + 1
            }
        }
    }
    if (attack > 0.0) {
        let rise = math.exp(-1.0 / (RATE * attack))
        var e = 1.0
        var i = 0
        while (i < n and e > 0.0001) {
            out[i] = out[i] * (1.0 - e)
            e = e * rise
            i = i + 1
        }
    }
    return out
}

let KALIMBA = [{ratio: 1.0, gain: 1.0, decay: 3.4}, {ratio: 3.0, gain: 0.07, decay: 9.0},
    {ratio: 5.4, gain: 0.22, decay: 16.0}, {ratio: 9.8, gain: 0.05, decay: 40.0}]
// a struck-and-bowed glass: two beating fundamentals and inharmonic rim modes
let GLASS = [{ratio: 1.0, gain: 0.7, decay: 1.1}, {ratio: 1.0035, gain: 0.45, decay: 1.2},
    {ratio: 2.32, gain: 0.2, decay: 2.4}, {ratio: 4.25, gain: 0.08, decay: 4.5}]

// the raw samples of one note on one instrument, peak-normalized and faded out
pub pn samples(voice, note) {
    let f = hz(note)
    var out: float[] = if (voice == "kalimba") additive(f, 1.6, KALIMBA, 0.002)
        else if (voice == "glass") additive(f, 2.8, GLASS, 0.04)
        else harp(f)
    let n = len(out)
    var peak = 0.0
    var i = 0
    while (i < n) { peak = max(peak, abs(out[i])); i = i + 1 }
    let gain = if (peak > 0.0) 0.85 / peak else 0.0
    let fade = int(RATE * 0.08)
    i = 0
    while (i < n) {
        out[i] = out[i] * gain * min(1.0, (n - i) / fade)
        i = i + 1
    }
    return out
}

// 16-bit mono PCM WAV bytes; samples are clipped to [-1, 1]
pub pn wav(samples) {
    let n = len(samples)
    let head = [82,73,70,70, *le32(36 + n * 2), 87,65,86,69, 102,109,116,32, *le32(16), *le16(1), *le16(1),
        *le32(RATE), *le32(RATE * 2), *le16(2), *le16(16), 100,97,116,97, *le32(n * 2)]
    var bytes: int[] = fill(44 + n * 2, 0)
    var i = 0
    while (i < 44) { bytes[i] = head[i]; i = i + 1 }
    i = 0
    while (i < n) {
        let v = int(max(-1.0, min(1.0, samples[i])) * 32000.0)
        let u = if (v < 0) v + 65536 else v
        bytes[44 + i * 2] = u % 256
        bytes[45 + i * 2] = u div 256
        i = i + 1
    }
    return binary(bytes)
}

pub pn tone(voice, note) { return wav(samples(voice, note)) }

// Mixes looping strokes into one recording. Each loop is rendered once as a
// circular cycle, so a note's tail wraps into the start of the next pass, and
// is then tiled from its current phase: the export sounds like the moment
// the button was pressed, already in its steady state.
// plan: [{period, phase, hits: [{at, voice, note, gain}]}], times in ms.
pub pn mixdown(plan, seconds) {
    let n = int(RATE * seconds)
    var out: float[] = fill(n, 0.0)
    var cache = {}
    for loop in plan {
        let cycle_len = max(1, int(loop.period * RATE / 1000.0))
        var cycle: float[] = fill(cycle_len, 0.0)
        for h in loop.hits {
            let key = h.voice ++ ":" ++ string(h.note)
            if (cache[key] == null) { cache = {*: cache, [key]: samples(h.voice, h.note)} }
            let sound = cache[key]
            let first = int(h.at * RATE / 1000.0)
            let m = len(sound)
            var j = 0
            while (j < m) {
                let at = (first + j) % cycle_len
                cycle[at] = cycle[at] + h.gain * sound[j]
                j = j + 1
            }
        }
        let offset = int(loop.phase * RATE / 1000.0)
        var i = 0
        while (i < n) {
            out[i] = out[i] + cycle[(i + offset) % cycle_len]
            i = i + 1
        }
    }
    var peak = 0.0
    var i = 0
    while (i < n) { peak = max(peak, abs(out[i])); i = i + 1 }
    let gain = if (peak > 0.0) 0.9 / peak else 0.0
    // a two-second fade lets the last loop ring out instead of stopping dead
    let fade = int(RATE * 2.0)
    i = 0
    while (i < n) {
        out[i] = out[i] * gain * min(1.0, (n - i) / fade)
        i = i + 1
    }
    return wav(out)
}

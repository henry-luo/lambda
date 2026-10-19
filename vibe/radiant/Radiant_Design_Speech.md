# Radiant Speech Synthesis — Design Proposal

**Status:** Design, 2026-10-19; **M1 (macOS) and M2 (sub-mixes, window bus,
scheduling) implemented 2026-10-19**, M3–M6 not started (§8; as-built notes in
Appendix B). The user
asked what text-to-speech (TTS) in Lambda would need and whether all three
desktop operating systems provide an API; this document turns that answer into
a reviewable design. All eight §9 questions were decided on 2026-10-19 (the
rulings below).
**Ruling (USER, 2026-10-19): Linux is deferred.** The first phases cover macOS
and Windows only; Linux builds report speech as unavailable, and Linux may later
get a separate implementation of its own. This resolves §9 questions 3 and 4 by
deferral. **Ruling (USER, 2026-10-19): audible output is gated by document
origin**, ratified as D7.5.4; this resolves §9 question 1 (§5). **Ruling
(USER, 2026-10-19): document sub-mixes on a window bus** resolve §9
question 2 (§3.2). **macOS minimum: Lambda's deployment target, macOS 15.7**
(2026-10-19), which resolves §9 question 5. **Ruling (USER, 2026-10-19):
`dom.speech_voices()` is `fn`**, a live read like `dom.midi_info`, which
resolves §9 question 6 (§4.1). **Ruling (USER, 2026-10-19): utterance
markup is Lambda elements in M1**, translated to each engine's SSML, which
resolves §9 question 7 (§4.2). **Ruling (USER, 2026-10-19): no mixer
ducking**; pages lower music themselves, which resolves §9 question 8 (§4.1).
Platform API details marked *verify* must be confirmed against vendor headers
when the matching milestone starts.

**Scope:** Synthesized speech for documents viewed in Radiant — Lambda pages and
LambdaJS pages — plus offline rendering for tests and export. Out of scope:
speech recognition, voice cloning and personal voices, singing synthesis, and
network TTS services.

**Specification linkage:** This proposal applies existing rulings and does not
revise the formal specifications. Policy gaps found while writing it are listed
in §9 rather than decided here.

| Topic | Governing rulings | Application here |
|---|---|---|
| Audible-output gate | [D7.5.4](../../doc/Lambda_Formal_Design.md) | Local documents may start speech from any `pn` context; network documents need sticky activation; only the host grants it up front |
| Effects | [S12.1.1v3, S12.1.3](../../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system), [D7.4.6](../../doc/Lambda_Formal_Design.md) | Starting, pausing and cancelling speech are `pn` host rows; templates stay pure, and speech starts from `on` handlers |
| Lambda ↔ Radiant boundary | D7.5.3 | Lambda reaches speech only through the `radiant-dom` module, like `dom.midi_open` |
| Host values and handles | D7.4.1v2 | An utterance is an integer token; no native pointer becomes visible to scripts |
| Asynchronous completion | D7.4.2v2 | Synthesis results and speech events are posted to the loop; modules never pump or poll |
| Native ownership | D4.5.1v4, D4.5.2 | Speech sessions are document resources; text is copied into native storage when an utterance starts |
| Resource IO | D7.1.2v2, D7.5.2 | Any future engine data files (bundled voice models) are acquired through the central IO door |
| Build profiles | D7.1.4v2, D7.1.6, D7.1.7v6 | Live speech in `lambda.exe`; a null engine in headless profile C; excluded from `lambda-cli` and `lambda-wasm` |
| Packages | D7.2.4v3 | An optional pure `lambda.speech` package for narration data |

Related designs: [the music package](../Lambda_Design_Pkg_Music.md), whose MIDI
transport this proposal reuses, and [the WebGL design](Radiant_Design_WebGL.md)
for the document-owned native resource pattern.

## 1. Goals

1. Speak text in a chosen language and voice from Lambda and LambdaJS pages on
   macOS and Windows. Linux is deferred (§9, questions 3–4).
2. Keep speech on the same clock as music and animation, so narration can be cued
   to a timeline. The first use case is optional narration for
   [`test/demo/cityscape`](../../test/demo/cityscape/README.md), cued to its chapters.
3. Report word and sentence boundaries, so pages can highlight spoken text.
4. Render speech offline to samples, for deterministic tests and audio export.
5. Behave the same way as Radiant's other audio: gated by document origin
   (D7.5.4), owned by the document, released when the document closes.

## 2. Platform survey

| | macOS | Windows | Linux |
|---|---|---|---|
| Built-in API | `AVSpeechSynthesizer` (AVFoundation, macOS 10.14+); `NSSpeechSynthesizer` is deprecated | WinRT `Windows.Media.SpeechSynthesis.SpeechSynthesizer` (Windows 10+); SAPI 5 `ISpVoice` (COM) on all supported versions | None standard |
| Returns samples to the caller | Yes: `write(_:toBufferCallback:)`, and `…toMarkerCallback:` for boundary metadata (`AVSpeechSynthesisMarker`, macOS 13; marker delegate and phoneme markers macOS 14) | WinRT: `SynthesizeTextToStreamAsync` / `SynthesizeSsmlToStreamAsync` return a WAV stream. SAPI: `ISpVoice::SetOutput` to a stream | Depends on the engine (below) |
| Boundary events | Delegate `willSpeakRangeOfSpeechString`; markers from `write` | `SpeechSynthesizerOptions.IncludeWordBoundaryMetadata` / `…SentenceBoundaryMetadata`; SAPI word-boundary events | Engine-specific |
| SSML input | `AVSpeechUtterance` from an SSML representation, macOS 13+ (*verify*) | Yes (both APIs) | Engine-specific |
| Chinese voices | Built in or downloadable (Tingting, Meijia and others) | Huihui, Kangkang, Yaoyao through language packs | Engine-specific |
| Caveats | `write` was broken on 10.15 and reported flaky on 11.x — both far below Lambda's macOS 15.7 target. Word markers from third-party voice providers (`AVSpeechSynthesisProviderAudioUnit`) were reported arriving all at once on iOS 18; test with system voices | WinRT use from a desktop (Win32) process must be confirmed (*verify*); SAPI is the fallback | — |

Linux options:

| Option | What it is | Samples to caller | Licence | Size |
|---|---|---|---|---|
| **Speech Dispatcher** | The common desktop speech service, used by screen readers; often installed, not guaranteed | No: the server plays audio itself (SSIP is text-only); index marks are available as callbacks | Client library LGPL-2.1+; server GPLv2 in a separate process | None bundled |
| **eSpeak NG** | Compact formant synthesizer, 100+ languages including Mandarin (`cmn`); robotic sound | Yes: synchronous or retrieval mode with a synthesis callback (*verify* exact API) | GPL-3.0-or-later | A few MB |
| **Piper** | Neural, offline, natural-sounding; ONNX models | Yes | Engine GPL-3.0 since the move to `OHF-Voice/piper1-gpl`; the MIT `rhasspy/piper` was archived 2025-10-06; each voice model has its own licence | 20–60 MB per voice, plus ONNX Runtime |
| Flite / RHVoice | Smaller engines with fewer languages | Yes | BSD-like / LGPL | Small |

**Conclusion.** macOS and Windows provide everything needed, including
sample-level output, with no bundled data. Linux has no equivalent: every
high-quality engine checked links eSpeak NG somewhere (GPL-3.0), and Speech
Dispatcher cannot sync or render offline. Linux is therefore deferred (USER,
2026-10-19). The Linux table above records what was surveyed, for when Linux is
designed.

## 3. Architecture

### 3.1 Render to samples, then play through Radiant

There are two ways to make speech audible:

- **Speak now:** the OS speaks through its own audio output. This is simple,
  but it cannot seek, schedule against another clock, mix with music, or render
  offline.
- **Render to samples:** the engine returns PCM and boundary metadata, and
  Radiant plays the PCM itself.

**Proposal: render to samples wherever the engine allows it,** and use speak-now
only as a degraded fallback (Speech Dispatcher). This reuses the output path
Radiant already has for music. [`midi_player.cpp`](../../radiant/midi_player.cpp)
renders TinySoundFont output into a **miniaudio** device, which covers CoreAudio,
WASAPI and ALSA/PulseAudio. The byte-decoded `dom.audio_*` path
([`rdt_audio.h`](../../radiant/rdt_audio.h)) cannot carry speech across
platforms: it is AVFoundation-only, and the Windows and Linux builds are stubs
that return nothing ([`rdt_video_stub.cpp`](../../radiant/rdt_video_stub.cpp)).

### 3.2 Document sub-mixes on a window bus

**Ruling (USER, 2026-10-19; resolves §9 question 2).** Audio mixes in two
levels:

- **Document sub-mix.** Each document, including each iframe document, owns a
  sub-mix: its streams (MIDI sessions, utterances, decoded audio), its clock,
  its volume and its D7.5.4 gate. The sub-mix is a document resource, like
  `DocumentAudio` today, and is released with the document (D4.5.1v4). A
  document never owns another document's audio.
- **Window bus.** The window owns the single output device and mixes the
  documents' sub-mixes. It carries window-wide master volume and mute.
  Sub-mixes attach to and detach from the bus by generation-checked
  handles, so a closed document simply stops contributing. The window already
  owns shared services this way (the loader runtime, ES12v2/EO3v2), and Radiant
  is single-window today (RAD_20), so the bus is effectively per process.

Within a document, everything shares that sub-mix's clock, which gives:

- one position for all of a document's audio, so narration can be cued against
  its score or an animation clip;
- shared document volume, pause and mute;
- one offline render path: run a sub-mix, or the bus, without a device for tests
  and export.

Neither level ducks: lowering music under speech is left to pages (§9,
question 8; §4.1).

Documents keep independent clocks, all driven by the bus's one sample counter,
so cross-document sync stays possible later without a redesign.

Today each MIDI session opens its own miniaudio device. The MIDI transport keeps
its public behaviour; only its device ownership moves into the document sub-mix
and the window bus. This is the largest structural change in the proposal (§8,
M2).

### 3.3 Native layers

```
Lambda page / LambdaJS page
   │  dom.speak / speechSynthesis.speak            (radiant-dom module, D7.5.3)
Radiant speech service      document resource: tokens, queue, events (D4.5.1v4)
   │  rdt_speech_* C ABI                           (platform engines)
   ├─ AVSpeechSynthesizer (macOS)   ├─ WinRT / SAPI (Windows)
   ├─ unavailable on Linux (deferred, §9)          └─ fake engine (tests)
Document sub-mix ── window bus ── miniaudio device, or the offline renderer
```

- **`rdt_speech_*`**, modelled on `rdt_audio_*`: list voices; render an
  utterance asynchronously into PCM chunks plus boundary/mark records on an
  engine thread; cancel. Engines never touch the DOM or the Lambda heap.
- **Radiant speech service**, modelled on `DocumentAudio` in
  [`audio.cpp`](../../radiant/audio.cpp): token slots that never alias (as
  `audio_slot_add` guarantees), a per-document utterance queue, PCM handed to
  the document sub-mix, events posted to the loop (D7.4.2v2). Text is copied into native
  storage at `speak` time (D4.5.2).
- **Fake engine:** deterministic synthesis — tones or silence with computed
  durations, and boundary events at known offsets — for headless and CI runs.

### 3.4 Timing and scheduling

An utterance either starts as soon as it is queued, or is scheduled at a time
on its document sub-mix's clock (`at: seconds`). Scheduling lets a page cue
narration to a timeline exactly, as the cityscape score cues music to chapters. Because synthesis is
asynchronous, scheduled utterances are rendered ahead of time; if rendering
misses its cue, speech starts late and an event reports the delay.

**Ruling (USER, 2026-10-19): `at` is seconds on the document sub-mix clock,
and `dom.audio_clock(owner)` reads that clock.** The read is a `fn` row, a
live host read like `dom.midi_info` (D7.4.6). A page records the clock when it
starts its music and speaks at that value plus a cue time:

```lambda
start = dom.audio_clock(owner)
dom.midi_command(owner, music, "play", 0)
dom.speak(owner, line, {at: start + 12.5})
```

The clock counts the frames the sub-mix has mixed. It runs while the window
bus runs, which is while any document has a live stream, and it holds still
otherwise, so it never moves backwards. Utterances still sound in queue order:
each starts when the one before it has ended and its own `at` time has come,
at the exact sample. One that misses its time starts with the next buffer, and
its `speechstart` reports how late it was in `delay`.

### 3.5 Events

| Event | When | Fields |
|---|---|---|
| `speechstart` | The first sample plays | token, `delay` (seconds after a missed `at` time) |
| `speechboundary` | A word or sentence starts | token, kind (`word`/`sentence`), char index and length, time |
| `speechmark` | A `<mark>` element is reached (§4.2) | token, name, time |
| `speechend` | The last sample plays, or the utterance is cancelled | token, `cancelled` |
| `speecherror` | No voice for the language, engine failure, or lost device | token, reason |
| `voiceschanged` | The installed voices change | — |

`voiceschanged` is delivered to the document (no token); a view whose template
reads `dom.speech_voices()` re-renders on it. The other events are delivered on
the owner element, so Lambda views handle them with
`on speechboundary(evt)`. LambdaJS maps them onto `SpeechSynthesisUtterance`
events.

## 4. API sketch

### 4.1 Lambda (`radiant-dom` rows)

| Row | Colour | Meaning |
|---|---|---|
| `dom.speech_voices()` | `fn`: a live host read (D7.4.6; §9, question 6) | `[{id, name, lang, local, quality, gender}]`, the voices installed now, most natural first; `quality` is `default`, `enhanced` or `premium`, `gender` is `male`, `female` or `unspecified` |
| `dom.speak(owner, utterance, options)` | `pn` | Queue a string or a `<speak>` element (§4.2); returns a token, or `null` if unavailable |
| `dom.speech_command(owner, token, command)` | `pn` | `pause`, `resume`, `cancel` |
| `dom.speech_info(owner, token)` | `fn` | `{state, position, char_index}` |
| `dom.audio_clock(owner)` | `fn`: a live host read (D7.4.6; §3.4) | Seconds on the owner document's sub-mix clock; 0 before its first stream |

Without `voice`, an utterance uses the most natural installed voice for its
`lang` (the first match in `dom.speech_voices()`), not the platform's language
default, which on macOS is the compact voice even when a better one is
installed. A page that wants a particular gender picks the first voice with
that `gender` and passes its `id`.

`options`: `voice`, `lang` (BCP 47), `rate`, `pitch`, `volume` (same ranges as
the Web Speech API), and `at` (seconds on the document sub-mix's clock).

There is no ducking option: a page that wants music lower under speech does it
itself, for example by lowering a MIDI session's volume with
`dom.midi_command(owner, music, "volume", 0.4)` in `on speechstart` and restoring
it in `on speechend`. For narration cued at known times, the page can change the
volume at those times instead. Handler latency makes these fades less precise
than a mixer ramp would be; that trade-off was accepted (§9, question 8). A
usage sketch, using the same handler pattern as `lmd/package/music/player.ls`:

```lambda
view <narrator> state token: null { ~.rendered }
on click(evt) {
    let owner = dom.closest(evt.target, "#narrate")
    if (owner == null) { return 'pass' }
    token = dom.speak(owner, "成都，三环以内。", {lang: "zh-CN", rate: 0.9})
    return 'handled'
}
on speechboundary(evt) { dom.set_attribute(evt.target, "data-word", string(evt.char_index)); return 'handled' }
```

### 4.2 Utterance markup

**Ruling (USER, 2026-10-19; resolves §9 question 7).** Structured speech is
written as **Lambda elements**, not SSML strings, and M1 supports it. Radiant
translates the element tree into each engine's SSML, escaping the text itself,
so text from users or data can never break the markup. Pages never send raw
SSML.

```lambda
// mixed text and element children go through a spread array, as in other Lambda markup.
let line = <speak lang: "zh-CN", *[
    "成都，三环以内，", <break time: 0.4>,
    <prosody rate: 0.85, "按一万分之一缩小。">,
    <mark name: "scale">,
    <lang lang: "en-US", "One to ten thousand.">]>
token = dom.speak(owner, line, {voice: chosen})
```

The vocabulary is a small portable core, chosen to lie within both Apple's SSML
subset and SSML 1.0 (Windows):

| Element | Attributes | Meaning |
|---|---|---|
| `<speak>` | `lang` | Root; text and elements below it |
| `<break>` | `time` (seconds) | Pause |
| `<prosody>` | `rate`, `pitch`, `volume` (Web Speech ranges) | Change delivery for the enclosed content |
| `<emphasis>` | `level` (`strong`/`moderate`/`reduced`) | Stress |
| `<say-as>` | `interpret` (`characters`, `digits`, `date`, `time`) | Read the enclosed text a particular way |
| `<sub>` | `alias` | Speak `alias` instead of the enclosed text |
| `<lang>` | `lang` | Switch language for the enclosed content |
| `<mark>` | `name` | Emit `speechmark` when reached (so `speechmark` ships in M1) |

Rules:

- **Validation is up front.** `dom.speak` checks the tree before queueing: an
  unknown element or attribute, or an out-of-range value, fails visibly with
  `speecherror` and returns `null`, like other refusals (S7.4.2). Nothing is
  silently dropped.
- **Degradation is explicit and per engine.** Where an engine lacks a feature
  in the core (for example `<emphasis>` on a voice that ignores it), the
  translation documents the fallback: speak the content without that effect.
  `<break>` and `<mark>` never degrade, because the speech service can split an
  utterance there and insert silence or the event itself.
- **Element values are copied** into native storage when `dom.speak` is called
  (D4.5.2).
- **Pure construction.** Building markup is ordinary `fn` element construction
  (S12.1.1v3); only `dom.speak` is a `pn` effect.
- **JavaScript stays plain text.** The Web Speech API's `text` remains a string
  (§4.4), as in browsers.

*Verify at M1:* Apple's exact SSML subset for each core element. Any core
element Apple cannot express gets a documented fallback before M1 ships.

### 4.3 `lambda.speech` package (optional, pure)

Pure helpers that build narration data — `<speak>` markup (§4.2) and cue lists
such as `[{t: 17.0, line: <speak …>}]` — and a ready-made narrator view that
schedules a cue list against the document sub-mix, as `lambda.music`'s player
does for performances (D7.2.4v3). All construction is `fn`; transport stays in
`on` handlers (S12.1.3).

### 4.4 LambdaJS: the Web Speech API

Implement the synthesis half of the Web Speech API on the same service:
`speechSynthesis.speak/cancel/pause/resume/getVoices`, `onvoiceschanged`,
`SpeechSynthesisUtterance` (`text`, `lang`, `voice`, `volume`, `rate`, `pitch`)
and its `start`/`end`/`boundary`/`mark`/`pause`/`resume`/`error` events.
Recognition (`SpeechRecognition`) stays out of scope.

## 5. Policy

- **Audible-output gate (D7.5.4, USER 2026-10-19).** Starting speech is judged
  by the requesting document's origin:
  - **Local documents** (a file the user opened with `view` or `demo`) may start
    speech from any `pn` context, so a local demo can narrate from the start.
  - **Network documents** may start speech only after sticky activation: a
    trusted pointer press, touch end or non-modifier key press delivered by the
    host, after which the document stays activated. Script-dispatched events
    never activate.
  - **Only the host grants activation up front**, through a `view`/`demo`
    option or an embedding application's setting. A document never grants it
    to itself.

  A refused start returns `null` from `dom.speak`, rejects with
  `NotAllowedError` in JavaScript, and raises `speecherror`. Offline rendering
  to buffers or files needs no activation. The rule covers all audible output,
  not just speech: music, decoded audio and unmuted media too.
- **Quotas:** maximum text length per utterance, queued utterances per document,
  and buffered PCM bytes per document, enforced like the GL budget in
  `gl_core.cpp`. Exact values need measurement.
- **Privacy:** everything runs locally and no text leaves the machine. The voice
  list is local configuration and is only exposed to pages the user opens.
- **Failure:** a missing voice or device raises `speecherror` and returns
  `null`/`false` from `dom.speak`; nothing degrades silently.

## 6. Testing

- **Fake engine by default in tests:** installed voices differ per machine, so
  conformance tests (events, tokens, scheduling, cancellation, quotas,
  document teardown) use the deterministic engine on offline output
  (`LAMBDA_AUDIO_OUTPUT=offline`), never on a null device.
- **Offline render assertions:** render a document sub-mix without a device and
  check that output is non-silent, starts at the scheduled sample, lasts the
  expected time, and emits boundaries in order.
- **Real-engine smoke tests per OS,** opt-in: speak a known sentence, assert a
  start event and an end event, and save the rendered WAV to `temp/` for a
  listening check.
- The existing headless gap applies: event-sim `wait` advances virtual time,
  while a real engine renders and a device plays on wall time. With offline
  output the sub-mix follows the frame clock instead, so fake-engine replays are
  deterministic. The smoke test waits through assertion retries, which sleep
  wall time while an utterance is live on a real engine or a device.
- **As built (M1):** `test/ui/speech_fake.json` (page `test/ui/_speech_fake.ls`,
  `LAMBDA_SPEECH_ENGINE=fake`) checks voices, event order and character
  indexes for a string and for `<speak>` markup with a bookmark, timed
  cancellation, `speech_info` states and nine refusals (null plus
  `speecherror`). It runs in the UI baseline, and under forced GC with a
  zero-leak memtrack check as `RadiantViewTest.SpeechFakeEngineEventsSurviveGcWithoutLeaks`.
  `RadiantViewTest.SpeechSmokeSpeaksWithBoundaries` (`LAMBDA_SPEECH_SMOKE=1`,
  macOS) speaks en-US, zh-CN and markup through AVSpeechSynthesizer and expects
  word boundaries, the bookmark and the end of each utterance.
- **As built (M2):**
  - `RadiantFrameDelivery.ScheduledUtteranceStartsAtItsSubmixSampleOffline`
    renders a sub-mix offline and finds the first sound of an `at: 0.5`
    utterance at sample 24000. The fixture adds a cue at
    `dom.audio_clock(owner) + 0.5`, which must not start by 0.4 s, and a missed
    cue that must report a `delay`.
  - `MidiPlayback.OfflineSubmixAdvancesItsClockAndMixesVoices` covers the
    sub-mix clock, mixing and stale handles. `MidiPlayback.LiveDeviceClock`
    (`LAMBDA_TEST_MIDI_DEVICE=1`) plays through the bus device.
  - The music-player fixtures pass unchanged.
  - The WAV capture for a listening check is still open.

## 7. Packaging and licensing

- **macOS and Windows:** nothing to bundle. AVFoundation is already linked for
  media.
- **Linux:** deferred (USER, 2026-10-19). Nothing is linked or bundled for
  Linux in the first phases, so no GPL code enters `lambda.exe` (§9, question 4).
  If Linux is designed later, its licensing options are in the §2 survey.
  Precedent for bundled data: the music package ships TimGM6mb under GPL-2.0,
  with its `COPYRIGHT` and `GPL-2.0.txt` files in `lmd/package/music/soundfonts/`.
- Any future bundled voice data goes under `lmd/package/speech/voices/`, with
  a licence file per voice.

## 8. Milestones

| Milestone | Delivers | Acceptance |
|---|---|---|
| M0 | Decisions on §9 | Rulings recorded here; formal rulings drafted where needed |
| M1 | macOS: `rdt_speech` over `AVSpeechSynthesizer.write`, the speech service, `dom.speak` with string and `<speak>` element input (§4.2) translated to Apple SSML, `speech_command`/`speech_info`/`speech_voices`, events including `speechmark`, and the fake engine | Fake-engine conformance tests pass; a macOS smoke test speaks zh-CN and en-US with boundaries. **Done 2026-10-19** (§6, Appendix B) |
| M2 | Document sub-mixes on a window bus: MIDI and speech share one device; each document has one clock; scheduling with `at`; offline render | The music-player tests still pass; a scheduled utterance starts at the expected sample offline; optional cityscape narration cued to chapters. **Done 2026-10-19**, narration included (§6, Appendix B) |
| M3 | Windows backend (WinRT, SAPI fallback) | The same conformance and smoke suite on Windows |
| M4 | *Deferred (USER, 2026-10-19):* Linux backend, possibly a separate implementation | — |
| M5 | LambdaJS Web Speech API | A Web Speech fixture set passes against the fake engine |
| M6 | `lambda.speech` package and user documentation | Package goldens; a `doc/` page; a demo |

## 9. Decisions

1. ~~**User activation:** should a formal ruling state that audio and speech start
   only from a user gesture, as the music player and browsers already behave?
   Today this is practice, not a ruling.~~ **Resolved (USER, 2026-10-19):** by document origin,
   ratified as D7.5.4. Local documents may start audio freely; network documents
   need sticky activation; only the host grants activation up front (§5).
2. ~~**Mixer scope:** one mixer per document, or one per window shared by embedded
   documents?~~ **Resolved (USER, 2026-10-19):** a sub-mix per
   document feeding one window bus that owns the output device (§3.2).
3. ~~**Linux engine:** Speech Dispatcher only (no bundle, no sync or offline
   render), a bundled engine (eSpeak NG or Piper), or a bundled engine with
   Speech Dispatcher as fallback?~~ **Deferred (USER, 2026-10-19):** Linux is skipped in
   the first phases and may get a separate implementation later.
4. ~~**Licensing:** is linking a GPL-3.0 engine into `lambda.exe` acceptable, or
   must a bundled engine be an optional plugin or a separate process?~~ **Deferred with Linux (USER, 2026-10-19):** macOS and
   Windows use OS APIs, so no engine is linked or bundled and the question does
   not arise yet.
5. ~~**Minimum macOS version** for the sample path, given the reported `write`
   problems on 10.15 and 11.x.~~ **Resolved (2026-10-19):** speech adopts Lambda's
   deployment target, macOS 15.7 (`-mmacosx-version-min=15.7` in
   `build_lambda_config.json`). Everything the sample path uses exists there:
   `write` (10.15), `AVSpeechSynthesisMarker` and SSML (13), the marker delegate
   and phoneme markers (14), so no speech-specific minimum or availability
   branches are needed. If the target is ever lowered below 14, markers need
   availability checks; below 13, SSML and markers do too.
6. ~~**Colour of voice enumeration:** the voice list is a host read but varies by
   machine. Keep it `fn` like other reads (D7.4.6), or make it `pn` because it
   is not deterministic across hosts (S12.1.1v3)?~~ **Resolved (USER, 2026-10-19):** `fn`, a live read.
   This follows D7.4.6 and the existing DOM reads whose answers vary by host or
   time (`dom.midi_info`, `dom.audio_state`, `dom.bounding_box`, all `fn` with
   `DOM_F_NEUTRAL`). `voiceschanged` tells templates to re-render (§3.5).
7. ~~**SSML:** accept SSML input in M1 (macOS 13+, Windows), or plain text only at
   first?~~ **Resolved (USER, 2026-10-19):** Lambda element
   markup in M1 (§4.2): a portable core of `<speak>`, `<break>`, `<prosody>`,
   `<emphasis>`, `<say-as>`, `<sub>`, `<lang>` and `<mark>`, translated to each
   engine's SSML with escaping. Pages never send raw SSML; JavaScript stays
   plain text.
8. ~~**Ducking:** should speech lower music automatically, or leave it to pages?~~ **Resolved (USER, 2026-10-19):** left to pages. The
   mixer provides no ducking, at the document or window level; pages adjust
   music volume from speech events (§4.1).

All §9 questions are now resolved or deferred.

## Appendix A. References

- Apple: [AVSpeechSynthesizer](https://developer.apple.com/documentation/avfaudio/avspeechsynthesizer);
  [AVSpeechSynthesisMarker](https://developer.apple.com/documentation/avfaudio/avspeechsynthesismarker);
  a report of [markers losing sync on iOS 18](https://developer.apple.com/forums/thread/769342);
  developer reports on `write` on
  [Catalina](https://developer.apple.com/forums/thread/678287) and
  [Big Sur](https://developer.apple.com/forums/thread/683471).
- Microsoft: [SpeechSynthesizer.SynthesizeTextToStreamAsync](https://learn.microsoft.com/en-us/uwp/api/Windows.Media.SpeechSynthesis.SpeechSynthesizer.SynthesizeTextToStreamAsync);
  [SpeechSynthesizerOptions.IncludeWordBoundaryMetadata](https://learn.microsoft.com/en-us/uwp/api/windows.media.speechsynthesis.speechsynthesizeroptions.IncludeWordBoundaryMetadata).
- Speech Dispatcher: [project wiki](https://github.com/brailcom/speechd/wiki).
- eSpeak NG: [repository and licence](https://github.com/espeak-ng/espeak-ng).
- Piper: [OHF-Voice/piper1-gpl](https://github.com/OHF-Voice/piper1-gpl); the archived [rhasspy/piper](https://github.com/rhasspy/piper).
- Web Speech API: [MDN, SpeechSynthesis](https://developer.mozilla.org/en-US/docs/Web/API/SpeechSynthesis).

## Appendix B. Implementation notes

As built in M1 (2026-10-19):

- **Sources.** `radiant/rdt_speech.h` is the engine C ABI: voice listing, a
  voices generation counter, and render jobs that yield mono float samples plus
  word and bookmark marks. `radiant/rdt_speech_avf.mm` implements it over
  `AVSpeechSynthesizer.writeUtterance:toBufferCallback:` and the
  `willSpeakMarker` delegate; its callbacks arrive on the main queue, which the
  UI loop pumps (`rdt_video_pump_events`). `radiant/rdt_speech_stub.cpp` is the
  null engine for other platforms and the static test library.
  `radiant/speech_markup.cpp` validates `<speak>` trees and writes escaped SSML
  with a UTF-16 → character map. `radiant/speech.cpp` is the per-document
  service and the fake engine. No `build_lambda_config.json` edit was needed:
  the `radiant` source glob takes the new files, and `rdt_speech_stub.cpp` is
  dropped wherever `rdt_speech_avf.mm` builds.
- **Rows.** `speech_voices`, `speak`, `speech_command` and `speech_info` are
  appended to `lambda/dom/dom_api.def` with weak seams in `dom_core.cpp`, and
  implemented in `lambda/module/radiant/radiant_module.cpp`. Options are
  checked against the Web Speech ranges; `at` is refused until M2.
- **Clock and events.** `dom_engine_frame_tick` calls `radiant_speech_tick`
  first, so speech follows the frame clock, which is virtual under EventSim.
  `radiant_document_has_pending_frames` stays true while an utterance is live,
  keeping the host loop frame-driven. Events go through
  `radiant_queue_timing_event` with bubbling on, so a view's `on` handlers hear
  them from the owner. Fields use Lambda names: `token`, `kind`, `char_index`,
  `char_length`, `time`, `name`, `cancelled` and `reason`. `voiceschanged` is a
  window notification, sent to documents that have read the voice list or
  spoken.
- **Ownership.** Utterances own copies of their text and samples (D4.5.2).
  Owners follow view rebuilds as MIDI sessions do
  (`radiant_frame_requests_follow_rebuild`). An utterance whose owner leaves the
  document is cancelled without an event, and the document resource frees
  everything at teardown.
- **Gate (D7.5.4).** `radiant/audio.cpp` holds the origin check, the sticky
  activation flag and the host grant (`view --autoplay`); see the D7.5.4
  footnote in the formal design for coverage and residue.
- **Quotas.** 32768 characters and nesting depth 16 per utterance, 64 live
  utterances per document. The PCM byte budget is not implemented yet.

As built in M2 (2026-10-19):

- **Bus.** `radiant/audio_bus.cpp` is the window bus: one stereo float device
  at 48 kHz, fixed tables of 16 sub-mixes and 96 voices, and generation-checked
  handles. The device opens with the first voice and closes with the last. The
  audio thread never blocks: it try-locks each sub-mix and voice and skips one
  it cannot take, and the UI thread holds a voice's lock only while it changes
  that voice's source, such as a MIDI command. A lost device marks its voices
  lost on the next frame tick: MIDI reports `error`, speech raises
  `speecherror`, and the next stream reopens a device. The miniaudio
  implementation moved here from `midi_player.cpp`, which is now a pure sample
  source (`midi_player_mix`).
- **Sub-mixes.** Each document's sub-mix lives in its `DocumentAudio`
  resource. MIDI sessions join it at `midi_open`, as their own devices used
  to. Speech has one voice per document, which joins while utterances are live
  and leaves when idle. Destroying the sub-mix retires every voice in it, so
  resource teardown order does not matter.
- **Speech playback.** Renders are resampled once to mono 48 kHz
  (`ma_convert_frames`), with marks rescaled. The voice starts the armed
  utterance at its `at` frame within a buffer and keeps a cursor that pause
  freezes.
- **Offline output.** `LAMBDA_AUDIO_OUTPUT=offline` (or
  `audio_bus_set_offline` in tests) opens no device. Each sub-mix then advances
  on its document's frame tick (`radiant_audio_tick`), and
  `audio_submix_render` renders one for tests.
- **Demo.** `test/demo/cityscape` gains a narration button, which speaks the
  remaining Chinese subtitles at their film times on the score's clock. It uses
  the best installed male Mandarin voice at rate 0.9.
- **Voice ranking (macOS).** Voices sort by quality (premium, enhanced, then
  default), then by family:
  - Apple's current voices first (`com.apple.voice.*`);
  - then Eloquence formant voices;
  - then the classic MacinTalk set, mostly novelty voices.

  The order is stable within each group. Enhanced and premium voices are free
  downloads (System Settings → Accessibility → Spoken Content → System voice →
  Manage Voices…). Apps cannot start the download. The Eloquence voices report
  no gender, so a page asking for a male Mandarin voice finds none until one
  such as Han (Premium), Li-mu or Binbin (Enhanced) is installed; Han reports
  male and premium (verified 2026-10-19).
- **Not yet routed or exposed.**
  - Decoded audio (`dom.audio_*`) still plays through AVFoundation, outside
    the bus.
  - Sub-mix volume and window master volume and mute exist only as design;
    no rows set them.
- **Known gaps.**
  - Engine jobs are sampled on the frame tick rather than posted to the loop
    (D7.4.2v2), because no general completion-post service exists yet.
  - Sentence boundaries are not reported on macOS: `AVSpeechSynthesizer.write`
    delivers only word and bookmark markers (probed 2026-10-19), and the fake
    engine matches it. The service already maps engine sentence marks to
    `kind: "sentence"`, ready for engines that report them (SAPI, M3); deriving
    sentences from the text on macOS is open.
  - Back-to-back utterances without `at` leave a gap of up to one frame tick,
    because the next one is armed on the tick after the previous one ends.
- **Later milestones.** Windows adds `radiant/rdt_speech_win.cpp` with the
  WinRT and SAPI libraries (M3). Linux links no speech library while it is
  deferred: `dom.speech_voices()` returns `[]`, and `dom.speak` returns `null`
  with a `speecherror` naming the platform. Edit `build_lambda_config.json`,
  never the generated Lua files.

## Appendix C. Superseded wording

- §3.2, window bus, 2026-10-19: ~~"It carries window-wide master volume and
  mute, and ducking across documents (a voice in an iframe can lower music in
  its parent)."~~ and the list item ~~"optional ducking within the document"~~.
  Superseded the same day by §9 question 8: no mixer ducking at either level.

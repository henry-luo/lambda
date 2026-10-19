# Local Neural Text-to-Speech for Radiant — Idea

**Status:** Idea, 2026-10-19. Not on the roadmap and not implemented. It
needs the user rulings listed in §9 before it can become a design.
**Context:** The sentence-game demo
([`test/demo/sentence-game`](../../test/demo/sentence-game/README.md)) reads
sentences and cards aloud through `dom.speak`. On a stock Mac the best
installed US English female voice is compact Samantha. The demo raises its
pitch to 1.5 to sound like a girl, and the result sounds robotic. The user
asked whether higher-quality, less robotic synthesis is possible. The speech
design ([`Radiant_Design_Speech.md`](../radiant/Radiant_Design_Speech.md))
uses the operating system's voices. It excludes network TTS services, voice
cloning and personal voices. It defers Linux, because every good Linux engine
it surveyed links eSpeak NG, which is GPL-3.0
([§2](../radiant/Radiant_Design_Speech.md#2-platform-survey),
[§9](../radiant/Radiant_Design_Speech.md#9-decisions) questions 3–4).
**In one line:** a small neural model behind the existing `rdt_speech_*`
engine interface could give natural voices on macOS, Windows and Linux. The
hard parts are not the model. They are the text front end's licence, binary
and model size, model distribution, and first-audio latency.

---

## 1. What exists today, and what this would add

**Platform voices** come in quality tiers. macOS has compact and default
voices, downloadable *Enhanced* voices, and neural *Premium* voices. Premium
voices are good, but each user has to install them by hand under System
Settings → Accessibility → Spoken Content → Manage Voices. Apps cannot start
those downloads, and AVSpeech cannot use Siri's voices. Windows quality depends
on the installed language packs, and Linux has no Radiant engine at all.

Two quick wins need no new engine:

- **Install a Premium voice** such as Zoe or Ava. The speech service ranks
  voices by quality first ([`speech.cpp`](../../radiant/speech.cpp)), so a
  page that selects by language and gender picks it up automatically.
- **Keep pitch near 1.0–1.2.** AVSpeech's `pitchMultiplier` shifts the
  rendered voice. Beyond about 1.2 the shift itself sounds artificial, whatever
  the voice.

**A bundled neural engine would add:**

- the same natural voice on every machine, with no setup;
- Linux coverage, unblocking the deferred milestone M4;
- per-voice metadata that Radiant controls, including honest `gender`
  (Eloquence voices report `unspecified` today);
- exact word and sentence timings from the model's predicted durations (§4).
  AVSpeech's `write` gives no sentence markers.

## 2. Candidate models (as of October 2026)

| Model | Size | Licence | Languages / voices | Text front end | CPU speed (core-hour ratio¹) | Notes |
|---|---|---|---|---|---|---|
| **Kokoro-82M** v1.0 (2025-01-27) | 82M params; ~325–341 MB fp32 ONNX; smaller quantized exports exist (*verify* sizes) | Weights Apache-2.0 | 8 languages, 54 voices; 24 kHz | Phonemes from **misaki** (Apache-2.0), with an *optional* eSpeak NG fallback for unknown words | 1.28× | The consistent favourite among small models for naturalness. StyleTTS 2 + ISTFTNet. Has a `speed` input; voices are style embeddings that can be averaged |
| **Pocket TTS** (Kyutai, report 2026-01-13) | ~100M; ~242 MB | Code MIT; weights reported CC BY 4.0 with prohibited-use terms (*verify*) | 7 languages; 26 built-in voices, 21 English | Not documented (*verify*) | 0.37×; ~6× real time on 2 M4 cores | Streams audio. One-sample voice cloning requires lawful consent. The official runtime is Python/PyTorch; the C++ and ONNX ports are community projects (sherpa-onnx among them) |
| **Supertonic 3** (Supertone) | ~99M (v2: 262 MB) | Code MIT; weights **OpenRAIL-M** (use restrictions) | 31 languages; 44.1 kHz | Reported to need no G2P (third-party claims; *verify*) | 0.52× (v2) | Has an official C++ ONNX Runtime example. **The repository was archived on 2026-09-09**, so there will be no fixes. Preset adult voice styles only |
| **Piper** | ~61 MB per voice | Engine **GPL-3.0** (`piper1-gpl`; MIT `rhasspy/piper` archived 2025-10-06); each voice has its own licence | 44 language variants | eSpeak NG phonemes (GPL-3.0) | 0.35× | The fastest and lightest, but less natural than Kokoro. Already surveyed in the speech design |
| **Kitten TTS** Nano | ~15M; 25–42 MB | Apache-2.0 | English | The reference setup phonemizes with eSpeak NG (*verify*) | 3.1× (whole-utterance synthesis) | The smallest footprint. Quality is a step below Kokoro |

¹ CPU time to produce one hour of audio; 1.0 means one core keeps up with
real time. The figures come from a vendor benchmark (Picovoice, which sells a
competing engine, Orca). It ran Python stacks on a Ryzen 7 5700X, and its
peak-memory figures (0.3–2.6 GB) include framework overhead. Treat them as
directional and measure on our own targets.

**Ruled out for an embedded engine:** GPU-class or restrictively licensed
models, including Chatterbox (MIT, 350–500M; the Python package watermarks its
output), Orpheus (3B), Dia, Qwen3-TTS, F5-TTS (CC-BY-NC weights), XTTS-v2
(non-commercial) and Fish Audio (research licence).

**First choice to evaluate: Kokoro.** It has the best quality for its size,
Apache-2.0 weights, and a G2P whose GPL fallback is optional. Pocket TTS is the
runner-up, if its weight licence and text front end check out.

## 3. The text front end is the real problem

Most of these models consume phonemes, not text. The reference phonemizer
everywhere is **eSpeak NG, which is GPL-3.0**. Linking it into `lambda.exe`
makes the binary GPL-3.0, which is the reason Linux speech was deferred. There
are three ways around it:

1. **A license-clean, lexicon-first G2P in C++.** Port misaki's English data
   (Apache-2.0 "gold" and "silver" dictionaries) and its rules. Unknown words
   need a clean fallback: letter-to-sound rules, or a small grapheme-to-phoneme
   model run through the same inference runtime. Text normalization (numbers,
   dates, currency, abbreviations) has to be written as well, since misaki
   uses Python's `num2words`. This work is per language, so English would come
   first. Misaki has Rust, Dart and C# ports but no C++ port. Both the Rust and
   Dart ports treat eSpeak as an optional extra.
2. **eSpeak NG in a separate helper process.** `lambda.exe` would exchange
   phoneme strings with a helper program over a pipe. The helper ships with
   its source under GPL-3.0, and Radiant's code stays separate. The cost is
   process management and shipping a second executable. A first-party helper
   is not a third-party module, so the T2 requirements (separate process + OS
   sandbox + capability broker, D7.5.1) do not automatically apply. That still
   needs to be confirmed.
3. **A model that reads raw text.** Supertonic reportedly needs no G2P;
   Pocket TTS might not either (*verify*). This removes the problem entirely.
   The price is the weight licence (OpenRAIL-M use restrictions; CC BY
   attribution) and, for Supertonic, an archived upstream.

**Chinese** needs a front end of its own: word segmentation, pinyin and tone
sandhi. Lambda's Chinese content (the cityscape narration) is served well by
macOS Premium voices such as Han. A sensible split is a neural engine for
English first, with platform voices for everything else.

## 4. Fitting the engine into Radiant

**The engine interface already fits.**
[`rdt_speech.h`](../../radiant/rdt_speech.h) renders a whole utterance,
asynchronously, to mono float PCM plus boundary marks, from plain copied data
(D4.5.2). The speech service already resamples each render to the bus rate and
rescales its marks. It also queues utterances, schedules `at:`, fires events,
and applies the audible-output gate (D7.5.4,
[speech design §3](../radiant/Radiant_Design_Speech.md#3-architecture)). A
neural engine would be one more implementation of `rdt_speech_*`. Nothing
above the interface changes.

**What does change:**

- **Two engines at once.** Exactly one platform engine is linked today
  (`rdt_speech_avf.mm` or `rdt_speech_stub.cpp`). Running a neural engine
  beside it needs a small dispatcher. It would merge the two voice lists under
  namespaced ids (e.g. `lambda.kokoro.en-US.heart`) and route each render by
  id prefix. A request with no voice id would still resolve by ranking.
- **Voice metadata.** Kokoro's voice names encode accent and gender (`af_…` is
  American female), so `speech_voices()` can report `gender` honestly. Where
  neural voices rank against platform Premium voices is a ruling (§9,
  question 6).
- **Threading.** Inference runs on an engine worker thread. The UI thread
  polls the job as it does now. Cancelling sets a flag that is checked between
  chunks.
- **First-audio latency.** The core-hour ratios above suggest roughly
  0.3–1.5 s on a multi-core CPU before a short sentence can start, when the
  whole utterance is rendered first. Ways to reduce it:
  - render clause by clause and start after the first chunk (this needs
    partial PCM in the engine interface);
  - cache renders keyed by (text, voice, rate, pitch), since card phrases are
    a finite set;
  - pre-render a sentence, cancellably, as soon as it becomes complete.
- **Boundaries.** The model predicts a duration for every phoneme, and the G2P
  knows which phonemes belong to which word. Word boundaries therefore fall
  out exactly, and sentence boundaries come from the splitter that chunks the
  text. Kokoro's Python pipeline is believed to expose per-token timestamps
  for English, and the ONNX graph must also expose the duration output (both
  *verify*).
- **Rate and pitch.** Rate maps onto the model's speed input. Most models have
  no pitch input, and DSP pitch shifting degrades a neural voice just as it
  does AVSpeech. Choose or blend voices instead (§6).
- **Markup.** The portable element subset
  ([speech design §4.2](../radiant/Radiant_Design_Speech.md#42-utterance-markup))
  would be interpreted by the engine itself rather than translated to SSML:
  `<break>` inserts silence, `<sub>` and `<say-as>` feed the normalizer,
  `<mark>` becomes a timed mark, and `<emphasis>` is approximate at best.
- **Determinism.** One model on one CPU gives near-repeatable PCM, with small
  float differences across CPUs and thread counts. Real-voice fixtures with a
  tolerance become possible. The fake engine stays the CI default.

## 5. Inference runtime and packaging

- **ONNX Runtime** (MIT) is mature and runs on all three operating systems,
  but it normally ships as a shared library. D7.1.1 keeps Lambda to static
  libraries (dynamic linking is reserved for Jube modules). The choice is a
  static ONNX Runtime build linked into `radiant.a`, or the helper-process
  route from §3. Its effect on binary size must be measured; the release
  `lambda` is about 25 MB today.
- **sherpa-onnx** (Apache-2.0) offers a C API over ONNX Runtime with Piper,
  Kokoro and Matcha voices on every desktop OS, and would save most of the
  integration work. It is reported to phonemize Kokoro and Piper with eSpeak
  NG (*verify*), which brings the GPL question straight back.
- **Vendoring** follows AGENTS rule 16: never patched in place; any upstream
  delta goes under `patches/`.
- **Models do not belong in `lambda.exe` or in git.** Even the smallest good
  model (80–340 MB) is several times the binary. The options are:
  1. an optional voice package under `lmd/package/speech/voices/`, the
     location the speech design already reserves
     ([§7](../radiant/Radiant_Design_Speech.md#7-packaging-and-licensing)),
     installed by an explicit user command;
  2. download on first use, behind a visible consent prompt and through the
     central IO API (D7.5.2). This conflicts with demos that promise no
     network use;
  3. one small voice bundled with the release, with larger models optional.
- **Build profiles.** The engine belongs only to Radiant builds, never to
  `lambda-cli` (D7.1.6). The headless profiles (D7.1.4v2) might want it for
  offline rendering and export.
- **Memory.** Load the model lazily on the first `speak` and release it after
  an idle period. Native peak memory has to be measured, since the benchmark's
  0.5–2 GB includes Python.

## 6. Child-like voices

- No small open model checked ships a child voice; Kokoro, Supertonic and
  Pocket TTS voices are all adult. Cloud services do have them (Azure Neural's
  "Ana", Amazon Polly's "Ivy"), but network TTS is out of scope.
- **Blending:** averaging a lighter adult voice's style embedding with
  another's (Kokoro) gives a brighter, younger timbre without DSP artifacts.
  This is the cheap path.
- **Fine-tuning:** research shows child-like synthesis by fine-tuning on child
  speech, for example the FastPitch work on the MyST corpus (~393 h of
  children's speech, licensed for research). Licensing and consent make this
  a research project, not a feature.
- **Cloning:** use only voices recorded with explicit consent. A voice actor
  performing a young voice is the clean source; never clone a real child's
  voice without a guardian's consent.

## 7. Alternatives considered

| Alternative | For | Against |
|---|---|---|
| Platform Premium voices only (status quo plus install guidance) | No cost; best quality on macOS | Manual install on each machine; none on Linux |
| Cloud TTS (Azure, Polly, Google, ElevenLabs) | Best quality; real child voices | Out of scope in the speech design: network, keys, privacy. Could live in a user package outside the engine |
| Pre-recorded audio for finite content | Studio quality for cards and fixed lines | Sentences are combinatorial; joining clips breaks prosody |
| Apple Personal Voice | The user's own voice | Personal voices are out of scope in the speech design; macOS only |

## 8. A possible path

1. **Rulings** on §9.
2. **Spike** outside `lambda.exe`: Kokoro through the ONNX Runtime C API.
   Measure per-sentence latency on Apple Silicon and x86, peak memory, and the
   size of a static ONNX Runtime build. Listening test against compact
   Samantha and Zoe (Premium).
3. **Text front end:** a license-clean English G2P (§3, option 1). Measure the
   unknown-word rate on Lambda's docs and demo text.
4. **Engine:** the dispatcher, the voice package format, boundaries from
   durations, and a render cache.
5. **Platforms:** Windows and Linux; revisit M4.

## 9. Open questions (rulings needed)

1. **Scope:** is a bundled local neural engine in scope for Radiant speech?
   The design excludes network services and cloning, not local engines.
2. **Licensing:** may eSpeak NG (GPL-3.0) run in a separate helper process,
   or must the text front end be license-clean?
3. **Process model:** a static ONNX Runtime inside `radiant.a` (D7.1.1), or a
   helper process?
4. **Weight licences:** Apache-2.0 only, or also OpenRAIL-M and CC BY 4.0
   with use restrictions?
5. **Distribution:** an optional package installed by explicit command,
   download on first use with consent, or a small bundled voice?
6. **Ranking:** where do neural voices rank against platform Premium voices?
   Should `speak` options gain an engine preference?
7. **Streaming:** should the engine interface grow partial PCM so that first
   audio starts after the first chunk?
8. **Linux:** does this engine become the Linux speech engine, resolving the
   deferred M4?
9. **Child voices:** settle for blends, or fund a research track?

## References

- Kokoro-82M model card: <https://huggingface.co/hexgrad/Kokoro-82M>;
  misaki G2P: <https://github.com/hexgrad/misaki>.
- Pocket TTS: <https://github.com/kyutai-labs/pocket-tts>.
- Supertonic (archived 2026-09-09): <https://github.com/supertone-inc/supertonic>;
  model card: <https://huggingface.co/Supertone/supertonic>.
- Piper: <https://github.com/OHF-Voice/piper1-gpl>.
- sherpa-onnx: <https://github.com/k2-fsa/sherpa-onnx>.
- Picovoice on-device TTS benchmark (2026-07-14, updated 2026-08-18):
  <https://picovoice.ai/blog/on-device-tts/>.
- Licence survey of local TTS models (2026-10-10):
  <https://www.yaps.ai/blog/best-local-tts-models>.
- Child TTS by FastPitch transfer learning: <https://arxiv.org/abs/2311.04313>;
  code: <https://github.com/C3Imaging/child_tts_fastpitch>.
- Formal rulings cited: D4.5.2, D7.1.1, D7.1.4v2, D7.1.6, D7.5.1, D7.5.2,
  D7.5.4 (`python3 utils/formal_index.py <ID>`).

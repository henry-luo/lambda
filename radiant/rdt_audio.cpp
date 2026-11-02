#include "audio_miniaudio.hpp"
#include "rdt_audio.h"
#include "audio_bus.hpp"
#include "../lib/mem.h"
#include "../lib/log.h"
#include <math.h>
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#endif

struct AudioPcm {
    float* samples;
    size_t frames, capacity, references; // UI-owned references; callbacks only borrow immutable samples
};
struct RdtAudio {
    AudioPcm* pcm;
    double cursor, left, right, pitch, decay, filter, filter_target, filtered[2];
    double peak, rms;
    uint64_t rendered;
    RdtAudioParameters parameters;
    float volume;
    int state;
    uint64_t identity;
};

struct AudioPcmBuilder {
    float* samples = nullptr;
    size_t frames = 0, capacity = 0, limit;
    explicit AudioPcmBuilder(size_t maximum) : limit(maximum) {}
    ~AudioPcmBuilder() { mem_free(samples); }
    bool append(const float* source, size_t count) {
        if (!count) return true;
        if (count > limit - frames) return false;
        for (size_t i = 0; i < count * 2; i++) if (!isfinite(source[i])) return false;
        size_t needed = frames + count;
        if (needed > capacity) {
            size_t next = capacity ? capacity * 2 : 4096;
            if (next < needed) next = needed;
            if (next > limit) next = limit;
            auto* resized = (float*)mem_realloc(samples, next * 2 * sizeof(float), MEM_CAT_RENDER);
            if (!resized) return false;
            samples = resized; capacity = next;
        }
        memcpy(samples + frames * 2, source, count * 2 * sizeof(float));
        frames = needed; return true;
    }
};

#ifdef __APPLE__
struct AudioMemoryFile {
    const unsigned char* bytes;
    size_t length;
    AudioFileID file = nullptr;
    ExtAudioFileRef decoder = nullptr;
    AudioMemoryFile(const void* source, size_t size) : bytes((const unsigned char*)source), length(size) {}
    ~AudioMemoryFile() {
        if (decoder) ExtAudioFileDispose(decoder);
        if (file) AudioFileClose(file);
    }
};
static OSStatus audio_memory_read(void* context, SInt64 position, UInt32 requested, void* output, UInt32* actual) {
    auto* source = (AudioMemoryFile*)context;
    if (position < 0 || (uint64_t)position > source->length) { *actual = 0; return kAudioFilePositionError; }
    size_t available = source->length - (size_t)position;
    *actual = available < requested ? (UInt32)available : requested;
    if (*actual) memcpy(output, source->bytes + (size_t)position, *actual);
    return noErr;
}
static SInt64 audio_memory_size(void* context) { return (SInt64)((AudioMemoryFile*)context)->length; }
static bool audio_decode_platform(const void* bytes, size_t length, AudioPcmBuilder* pcm) {
    AudioMemoryFile source(bytes, length);
    // Core Audio retains the platform formats formerly decoded by AVAudioPlayer, including AAC.
    if (AudioFileOpenWithCallbacks(&source, audio_memory_read, nullptr, audio_memory_size, nullptr, 0,
                                  &source.file) != noErr ||
        ExtAudioFileWrapAudioFileID(source.file, false, &source.decoder) != noErr) return false;
    AudioStreamBasicDescription format = {};
    format.mSampleRate = AUDIO_BUS_RATE; format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    format.mBytesPerPacket = format.mBytesPerFrame = 2 * sizeof(float);
    format.mFramesPerPacket = 1; format.mChannelsPerFrame = 2; format.mBitsPerChannel = 32;
    if (ExtAudioFileSetProperty(source.decoder, kExtAudioFileProperty_ClientDataFormat, sizeof(format), &format) != noErr)
        return false;
    float samples[4096 * 2];
    for (;;) {
        AudioBufferList buffers = {1, {{2, sizeof(samples), samples}}};
        UInt32 frames = 4096;
        if (ExtAudioFileRead(source.decoder, &frames, &buffers) != noErr) return false;
        if (!frames) return pcm->frames != 0;
        if (!pcm->append(samples, frames)) return false;
    }
}
#endif

static bool audio_decode(const void* bytes, size_t length, AudioPcmBuilder* pcm) {
    ma_decoder decoder;
    auto config = ma_decoder_config_init(ma_format_f32, 2, AUDIO_BUS_RATE);
    if (ma_decoder_init_memory(bytes, length, &config, &decoder) != MA_SUCCESS) {
#ifdef __APPLE__
        return audio_decode_platform(bytes, length, pcm);
#else
        return false;
#endif
    }
    struct DecoderOwner {
        ma_decoder* value;
        ~DecoderOwner() { ma_decoder_uninit(value); }
    } owner{&decoder};
    ma_uint64 expected = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &expected) == MA_SUCCESS && expected > pcm->limit)
        return false;
    float samples[4096 * 2];
    for (;;) {
        ma_uint64 frames = 0;
        auto result = ma_decoder_read_pcm_frames(&decoder, samples, 4096, &frames);
        if ((result != MA_SUCCESS && result != MA_AT_END) || !pcm->append(samples, (size_t)frames)) return false;
        if (result == MA_AT_END || !frames) return pcm->frames != 0;
    }
}

RdtAudio* rdt_audio_open_bytes_limit(const void* bytes, size_t length, size_t storage_budget) {
    const size_t header = sizeof(RdtAudio) + sizeof(AudioPcm);
    if (!bytes || !length || length > RDT_AUDIO_SOURCE_LIMIT || storage_budget < header + 2 * sizeof(float))
        return nullptr;
    size_t available = storage_budget - header;
    if (available > RDT_AUDIO_PCM_LIMIT) available = RDT_AUDIO_PCM_LIMIT;
    AudioPcmBuilder pcm(available / (2 * sizeof(float)));
    if (!audio_decode(bytes, length, &pcm)) {
        log_error("audio-decode: invalid/unsupported source or decoded PCM exceeds finite storage"); return nullptr;
    }
    auto* audio = (RdtAudio*)mem_calloc(1, sizeof(RdtAudio), MEM_CAT_RENDER);
    if (!audio) return nullptr;
    auto* storage = (AudioPcm*)mem_calloc(1, sizeof(AudioPcm), MEM_CAT_RENDER);
    if (!storage) { mem_free(audio); return nullptr; }
    if (pcm.frames < pcm.capacity) {
        auto* compact = (float*)mem_realloc(pcm.samples, pcm.frames * 2 * sizeof(float), MEM_CAT_RENDER);
        if (compact) { pcm.samples = compact; pcm.capacity = pcm.frames; }
    }
    storage->samples = pcm.samples; pcm.samples = nullptr;
    storage->frames = pcm.frames; storage->capacity = pcm.capacity; storage->references = 1;
    audio->pcm = storage; audio->volume = 1; audio->state = RDT_AUDIO_READY; audio->pitch = 1;
    audio->parameters = rdt_audio_default_parameters();
    return audio;
}
RdtAudio* rdt_audio_open_bytes(const void* bytes, size_t length) {
    return rdt_audio_open_bytes_limit(bytes, length, sizeof(RdtAudio) + sizeof(AudioPcm) + RDT_AUDIO_PCM_LIMIT);
}
size_t rdt_audio_storage_bytes(const RdtAudio* audio) {
    auto storage = rdt_audio_storage(audio);
    return storage.player_bytes + storage.shared_bytes;
}
size_t rdt_audio_frames(const RdtAudio* audio) { return audio ? audio->pcm->frames : 0; }
RdtAudioStorage rdt_audio_storage(const RdtAudio* audio) {
    return audio ? RdtAudioStorage{audio->pcm, sizeof(AudioPcm) + audio->pcm->capacity * 2 * sizeof(float), sizeof(RdtAudio)}
                 : RdtAudioStorage{};
}
RdtAudio* rdt_audio_clone(const RdtAudio* source) {
    if (!source || source->pcm->references == SIZE_MAX) return nullptr;
    auto* player = (RdtAudio*)mem_calloc(1, sizeof(RdtAudio), MEM_CAT_RENDER);
    if (!player) return nullptr;
    player->pcm = source->pcm; player->pcm->references++;
    player->identity = source->identity;
    player->volume = 1; player->pitch = 1; player->parameters = rdt_audio_default_parameters();
    player->state = RDT_AUDIO_READY; return player;
}
void rdt_audio_destroy(RdtAudio* audio) {
    if (!audio) return;
    if (!--audio->pcm->references) { mem_free(audio->pcm->samples); mem_free(audio->pcm); }
    mem_free(audio);
}
RdtAudioParameters rdt_audio_default_parameters(void) { return {1, 1, 1, 0, AUDIO_BUS_RATE / 2.f, false, false}; }
bool rdt_audio_parameters(RdtAudio* audio, const RdtAudioParameters* parameters) {
    if (!audio || !parameters) return false;
    const auto& p = *parameters;
    if (!isfinite(p.left) || !isfinite(p.right) || !isfinite(p.pitch) || !isfinite(p.smoothing) || !isfinite(p.lowpass) ||
        p.left < 0 || p.left > 1 || p.right < 0 || p.right > 1 || p.pitch < .125f || p.pitch > 8 ||
        p.smoothing < 0 || p.smoothing > 10 || p.lowpass < 20 || p.lowpass > AUDIO_BUS_RATE / 2.f) return false;
    audio->parameters = p;
    audio->decay = p.smoothing > 0 ? exp(-1.0 / (AUDIO_BUS_RATE * (double)p.smoothing)) : 0;
    audio->filter_target = p.lowpass >= AUDIO_BUS_RATE / 2.f ? 0 : exp(-2 * M_PI * p.lowpass / AUDIO_BUS_RATE);
    return true;
}
bool rdt_audio_gain(RdtAudio* audio, float gain) {
    if (!audio || !isfinite(gain) || gain < 0 || gain > 1) return false;
    audio->volume = gain; return true;
}
void rdt_audio_identity(RdtAudio* audio, uint64_t identity) { if (audio) audio->identity = identity; }
static void audio_rewind(RdtAudio* audio) {
    audio->cursor = 0; audio->rendered = 0; audio->left = audio->right = 0;
    audio->filtered[0] = audio->filtered[1] = 0; audio->peak = audio->rms = 0;
}
void rdt_audio_stop(RdtAudio* audio) {
    if (!audio) return;
    audio_rewind(audio); __atomic_store_n(&audio->state, RDT_AUDIO_READY, __ATOMIC_RELEASE);
}
void rdt_audio_replace(RdtAudio* audio, RdtAudio* prepared) {
    if (!audio || !prepared) return;
    auto* pcm = audio->pcm; audio->pcm = prepared->pcm; prepared->pcm = pcm;
    uint64_t identity = audio->identity; audio->identity = prepared->identity; prepared->identity = identity;
    rdt_audio_stop(audio); rdt_audio_play(audio, audio->volume);
}
void rdt_audio_swap(RdtAudio* destination, RdtAudio* candidate) {
    if (destination && candidate) { auto before = *destination; *destination = *candidate; *candidate = before; }
}
bool rdt_audio_info(const RdtAudio* audio, RdtAudioInfo* info) {
    if (!audio || !info) return false;
    *info = {rdt_audio_state((RdtAudio*)audio), audio->cursor / AUDIO_BUS_RATE, (double)audio->pcm->frames / AUDIO_BUS_RATE,
             audio->left, audio->right, audio->pitch, audio->volume, audio->peak, audio->rms, audio->rendered, audio->parameters.loop, audio->identity, false};
    return true;
}
RdtAudioState rdt_audio_state(RdtAudio* audio) {
    return audio ? (RdtAudioState)__atomic_load_n(&audio->state, __ATOMIC_ACQUIRE) : RDT_AUDIO_ERROR;
}
const char* rdt_audio_state_name(RdtAudioState state) {
    static const char* names[] = {"ready", "playing", "paused", "ended", "error"};
    return state >= RDT_AUDIO_READY && state <= RDT_AUDIO_ERROR ? names[state] : "error";
}
void rdt_audio_fail(RdtAudio* audio) {
    if (audio) __atomic_store_n(&audio->state, RDT_AUDIO_ERROR, __ATOMIC_RELEASE);
}
bool rdt_audio_play(RdtAudio* audio, float volume) {
    if (!audio || !isfinite(volume) || volume < 0 || volume > 1 || rdt_audio_state(audio) == RDT_AUDIO_ERROR) return false;
    auto before = rdt_audio_state(audio);
    if (before == RDT_AUDIO_ENDED) {
        audio_rewind(audio);
    }
    if (before == RDT_AUDIO_READY || before == RDT_AUDIO_ENDED) {
        audio->pitch = audio->parameters.pitch; audio->filter = audio->filter_target;
    }
    audio->volume = volume;
    __atomic_store_n(&audio->state, RDT_AUDIO_PLAYING, __ATOMIC_RELEASE); return true;
}
void rdt_audio_pause(RdtAudio* audio) {
    if (rdt_audio_state(audio) == RDT_AUDIO_PLAYING)
        __atomic_store_n(&audio->state, RDT_AUDIO_PAUSED, __ATOMIC_RELEASE);
}
void rdt_audio_mix(void* source, float* output, uint32_t frames, uint64_t) {
    auto* audio = (RdtAudio*)source;
    if (!output || rdt_audio_state(audio) != RDT_AUDIO_PLAYING) return;
    const auto& p = audio->parameters; auto* pcm = audio->pcm;
    double energy = 0, peak = 0; uint32_t count = 0;
    for (; count < frames; count++) {
        if (audio->rendered == UINT64_MAX) { rdt_audio_fail(audio); break; }
        size_t at = (size_t)audio->cursor, next = at + 1;
        if (next == pcm->frames) next = p.loop ? 0 : at;
        double fraction = audio->cursor - at, samples[2];
        for (unsigned channel = 0; channel < 2; channel++)
            samples[channel] = pcm->samples[at * 2 + channel] * (1 - fraction) + pcm->samples[next * 2 + channel] * fraction;
        if (p.mono) samples[0] = samples[1] = (samples[0] + samples[1]) * .5;
        // sample-clock smoothing is independent of render frames and callback partitions.
        audio->left = p.left * audio->volume + (audio->left - p.left * audio->volume) * audio->decay;
        audio->right = p.right * audio->volume + (audio->right - p.right * audio->volume) * audio->decay;
        audio->pitch = p.pitch + (audio->pitch - p.pitch) * audio->decay;
        audio->filter = audio->filter_target + (audio->filter - audio->filter_target) * audio->decay;
        double gains[2] = {audio->left, audio->right}; float mixed[2];
        for (unsigned channel = 0; channel < 2; channel++) {
            auto& filtered = audio->filtered[channel];
            filtered = samples[channel] + (filtered - samples[channel]) * audio->filter;
            double value = filtered * gains[channel]; mixed[channel] = output[count * 2 + channel] + (float)value;
            peak = fmax(peak, fabs(value)); energy += value * value;
        }
        if (!isfinite(mixed[0]) || !isfinite(mixed[1])) { rdt_audio_fail(audio); break; }
        output[count * 2] = mixed[0]; output[count * 2 + 1] = mixed[1]; audio->rendered++;
        audio->cursor += audio->pitch;
        if (audio->cursor >= pcm->frames) {
            if (p.loop) audio->cursor = fmod(audio->cursor, (double)pcm->frames);
            else { audio->cursor = (double)pcm->frames; __atomic_store_n(&audio->state, RDT_AUDIO_ENDED, __ATOMIC_RELEASE); count++; break; }
        }
    }
    audio->peak = peak; audio->rms = count ? sqrt(energy / (count * 2)) : 0;
}

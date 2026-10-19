// rdt_speech_stub.cpp — null speech engine for builds without a platform backend
// (Linux is deferred and Windows arrives in M3; see Radiant_Design_Speech.md §8).
// No voices exist and every render fails, so dom.speak reports speecherror.

#include "rdt_speech.h"

extern "C" {
size_t rdt_speech_voices(RdtSpeechVoice*, size_t) { return 0; }
uint64_t rdt_speech_voices_generation(void) { return 1; }
RdtSpeechJob* rdt_speech_render_begin(const RdtSpeechRequest*) { return nullptr; }
RdtSpeechJobState rdt_speech_job_state(RdtSpeechJob*) { return RDT_SPEECH_FAILED; }
const float* rdt_speech_job_samples(RdtSpeechJob*, uint32_t* frames, uint32_t* sample_rate) {
    if (frames) *frames = 0;
    if (sample_rate) *sample_rate = 0;
    return nullptr;
}
const RdtSpeechMark* rdt_speech_job_marks(RdtSpeechJob*, uint32_t* count) {
    if (count) *count = 0;
    return nullptr;
}
const char* rdt_speech_job_error(RdtSpeechJob*) { return "speech is not available on this platform"; }
void rdt_speech_job_destroy(RdtSpeechJob*) {}
}

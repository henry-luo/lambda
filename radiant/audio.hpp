#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rdt_audio.h"
struct DomDocument;
uint64_t radiant_audio_open(DomDocument* document, const void* bytes, size_t length);
bool radiant_audio_play(DomDocument* document, uint64_t token, float volume);
bool radiant_audio_pause(DomDocument* document, uint64_t token);
bool radiant_audio_close(DomDocument* document, uint64_t token);
const char* radiant_audio_state(DomDocument* document, uint64_t token);

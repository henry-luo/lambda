#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rdt_audio.h"
#include "midi_player.hpp"
struct DomDocument;
struct DomNode;
struct DomElement;
uint64_t radiant_audio_open(DomDocument* document, const void* bytes, size_t length);
bool radiant_audio_play(DomDocument* document, uint64_t token, float volume);
bool radiant_audio_pause(DomDocument* document, uint64_t token);
bool radiant_audio_close(DomDocument* document, uint64_t token);
const char* radiant_audio_state(DomDocument* document, uint64_t token);
uint64_t radiant_midi_open(DomElement* owner, const MidiEvent* events, size_t count,
    double duration, const void* font, size_t font_size);
bool radiant_midi_command(DomDocument* document, uint64_t token, const char* command, double value);
bool radiant_midi_info(DomDocument* document, uint64_t token, MidiSnapshot* snapshot);
void radiant_audio_prune(DomDocument* document);
void radiant_audio_follow_rebuild(DomDocument* document, DomNode* old_root, DomNode* new_root);

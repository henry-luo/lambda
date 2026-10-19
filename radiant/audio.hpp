#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rdt_audio.h"
#include "midi_player.hpp"
#include "audio_bus.hpp"
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
// the document's sub-mix on the window bus, created on first use (§3.2).
AudioSubmixId radiant_document_submix(DomDocument* document);
// seconds on the document's sub-mix clock; 0 before its first audible stream.
double radiant_audio_clock(DomDocument* document);
// frame-clock tick: retires a lost device and, with offline output, advances the
// sub-mix to `now_ms`. Returns whether the sub-mix needs further frames.
bool radiant_audio_tick(DomDocument* document, double now_ms);
// offline output only: the sub-mix has voices, so the host must keep ticking frames.
bool radiant_audio_frames_pending(DomDocument* document);
// D7.5.4 audible-output gate. A local document may start audio; a network
// document needs sticky activation from trusted host input; only the host
// grants activation up front (`view --autoplay`).
void radiant_audio_grant_activation(bool granted);
void radiant_audio_note_activation(DomDocument* document);
bool radiant_document_may_start_audio(DomDocument* document);

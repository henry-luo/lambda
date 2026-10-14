#pragma once
#include <stddef.h>
#include <stdint.h>

// copied native MIDI bytes and absolute seconds; no Lambda/DOM pointers cross the audio boundary.
struct MidiEvent { double seconds; uint8_t status, a, b; };
struct MidiPlayer;
enum MidiState { MIDI_READY, MIDI_PLAYING, MIDI_PAUSED, MIDI_ENDED, MIDI_ERROR };
struct MidiSnapshot { MidiState state; double position, duration, speed, volume; uint16_t mute, solo; };
MidiPlayer* midi_player_open(const MidiEvent* events, size_t count, double duration,
    const void* font, size_t font_size, bool realtime = true, uint32_t sample_rate = 48000);
void midi_player_close(MidiPlayer* player);
bool midi_player_command(MidiPlayer* player, const char* command, double value);
MidiSnapshot midi_player_info(MidiPlayer* player);
// the same sample scheduler serves hardware callbacks and deterministic offline tests.
void midi_player_render(MidiPlayer* player, float* stereo, uint32_t frames);

#include <gtest/gtest.h>
#include "../radiant/midi_player.hpp"
#include "../radiant/audio_bus.hpp"
#include "../lib/mem.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

class MidiPlayback : public ::testing::Test {
protected:
    void* font = nullptr;
    size_t font_size = 0;
    MidiPlayer* player = nullptr;
    float samples[4800 * 2] = {};
    void SetUp() override {
        FILE* file = fopen("lmd/package/music/soundfonts/TimGM6mb.sf2", "rb");
        ASSERT_NE(file, nullptr);
        ASSERT_EQ(fseek(file, 0, SEEK_END), 0);
        long size = ftell(file);
        ASSERT_GT(size, 0);
        rewind(file);
        font_size = (size_t)size;
        font = mem_alloc(font_size, MEM_CAT_RENDER);
        ASSERT_NE(font, nullptr);
        ASSERT_EQ(fread(font, 1, font_size, file), font_size);
        fclose(file);
    }
    void TearDown() override { midi_player_close(player); mem_free(font); }
    void open(const MidiEvent* events, size_t count, double duration = 2) {
        player = midi_player_open(events, count, duration, font, font_size);
        ASSERT_NE(player, nullptr);
    }
    double render(uint32_t frames = 4800) {
        midi_player_render(player, samples, frames);
        double energy = 0;
        for (uint32_t i = 0; i < frames * 2; i++) { EXPECT_TRUE(isfinite(samples[i])); energy += samples[i] * samples[i]; }
        return energy;
    }
};
TEST_F(MidiPlayback, SampleClockAndDelayedOnset) {
    MidiEvent events[] = {{0.1,144,60,100},{0.5,128,60,0}};
    open(events, 2);
    ASSERT_TRUE(midi_player_command(player,"play",0));
    EXPECT_EQ(render(),0);
    EXPECT_NEAR(midi_player_info(player).position,0.1,1e-9);
    EXPECT_GT(render(),0.1);
}
TEST_F(MidiPlayback, PauseStopAndResumeSilence) {
    MidiEvent events[] = {{0,144,60,100},{1,128,60,0}};
    open(events,2);
    ASSERT_TRUE(midi_player_command(player,"play",0)); EXPECT_GT(render(),0.1);
    ASSERT_TRUE(midi_player_command(player,"pause",0)); double position=midi_player_info(player).position;
    EXPECT_EQ(render(),0); EXPECT_EQ(midi_player_info(player).position,position);
    ASSERT_TRUE(midi_player_command(player,"play",0)); EXPECT_GT(render(),0.1);
    ASSERT_TRUE(midi_player_command(player,"stop",0)); EXPECT_EQ(render(),0);
    EXPECT_EQ(midi_player_info(player).position,0); EXPECT_EQ(midi_player_info(player).state,MIDI_READY);
}
TEST_F(MidiPlayback, SeekRestoresSustainAndProgram) {
    MidiEvent events[] = {{0,192,16,0},{0,176,64,127},{0,144,60,100},{0.2,128,60,0},{1,176,64,0}};
    open(events,5);
    ASSERT_TRUE(midi_player_command(player,"seek",0.5)); EXPECT_EQ(render(),0);
    ASSERT_TRUE(midi_player_command(player,"play",0)); EXPECT_GT(render(),0.1);
    ASSERT_TRUE(midi_player_command(player,"seek",1.5)); EXPECT_EQ(render(),0);
}
TEST_F(MidiPlayback, RateVolumeMuteSoloAndEnd) {
    MidiEvent events[] = {{0,144,60,100},{1,128,60,0}};
    open(events,2,1);
    ASSERT_TRUE(midi_player_command(player,"speed",2));
    ASSERT_TRUE(midi_player_command(player,"play",0)); render();
    EXPECT_NEAR(midi_player_info(player).position,0.2,1e-9);
    ASSERT_TRUE(midi_player_command(player,"mute",1)); EXPECT_EQ(render(),0);
    ASSERT_TRUE(midi_player_command(player,"mute",0)); EXPECT_GT(render(),0.1);
    ASSERT_TRUE(midi_player_command(player,"solo",2)); EXPECT_EQ(render(),0);
    render(); EXPECT_EQ(midi_player_info(player).state,MIDI_ENDED); EXPECT_EQ(render(),0);
    ASSERT_TRUE(midi_player_command(player,"play",0)); EXPECT_LT(midi_player_info(player).position,0.01);
    ASSERT_TRUE(midi_player_command(player,"volume",0)); EXPECT_EQ(render(),0);
}
TEST_F(MidiPlayback, ChannelReleaseHonorsSustainAndSoundOffIsImmediate) {
    MidiEvent events[]={{0,192,16,0},{0,176,64,127},{0,144,60,100},
        {0.1,176,123,0},{0.5,176,120,0}};
    open(events,5);
    ASSERT_TRUE(midi_player_command(player,"play",0));
    EXPECT_GT(render(),0.1);
    EXPECT_GT(render(),0.1);
    ASSERT_TRUE(midi_player_command(player,"seek",0.3));
    EXPECT_GT(render(),0.1);
    render();
    EXPECT_EQ(render(),0);
}
TEST_F(MidiPlayback, RejectsInvalidCommandsWithoutMovingClock) {
    MidiEvent events[]={{0,144,60,100},{1,128,60,0}}; open(events,2);
    EXPECT_FALSE(midi_player_command(player,"seek",3)); EXPECT_FALSE(midi_player_command(player,"speed",0));
    EXPECT_FALSE(midi_player_command(player,"volume",NAN)); EXPECT_FALSE(midi_player_command(player,"mute",1.5));
    EXPECT_FALSE(midi_player_command(player,"unknown",0)); EXPECT_EQ(midi_player_info(player).position,0);
}
TEST_F(MidiPlayback, RejectsInvalidEventBuffers) {
    MidiEvent unordered[]={{1,144,60,100},{0,128,60,0}};
    EXPECT_EQ(midi_player_open(unordered,2,2,font,font_size),nullptr);
    MidiEvent invalid[]={{0,144,128,100}};
    EXPECT_EQ(midi_player_open(invalid,1,2,font,font_size),nullptr);
    MidiEvent sysex[]={{0,240,0,0}};
    EXPECT_EQ(midi_player_open(sysex,1,2,font,font_size),nullptr);
}
TEST_F(MidiPlayback, EveryGeneralMidiProgramProducesSamples) {
    MidiEvent events[]={{0,192,0,0},{0,144,60,100},{1,128,60,0}};
    for (unsigned program=0;program<128;program++) {
        events[0].a=(uint8_t)program;
        open(events,3);
        ASSERT_TRUE(midi_player_command(player,"play",0));
        EXPECT_GT(render(),0) << "program " << program;
        midi_player_close(player); player=nullptr;
    }
}
TEST_F(MidiPlayback, RepeatedOpenAndClose) {
    MidiEvent events[]={{0,144,60,100},{1,128,60,0}};
    for (unsigned i=0;i<8;i++) { open(events,2); midi_player_command(player,"play",0); render(); midi_player_close(player); player=nullptr; }
}
TEST_F(MidiPlayback, OfflineSubmixAdvancesItsClockAndMixesVoices) {
    // Radiant_Design_Speech §3.2: a session plays as a voice of a sub-mix, whose clock counts mixed frames.
    audio_bus_set_offline(true);
    MidiEvent events[]={{0,144,60,100},{1,128,60,0}}; open(events,2);
    AudioSubmixId submix=audio_submix_create(); ASSERT_NE(submix,0u);
    const char* error=nullptr;
    AudioVoiceId voice=audio_voice_add(submix,midi_player_mix,player,&error);
    ASSERT_NE(voice,0u)<<(error?error:"");
    ASSERT_TRUE(audio_voice_lock(voice)); ASSERT_TRUE(midi_player_command(player,"play",0)); audio_voice_unlock(voice);
    ASSERT_TRUE(audio_submix_render(submix,samples,4800));
    double energy=0; for (float v:samples) energy+=v*v;
    EXPECT_GT(energy,0.1);
    EXPECT_EQ(audio_submix_clock(submix),4800u);
    EXPECT_NEAR(midi_player_info(player).position,0.1,1e-9);
    // a destroyed sub-mix retires its voices; their stale handles stay inert.
    audio_submix_destroy(submix);
    EXPECT_FALSE(audio_voice_lock(voice));
    EXPECT_FALSE(audio_submix_render(submix,samples,480));
    EXPECT_EQ(audio_submix_clock(submix),0u);
    audio_bus_set_offline(false);
}
TEST_F(MidiPlayback, LiveDeviceClock) {
    if (!getenv("LAMBDA_TEST_MIDI_DEVICE")) GTEST_SKIP() << "set LAMBDA_TEST_MIDI_DEVICE=1 for audible hardware verification";
    MidiEvent events[]={{0,144,60,80},{0.25,128,60,0}};
    player=midi_player_open(events,2,0.4,font,font_size,AUDIO_BUS_RATE);
    ASSERT_NE(player,nullptr);
    // the window bus owns the device; the session is one voice of a sub-mix.
    AudioSubmixId submix=audio_submix_create(); ASSERT_NE(submix,0u);
    const char* error=nullptr;
    AudioVoiceId voice=audio_voice_add(submix,midi_player_mix,player,&error);
    ASSERT_NE(voice,0u)<<(error?error:"");
    ASSERT_TRUE(audio_voice_lock(voice));
    ASSERT_TRUE(midi_player_command(player,"volume",0.15));
    ASSERT_TRUE(midi_player_command(player,"play",0));
    audio_voice_unlock(voice);
#ifdef _WIN32
    Sleep(500);
#else
    usleep(500000);
#endif
    EXPECT_GT(midi_player_info(player).position,0.1);
    EXPECT_GT(audio_submix_clock(submix),(uint64_t)AUDIO_BUS_RATE/10);
    EXPECT_NE(midi_player_info(player).state,MIDI_ERROR);
    ASSERT_TRUE(audio_voice_lock(voice));
    EXPECT_TRUE(midi_player_command(player,"stop",0));
    audio_voice_unlock(voice);
    audio_submix_destroy(submix);
}

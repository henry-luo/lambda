# TinySoundFont

Unmodified `tsf.h` and `LICENSE` from
https://github.com/schellingb/TinySoundFont at
`853a0a171759f1ddba0de1442133a75912bbeffa` (MIT).

SHA-256 of `tsf.h`:
`70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197`.

Lambda's adapter is `radiant/midi_player.cpp`. It preallocates 256 voices and
16 channels. Immediate transport silence uses the pinned voice record's
`playingPreset` field because public reset frees channels and public
all-sounds-off retains a release tail. Audit this adapter when updating TSF.
No upstream code is patched. The upstream project does not accept AI-generated
contributions; this integration makes no upstream submissions.

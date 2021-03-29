/*
  SDL_mixer:  An audio mixer library based on the SDL library
  Copyright (C) 1997-2021 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  James Le Cuirot
  chewi@aura-online.co.uk
*/

#ifdef MUSIC_MID_FLUIDSYNTH

#include "SDL_loadso.h"
#include "SDL_rwops.h"

#include "utils.h"
#include "music_fluidsynth.h"
#include "midi_seq/mix_midi_seq.h"

#include <fluidlite.h>

typedef struct {
    int loaded;
    void *handle;

    int (*delete_fluid_synth)(fluid_synth_t*);
    void (*delete_fluid_settings)(fluid_settings_t*);
    int (*fluid_settings_setnum)(fluid_settings_t*, const char*, double);
    int (*fluid_settings_getnum)(fluid_settings_t*, const char*, double*);
    fluid_settings_t* (*fluid_synth_get_settings)(fluid_synth_t*);
    void (*fluid_synth_set_gain)(fluid_synth_t*, float);
    int (*fluid_synth_sfload)(fluid_synth_t*, const char*, int);
    int (*fluid_synth_write_s16)(fluid_synth_t*, int, void*, int, int, void*, int, int);
    int (*fluid_synth_write_float)(fluid_synth_t*, int, void*, int, int, void*, int, int);
    fluid_settings_t* (*new_fluid_settings)(void);
    fluid_synth_t* (*new_fluid_synth)(fluid_settings_t*);
    int (*fluid_synth_noteon)(fluid_synth_t*, int, int, int);
    int (*fluid_synth_noteoff)(fluid_synth_t*, int, int);
    int (*fluid_synth_cc)(fluid_synth_t*, int, int, int);
    int (*fluid_synth_pitch_bend)(fluid_synth_t*, int, int);
    int (*fluid_synth_key_pressure)(fluid_synth_t*, int, int, int);
    int (*fluid_synth_channel_pressure)(fluid_synth_t*, int, int);
    int (*fluid_synth_program_change)(fluid_synth_t*, int, int);
    int (*fluid_synth_sysex)(fluid_synth_t *, const char *, int, char *, int *, int *, int);
} fluidsynth_loader;

static fluidsynth_loader fluidsynth = {
    0, NULL
};

#ifdef FLUIDSYNTH_DYNAMIC
#define FUNCTION_LOADER(FUNC, SIG) \
    fluidsynth.FUNC = (SIG) SDL_LoadFunction(fluidsynth.handle, #FUNC); \
    if (fluidsynth.FUNC == NULL) { SDL_UnloadObject(fluidsynth.handle); return -1; }
#else
#define FUNCTION_LOADER(FUNC, SIG) \
    fluidsynth.FUNC = FUNC;
#endif

static int FLUIDSYNTH_Load()
{
    if (fluidsynth.loaded == 0) {
#ifdef FLUIDSYNTH_DYNAMIC
        fluidsynth.handle = SDL_LoadObject(FLUIDSYNTH_DYNAMIC);
        if (fluidsynth.handle == NULL) {
            return -1;
        }
#endif

        FUNCTION_LOADER(delete_fluid_synth, int (*)(fluid_synth_t*))
        FUNCTION_LOADER(delete_fluid_settings, void (*)(fluid_settings_t*))
        FUNCTION_LOADER(fluid_settings_setnum, int (*)(fluid_settings_t*, const char*, double))
        FUNCTION_LOADER(fluid_settings_getnum, int (*)(fluid_settings_t*, const char*, double*))
        FUNCTION_LOADER(fluid_synth_get_settings, fluid_settings_t* (*)(fluid_synth_t*))
        FUNCTION_LOADER(fluid_synth_set_gain, void (*)(fluid_synth_t*, float))
        FUNCTION_LOADER(fluid_synth_sfload, int(*)(fluid_synth_t*, const char*, int))
        FUNCTION_LOADER(fluid_synth_write_s16, int(*)(fluid_synth_t*, int, void*, int, int, void*, int, int))
        FUNCTION_LOADER(fluid_synth_write_float, int(*)(fluid_synth_t*, int, void*, int, int, void*, int, int))
        FUNCTION_LOADER(new_fluid_settings, fluid_settings_t* (*)(void))
        FUNCTION_LOADER(new_fluid_synth, fluid_synth_t* (*)(fluid_settings_t*))
        FUNCTION_LOADER(fluid_synth_noteon, int (*)(fluid_synth_t*, int, int, int))
        FUNCTION_LOADER(fluid_synth_noteoff, int (*)(fluid_synth_t*, int, int))
        FUNCTION_LOADER(fluid_synth_cc, int (*)(fluid_synth_t*, int, int, int))
        FUNCTION_LOADER(fluid_synth_pitch_bend, int (*)(fluid_synth_t*, int, int))
        FUNCTION_LOADER(fluid_synth_key_pressure, int (*)(fluid_synth_t*, int, int, int))
        FUNCTION_LOADER(fluid_synth_channel_pressure, int (*)(fluid_synth_t*, int, int))
        FUNCTION_LOADER(fluid_synth_program_change, int (*)(fluid_synth_t*, int, int))
        FUNCTION_LOADER(fluid_synth_sysex, int (*)(fluid_synth_t *, const char *, int, char *, int *, int *, int))
    }
    ++fluidsynth.loaded;

    return 0;
}

static void FLUIDSYNTH_Unload()
{
    if (fluidsynth.loaded == 0) {
        return;
    }
    if (fluidsynth.loaded == 1) {
#ifdef FLUIDSYNTH_DYNAMIC
        SDL_UnloadObject(fluidsynth.handle);
#endif
    }
    --fluidsynth.loaded;
}

typedef struct {
    fluid_synth_t *synth;
    BW_MidiRtInterface seq_if;
    void *player;
    SDL_AudioStream *stream;
    void *buffer;
    int buffer_size;
    int sample_size;
    int volume;
    int play_count;
    double tempo;

    Mix_MusicMetaTags tags;
} FLUIDSYNTH_Music;



/****************************************************
 *           Real-Time MIDI calls proxies           *
 ****************************************************/

static void rtNoteOn(void *userdata, uint8_t channel, uint8_t note, uint8_t velocity)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_noteon(seqi->synth, channel, note, velocity);
}

static void rtNoteOff(void *userdata, uint8_t channel, uint8_t note)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_noteoff(seqi->synth, channel, note);
}

static void rtNoteAfterTouch(void *userdata, uint8_t channel, uint8_t note, uint8_t atVal)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_key_pressure(seqi->synth, channel, note, atVal);
}

static void rtChannelAfterTouch(void *userdata, uint8_t channel, uint8_t atVal)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_channel_pressure(seqi->synth, channel, atVal);
}

static void rtControllerChange(void *userdata, uint8_t channel, uint8_t type, uint8_t value)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_cc(seqi->synth, channel, type, value);
}

static void rtPatchChange(void *userdata, uint8_t channel, uint8_t patch)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_program_change(seqi->synth, channel, patch);
}

static void rtPitchBend(void *userdata, uint8_t channel, uint8_t msb, uint8_t lsb)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_pitch_bend(seqi->synth, channel, (msb << 7) | lsb);
}

static void rtSysEx(void *userdata, const uint8_t *msg, size_t size)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_sysex(seqi->synth, (const char*)(msg), (int)(size), NULL, NULL, NULL, 0);
}

static void playSynthS16(void *userdata, uint8_t *stream, size_t length)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_write_s16(seqi->synth, (int)(length / 4),
                                     stream, 0, 2,
                                     stream, 1, 2);
}

static void playSynthF32(void *userdata, uint8_t *stream, size_t length)
{
    FLUIDSYNTH_Music *seqi = (FLUIDSYNTH_Music*)(userdata);
    fluidsynth.fluid_synth_write_float(seqi->synth, (int)(length / 8),
                                       stream, 0, 2,
                                       stream, 1, 2);
}


static int init_interface(FLUIDSYNTH_Music *seqi, int outFormat)
{
    int inFormat = outFormat;
    SDL_memset(&seqi->seq_if, 0, sizeof(BW_MidiRtInterface));

    /* seq->onDebugMessage             = hooks.onDebugMessage; */
    /* seq->onDebugMessage_userData    = hooks.onDebugMessage_userData; */

    /* MIDI Real-Time calls */
    seqi->seq_if.rtUserData = (void *)seqi;
    seqi->seq_if.rt_noteOn  = rtNoteOn;
    seqi->seq_if.rt_noteOff = rtNoteOff;
    seqi->seq_if.rt_noteAfterTouch = rtNoteAfterTouch;
    seqi->seq_if.rt_channelAfterTouch = rtChannelAfterTouch;
    seqi->seq_if.rt_controllerChange = rtControllerChange;
    seqi->seq_if.rt_patchChange = rtPatchChange;
    seqi->seq_if.rt_pitchBend = rtPitchBend;
    seqi->seq_if.rt_systemExclusive = rtSysEx;

    switch(outFormat)
    {
    default:
    case AUDIO_U8:
    case AUDIO_S8:
    case AUDIO_S16LSB:
    case AUDIO_S16MSB:
    case AUDIO_U16LSB:
    case AUDIO_U16MSB:
        seqi->seq_if.onPcmRender = playSynthS16;
        seqi->seq_if.pcmSampleRate = music_spec.freq;
        seqi->seq_if.pcmFrameSize = 2 /*channels*/ * sizeof(Sint16) /*size of one sample*/;
        inFormat = AUDIO_S16SYS;
        break;
    case AUDIO_S32LSB:
    case AUDIO_S32MSB:
    case AUDIO_F32LSB:
    case AUDIO_F32MSB:
        seqi->seq_if.onPcmRender = playSynthF32;
        seqi->seq_if.pcmSampleRate = music_spec.freq;
        seqi->seq_if.pcmFrameSize = 2 /*channels*/ * sizeof(float) /*size of one sample*/;
        inFormat = AUDIO_F32SYS;
        break;
    }
    seqi->seq_if.onPcmRender_userData = seqi;

    return inFormat;
}


static void FLUIDSYNTH_Delete(void *context);

static int SDLCALL fluidsynth_check_soundfont(const char *path, void *data)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");

    (void)data;
    if (rw) {
        SDL_RWclose(rw);
        return 1;
    } else {
        Mix_SetError("Failed to access the SoundFont %s", path);
        return 0;
    }
}

static int SDLCALL fluidsynth_load_soundfont(const char *path, void *data)
{
    /* If this fails, it's too late to try Timidity so pray that at least one works. */
    fluidsynth.fluid_synth_sfload((fluid_synth_t*) data, path, 1);
    return 1;
}

static int FLUIDSYNTH_Open(const SDL_AudioSpec *spec)
{
    (void)spec;
    if (!Mix_EachSoundFont(fluidsynth_check_soundfont, NULL)) {
        return -1;
    }
    return 0;
}

static FLUIDSYNTH_Music *FLUIDSYNTH_LoadMusic(void *data)
{
    SDL_RWops *src = (SDL_RWops *)data;
    FLUIDSYNTH_Music *music;
    fluid_settings_t *settings;
    double samplerate; /* as set by the lib. */
    int srcFormat;

    if ((music = SDL_calloc(1, sizeof(FLUIDSYNTH_Music)))) {
        Uint8 channels = 2;
        music->volume = MIX_MAX_VOLUME;
        music->tempo = 1.0;
        music->play_count = 0;

        srcFormat = init_interface(music, music_spec.format);
        if(srcFormat == AUDIO_S16SYS)
            music->sample_size = sizeof(Sint16);
        else
            music->sample_size = sizeof(float);

        music->buffer_size = music_spec.samples * music->sample_size * channels;
        if ((music->buffer = SDL_malloc((size_t)music->buffer_size))) {
            if ((settings = fluidsynth.new_fluid_settings())) {
                fluidsynth.fluid_settings_setnum(settings, "synth.sample-rate", (double) music_spec.freq);
                fluidsynth.fluid_settings_getnum(settings, "synth.sample-rate", &samplerate);
                music->seq_if.pcmSampleRate = samplerate;

                if ((music->synth = fluidsynth.new_fluid_synth(settings))) {
                    if (Mix_EachSoundFont(fluidsynth_load_soundfont, (void*) music->synth)) {

                        if ((music->player = midi_seq_init_interface(&music->seq_if))) {
                            void *buffer;
                            size_t size;

                            buffer = SDL_LoadFile_RW(src, &size, SDL_FALSE);
                            if (buffer) {
                                if (midi_seq_openData(music->player, buffer, size) == 0) {
                                    SDL_free(buffer);

                                    if ((music->stream = SDL_NewAudioStream(srcFormat, channels, (int) samplerate,
                                                          music_spec.format, music_spec.channels, music_spec.freq))) {
                                        meta_tags_init(&music->tags);
                                        _Mix_ParseMidiMetaTag(&music->tags, MIX_META_TITLE, midi_seq_meta_title(music->player));
                                        _Mix_ParseMidiMetaTag(&music->tags, MIX_META_COPYRIGHT, midi_seq_meta_copyright(music->player));
                                        return music;
                                    } else {
                                        FLUIDSYNTH_Delete(music);
                                        return NULL;
                                    }
                                } else {
                                    Mix_SetError("FluidSynth failed to load in-memory song: %s", midi_seq_get_error(music->player));
                                }
                                SDL_free(buffer);
                            } else {
                                SDL_OutOfMemory();
                            }
                            midi_seq_free(music->player);
                        } else {
                            Mix_SetError("Failed to create FluidSynth player");
                        }
                    }
                    fluidsynth.delete_fluid_synth(music->synth);
                } else {
                    Mix_SetError("Failed to create FluidSynth synthesizer");
                }
                fluidsynth.delete_fluid_settings(settings);
            } else {
                Mix_SetError("Failed to create FluidSynth settings");
            }
        } else {
            SDL_OutOfMemory();
        }
        SDL_free(music);
    } else {
        SDL_OutOfMemory();
    }
    return NULL;
}

static void *FLUIDSYNTH_CreateFromRW(SDL_RWops *src, int freesrc)
{
    FLUIDSYNTH_Music *music;

    music = FLUIDSYNTH_LoadMusic(src);
    if (music && freesrc) {
        SDL_RWclose(src);
    }
    return music;
}

static void FLUIDSYNTH_SetVolume(void *context, int volume)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    /* FluidSynth's default is 0.2. Make 1.2 the maximum. */
    music->volume = volume;
    fluidsynth.fluid_synth_set_gain(music->synth, (float) (volume * 1.2 / MIX_MAX_VOLUME));
}

static int FLUIDSYNTH_GetVolume(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    return music->volume;
}

static int FLUIDSYNTH_Play(void *context, int play_count)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    midi_seq_rewind(music->player);
    music->play_count = play_count;
    midi_seq_set_loop_enabled(music->player,  (play_count < 0));
    return 0;
}

static const char* FLUIDSYNTH_GetMetaTag(void *context, Mix_MusicMetaTag tag_type)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    return meta_tags_get(&music->tags, tag_type);
}

static int FLUIDSYNTH_Seek(void *context, double time)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    midi_seq_seek(music->player, time);
    return 0;
}

static double FLUIDSYNTH_Tell(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    return midi_seq_tell(music->player);
}

static double FLUIDSYNTH_Duration(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    return midi_seq_length(music->player);
}

static int FLUIDSYNTH_SetTempo(void *context, double tempo)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    if (music && (tempo > 0.0)) {
        midi_seq_set_tempo_multiplier(music->player, tempo);
        music->tempo = tempo;
        return 0;
    }

    return 0;
}

static double FLUIDSYNTH_GetTempo(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    if (music) {
        return music->tempo;
    }
    return -1.0;
}

static double FLUIDSYNTH_LoopStart(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    if (music) {
        return midi_seq_loop_start(music->player);
    }
    return -1;
}

static double FLUIDSYNTH_LoopEnd(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    if (music) {
        return midi_seq_loop_end(music->player);
    }
    return -1;
}

static double FLUIDSYNTH_oopLength(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    if (music) {
        double start = midi_seq_loop_start(music->player);
        double end = midi_seq_loop_end(music->player);
        if (start >= 0 && end >= 0) {
            return (end - start);
        }
    }
    return -1;
}

static int FLUIDSYNTH_GetSome(void *context, void *data, int bytes, SDL_bool *done)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    int filled, gotten_len, amount;

    filled = SDL_AudioStreamGet(music->stream, data, bytes);
    if (filled != 0) {
        return filled;
    }

    if (!music->play_count) {
        /* All done */
        *done = SDL_TRUE;
        return 0;
    }

    gotten_len = midi_seq_play_buffer(music->player, music->buffer, music->buffer_size);

    if (gotten_len <= 0) {
        *done = SDL_TRUE;
        return 0;
    }

    amount = gotten_len;
    if (amount > 0) {
        if (SDL_AudioStreamPut(music->stream, music->buffer, amount) < 0) {
            return -1;
        }
    } else {
        if (music->play_count == 1) {
            music->play_count = 0;
            SDL_AudioStreamFlush(music->stream);
        } else {
            int play_count = -1;
            if (music->play_count > 0) {
                play_count = (music->play_count - 1);
            }
            midi_seq_rewind(music->player);
            music->play_count = play_count;
        }
    }

    return 0;
}
static int FLUIDSYNTH_GetAudio(void *context, void *data, int bytes)
{
    return music_pcm_getaudio(context, data, bytes, MIX_MAX_VOLUME, FLUIDSYNTH_GetSome);
}

static void FLUIDSYNTH_Delete(void *context)
{
    FLUIDSYNTH_Music *music = (FLUIDSYNTH_Music *)context;
    fluid_settings_t *settings = fluidsynth.fluid_synth_get_settings(music->synth);
    midi_seq_free(music->player);
    meta_tags_clear(&music->tags);
    fluidsynth.delete_fluid_synth(music->synth);
    fluidsynth.delete_fluid_settings(settings);
    if (music->stream) {
        SDL_FreeAudioStream(music->stream);
    }
    SDL_free(music);
}

Mix_MusicInterface Mix_MusicInterface_FLUIDSYNTH =
{
    "FLUIDSYNTH",
    MIX_MUSIC_FLUIDSYNTH,
    MUS_MID,
    SDL_FALSE,
    SDL_FALSE,

    FLUIDSYNTH_Load,
    FLUIDSYNTH_Open,
    FLUIDSYNTH_CreateFromRW,
    NULL,   /* CreateFromRWex [MIXER-X]*/
    NULL,   /* CreateFromFile */
    NULL,   /* CreateFromFileEx [MIXER-X]*/
    FLUIDSYNTH_SetVolume,
    FLUIDSYNTH_GetVolume,
    FLUIDSYNTH_Play,
    NULL,
    FLUIDSYNTH_GetAudio,
    NULL,   /* Jump */
    FLUIDSYNTH_Seek,
    FLUIDSYNTH_Tell,
    FLUIDSYNTH_Duration,
    FLUIDSYNTH_SetTempo,
    FLUIDSYNTH_GetTempo,
    FLUIDSYNTH_LoopStart,
    FLUIDSYNTH_LoopEnd,
    FLUIDSYNTH_oopLength,
    FLUIDSYNTH_GetMetaTag,
    NULL,   /* Pause */
    NULL,   /* Resume */
    NULL,   /* Stop */
    FLUIDSYNTH_Delete,
    NULL,   /* Close */
    FLUIDSYNTH_Unload
};

#endif /* MUSIC_MID_FLUIDSYNTH */

/* vi: set ts=4 sw=4 expandtab: */
#include "audio.h"

#include <SDL2/SDL.h>
#include "aoa.h"
#include "command.h"
#include "log.h"

SDL_bool sdl_audio_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOGC("Could not initialize SDL audio: %s", SDL_GetError());
        return SDL_FALSE;
    }
    return SDL_TRUE;
}

static void init_audio_spec(SDL_AudioSpec *spec) {
    SDL_zero(*spec);
    spec->freq = 44100;
    spec->format = AUDIO_S16LSB;
    spec->channels = 2;
    spec->samples = 1024;
}

SDL_bool audio_player_init(struct audio_player *player, const char *serial) {
    player->serial = SDL_strdup(serial);
    return !!player->serial;
}

void audio_player_destroy(struct audio_player *player) {
    SDL_free((void *) player->serial);
}

void audio_player_destroy(struct audio_player *player);

static void audio_input_callback(void *userdata, Uint8 *stream, int len) {
    struct audio_player *player = userdata;
    if (SDL_QueueAudio(player->output_device, stream, len)) {
        LOGE("Cannot queue audio: %s", SDL_GetError());
    }
}

static SDL_AudioDeviceID open_accessory_audio_input(struct audio_player *player) {
    int count = SDL_GetNumAudioDevices(1);
    if (!count) {
        LOGE("No input audio source found");
        return 0;
    }

    // the audio input source has just been added, it should be the last one
    // TODO compare the audio device name with the device model instead
    const char *selected_name = SDL_GetAudioDeviceName(count - 1, 1);
    LOGI("Selecting input audio source: %s", selected_name);

    SDL_AudioSpec spec;
    init_audio_spec(&spec);
    spec.callback = audio_input_callback;
    spec.userdata = player;

    return SDL_OpenAudioDevice(selected_name, 1, &spec, NULL, 0);
}

static SDL_AudioDeviceID open_default_audio_output() {
    SDL_AudioSpec spec;
    init_audio_spec(&spec);
    return SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
}

SDL_bool audio_player_open(struct audio_player *player) {
    player->output_device = open_default_audio_output();
    if (!player->output_device) {
        LOGE("Cannot open audio output: %s", SDL_GetError());
        return SDL_FALSE;
    }

    player->input_device = open_accessory_audio_input(player);
    if (!player->input_device) {
        LOGE("Cannot open audio input: %s", SDL_GetError());
        SDL_CloseAudioDevice(player->output_device);
        return SDL_FALSE;
    }

    return SDL_TRUE;
}

static void audio_player_set_paused(struct audio_player *player, SDL_bool paused) {
    SDL_PauseAudioDevice(player->input_device, paused);
    SDL_PauseAudioDevice(player->output_device, paused);
}

void audio_player_play(struct audio_player *player) {
    audio_player_set_paused(player, SDL_FALSE);
}

void audio_player_pause(struct audio_player *player) {
    audio_player_set_paused(player, SDL_TRUE);
}

void audio_player_close(struct audio_player *player) {
    SDL_CloseAudioDevice(player->input_device);
    SDL_CloseAudioDevice(player->output_device);
}

SDL_bool audio_forwarding_start(struct audio_player *player, const char *serial) {
    if (!aoa_init()) {
        LOGE("Cannot initialize AOA");
        return SDL_FALSE;
    }

    char serialno[128];
    if (!serial) {
        LOGD("No serial provided, request it to the device");
        int r = adb_read_serialno(NULL, serialno, sizeof(serialno));
        if (r <= 0) {
            LOGE("Cannot read serial from the device");
            goto error_aoa_exit;
        }
        LOGD("Device serial is %s", serialno);
        serial = serialno;
    }

    if (!audio_player_init(player, serial)) {
        LOGE("Cannot initialize audio player");
        goto error_aoa_exit;
    }

    // adb connection will be reset!
    if (!aoa_forward_audio(player->serial, SDL_TRUE)) {
        LOGE("AOA audio forwarding failed");
        goto error_destroy_player;
    }

    LOGI("Audio forwarding enabled");

    if (!sdl_audio_init()) {
        goto error_disable_audio_forwarding;
    }

    LOGI("Waiting 2s for USB reconfiguration...");
    SDL_Delay(2000);

    if (!audio_player_open(player)) {
        goto error_disable_audio_forwarding;
    }

    audio_player_play(player);
    return SDL_TRUE;

error_disable_audio_forwarding:
    if (!aoa_forward_audio(serial, SDL_FALSE)) {
        LOGW("Cannot disable audio forwarding");
    }
error_destroy_player:
    audio_player_destroy(player);
error_aoa_exit:
    aoa_exit();

    return SDL_FALSE;
}

void audio_forwarding_stop(struct audio_player *player) {
    audio_player_close(player);

    if (aoa_forward_audio(player->serial, SDL_FALSE)) {
        LOGI("Audio forwarding disabled");
    } else {
        LOGW("Cannot disable audio forwarding");
    }
    aoa_exit();

    audio_player_destroy(player);
}

#include "scrcpy.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include <sys/time.h>
#include <SDL2/SDL.h>

#include "aoa.h"
#include "audio.h"
#include "command.h"
#include "common.h"
#include "controller.h"
#include "decoder.h"
#include "device.h"
#include "events.h"
#include "frames.h"
#include "fpscounter.h"
#include "inputmanager.h"
#include "log.h"
#include "lockutil.h"
#include "net.h"
#include "screen.h"
#include "server.h"
#include "tinyxpm.h"

static struct server server = SERVER_INITIALIZER;
static struct screen screen = SCREEN_INITIALIZER;
static struct frames frames;
static struct decoder decoder;
static struct controller controller;

#ifdef AUDIO_SUPPORT
static struct audio_player audio_player;
#endif

static struct input_manager input_manager = {
    .controller = &controller,
    .frames = &frames,
    .screen = &screen,
};

#if defined(__APPLE__) || defined(__WINDOWS__)
# define CONTINUOUS_RESIZING_WORKAROUND
#endif

#ifdef CONTINUOUS_RESIZING_WORKAROUND
// On Windows and MacOS, resizing blocks the event loop, so resizing events are
// not triggered. As a workaround, handle them in an event handler.
//
// <https://bugzilla.libsdl.org/show_bug.cgi?id=2077>
// <https://stackoverflow.com/a/40693139/1987178>
static int event_watcher(void *data, SDL_Event *event) {
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_RESIZED) {
        // called from another thread, not very safe, but it's a workaround!
        screen_render(&screen);
    }
    return 0;
}
#endif

static void event_loop(void) {
#ifdef CONTINUOUS_RESIZING_WORKAROUND
    SDL_AddEventWatch(event_watcher, NULL);
#endif
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        switch (event.type) {
            case EVENT_DECODER_STOPPED:
                LOGD("Video decoder stopped");
                return;
            case SDL_QUIT:
                LOGD("User requested to quit");
                return;
            case EVENT_NEW_FRAME:
                if (!screen.has_frame) {
                    screen.has_frame = SDL_TRUE;
                    // this is the very first frame, show the window
                    screen_show_window(&screen);
                }
                if (!screen_update_frame(&screen, &frames)) {
                    return;
                }
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_EXPOSED:
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                        screen_render(&screen);
                        break;
                }
                break;
            case SDL_TEXTINPUT:
                input_manager_process_text_input(&input_manager, &event.text);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                input_manager_process_key(&input_manager, &event.key);
                break;
            case SDL_MOUSEMOTION:
                input_manager_process_mouse_motion(&input_manager, &event.motion);
                break;
            case SDL_MOUSEWHEEL:
                input_manager_process_mouse_wheel(&input_manager, &event.wheel);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                input_manager_process_mouse_button(&input_manager, &event.button);
                break;
        }
    }
}

SDL_bool scrcpy(const struct scrcpy_options *options) {
    if (!SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1")) {
        LOGW("Cannot request to keep default signal handlers");
    }

#ifdef AUDIO_SUPPORT
    if (options->forward_audio) {
        if (!audio_forwarding_start(&audio_player, options->serial)) {
            return SDL_FALSE;
        }
    }
#endif

    SDL_bool ret = SDL_TRUE;

    if (!server_start(&server, options->serial, options->port,
                      options->max_size, options->bit_rate)) {
        ret = SDL_FALSE;
        goto finally_disable_audio_forwarding;
    }

    if (!sdl_video_init()) {
        ret = SDL_FALSE;
        goto finally_destroy_server;
    }

    socket_t device_socket = server_connect_to(&server);
    if (device_socket == INVALID_SOCKET) {
        server_stop(&server);
        ret = SDL_FALSE;
        goto finally_destroy_server;
    }

    char device_name[DEVICE_NAME_FIELD_LENGTH];
    struct size frame_size;

    // screenrecord does not send frames when the screen content does not change
    // therefore, we transmit the screen size before the video stream, to be able
    // to init the window immediately
    if (!device_read_info(device_socket, device_name, &frame_size)) {
        server_stop(&server);
        ret = SDL_FALSE;
        goto finally_destroy_server;
    }

    if (!frames_init(&frames)) {
        server_stop(&server);
        ret = SDL_FALSE;
        goto finally_destroy_server;
    }

    decoder_init(&decoder, &frames, device_socket);

    // now we consumed the header values, the socket receives the video stream
    // start the decoder
    if (!decoder_start(&decoder)) {
        ret = SDL_FALSE;
        server_stop(&server);
        goto finally_destroy_frames;
    }

    if (!controller_init(&controller, device_socket)) {
        ret = SDL_FALSE;
        goto finally_stop_decoder;
    }

    if (!controller_start(&controller)) {
        ret = SDL_FALSE;
        goto finally_destroy_controller;
    }

    if (!screen_init_rendering(&screen, device_name, frame_size)) {
        ret = SDL_FALSE;
        goto finally_stop_and_join_controller;
    }

    event_loop();

    LOGD("quit...");
    screen_destroy(&screen);
finally_stop_and_join_controller:
    controller_stop(&controller);
    controller_join(&controller);
finally_destroy_controller:
    controller_destroy(&controller);
finally_stop_decoder:
    decoder_stop(&decoder);
    // stop the server before decoder_join() to wake up the decoder
    server_stop(&server);
    decoder_join(&decoder);
finally_destroy_frames:
    frames_destroy(&frames);
finally_destroy_server:
    server_destroy(&server);
finally_disable_audio_forwarding:
#ifdef AUDIO_SUPPORT
    if (options->forward_audio) {
        audio_forwarding_stop(&audio_player);
    }
#endif

    return ret;
}

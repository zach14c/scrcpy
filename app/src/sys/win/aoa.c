#include "aoa.h"

#include "log.h"

SDL_bool aoa_init(void) {
    LOGW("Audio forwarding is not implemented for Windows");
    return SDL_FALSE;
}

void aoa_exit(void) {
    // do nothing
}

SDL_bool aoa_forward_audio(const char *serial, SDL_bool forward) {
    (void) serial;
    (void) forward;
    return SDL_FALSE;
}

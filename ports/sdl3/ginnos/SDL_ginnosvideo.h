/**
 * @file SDL_ginnosvideo.h
 * @brief SDL's video driver for ginnOS: windows on the desktop (heimdall).
 *
 * this file is not SDL's: ports/sdl3/build.sh copies it into SDL's tree
 * as src/video/ginnos/.
 */

#ifndef SDL_GINNOSVIDEO_H
#define SDL_GINNOSVIDEO_H

#include "../SDL_sysvideo.h"

#include <heimdall.h>

struct SDL_VideoData
{
    heim_conn_t *conn;
    bool server_gone; // heim_next_event said the server is gone
};

struct SDL_WindowData
{
    heim_window_t *heim;
    SDL_Window *window;
};

#endif

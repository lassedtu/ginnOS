/**
 * @file sdldemo.c
 * @brief a small SDL 3 program to try the port: a window with a ball
 *        that moves, lines drawn with the mouse, the last key in the
 *        title, and a tone on the space bar.
 *
 * keys: space plays a tone, c clears the lines, Escape quits.
 */

#include <SDL3/SDL.h>

#include <stdio.h>

#define WIDTH       480
#define HEIGHT      320
#define BALL        24
#define POINTS_MAX  4096
#define TONE_RATE   22050
#define TONE_HZ     440.0f
#define TONE_LENGTH (TONE_RATE / 4) // samples: a quarter of a second

typedef struct
{
    float x;
    float y;
    bool start; // a new line starts at this point
} point_t;

static point_t points[POINTS_MAX];
static int point_count;

static void add_point(float x, float y, bool start)
{
    if (point_count < POINTS_MAX)
    {
        points[point_count++] = (point_t){x, y, start};
    }
}

static void play_tone(SDL_AudioStream *stream)
{
    static int16_t samples[TONE_LENGTH];
    for (int i = 0; i < TONE_LENGTH; i++)
    {
        // fade out in the last quarter, so that the end does not click
        float fade = i < TONE_LENGTH * 3 / 4 ? 1.0f : (float)(TONE_LENGTH - i) / (TONE_LENGTH / 4);
        float wave = SDL_sinf(2.0f * SDL_PI_F * TONE_HZ * (float)i / TONE_RATE);
        samples[i] = (int16_t)(wave * fade * 8000.0f);
    }
    SDL_PutAudioStreamData(stream, samples, sizeof(samples));
}

static void draw(SDL_Renderer *renderer, float ball_x, float ball_y)
{
    int w;
    int h;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    for (int y = 0; y < h; y += 4)
    {
        Uint8 shade = (Uint8)(40 + 60 * y / (h > 0 ? h : 1));
        SDL_SetRenderDrawColor(renderer, 20, shade / 2, shade, 255);
        SDL_FRect band = {0, (float)y, (float)w, 4};
        SDL_RenderFillRect(renderer, &band);
    }

    SDL_SetRenderDrawColor(renderer, 250, 200, 60, 255);
    for (int i = 1; i < point_count; i++)
    {
        if (!points[i].start)
        {
            SDL_RenderLine(renderer, points[i - 1].x, points[i - 1].y, points[i].x, points[i].y);
        }
    }

    SDL_SetRenderDrawColor(renderer, 240, 80, 90, 255);
    SDL_FRect ball = {ball_x, ball_y, BALL, BALL};
    SDL_RenderFillRect(renderer, &ball);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(renderer, 8, 8, "space: tone   c: clear   esc: quit");
    SDL_RenderPresent(renderer);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "sdldemo: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window;
    SDL_Renderer *renderer;
    if (!SDL_CreateWindowAndRenderer("SDL demo", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE, &window,
                                     &renderer))
    {
        fprintf(stderr, "sdldemo: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    // the program goes on without sound when there is no audio device
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, TONE_RATE};
    SDL_AudioStream *stream = NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    }
    if (stream)
    {
        SDL_ResumeAudioStreamDevice(stream);
    }
    else
    {
        fprintf(stderr, "sdldemo: no sound: %s\n", SDL_GetError());
    }

    printf("sdldemo: video %s, audio %s, renderer %s\n", SDL_GetCurrentVideoDriver(),
           stream ? SDL_GetCurrentAudioDriver() : "none", SDL_GetRendererName(renderer));

    float ball_x = 40;
    float ball_y = 60;
    float speed_x = 120; // pixels per second
    float speed_y = 90;
    bool drawing = false;
    Uint64 last = SDL_GetTicksNS();
    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            {
                char title[64];
                snprintf(title, sizeof(title), "SDL demo: %s", SDL_GetKeyName(event.key.key));
                SDL_SetWindowTitle(window, title);
                if (event.key.key == SDLK_ESCAPE)
                {
                    running = false;
                }
                else if (event.key.key == SDLK_SPACE && stream)
                {
                    play_tone(stream);
                }
                else if (event.key.key == SDLK_C)
                {
                    point_count = 0;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                drawing = true;
                add_point(event.button.x, event.button.y, true);
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                drawing = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (drawing)
                {
                    add_point(event.motion.x, event.motion.y, false);
                }
                break;
            default:
                break;
            }
        }

        Uint64 now = SDL_GetTicksNS();
        float dt = (float)(now - last) / 1e9f;
        last = now;
        int w;
        int h;
        SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
        ball_x += speed_x * dt;
        ball_y += speed_y * dt;
        if (ball_x < 0 || ball_x + BALL > w)
        {
            speed_x = -speed_x;
            ball_x = SDL_clamp(ball_x, 0, (float)(w - BALL));
        }
        if (ball_y < 0 || ball_y + BALL > h)
        {
            speed_y = -speed_y;
            ball_y = SDL_clamp(ball_y, 0, (float)(h - BALL));
        }
        draw(renderer, ball_x, ball_y);
        SDL_Delay(16);
    }

    if (stream)
    {
        SDL_DestroyAudioStream(stream);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/* pop-sdl: show the machine in a window at real-time speed.
 *
 *   pop-sdl <image>
 *
 * Esc or closing the window quits.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "machine.h"
#include "read_file.h"

#define SCALE 2
#define FRAME_MS 17 /* 16.69 ms rounded; close enough for looking at it */

static void to_pixels(const uint8_t *picture, uint32_t *pixels)
{
    int i;

    for (i = 0; i < MACHINE_VIDEO_WIDTH * MACHINE_VIDEO_HEIGHT; i++) {
        int lit = (picture[i / 8] >> (7 - (i % 8))) & 1;
        pixels[i] = lit ? 0xFFFFFFFFu : 0xFF000000u;
    }
}

int main(int argc, char **argv)
{
    static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];
    static uint32_t pixels[MACHINE_VIDEO_WIDTH * MACHINE_VIDEO_HEIGHT];
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint8_t *image;
    size_t image_len = 0;
    int running = 1;
    int reported_halt = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: pop-sdl <image>\n");
        return 2;
    }
    image = read_file(argv[1], &image_len);
    if (image == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("Prince of Persia (Apple II)", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, MACHINE_VIDEO_WIDTH * SCALE,
                              MACHINE_VIDEO_HEIGHT * SCALE, 0);
    renderer = SDL_CreateRenderer(window, -1, 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
    if (window == NULL || renderer == NULL || texture == NULL) {
        fprintf(stderr, "SDL setup: %s\n", SDL_GetError());
        return 1;
    }

    machine_init(image, image_len);

    while (running) {
        uint32_t start = SDL_GetTicks();
        uint32_t elapsed;
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = 0;
            }
        }

        if (machine_run_frame() == MACHINE_HALTED) {
            /* Keep the last picture on screen; report once. */
            if (!reported_halt) {
                fprintf(stderr, "halted: %s\n", machine_halt_reason());
                reported_halt = 1;
            }
        } else {
            machine_render(picture);
            to_pixels(picture, pixels);
            SDL_UpdateTexture(texture, NULL, pixels, MACHINE_VIDEO_WIDTH * (int)sizeof(uint32_t));
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        elapsed = SDL_GetTicks() - start;
        if (elapsed < FRAME_MS) {
            SDL_Delay(FRAME_MS - elapsed);
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(image);
    return 0;
}

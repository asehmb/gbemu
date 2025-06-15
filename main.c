#include "cpu.h"
#include "graphics.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "timer.h"

int load_rom(struct CPU *cpu, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open ROM file");
        return -1;
    }

    // Read the ROM into memory
    fread(cpu->bus.memory, 1, 32768, file); // Load first 32KB
    fclose(file);
    cpu->pc = 0x0100;  // Set PC to start of ROM
    return 0;
}



int main() {

    struct CPU cpu;
    uint8_t memory[65536]; // 64KB memory
    struct MemoryBus bus = {
        .memory = memory, // Allocate 64KB memory
        .size = 65536
    };
    // Initialize GPU and CPU
    cpu_init(&cpu, &bus);

    struct GPU gpu = {
        .mode = 2,    // OAM Search mode initially
        .mode_clock = 0,
        .vram = bus.memory, // code uses 8000-9FFF for VRAM
        .framebuffer = {0}, // Initialize framebuffer to 0
    };
    // Initialize Timer
    struct Timer timer = {
        .main_clock = CLOCK_SPEED/4096, // Main clock
        .sub_clock = 0,  // Sub clock
        .divider_cycles = 0 // Cycles for divider increment
    };

    // Load a ROM or set up initial state
    // ...
    if (load_rom(&cpu, "testing/dmg-acid2.gb") != 0) {
        return -1; // Exit if ROM loading fails
    }
    printf("ROM loaded successfully.\n");


    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 160*4, 144*4, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);


    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        160, 144
    );

    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    SDL_Event event;

    uint32_t *sdl_pixels = (uint32_t *)malloc(160 * 144 * sizeof(uint32_t));
    const uint32_t palette[4] = {0xFFFFFFFF,  // White
    0xFFAAAAAA,  // Light gray
    0xFF555555,  // Dark gray
    0xFF000000   // Black
    }; // White to Black

    // Main emulation loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }
        step_cpu(&cpu); // Step the CPU

        // Render graphics
        step_gpu(&gpu, cpu.cycles); // Step the GPU with 4 cycles (example)
        step_timer(&timer, &cpu); // Step the timer

        if (gpu.should_render) {

            // Convert framebuffer to SDL pixel format
            for (int y = 0; y < 144; y++) {
                for (int x = 0; x < 160; x++) {
                    sdl_pixels[y * 160 + x] = palette[gpu.framebuffer[y * 160 + x]];
                }
            }
            SDL_UpdateTexture(texture, NULL, sdl_pixels, 160 * sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            gpu.should_render = false; // Reset render flag
        }

        // Update texture and render
        // Update display
        // ...
    }

    free(sdl_pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Emulation finished.\n");

    return 0;
}
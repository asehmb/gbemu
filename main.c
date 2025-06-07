#include "cpu.h"
#include "graphics.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>

int load_rom(struct CPU *cpu, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open ROM file");
        return -1;
    }

    // Read the ROM into memory
    fread(cpu->bus.memory + 0x0000, 1, 32768, file); // Load first 32KB
    fclose(file);
    return 0;
}



int main() {
    struct CPU cpu;
    struct MemoryBus bus = {
        .memory = (uint8_t[65536]){0}, // Allocate 64KB memory
        .size = 65536
    };
    // Initialize GPU and CPU
    cpu_init(&cpu, &bus);

    struct GPU gpu = {
        .mode = 2,    // OAM Search mode initially
        .mode_clock = 0,
        .vram = bus.memory, // code uses 8000-9FFF for VRAM
    };

    // Load a ROM or set up initial state
    // ...
    if (load_rom(&cpu, "blue.gb") != 0) {
        return -1; // Exit if ROM loading fails
    }
    printf("ROM loaded successfully.\n");


    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "My Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640, 480,   // Example resolution
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    SDL_Event event;

    // Main emulation loop
    while (true) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // black background
        SDL_RenderClear(renderer);
        // Execute CPU instructions
        cpu_handle_interrupts(&cpu); // Handle interrupts
        step_cpu(&cpu); // Step the CPU

        // Render graphics
        step_gpu(&gpu, cpu.cycles); // Step the GPU with 4 cycles (example)
        // printf("Opcode executed: 0x%02X, PC: 0x%04X, SP: 0x%04X\n", cpu.bus.memory[cpu.pc - 1], cpu.pc, cpu.sp);


        if (cpu.pc == 0x0150) {
            printf("Hit 0x0150: CPU state: ...\n");
            getchar(); // Wait for user input to continue
        }
        SDL_RenderPresent(renderer);

        // Update display
        // ...
    }

    return 0;
}
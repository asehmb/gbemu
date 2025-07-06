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
        .tima_cycles = CLOCK_SPEED/4096, // Main clock
        .div_cycles = 0 // Cycles for divider increment
    };

    // Load a ROM or set up initial state
    // ...
    if (load_rom(&cpu, "testing/dmg-acid2.gb") != 0) {
        return -1; // Exit if ROM loading fails
    }
    printf("ROM loaded successfully.\n");

    FILE *log_file = fopen("testing/test.log", "w");


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
    uint32_t pallete[8] = {
        0xFFFFFFFF, // White
        0xFF555555, // Dark Gray
        0xFFAAAAAA, // Light Gray
        0xFF000000, // Black
        0xFFFFFFFF, // White
        0xFFAAAAAA, // Light Gray
        0xFF555555, // Dark Gray
        0xFF000000, // Black
    };
    // Main emulation loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            uint8_t directional_keys = 0x0F; // bits 0-3, 1=not pressed
            uint8_t action_keys = 0x0F;      // bits 0-3, 1=not pressed

            if (event.type == SDL_KEYDOWN) {
                uint8_t old_joypad = cpu.bus.memory[INPUT_JOYPAD];

                switch (event.key.keysym.sym) {
                    case SDLK_UP:     directional_keys &= ~(1 << 2); break; // Up
                    case SDLK_DOWN:   directional_keys &= ~(1 << 3); break; // Down
                    case SDLK_LEFT:   directional_keys &= ~(1 << 1); break; // Left
                    case SDLK_RIGHT:  directional_keys &= ~(1 << 0); break; // Right
                    case SDLK_a:      action_keys &= ~(1 << 0); break;      // A
                    case SDLK_b:      action_keys &= ~(1 << 1); break;      // B
                    case SDLK_RETURN: action_keys &= ~(1 << 3); break;      // Start
                    case SDLK_SPACE:  action_keys &= ~(1 << 2); break;      // Select
                }

                // Update joypad register based on P14/P15 selection
                uint8_t joypad = cpu.bus.memory[INPUT_JOYPAD] & 0xF0; // Keep upper bits

                if (!(joypad & (1 << 4))) { // P14 selected (directional)
                    joypad |= directional_keys;
                }
                if (!(joypad & (1 << 5))) { // P15 selected (action)
                    joypad |= action_keys;
                }

                cpu.bus.memory[INPUT_JOYPAD] = joypad;
                // Set interrupt flag if state changed
                if (old_joypad != joypad) {
                    cpu.bus.memory[0xFF0F] |= 0x10;
                }
            }
            else if (event.type == SDL_KEYUP) {
                // Similar logic for key release
                uint8_t old_joypad = cpu.bus.memory[INPUT_JOYPAD];

                switch (event.key.keysym.sym) {
                    case SDLK_UP:     directional_keys |= (1 << 2); break;
                    case SDLK_DOWN:   directional_keys |= (1 << 3); break;
                    case SDLK_LEFT:   directional_keys |= (1 << 1); break;
                    case SDLK_RIGHT:  directional_keys |= (1 << 0); break;
                    case SDLK_a:      action_keys |= (1 << 0); break;
                    case SDLK_b:      action_keys |= (1 << 1); break;
                    case SDLK_RETURN: action_keys |= (1 << 3); break;
                    case SDLK_SPACE:  action_keys |= (1 << 2); break;
                }

                uint8_t joypad = cpu.bus.memory[INPUT_JOYPAD] & 0xF0;

                if (!(joypad & (1 << 4))) {
                    joypad |= directional_keys;
                }
                if (!(joypad & (1 << 5))) {
                    joypad |= action_keys;
                }

                cpu.bus.memory[INPUT_JOYPAD] = joypad;

                if (old_joypad != joypad) {
                    cpu.bus.memory[0xFF0F] |= 0x10;
                }
            }

        }

        fprintf(log_file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
                cpu.regs.a, PACK_FLAGS(&cpu), cpu.regs.b, cpu.regs.c, cpu.regs.d,
                cpu.regs.e, GET_H(&cpu), GET_L(&cpu), cpu.sp, cpu.pc,
                cpu.bus.memory[cpu.pc], cpu.bus.memory[cpu.pc + 1],
                cpu.bus.memory[cpu.pc + 2], cpu.bus.memory[cpu.pc + 3]);
        fflush(log_file);
        step_cpu(&cpu); // Step the CPU
        step_timer(&timer, &cpu);  // Step the timer

        // Render graphics
        step_gpu(&gpu, cpu.cycles); // Step the GPU with 4 cycles (example)


        if (gpu.should_render) {

            // Convert framebuffer to SDL pixel format
            for (int y = 0; y < 144; y++) {
                for (int x = 0; x < 160; x++) {
                    sdl_pixels[y * 160 + x] = pallete[gpu.framebuffer[y * 160 + x]];
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
#include "../src/cpu.h"
#include "../src/graphics.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "../src/timer.h"
#include "../src/rom.h"
#include <stdlib.h>
#include <string.h>


int main(int argc, char *argv[]) {
    const char *rom_path; // Default ROM
    const char *bootrom_path; // Default boot ROM

    // Check if a ROM file was specified on the command line
    if (argc == 2) {
        rom_path = argv[1];
    } else if (argc == 3) {
        rom_path = argv[1];
        bootrom_path = argv[2];
    } else {
        fprintf(stderr, "No ROM file specified.\n");
        return 1;
    }

    struct CPU cpu = {0};
    struct MemoryBus bus; // leave bus uninitialized for now
    cpu_init(&cpu, &bus);

    // Load the selected ROM
    LOG("Loading ROM: %s\n", rom_path);
    if (load_rom(&cpu, rom_path) != 0) {
        fprintf(stderr, "Failed to load ROM\n");
        return -1;
    }

    if (load_bootrom(&cpu, bootrom_path) != 0) {
        fprintf(stderr, "Failed to load boot ROM\n");
    }
    
    LOG("Boot ROM status: %s\n", cpu.bootrom_enabled ? "ENABLED" : "DISABLED");
    
    LOG("ROM type: 0x%02X\n", cpu.bus.rom[0x0147]);

    LOG("CPU and Memory Bus initialized.\n");

    patch_checksum(cpu.bus.rom); // Patch the checksum after loading the ROM

    // Initialize GPU
    struct GPU gpu = {
        .mode = 0,
        .mode_clock = 0,
        .vram = cpu.bus.rom,
        .framebuffer = {0},
        .should_render = false,
        .off_count = 0,
        .delay_cycles = 0,
        .stopped = false,
    };

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
    uint32_t pallete[4] = {
        0xFFFFFFFF, // White
        0xFFAAAAAA, // Light Gray
        0xFF555555, // Dark Gray
        0xFF000000 // Black
    };

    // Frame counter variables
    uint32_t frame_count = 0;
    uint32_t fps_timer = SDL_GetTicks();
    uint32_t fps = 0;
    char window_title[256];
    const int TARGET_FPS = 59;
    const int FRAME_TIME = 1000 / TARGET_FPS; // in ms

    // Track button states
    static uint8_t button_directions = 0x0F;  // All direction buttons released (1=released, 0=pressed)
    static uint8_t button_actions = 0x0F;     // All action buttons released (1=released, 0=pressed)
    
    while (running) {
        uint32_t frame_start = SDL_GetTicks();
        
        // Process events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                bool pressed = (event.type == SDL_KEYDOWN);
                
                switch (event.key.keysym.sym) {
                    // Direction buttons
                    case SDLK_UP:
                        pressed ? (button_directions &= ~0x04) : (button_directions |= 0x04);
                        // LOG("Up button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_DOWN:
                        pressed ? (button_directions &= ~0x08) : (button_directions |= 0x08);
                        // LOG("Down button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_LEFT:
                        pressed ? (button_directions &= ~0x02) : (button_directions |= 0x02);
                        // LOG("Left button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_RIGHT:
                        pressed ? (button_directions &= ~0x01) : (button_directions |= 0x01);
                        // LOG("Right button %s\n", pressed ? "pressed" : "released");
                        break;
                    
                    // Action buttons
                    case SDLK_z:  // Use Z for A button
                        button_actions = pressed ? (button_actions & ~0x01) : (button_actions | 0x01);
                        // LOG("A button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_x:  // Use X for B button
                        button_actions = pressed ? (button_actions & ~0x02) : (button_actions | 0x02);
                        // LOG("B button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_SPACE:
                        button_actions = pressed ? (button_actions & ~0x04) : (button_actions | 0x04); // Select
                        // LOG("Select button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_RETURN:
                        button_actions = pressed ? (button_actions & ~0x08) : (button_actions | 0x08); // Start
                        // LOG("Start button %s\n", pressed ? "pressed" : "released");
                        break;
                }
                
                // Update joypad state
                // store in cpu struct
                cpu.p1_actions = button_actions;
                cpu.p1_directions = button_directions;

            }
        }

        // Run emulation for one frame (approximately 70224 cycles for Game Boy)
        int frame_cycles = 0;
        
        while (!gpu.should_render) {
        
            
            uint32_t prev_cycles = cpu.cycles;

            step_cpu(&cpu); // Step the CPU
            do {
                step_timer(&cpu);  // Step the timer
                step_gpu(&gpu, cpu.cycles); // Step the GPU
            } while (cpu.halted && ((cpu.bus.rom[0xFF0F] & cpu.bus.rom[0xFFFF]) == 0)); // Handle interrupts if CPU is halted

            frame_cycles += (cpu.cycles - prev_cycles);
        }

        // Only limit frame rate when we actually render a frame
        if (gpu.should_render) {
            // Frame timing
            uint32_t frame_time = SDL_GetTicks() - frame_start;
            if (frame_time < FRAME_TIME) {
                SDL_Delay(FRAME_TIME - frame_time);
            }
        }
        
        if (gpu.should_render) {
            frame_count++;

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

            // Update FPS counter every second
            uint32_t current_time = SDL_GetTicks();
            if (current_time - fps_timer >= 1000) {
                fps = frame_count;
                frame_count = 0;
                fps_timer = current_time;
                
                // Update window title with FPS
                snprintf(window_title, sizeof(window_title), "Game Boy Emulator - FPS: %u", fps);
                SDL_SetWindowTitle(window, window_title);
            }
        }


    }

    free(sdl_pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Emulation finished.\n");

    return 0;
}

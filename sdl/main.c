#include "cpu.h"
#include "graphics.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "timer.h"
#include "rom.h"
#include <stdlib.h>
#include <string.h>
#include "input.h"

#define LOGGING

#ifdef LOGGING
#define LOG(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...) ((void)0)
#endif

#define READ_BYTE_DEBUG(cpu, addr) \
    ((cpu).bootrom_enabled && (addr) < 0x0100) ? (cpu).bootrom[(addr)] : \
    ((cpu).bus.banking && (addr) >= 0x4000 && (addr) < 0x8000 ? \
    (cpu).bus.current_rom_bank == 1 ? (cpu).bus.rom[(addr)] : \
	(cpu).bus.rom_banks[((cpu).bus.current_rom_bank - 2) * 0x4000 + (addr-0x4000)] : \
    (cpu).bus.rom[(addr)]) // Read from boot ROM, then banked ROM, or ROM


int main(int argc, char *argv[]) {
    const char *rom_path = "testing/blue.gb"; // Default ROM

    // Check if a ROM file was specified on the command line
    if (argc > 1) {
        rom_path = argv[1];
    }

    struct CPU cpu = {0};
    struct MemoryBus bus; // leave bus uninitialized for now
    bus.rom_size = 0x8000;

    // cpu_init(&cpu, &bus);
    /*
    cpu.bus = bus; // Connect bus to CPU
    cpu.ime = false;
    cpu.ime_pending = false;
    cpu.halted = false;
    cpu.cycles = 0;
    cpu.divider_cycles = 0;
    cpu.tima_counter = 0;
    cpu.bus.rom[0xFF00] = 0xCF; // Initialize Joypad register
    */
    cpu_init(&cpu, &bus);

    // Load the selected ROM
    LOG("Loading ROM: %s\n", rom_path);
    if (load_rom(&cpu, rom_path) != 0) {
        return -1;
    }

    if (load_bootrom(&cpu, "testing/dmg_boot.bin") != 0) {
        fprintf(stderr, "Failed to load boot ROM\n");
        return -1;
    }
    
    // Debug bootrom status
    LOG("Boot ROM status: %s\n", cpu.bootrom_enabled ? "ENABLED" : "DISABLED");
    
    LOG("ROM type: 0x%02X\n", cpu.bus.rom[0x0147]);

    LOG("CPU and Memory Bus initialized.\n");

    patch_checksum(cpu.bus.rom); // Patch the checksum after loading the ROM

    // Initialize GPU
    struct GPU gpu = {
        .mode = 1,
        .mode_clock = 0,
        .vram = cpu.bus.rom,
        .framebuffer = {0},
        .should_render = false,
        .off_count = 0,
        .delay_cycles = 0,
        .stopped = false,
    };

    FILE *log_file = fopen("testing/test.log", "w");
    if (!log_file) {
        perror("Failed to open log file");
        return -1;
    }

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
    FILE *memory_dump = fopen("testing/memory_dump.txt", "w");
    if (!memory_dump) {
        fprintf(stderr, "Failed to open memory dump file\n");
        free(sdl_pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        fclose(log_file);
        return -1;
    }
    uint8_t prev_joypad = 0x30;

    // Frame counter variables
    uint32_t frame_count = 0;
    uint32_t fps_timer = SDL_GetTicks();
    uint32_t fps = 0;
    char window_title[256];
    const int TARGET_FPS = 60;
    const int FRAME_TIME = 1000 / TARGET_FPS; // in ms

    // Main emulation loop
    // Track button states
    static uint8_t button_directions = 0x0F;  // All direction buttons released (1=released, 0=pressed)
    static uint8_t button_actions = 0x0F;     // All action buttons released (1=released, 0=pressed)
    cpu.bus.rom[0xFF44] = 144;
    
    // Frame timing variables
    
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
                        LOG("Up button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_DOWN:
                        pressed ? (button_directions &= ~0x08) : (button_directions |= 0x08);
                        LOG("Down button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_LEFT:
                        pressed ? (button_directions &= ~0x02) : (button_directions |= 0x02);
                        LOG("Left button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_RIGHT:
                        pressed ? (button_directions &= ~0x01) : (button_directions |= 0x01);
                        LOG("Right button %s\n", pressed ? "pressed" : "released");
                        break;
                    
                    // Action buttons
                    case SDLK_z:  // Use Z for A button
                        button_actions = pressed ? (button_actions & ~0x01) : (button_actions | 0x01);
                        LOG("A button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_x:  // Use X for B button
                        button_actions = pressed ? (button_actions & ~0x02) : (button_actions | 0x02);
                        LOG("B button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_SPACE:
                        button_actions = pressed ? (button_actions & ~0x04) : (button_actions | 0x04); // Select
                        LOG("Select button %s\n", pressed ? "pressed" : "released");
                        break;
                    case SDLK_RETURN:
                        button_actions = pressed ? (button_actions & ~0x08) : (button_actions | 0x08); // Start
                        LOG("Start button %s\n", pressed ? "pressed" : "released");
                        break;
                }
                
                // Update joypad state
                // store in cpu struct
                cpu.p1_actions = button_actions;
                cpu.p1_directions = button_directions;

            }
        }

        // Run emulation for one frame (approximately 70224 cycles for Game Boy)
        const int CYCLES_PER_FRAME = 70224;
        int frame_cycles = 0;
        
        while (frame_cycles < CYCLES_PER_FRAME && !gpu.should_render) {
            // fprintf(log_file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X"\
            //     "L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X,%02X,%02X" \
            //     " IE:%02X CURRENT ROM BANK:%d PPU MODE:%d CYCLES TAKEN:%d"\
            //     " LY:%02X P1:%02X\n",
            //         cpu.regs.a, PACK_FLAGS(&cpu), cpu.regs.b, cpu.regs.c, cpu.regs.d,
            //         cpu.regs.e, GET_H(&cpu), GET_L(&cpu), cpu.sp, cpu.pc,
            //         READ_BYTE_DEBUG(cpu, cpu.pc), READ_BYTE_DEBUG(cpu, cpu.pc + 1),
            //         READ_BYTE_DEBUG(cpu, cpu.pc + 2), READ_BYTE_DEBUG(cpu, cpu.pc + 3),
            //         READ_BYTE_DEBUG(cpu, cpu.pc + 4), READ_BYTE_DEBUG(cpu, cpu.pc + 5)
            //         ,cpu.bus.rom[0xFFFF], cpu.bus.current_rom_bank, 
            //         cpu.bus.rom[0xFF41] & 0x03, cpu.cycles, cpu.bus.rom[0xFF44],
            //         cpu.bus.rom[INPUT_JOYPAD]
            //     );
            // fflush(log_file);
            
            uint32_t prev_cycles = cpu.cycles;
            step_cpu(&cpu); // Step the CPU
            step_timer(&cpu);  // Step the timer
            step_gpu(&gpu, cpu.cycles); // Step the GPU
            
            frame_cycles += (cpu.cycles - prev_cycles);
        }

        if (prev_joypad != cpu.bus.rom[INPUT_JOYPAD]) {
            // Check if this is a meaningful joypad state change
            uint8_t current_joypad = cpu.bus.rom[INPUT_JOYPAD];
            if (current_joypad != 0xFF) {
                LOG("Joypad state changed: %02X\n", current_joypad);
                prev_joypad = current_joypad;
            }
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

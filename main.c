#include "cpu.h"
#include "graphics.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "timer.h"
#include "rom.h"
#include <stdlib.h>
#include <string.h>

#define LOGGING

#ifdef LOGGING
#define LOG(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...) ((void)0)
#endif


int load_rom(struct CPU *cpu, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open ROM file");
        return -1;
    }

    // Read header into temp buffer
    uint8_t header[0x150];
    size_t bytes_read = fread(header, 1, sizeof(header), file);
    if (bytes_read < sizeof(header)) {
        fprintf(stderr, "Failed to read ROM header\n");
        fclose(file);
        return -1;
    }

    // Copy header into the ROM array
    memcpy(cpu->bus.rom, header, sizeof(header));

    uint16_t num_banks = rom_size(header);
    if (num_banks == 0) {
        fprintf(stderr, "Invalid ROM size\n");
        fclose(file);
        return -1;
    }
    // read the first 32KB of the ROM MBC_NONE
    if (fread(cpu->bus.rom + 0x150, 1, 0x8000 - 0x150, file) != 0x8000 - 0x150) {
        fprintf(stderr, "Failed to read ROM data\n");
        fclose(file);
        return -1;
    }
    // return if rom is only 32KB
    if (num_banks == 2) {
        LOG("ROM size: 32KB\n");
        cpu->bus.banking = false;

        return 0;
    }

    // Load the rest of the rom into RAM (probably should be renamed)
    cpu->bus.rom_banks = malloc((num_banks - 2) * 0x4000); //when reading from ram account for 0x8000 missing (32KB)
    if (!cpu->bus.rom_banks) {
        fprintf(stderr, "Failed to allocate memory for RAM\n");
        fclose(file);
        return -1;
    }

    if (fread(cpu->bus.rom_banks, 1, (num_banks - 2) * 0x4000, file) != (num_banks - 2) * 0x4000) {
        fprintf(stderr, "Failed to read RAM data\n");
        free(cpu->bus.rom_banks);
        fclose(file);
        return -1;
    }
    cpu->bus.rom_size = num_banks * 0x4000;
    cpu->bus.banking = true; // Enable banking for MBCs that support it
    cpu->bus.current_rom_bank = 1;

    size_t ram_sizes[] = {
        0,       // 0x00: no RAM
        2 * 1024,  // 0x01: 2 KB
        8 * 1024,  // 0x02: 8 KB
        32 * 1024, // 0x03: 32 KB
        128 * 1024,// 0x04: 128 KB
        64 * 1024  // 0x05: 64 KB
    };

    uint8_t ram_size_code = header[0x149];
    size_t cart_ram_size = 0;
    if (ram_size_code < sizeof(ram_sizes)/sizeof(ram_sizes[0])) {
        cart_ram_size = ram_sizes[ram_size_code];
    } else {
        cart_ram_size = 0; // unknown or no RAM
    }

    cpu->bus.cart_ram = NULL;
    cpu->bus.ram_size = cart_ram_size;

    if (cart_ram_size > 0) {
        cpu->bus.cart_ram = malloc(cart_ram_size);
        if (!cpu->bus.cart_ram) {
            fprintf(stderr, "Failed to allocate cartridge RAM\n");
            // handle error or exit
        }
    }
    LOG("ROM banks: %d, RAM size: %zu bytes\n", num_banks - 2, cart_ram_size);
    cpu->bus.num_rom_banks = num_banks - 2; // Exclude the first two banks (header and first 32KB)

    cpu->bus.mbc_type = rom_init(&cpu->bus);
    printf("MBC Type: %d\n", cpu->bus.mbc_type);

    fclose(file);
    LOG("ROM loaded successfully. Size: %d banks (%d bytes)\n", num_banks, num_banks * 0x4000);
    return 0;
}




int main() {

    struct CPU cpu = {0};
    struct MemoryBus bus; // leave bus uninitialized for now
    bus.rom_size = 0x8000;

    // Connect bus to CPU
    cpu_init(&cpu, &bus);

    // Now load ROM
    if (load_rom(&cpu, "testing/blue.gb") != 0) {
        return -1;
    }

    LOG("CPU and Memory Bus initialized.\n");

    // Initialize GPU
    struct GPU gpu = {
        .mode = 2,
        .mode_clock = 0,
        .vram = cpu.bus.rom,
        .framebuffer = {0}
    };

    struct Timer timer = {
        .div_cycles = 0,
        .tima_cycles = 0,
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
    // Main emulation loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            uint8_t directional_keys = 0x0F; // bits 0-3, 1=not pressed
            uint8_t action_keys = 0x0F;      // bits 0-3, 1=not pressed

            if (event.type == SDL_KEYDOWN) {
                uint8_t old_joypad = cpu.bus.rom[INPUT_JOYPAD];

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
                uint8_t joypad = cpu.bus.rom[INPUT_JOYPAD] & 0xF0; // Keep upper bits

                if (!(joypad & (1 << 4))) { // P14 selected (directional)
                    joypad |= directional_keys;
                }
                if (!(joypad & (1 << 5))) { // P15 selected (action)
                    joypad |= action_keys;
                }

                cpu.bus.rom[INPUT_JOYPAD] = joypad;
                // Set interrupt flag if state changed
                if (old_joypad != joypad) {
                    cpu.bus.rom[0xFF0F] |= 0x10;
                }
            }
            else if (event.type == SDL_KEYUP) {
                // Similar logic for key release
                uint8_t old_joypad = cpu.bus.rom[INPUT_JOYPAD];

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

                uint8_t joypad = cpu.bus.rom[INPUT_JOYPAD] & 0xF0;

                if (!(joypad & (1 << 4))) {
                    joypad |= directional_keys;
                }
                if (!(joypad & (1 << 5))) {
                    joypad |= action_keys;
                }

                cpu.bus.rom[INPUT_JOYPAD] = joypad;

                if (old_joypad != joypad) {
                    cpu.bus.rom[0xFF0F] |= 0x10;
                }
            }

        }

        fprintf(log_file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
                cpu.regs.a, PACK_FLAGS(&cpu), cpu.regs.b, cpu.regs.c, cpu.regs.d,
                cpu.regs.e, GET_H(&cpu), GET_L(&cpu), cpu.sp, cpu.pc,
                cpu.bus.rom[cpu.pc], cpu.bus.rom[cpu.pc + 1],
                cpu.bus.rom[cpu.pc + 2], cpu.bus.rom[cpu.pc + 3]);
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
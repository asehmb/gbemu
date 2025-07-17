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

#define JOYPAD_A            0x01
#define JOYPAD_B            0x02
#define JOYPAD_SELECT       0x04
#define JOYPAD_START        0x08
#define JOYPAD_RIGHT        0x10
#define JOYPAD_LEFT         0x20
#define JOYPAD_UP           0x40
#define JOYPAD_DOWN         0x80

#define READ_BYTE_DEBUG(cpu, addr) \
    ((cpu).bootrom_enabled && (addr) < 0x0100) ? (cpu).bootrom[(addr)] : \
    ((cpu).bus.banking && (addr) >= 0x4000 && (addr) < 0x8000 ? \
    (cpu).bus.current_rom_bank == 1 ? (cpu).bus.rom[(addr)] : \
	(cpu).bus.rom_banks[((cpu).bus.current_rom_bank - 2) * 0x4000 + (addr-0x4000)] : \
    (cpu).bus.rom[(addr)]) // Read from boot ROM, then banked ROM, or ROM


int load_rom(struct CPU *cpu, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open ROM file");
        return -1;
    }

    if (fread(cpu->bus.rom, 0x8000,1, file) != 1) {
        fprintf(stderr, "Failed to read ROM data\n");
        fclose(file);
        return -1;
    }
    int num_banks = rom_size(cpu->bus.rom); // Number of 16KB ROM banks
    // return if rom is only 32KB
    if (num_banks == 2) {
        LOG("ROM size: 32KB\n");
        cpu->bus.banking = false;
        cpu->bus.current_rom_bank = 0; // just use the first bank
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

    uint8_t ram_size = cpu->bus.rom[0x149]; // RAM size code from header
    size_t cart_ram_size = 0;


    if (ram_size < sizeof(ram_sizes)/sizeof(ram_sizes[0])) {
        cart_ram_size = ram_sizes[ram_size];
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

int load_bootrom(struct CPU *cpu, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open bootrom file");
        return -1;
    }

    // Load 256 bytes into the CPU's bootrom buffer
    size_t read = fread(cpu->bootrom, 1, 256, file);
    fclose(file);

    if (read != 256) {
        fprintf(stderr, "Boot ROM size incorrect (read %zu bytes, expected 256)\n", read);
        return -1;
    }

    // Check first few bytes of the boot ROM (should be 0x31, 0xFE, 0xFF for DMG boot ROM)
    printf("Boot ROM first bytes: %02X %02X %02X %02X\n", 
           cpu->bootrom[0], cpu->bootrom[1], cpu->bootrom[2], cpu->bootrom[3]);

    cpu->bootrom_enabled = true;  // Enable boot ROM overlay
    cpu->pc = 0x0000;             // Start execution at boot ROM
    printf("Boot ROM loaded and enabled\n");
    return 0;
}



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
    printf("Loading ROM: %s\n", rom_path);
    if (load_rom(&cpu, rom_path) != 0) {
        return -1;
    }
    
    // Debug bootrom status
    LOG("Boot ROM status: %s\n", cpu.bootrom_enabled ? "ENABLED" : "DISABLED");
    
    uint8_t rom_type = cpu.bus.rom[0x0147];
    LOG("ROM type: 0x%02X\n", rom_type);

    LOG("CPU and Memory Bus initialized.\n");

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
    int i = 0;
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

    // Main emulation loop
    // Track button states
    static uint8_t button_directions = 0x0F;  // All direction buttons released (1=released, 0=pressed)
    static uint8_t button_actions = 0x0F;     // All action buttons released (1=released, 0=pressed)
    cpu.bus.rom[0xFF44] = 144;
    
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                bool pressed = (event.type == SDL_KEYDOWN);
                
                switch (event.key.keysym.sym) {
                    // Direction buttons
                    case SDLK_UP:
                        button_directions = pressed ? (button_directions & ~0x04) : (button_directions | 0x04);
                        break;
                    case SDLK_DOWN:
                        button_directions = pressed ? (button_directions & ~0x08) : (button_directions | 0x08);
                        break;
                    case SDLK_LEFT:
                        button_directions = pressed ? (button_directions & ~0x02) : (button_directions | 0x02);
                        break;
                    case SDLK_RIGHT:
                        button_directions = pressed ? (button_directions & ~0x01) : (button_directions | 0x01);
                        break;
                    
                    // Action buttons
                    case SDLK_z:  // Use Z for A button
                        button_actions = pressed ? (button_actions & ~0x01) : (button_actions | 0x01);
                        break;
                    case SDLK_x:  // Use X for B button
                        button_actions = pressed ? (button_actions & ~0x02) : (button_actions | 0x02);
                        break;
                    case SDLK_SPACE:
                        button_actions = pressed ? (button_actions & ~0x04) : (button_actions | 0x04); // Select
                        break;
                    case SDLK_RETURN:
                        button_actions = pressed ? (button_actions & ~0x08) : (button_actions | 0x08); // Start
                        break;
                }
                
                // Update P1 register based on P14/P15 selection lines
                uint8_t p1 = cpu.bus.rom[INPUT_JOYPAD] & 0xF0;  // Keep top 4 bits (selection)
                
                // Apply appropriate button states based on selection
                if (!(p1 & 0x10)) {  // P14 low (directions selected)
                    p1 |= button_directions;
                }
                if (!(p1 & 0x20)) {  // P15 low (actions selected)
                    p1 |= button_actions;
                }
                
                // If either selection line is high, return all buttons as released
                if ((p1 & 0x30) == 0x30) {
                    p1 |= 0x0F;
                }
                
                // Store result back
                uint8_t old_input = cpu.bus.rom[INPUT_JOYPAD];
                cpu.bus.rom[INPUT_JOYPAD] = p1;
                
                // Trigger interrupt if any button was pressed (bit changed from 1->0)
                if ((old_input & 0x0F) != (p1 & 0x0F) && 
                    ((old_input & 0x0F) > (p1 & 0x0F))) {
                    cpu.bus.rom[0xFF0F] |= 0x10;  // Request joypad interrupt
                }
            }
        }

        fprintf(log_file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X,%02X,%02X" \
            " IE:%02X CURRENT ROM BANK:%d PPU MODE:%d CYCLES TAKEN:%d",
                cpu.regs.a, PACK_FLAGS(&cpu), cpu.regs.b, cpu.regs.c, cpu.regs.d,
                cpu.regs.e, GET_H(&cpu), GET_L(&cpu), cpu.sp, cpu.pc,
                READ_BYTE_DEBUG(cpu, cpu.pc), READ_BYTE_DEBUG(cpu, cpu.pc + 1),
                READ_BYTE_DEBUG(cpu, cpu.pc + 2), READ_BYTE_DEBUG(cpu, cpu.pc + 3),
                READ_BYTE_DEBUG(cpu, cpu.pc + 4), READ_BYTE_DEBUG(cpu, cpu.pc + 5)
                ,cpu.bus.rom[0xFFFF], cpu.bus.current_rom_bank, cpu.bus.rom[0xFF41] & 0x03, cpu.cycles
            );
        fprintf(log_file, "\n");
        fflush(log_file);
        step_cpu(&cpu); // Step the CPU
        step_timer(&timer, &cpu);  // Step the timer

        // Render graphics
        step_gpu(&gpu, cpu.cycles); // Step the GPU with 4 cycles (example)
        if (prev_joypad != cpu.bus.rom[INPUT_JOYPAD]) {
            // Check if this is a meaningful joypad state change
            uint8_t current_joypad = cpu.bus.rom[INPUT_JOYPAD];
            if (current_joypad != 0xFF) {
                LOG("Joypad state changed: %02X\n", current_joypad);
                prev_joypad = current_joypad;
            }
        }
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
        // if (cpu.bus.rom[0xFF44])
        //     printf("LY: %d, LCDC:0x%02X\n", cpu.bus.rom[0xFF44], cpu.bus.rom[0xFF40]);
        if (i == 156932) {
            // Dump memory to file
            fprintf(memory_dump, "ROM:\n");
            for (int j = 0; j < 0xFFFF; j++) {
                fprintf(memory_dump, "%02X ", cpu.bus.rom[j]);
                if ((j + 1) % 16 == 0) {
                    fprintf(memory_dump, "\n");
                }
            }
            fprintf(memory_dump, "\nROM_BANKS:\n");
            for (int j = 0; j < cpu.bus.num_rom_banks * 0x4000; j++) {
                fprintf(memory_dump, "%02X ", cpu.bus.rom_banks[j]);
                if ((j + 1) % 16 == 0) {
                    fprintf(memory_dump, "\n");
                }
            }
            fprintf(memory_dump, "\nCartridge RAM:\n");
            if (cpu.bus.cart_ram) {
                for (int j = 0; j < cpu.bus.ram_size; j++) {
                    fprintf(memory_dump, "%02X ", cpu.bus.cart_ram[j]);
                    if ((j + 1) % 16 == 0) {
                        fprintf(memory_dump, "\n");
                    }
                }
            } else {
                fprintf(memory_dump, "No cartridge RAM\n");
            }

            fclose(memory_dump);
            printf("Memory dump completed.\n");
        }
        i++;
    }

    free(sdl_pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Emulation finished.\n");

    return 0;
}

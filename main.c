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
    FILE *log_file = fopen("testing/test.log", "w");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file\n");
        return 1;
    }
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
        .framebuffer = {0}, // Initialize framebuffer to 0
    };

    for (int i = 0xFF04; i <= 0xFF07; i++) {
        bus.memory[i] = 0; // Initialize timer registers to 0
    }

    // Initialize Timer
    struct Timer timer = {
        .main_clock = CLOCK_SPEED/4096, // Main clock
        .sub_clock = 0,  // Sub clock
        .divider_cycles = 0 // Cycles for divider increment
    };

    // Load a ROM or set up initial state
    // ...
    if (load_rom(&cpu, "testing/cpu_instrs/individual/03-op sp,hl.gb") != 0) {
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

    uint32_t palette[4] = {
        0xFFFFFFFF,  // White
        0xAAAAAAFF,  // Light gray
        0x555555FF,  // Dark gray
        0x000000FF   // Black
    };
    uint32_t *sdl_pixels = (uint32_t *)malloc(160 * 144 * sizeof(uint32_t));

    uint16_t cycles = 0; // Initialize cycles
    // Main emulation loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }


        // Execute CPU instructions
        // cpu_handle_interrupts(&cpu); // Handle interrupts
        fprintf(log_file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
                cpu.regs.a, PACK_FLAGS(&cpu), cpu.regs.b, cpu.regs.c, cpu.regs.d, cpu.regs.e, GET_H(&cpu), GET_L(&cpu), cpu.sp, cpu.pc,
                cpu.bus.memory[cpu.pc], cpu.bus.memory[cpu.pc + 1], cpu.bus.memory[cpu.pc + 2], cpu.bus.memory[cpu.pc + 3]);
        fflush(log_file);

        step_cpu(&cpu); // Step the CPU
        cycles += cpu.cycles; // Update total cycles

        // Render graphics
        step_gpu(&gpu, cpu.cycles); // Step the GPU with 4 cycles (example)
        step_timer(&timer, &cpu); // Step the timer



        // Update texture and render
        // Update display
        // ...
    }

    free(sdl_pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    fclose(log_file);
    printf("Emulation finished.\n");

    return 0;
}
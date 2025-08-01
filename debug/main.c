
#define LOGGING
#include "../src/cpu.h"
#include "../src/graphics.h"
#include "../src/rom.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "../src/timer.h"
#include "../src/rom.h"
#include <stdlib.h>
#include <string.h>
#include "../src/input.h"


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
        return -1;
    }

    // Set up save file path
    cpu.save_file_path = save_file_name(&cpu, rom_path);
    if (cpu.save_file_path) {
        LOG("Save file will be: %s\n", cpu.save_file_path);
    } else {
        LOG("No save file support for this cartridge type\n");
    }

    if (load_bootrom(&cpu, bootrom_path) != 0) {
        fprintf(stderr, "Failed to load boot ROM\n");
    }
    
    // Debug bootrom status
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
    uint32_t pallete[4] = {
        0xFFFFFFFF, // White
        0xFFAAAAAA, // Light Gray
        0xFF555555, // Dark Gray
        0xFF000000 // Black
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
    const int TARGET_FPS = 59;
    const int FRAME_TIME = 1000 / TARGET_FPS; // in ms

    bool mem_dumped = false;
    uint32_t debug_cycle_counter = 0;

    // Debug button states
    bool debug_cpu_logging = false;
    bool debug_memory_dump = false;
    bool debug_rtc_info = false;
    
    // Debug button rectangles (positioned in top right)
    SDL_Rect button_cpu_log = {160*4 - 120, 10, 100, 30};
    SDL_Rect button_mem_dump = {160*4 - 120, 50, 100, 30};
    SDL_Rect button_step_mode = {160*4 - 120, 90, 100, 30};
    SDL_Rect button_rtc_info = {160*4 - 120, 130, 100, 30};


    // Main emulation loop
    // Track button states
    static uint8_t button_directions = 0x0F;  // All direction buttons released (1=released, 0=pressed)
    static uint8_t button_actions = 0x0F;     // All action buttons released (1=released, 0=pressed)
    LOG("Save loaded: %s\n", cpu.save_loaded ? "YES" : "NO");
    LOG("CPU save file path: %s\n", cpu.save_file_path ? cpu.save_file_path : "NULL");
    // force load save file on first run
    if (!cpu.save_loaded && cpu.save_file_path) {
        LOG("Loading save file: %s\n", cpu.save_file_path);
        if (load_save_file(&cpu, cpu.save_file_path) != 0) {
            LOG("Failed to load save file: %s\n", cpu.save_file_path);
        } else {
            cpu.save_loaded = true; // Mark save as loaded
            LOG("Save file loaded successfully.\n");
        }
    }
    while (running) {
        uint32_t frame_start = SDL_GetTicks();
        
        // Process events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int x = event.button.x;
                    int y = event.button.y;
                    
                    // Check debug button clicks
                    if (x >= button_cpu_log.x && x < button_cpu_log.x + button_cpu_log.w &&
                        y >= button_cpu_log.y && y < button_cpu_log.y + button_cpu_log.h) {
                        debug_cpu_logging = !debug_cpu_logging;
                        LOG("Debug CPU logging %s\n", debug_cpu_logging ? "enabled" : "disabled");
                    }
                    else if (x >= button_mem_dump.x && x < button_mem_dump.x + button_mem_dump.w &&
                             y >= button_mem_dump.y && y < button_mem_dump.y + button_mem_dump.h) {
                        debug_memory_dump = !debug_memory_dump;
                        LOG("Debug memory dump %s\n", debug_memory_dump ? "enabled" : "disabled");
                    }
                    else if (x >= button_step_mode.x && x < button_step_mode.x + button_step_mode.w &&
                             y >= button_step_mode.y && y < button_step_mode.y + button_step_mode.h) {
                        // Instantaneous VRAM dump - dump immediately and don't toggle
                        LOG("Performing instantaneous VRAM dump...\n");
                        fprintf(log_file, "\n=== INSTANTANEOUS VRAM & LCDC DEBUG DUMP ===\n");
                        fprintf(log_file, "LCDC (0xFF40): 0x%02X\n", cpu.bus.rom[0xFF40]);
                        fprintf(log_file, "  - LCD Enable: %s\n", (cpu.bus.rom[0xFF40] & 0x80) ? "ON" : "OFF");
                        fprintf(log_file, "  - Window Tile Map: %s\n", (cpu.bus.rom[0xFF40] & 0x40) ? "0x9C00-0x9FFF" : "0x9800-0x9BFF");
                        fprintf(log_file, "  - Window Enable: %s\n", (cpu.bus.rom[0xFF40] & 0x20) ? "ON" : "OFF");
                        fprintf(log_file, "  - BG & Window Tile Data: %s\n", (cpu.bus.rom[0xFF40] & 0x10) ? "0x8000-0x8FFF" : "0x8800-0x97FF");
                        fprintf(log_file, "  - BG Tile Map: %s\n", (cpu.bus.rom[0xFF40] & 0x08) ? "0x9C00-0x9FFF" : "0x9800-0x9BFF");
                        fprintf(log_file, "  - Sprite Size: %s\n", (cpu.bus.rom[0xFF40] & 0x04) ? "8x16" : "8x8");
                        fprintf(log_file, "  - Sprite Enable: %s\n", (cpu.bus.rom[0xFF40] & 0x02) ? "ON" : "OFF");
                        fprintf(log_file, "  - BG/Window Enable: %s\n", (cpu.bus.rom[0xFF40] & 0x01) ? "ON" : "OFF");
                        
                        fprintf(log_file, "STAT (0xFF41): 0x%02X (Mode %d)\n", cpu.bus.rom[0xFF41], cpu.bus.rom[0xFF41] & 0x03);
                        fprintf(log_file, "SCY (0xFF42): %d\n", cpu.bus.rom[0xFF42]);
                        fprintf(log_file, "SCX (0xFF43): %d\n", cpu.bus.rom[0xFF43]);
                        fprintf(log_file, "LY (0xFF44): %d\n", cpu.bus.rom[0xFF44]);
                        fprintf(log_file, "LYC (0xFF45): %d\n", cpu.bus.rom[0xFF45]);
                        fprintf(log_file, "WY (0xFF4A): %d\n", cpu.bus.rom[0xFF4A]);
                        fprintf(log_file, "WX (0xFF4B): %d\n", cpu.bus.rom[0xFF4B]);
                        
                        // Dump first 16 bytes of tile data
                        fprintf(log_file, "\nTile Data (0x8000-0x800F):\n");
                        for (int i = 0; i < 16; i++) {
                            if (i % 8 == 0) fprintf(log_file, "0x%04X: ", 0x8000 + i);
                            fprintf(log_file, "%02X ", cpu.bus.rom[0x8000 + i]);
                            if ((i + 1) % 8 == 0) fprintf(log_file, "\n");
                        }
                        
                        // Dump complete background tile map (0x9800-0x9BFF)
                        fprintf(log_file, "\nComplete BG Tile Map (0x9800-0x9BFF):\n");
                        for (int i = 0; i < 0x400; i++) {
                            if (i % 32 == 0) fprintf(log_file, "0x%04X: ", 0x9800 + i);
                            fprintf(log_file, "%02X ", cpu.bus.rom[0x9800 + i]);
                            if ((i + 1) % 32 == 0) fprintf(log_file, "\n");
                        }
                        
                        // Dump complete window tile map (0x9C00-0x9FFF)
                        fprintf(log_file, "\nComplete Window Tile Map (0x9C00-0x9FFF):\n");
                        for (int i = 0; i < 0x400; i++) {
                            if (i % 32 == 0) fprintf(log_file, "0x%04X: ", 0x9C00 + i);
                            fprintf(log_file, "%02X ", cpu.bus.rom[0x9C00 + i]);
                            if ((i + 1) % 32 == 0) fprintf(log_file, "\n");
                        }
                        
                        // Dump OAM (sprite data)
                        fprintf(log_file, "\nOAM (First 16 bytes - 4 sprites):\n");
                        for (int i = 0; i < 16; i++) {
                            if (i % 4 == 0) fprintf(log_file, "Sprite %d: ", i / 4);
                            fprintf(log_file, "%02X ", cpu.bus.rom[0xFE00 + i]);
                            if ((i + 1) % 4 == 0) fprintf(log_file, "\n");
                        }
                        fprintf(log_file, "\nSRAM BANK0:\n");
                        for (int i = 0; i < 0x2000; i++) {
                            if (i % 16 == 0) {
                                fprintf(log_file, "\n");
                            }
                            fprintf(log_file, "%02X ", cpu.bus.cart_ram[i]);
                        }
                        
                        fprintf(log_file, "\n=== END INSTANTANEOUS VRAM & LCDC DUMP ===\n\n");
                        fflush(log_file);
                        LOG("VRAM dump completed!\n");
                    }
                    else if (x >= button_rtc_info.x && x < button_rtc_info.x + button_rtc_info.w &&
                             y >= button_rtc_info.y && y < button_rtc_info.y + button_rtc_info.h) {
                        debug_rtc_info = !debug_rtc_info;
                        LOG("Debug RTC info %s\n", debug_rtc_info ? "enabled" : "disabled");
                    }
                }
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

        int frame_cycles = 0;
        
        while (!gpu.should_render) {
            if (debug_cpu_logging) {
                fprintf(log_file, "A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X"\
                    "L:%02X SP:%04X PC:%04X PCMEM:%02X,%02X,%02X,%02X,%02X,%02X" \
                    " IE:%02X CURRENT ROM BANK:%d PPU MODE:%d CYCLES TAKEN:%d"\
                    " LY:%02X P1:%02X\n",
                        cpu.regs.a, PACK_FLAGS(&cpu), cpu.regs.b, cpu.regs.c, cpu.regs.d,
                        cpu.regs.e, GET_H(&cpu), GET_L(&cpu), cpu.sp, cpu.pc,
                        READ_BYTE(&cpu, cpu.pc), READ_BYTE(&cpu, cpu.pc + 1),
                        READ_BYTE(&cpu, cpu.pc + 2), READ_BYTE(&cpu, cpu.pc + 3),
                        READ_BYTE(&cpu, cpu.pc + 4), READ_BYTE(&cpu, cpu.pc + 5)
                        ,cpu.bus.rom[0xFFFF], cpu.bus.current_rom_bank, 
                        cpu.bus.rom[0xFF41] & 0x03, cpu.cycles, cpu.bus.rom[0xFF44],
                        cpu.bus.rom[INPUT_JOYPAD]
                    );
                fflush(log_file);
            }
            
            uint32_t prev_cycles = cpu.cycles;

            step_cpu(&cpu); // Step the CPU
            
            if (debug_memory_dump && cpu.regs.hl == 0xA000 && mem_dumped == false) {
                debug_cycle_counter++;
                if (debug_cycle_counter >= 2){
                    fprintf(memory_dump, "\nMemory dump at HL=0x%04X:\n", cpu.regs.hl);
                    for (int i = 0; i < 16; i++) {
                        if (i % 16 == 0) {
                            fprintf(memory_dump, "\n%04X: ", 0xA000 + i);
                        }
                        fprintf(memory_dump, "%02X ", READ_BYTE(&cpu, 0xA000 + i));
                    }
                    fprintf(memory_dump, "\nDE:   ");

                    for (int i = GET_DE(&cpu); i < GET_DE(&cpu) + 16; i++) {
                        fprintf(memory_dump, "%02X ", READ_BYTE(&cpu, i));
                    }
                    fflush(memory_dump);
                    mem_dumped = true;
                }
            }
            
            if (debug_rtc_info && cpu.bus.mbc_type == 3) {
                LOG("RTC Selected Register: 0x%02X, Current RAM Bank: %d\n", 
                    cpu.selected_rtc_register, cpu.bus.current_ram_bank);
            }
            
            if (cpu.bus.current_rom_bank == 0) {
                printf("CURRENT ROM BANK: %d\n", cpu.bus.current_rom_bank);
            }
            // LOG("CURRENT ROM BANK: %d\n", cpu.bus.current_rom_bank);
            do {
                step_timer(&cpu);  // Step the timer
                step_gpu(&gpu, cpu.cycles); // Step the GPU
            } while (cpu.halted && ((cpu.bus.rom[0xFF0F] & cpu.bus.rom[0xFFFF]) == 0)); // Handle interrupts if CPU is halted

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
            
            // Render debug buttons
            // CPU Log button
            SDL_SetRenderDrawColor(renderer, debug_cpu_logging ? 0 : 100, debug_cpu_logging ? 255 : 100, 0, 255);
            SDL_RenderFillRect(renderer, &button_cpu_log);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &button_cpu_log);
            
            // Memory Dump button
            SDL_SetRenderDrawColor(renderer, debug_memory_dump ? 0 : 100, debug_memory_dump ? 255 : 100, 0, 255);
            SDL_RenderFillRect(renderer, &button_mem_dump);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &button_mem_dump);
            
            // Step Mode button (now LCD dump button - not a toggle)
            SDL_SetRenderDrawColor(renderer, 100, 100, 0, 255); // Always gray since it's not a toggle
            SDL_RenderFillRect(renderer, &button_step_mode);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &button_step_mode);
            
            // RTC Info button
            SDL_SetRenderDrawColor(renderer, debug_rtc_info ? 0 : 100, debug_rtc_info ? 255 : 100, 0, 255);
            SDL_RenderFillRect(renderer, &button_rtc_info);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &button_rtc_info);
            
            // Draw simple text labels using lines (crude but functional)
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            
            // "CPU" text for first button (very simple pixel art)
            int btn1_x = button_cpu_log.x + 10;
            int btn1_y = button_cpu_log.y + 10;
            // C
            SDL_RenderDrawLine(renderer, btn1_x, btn1_y, btn1_x, btn1_y + 10);
            SDL_RenderDrawLine(renderer, btn1_x, btn1_y, btn1_x + 5, btn1_y);
            SDL_RenderDrawLine(renderer, btn1_x, btn1_y + 10, btn1_x + 5, btn1_y + 10);
            // P
            SDL_RenderDrawLine(renderer, btn1_x + 8, btn1_y, btn1_x + 8, btn1_y + 10);
            SDL_RenderDrawLine(renderer, btn1_x + 8, btn1_y, btn1_x + 13, btn1_y);
            SDL_RenderDrawLine(renderer, btn1_x + 8, btn1_y + 5, btn1_x + 13, btn1_y + 5);
            SDL_RenderDrawLine(renderer, btn1_x + 13, btn1_y, btn1_x + 13, btn1_y + 5);
            // U
            SDL_RenderDrawLine(renderer, btn1_x + 16, btn1_y, btn1_x + 16, btn1_y + 10);
            SDL_RenderDrawLine(renderer, btn1_x + 21, btn1_y, btn1_x + 21, btn1_y + 10);
            SDL_RenderDrawLine(renderer, btn1_x + 16, btn1_y + 10, btn1_x + 21, btn1_y + 10);
            
            // "MEM" text for second button
            int btn2_x = button_mem_dump.x + 10;
            int btn2_y = button_mem_dump.y + 10;
            // M
            SDL_RenderDrawLine(renderer, btn2_x, btn2_y, btn2_x, btn2_y + 10);
            SDL_RenderDrawLine(renderer, btn2_x + 5, btn2_y, btn2_x + 5, btn2_y + 10);
            SDL_RenderDrawLine(renderer, btn2_x, btn2_y, btn2_x + 5, btn2_y);
            SDL_RenderDrawLine(renderer, btn2_x + 2, btn2_y + 3, btn2_x + 3, btn2_y + 5);
            // E
            SDL_RenderDrawLine(renderer, btn2_x + 8, btn2_y, btn2_x + 8, btn2_y + 10);
            SDL_RenderDrawLine(renderer, btn2_x + 8, btn2_y, btn2_x + 13, btn2_y);
            SDL_RenderDrawLine(renderer, btn2_x + 8, btn2_y + 5, btn2_x + 12, btn2_y + 5);
            SDL_RenderDrawLine(renderer, btn2_x + 8, btn2_y + 10, btn2_x + 13, btn2_y + 10);
            // M
            SDL_RenderDrawLine(renderer, btn2_x + 16, btn2_y, btn2_x + 16, btn2_y + 10);
            SDL_RenderDrawLine(renderer, btn2_x + 21, btn2_y, btn2_x + 21, btn2_y + 10);
            SDL_RenderDrawLine(renderer, btn2_x + 16, btn2_y, btn2_x + 21, btn2_y);
            SDL_RenderDrawLine(renderer, btn2_x + 18, btn2_y + 3, btn2_x + 19, btn2_y + 5);
            
            // "LCD" text for third button
            int btn3_x = button_step_mode.x + 10;
            int btn3_y = button_step_mode.y + 10;
            // L
            SDL_RenderDrawLine(renderer, btn3_x, btn3_y, btn3_x, btn3_y + 10);
            SDL_RenderDrawLine(renderer, btn3_x, btn3_y + 10, btn3_x + 5, btn3_y + 10);
            // C
            SDL_RenderDrawLine(renderer, btn3_x + 8, btn3_y, btn3_x + 8, btn3_y + 10);
            SDL_RenderDrawLine(renderer, btn3_x + 8, btn3_y, btn3_x + 13, btn3_y);
            SDL_RenderDrawLine(renderer, btn3_x + 8, btn3_y + 10, btn3_x + 13, btn3_y + 10);
            // D
            SDL_RenderDrawLine(renderer, btn3_x + 16, btn3_y, btn3_x + 16, btn3_y + 10);
            SDL_RenderDrawLine(renderer, btn3_x + 16, btn3_y, btn3_x + 20, btn3_y + 2);
            SDL_RenderDrawLine(renderer, btn3_x + 16, btn3_y + 10, btn3_x + 20, btn3_y + 8);
            SDL_RenderDrawLine(renderer, btn3_x + 20, btn3_y + 2, btn3_x + 20, btn3_y + 8);
            
            // "RTC" text for fourth button
            int btn4_x = button_rtc_info.x + 10;
            int btn4_y = button_rtc_info.y + 10;
            // R
            SDL_RenderDrawLine(renderer, btn4_x, btn4_y, btn4_x, btn4_y + 10);
            SDL_RenderDrawLine(renderer, btn4_x, btn4_y, btn4_x + 5, btn4_y);
            SDL_RenderDrawLine(renderer, btn4_x, btn4_y + 5, btn4_x + 5, btn4_y + 5);
            SDL_RenderDrawLine(renderer, btn4_x + 5, btn4_y, btn4_x + 5, btn4_y + 5);
            SDL_RenderDrawLine(renderer, btn4_x + 3, btn4_y + 5, btn4_x + 5, btn4_y + 10);
            // T
            SDL_RenderDrawLine(renderer, btn4_x + 8, btn4_y, btn4_x + 16, btn4_y);
            SDL_RenderDrawLine(renderer, btn4_x + 12, btn4_y, btn4_x + 12, btn4_y + 10);
            // C
            SDL_RenderDrawLine(renderer, btn4_x + 19, btn4_y, btn4_x + 19, btn4_y + 10);
            SDL_RenderDrawLine(renderer, btn4_x + 19, btn4_y, btn4_x + 24, btn4_y);
            SDL_RenderDrawLine(renderer, btn4_x + 19, btn4_y + 10, btn4_x + 24, btn4_y + 10);
            
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

    // Cleanup
    if (cpu.save_file_path) {
        // Save the state if a save file path is provided
        if (write_save_file(&cpu, cpu.save_file_path) != 0) {
            LOG("Failed to save CPU state to %s\n", cpu.save_file_path);
        }
        free(cpu.save_file_path);
    }
    fclose(log_file);
    fclose(memory_dump);
    
    free(sdl_pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Emulation finished.\n");

    return 0;
}

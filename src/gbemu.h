#ifndef GBEMU_H
#define GBEMU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

// ROM/MBC Type Constants
#define ROM_ONLY 0x00
#define MBC1 0x01
#define MBC1_RAM 0x02
#define MBC1_RAM_BATTERY 0x03
#define MBC2 0x05
#define MBC2_BATTERY 0x06
#define ROM_RAM 0x08
#define ROM_RAM_BATTERY 0x09
#define MMM01 0x0B
#define MMM01_RAM 0x0C
#define MMM01_RAM_BATTERY 0x0D
#define MBC3_TIMER_BATTERY 0x0F
#define MBC3_TIMER_RAM_BATTERY 0x10
#define MBC3 0x11
#define MBC3_RAM 0x12
#define MBC3_RAM_BATTERY 0x13
#define MBC5 0x19
#define MBC5_RAM 0x1A
#define MBC5_RAM_BATTERY 0x1B
#define MBC5_RUMBLE 0x1C
#define MBC5_RUMBLE_RAM 0x1D
#define MBC5_RUMBLE_RAM_BATTERY 0x1E
#define MBC6 0x20
#define MBC7_SENSOR_RUMBLE_RAM_BATTERY 0x22
#define POCKET_CAMERA 0xFC
#define BANDAI_TAMA5 0xFD
#define HuC3 0xFE
#define HuC1_RAM_BATTERY 0xFF

// ROM Size Constants
#define SIZE_32KB 0x00
#define SIZE_64KB 0x01
#define SIZE_128KB 0x02
#define SIZE_256KB 0x03
#define SIZE_512KB 0x04
#define SIZE_1MB 0x05
#define SIZE_2MB 0x06
#define SIZE_4MB 0x07
#define SIZE_8MB 0x08
#define SIZE_1_1MB 0x52
#define SIZE_1_2MB 0x53
#define SIZE_1_5MB 0x54

// RAM Size Constants
#define RAM_NONE 0x00
#define RAM_2KB  0x01
#define RAM_8KB  0x02
#define RAM_32KB 0x03
#define RAM_128KB 0x04
#define RAM_64KB 0x05

// Game Boy Initialization Result
typedef enum {
    GB_INIT_SUCCESS = 0,
    GB_INIT_ERROR_ROM_FILE = -1,
    GB_INIT_ERROR_ROM_READ = -2,
    GB_INIT_ERROR_ROM_SIZE = -3,
    GB_INIT_ERROR_MEMORY = -4,
    GB_INIT_ERROR_BOOTROM = -5
} gb_init_result_t;

// Helper functions (inline for performance)
static inline uint8_t gb_rom_get_mbc_type(uint8_t *rom) {
    uint8_t type = rom[0x147];
    switch (type) {
        case 0x00:
            return 0;
        case 0x01: case 0x02: case 0x03:
            return 1;
        case 0x05: case 0x06:
            return 2;
        case 0x0F: case 0x10: case 0x11:
        case 0x12: case 0x13:
            return 3;
        case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E:
            return 5;
        default:
            return 0x04; // Unknown ROM type
    }
}

static inline uint16_t gb_rom_get_size_banks(uint8_t *rom) {
    uint8_t bank_type = rom[0x0148];
    switch (bank_type) {
        case SIZE_32KB:
            return 2;
        case SIZE_64KB:
            return 4;
        case SIZE_128KB:
            return 8;
        case SIZE_256KB:
            return 16;
        case SIZE_512KB:
            return 32;
        case SIZE_1MB:
            return 64;
        case SIZE_2MB:
            return 128;
        case SIZE_4MB:
            return 256;
        case SIZE_8MB:
            return 512;
        case SIZE_1_1MB:
            return 72; // Special case for 1.1MB
        case SIZE_1_2MB:
            return 80; // Special case for 1.2MB
        case SIZE_1_5MB:
            return 96; // Special case for 1.5MB
        default:
            fprintf(stderr, "Unknown ROM size: 0x%02X\n", bank_type);
            return 0; // Unknown size
    }
}

static inline int gb_ram_get_size_banks(uint8_t *rom) {
    uint8_t ram_type = rom[0x0149];
    switch (ram_type) {
        case 0x00: // No RAM
            return 0;
        case 0x02: // 8KB RAM
            return 1; // 1 bank of 8KB
        case 0x03: // 32KB RAM
            return 4; // 4 banks of 8KB
        case 0x04: // 128KB RAM
            return 16; // 16 banks of 8KB
        case 0x05: // 64KB RAM
            return 8; // 8 banks of 8KB
        default:
            fprintf(stderr, "Unknown RAM size: 0x%02X\n", ram_type);
            return 0; // Unknown size
    }
}

static inline size_t gb_ram_get_size_bytes(uint8_t ram_type) {
    static const size_t ram_sizes[] = {
        0,         // 0x00: no RAM
        2 * 1024,  // 0x01: 2 KB
        8 * 1024,  // 0x02: 8 KB
        32 * 1024, // 0x03: 32 KB
        128 * 1024,// 0x04: 128 KB
        64 * 1024  // 0x05: 64 KB
    };
    
    if (ram_type < sizeof(ram_sizes)/sizeof(ram_sizes[0])) {
        return ram_sizes[ram_type];
    }
    return 0;
}

static inline void gb_patch_checksum(uint8_t *rom) {
    uint8_t checksum = 0;
    for (uint16_t addr = 0x0134; addr <= 0x014C; addr++) {
        checksum = checksum - rom[addr] - 1;
    }
    rom[0x014D] = checksum; // Overwrite the checksum byte
}

// Main Game Boy initialization function
static inline gb_init_result_t gb_init(struct CPU *cpu, const char *rom_filename, const char *bootrom_filename) {
    // Initialize ROM file
    FILE *rom_file = fopen(rom_filename, "rb");
    if (!rom_file) {
        perror("Failed to open ROM file");
        return GB_INIT_ERROR_ROM_FILE;
    }

    // Read first 32KB (banks 0-1) into ROM area
    if (fread(cpu->bus.rom, 0x8000, 1, rom_file) != 1) {
        fprintf(stderr, "Failed to read ROM data\n");
        fclose(rom_file);
        return GB_INIT_ERROR_ROM_READ;
    }

    // Initialize MBC type
    cpu->bus.mbc_type = gb_rom_get_mbc_type(cpu->bus.rom);

    // Get ROM size and setup banking
    int num_banks = gb_rom_get_size_banks(cpu->bus.rom);
    cpu->bus.num_rom_banks = num_banks - 2; // Exclude the first two banks

    if (num_banks == 2) {
        // 32KB ROM - no banking needed
        cpu->bus.banking = false;
        cpu->bus.current_rom_bank = 0;
        fclose(rom_file);
    } else {
        // Multi-bank ROM - setup banking
        cpu->bus.rom_banks = (uint8_t*)malloc((num_banks - 2) * 0x4000);
        if (!cpu->bus.rom_banks) {
            fprintf(stderr, "Failed to allocate memory for ROM banks\n");
            fclose(rom_file);
            return GB_INIT_ERROR_MEMORY;
        }

        // Read remaining ROM banks
        if (fread(cpu->bus.rom_banks, 1, (num_banks - 2) * 0x4000, rom_file) != (num_banks - 2) * 0x4000) {
            fprintf(stderr, "Failed to read ROM bank data\n");
            free(cpu->bus.rom_banks);
            fclose(rom_file);
            return GB_INIT_ERROR_ROM_READ;
        }

        cpu->bus.rom_size = num_banks * 0x4000;
        cpu->bus.banking = true;
        cpu->bus.current_rom_bank = 1;
        fclose(rom_file);
    }

    // Setup cartridge RAM
    uint8_t ram_size_code = cpu->bus.rom[0x149];
    size_t cart_ram_size = gb_ram_get_size_bytes(ram_size_code);
    
    cpu->bus.cart_ram = NULL;
    cpu->bus.ram_size = cart_ram_size;

    if (cart_ram_size > 0) {
        cpu->bus.cart_ram = (uint8_t*)malloc(cart_ram_size);
        if (!cpu->bus.cart_ram) {
            fprintf(stderr, "Failed to allocate cartridge RAM\n");
            return GB_INIT_ERROR_MEMORY;
        }
        // Initialize RAM to 0
        memset(cpu->bus.cart_ram, 0, cart_ram_size);
    }

    // Load boot ROM if provided
    if (bootrom_filename) {
        FILE *bootrom_file = fopen(bootrom_filename, "rb");
        if (!bootrom_file) {
            perror("Failed to open bootrom file");
            return GB_INIT_ERROR_BOOTROM;
        }

        size_t read = fread(cpu->bootrom, 1, 256, bootrom_file);
        fclose(bootrom_file);

        if (read != 256) {
            fprintf(stderr, "Boot ROM size incorrect (read %zu bytes, expected 256)\n", read);
            return GB_INIT_ERROR_BOOTROM;
        }

        cpu->bootrom_enabled = true;
        cpu->pc = 0x0000; // Start execution at boot ROM
    } else {
        cpu->bootrom_enabled = false;
        cpu->pc = 0x0100; // Start execution at cartridge entry point
    }

    printf("Game Boy initialized successfully!\n");
    printf("ROM type: 0x%02X (MBC %d)\n", cpu->bus.rom[0x147], cpu->bus.mbc_type);
    printf("ROM size: %d banks (%d KB)\n", num_banks, num_banks * 16);
    printf("RAM size: %zu bytes\n", cart_ram_size);
    printf("Boot ROM: %s\n", cpu->bootrom_enabled ? "ENABLED" : "DISABLED");

    return GB_INIT_SUCCESS;
}

// Cleanup function
static inline void gb_cleanup(struct CPU *cpu) {
    if (cpu->bus.rom_banks) {
        free(cpu->bus.rom_banks);
        cpu->bus.rom_banks = NULL;
    }
    if (cpu->bus.cart_ram) {
        free(cpu->bus.cart_ram);
        cpu->bus.cart_ram = NULL;
    }
}

#endif // GBEMU_H

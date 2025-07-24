
#include "rom.h"
#include <stdio.h>

uint8_t rom_init(struct MemoryBus *bus) {

    uint8_t type = bus->rom[0x147];

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
            return 0x04; // Unknown ROM typeq

    }
}

uint16_t rom_size(uint8_t *rom) {
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

int ram_size(struct MemoryBus *bus) {
    uint8_t ram_type = bus->rom[0x0149];
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
    cpu->bus.mbc_type = rom_init(&cpu->bus);

    int num_banks = rom_size(cpu->bus.rom); // Number of 16KB ROM banks
    // return if rom is only 32KB
    cpu->bus.num_rom_banks = num_banks - 2; // Exclude the first two banks (header and first 32KB)

    if (num_banks == 2) {
        cpu->bus.banking = false;
        cpu->bus.current_rom_bank = 0; // just use the first bank
    } else {
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
    }

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
    LOG("RAM SIZE: %zu bytes\n", cart_ram_size);

    if (cart_ram_size > 0) {
        cpu->bus.cart_ram = malloc(cart_ram_size);
        if (!cpu->bus.cart_ram) {
            fprintf(stderr, "Failed to allocate cartridge RAM\n");
            // handle error or exit
        }
    }

    fclose(file);
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

    cpu->bootrom_enabled = true;  // Enable boot ROM overlay
    cpu->pc = 0x0000;             // Start execution at boot ROM
    return 0;
}

void patch_checksum(uint8_t *rom) {
    uint8_t checksum = 0;
    for (uint16_t addr = 0x0134; addr <= 0x014C; addr++) {
        checksum = checksum - rom[addr] - 1;
    }
    rom[0x014D] = checksum; // Overwrite the checksum byte
}
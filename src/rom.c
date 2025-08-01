
#include "rom.h"
#include <stdio.h>
#include <string.h>

/* Generate a save file name based on the ROM filename 
 * Must be freed by the caller
 * Returns NULL if no save file is needed (e.g., no battery-backed RAM)
 */
char *save_file_name(struct CPU *cpu, const char *filename) {
    // Cartridge type is at 0x147 in the ROM header
    uint8_t cart_type = cpu->bus.rom[0x147];

    // Cartridge types that support save (SRAM or battery-backed RAM)
    // This list includes common battery-backed cartridges:
    // See https://gbdev.io/pandocs/#0147---cartridge-type
    switch(cart_type) {
        case 0x03: // MBC1 + RAM + BATTERY
        case 0x06: // MBC2 + BATTERY
        case 0x09: // RAM + BATTERY
        case 0x0D: // MBC3 + RAM + BATTERY
        case 0x0F: // MBC3 + TIMER + BATTERY
        case 0x10: // MBC3 + TIMER + RAM + BATTERY
        case 0x13: // MBC3 + RAM + BATTERY
        case 0x1B: // MBC5 + RAM + BATTERY
        case 0x1E: // MBC5 + RAM + BATTERY
        case 0xFF: // Special cases, maybe no battery but treat as save (optional)
            break;
        default:
            cpu->save_loaded = true; // No save support
            // No save support
            return NULL;
    }

    // Allocate buffer for filename + ".sav" extension (max 256 bytes for safety)
    char *save_filename = malloc(256);
    if (!save_filename) return NULL;

    // Copy original filename
    strncpy(save_filename, filename, 255);
    save_filename[255] = '\0';

    // Find ".gb" or ".GB" extension and replace it with ".sav"
    char *ext = strrchr(save_filename, '.');
    if (ext && (strcasecmp(ext, ".gb") == 0)) {
        strcpy(ext, ".sav");
    } else {
        // No .gb extension found, just append .sav
        strncat(save_filename, ".sav", 255 - strlen(save_filename));
    }
    cpu->save_loaded = false;

    return save_filename;
}
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
    // bank 0
    if (fread(cpu->bus.rom, 0x4000,1, file) != 1) {
        fprintf(stderr, "Failed to read ROM data\n");
        fclose(file);
        return -1;
    }
    cpu->bus.mbc_type = rom_init(&cpu->bus);

    int num_banks = rom_size(cpu->bus.rom); // Number of 16KB ROM banks
    // return if rom is only 32KB
    cpu->bus.num_rom_banks = num_banks;

    // bank 01 - nn
    cpu->bus.rom_banks = malloc((num_banks - 1) * 0x4000); //when reading from ram account for 0x4000 missing (16KB)
    if (!cpu->bus.rom_banks) {
        fprintf(stderr, "Failed to allocate memory for ROM BANKS\n");
        fclose(file);
        return -1;
    }
    if (fread(cpu->bus.rom_banks, 1, (num_banks - 1) * 0x4000, file) != (num_banks - 1) * 0x4000) {
        LOG(stderr, "Failed to read ROM BANKS data\n");
        free(cpu->bus.rom_banks);
        fclose(file);
        return -1;
    }
    cpu->bus.rom_size = num_banks * 0x4000;
    cpu->bus.rom_banking_toggle = true; // Enable banking for MBCs that support it
    cpu->bus.current_rom_bank = 1;

    LOG("ROM loaded: %s, type: 0x%02X, size: %d banks (%d KB)\n",
        filename, cpu->bus.mbc_type, num_banks, num_banks * 16);
    
    // Initialize RAM
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
            LOG(stderr, "Failed to allocate cartridge RAM\n");
            // handle error or exit
        } else {
            // Initialize cartridge RAM to zero
            memset(cpu->bus.cart_ram, 0, cart_ram_size);
            LOG("Cartridge RAM allocated and initialized: %zu bytes\n", cart_ram_size);
        }
    }

    fclose(file);
    return 0;
}

int load_bootrom(struct CPU *cpu, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        LOG("Failed to open bootrom file");
        return -1;
    }

    // Load 256 bytes into the CPU's bootrom buffer
    size_t read = fread(cpu->bootrom, 1, 256, file);
    fclose(file);

    if (read != 256) {
        LOG(stderr, "Boot ROM size incorrect (read %zu bytes, expected 256)\n", read);
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

/* Load save file data into cartridge RAM
 * Returns 0 on success, -1 on failure (file not found is not considered failure)
 */
int load_save_file(struct CPU *cpu, const char *save_path) {
    LOG("load_save_file called with path: %s\n", save_path ? save_path : "NULL");
    LOG("  cart_ram: %p, ram_size: %zu\n", cpu->bus.cart_ram, cpu->bus.ram_size);
    
    if (!save_path || !cpu->bus.cart_ram || cpu->bus.ram_size == 0) {
        LOG("  Skipping load: save_path=%p, cart_ram=%p, ram_size=%zu\n", 
            save_path, cpu->bus.cart_ram, cpu->bus.ram_size);
        return 0; // No save path or no RAM to load into
    }

    FILE *file = fopen(save_path, "rb");
    if (!file) {
        LOG("Save file not found: %s (this is normal for new games)\n", save_path);
        return 0; // File not found is normal, not an error
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Verify file size matches expected RAM size
    if (file_size != (long)cpu->bus.ram_size) {
        LOG("Warning: Save file size (%ld) doesn't match expected RAM size (%zu)\n", 
            file_size, cpu->bus.ram_size);
        fclose(file);
        return -1;
    }

    // Load save data into cart RAM
    size_t bytes_read = fread(cpu->bus.cart_ram, 1, cpu->bus.ram_size, file);
    fclose(file);

    if (bytes_read != cpu->bus.ram_size) {
        LOG("Error: Failed to read complete save file (read %zu of %zu bytes)\n", 
            bytes_read, cpu->bus.ram_size);
        return -1;
    }

    LOG("Save file loaded successfully: %s (%zu bytes)\n", save_path, bytes_read);
    cpu->save_loaded = true;
    return 0;
}

/* Write save file data from cartridge RAM
 * Returns 0 on success, -1 on failure
 */
int write_save_file(struct CPU *cpu, const char *save_path) {
    if (!save_path || !cpu->bus.cart_ram || cpu->bus.ram_size == 0) {
        return 0; // No save path or no RAM to save
    }

    FILE *file = fopen(save_path, "wb");
    if (!file) {
        LOG("Error: Failed to open save file for writing: %s\n", save_path);
        return -1;
    }

    // Write all cartridge RAM to file
    size_t bytes_written = fwrite(cpu->bus.cart_ram, 1, cpu->bus.ram_size, file);
    fclose(file);

    if (bytes_written != cpu->bus.ram_size) {
        LOG("Error: Failed to write complete save file (wrote %zu of %zu bytes)\n", 
            bytes_written, cpu->bus.ram_size);
        return -1;
    }

    LOG("Save file written successfully: %s (%zu bytes)\n", save_path, bytes_written);
    return 0;
}
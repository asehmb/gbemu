
#include "rom.h"

uint8_t rom_init(struct MemoryBus *bus) {

    uint8_t type = bus->rom[0x147];

    switch (type) {
        case 0x00:
            return ROM_ONLY;
        case 0x01: case 0x02: case 0x03:
            return MBC1;
        case 0x05: case 0x06:
            return MBC2;
        case 0x0F: case 0x10: case 0x11:
        case 0x12: case 0x13:
            return MBC3;
        case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E:
            return MBC5;
        default:
            return 0x04; // Unknown ROM typeq

    }
}

uint16_t rom_size(struct MemoryBus *bus) {
    uint8_t bank_type = bus->rom[0x0148];
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
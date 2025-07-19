
#ifndef _ROM_H
#define _ROM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

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

// SIZES
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

uint8_t rom_init(struct MemoryBus *bus);
uint16_t rom_size(uint8_t *rom);
int ram_size(struct MemoryBus *bus);


#endif // _ROM_H
#ifndef _CPU_H
#define _CPU_H


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "input.h"


#define FLAG_ZERO      0x80 // 1000 0000
#define FLAG_SUBTRACTION 0x40 // 0100 0000
#define FLAG_HALF_CARRY 0x20 // 0010 0000
#define FLAG_CARRY     0x10 // 0001 0000

struct registers{
	uint8_t a;
	uint8_t b;
	uint8_t c;
	uint8_t d;
	uint8_t e;
	uint8_t f; // Flags
	uint16_t hl; // Combined HL register
};

// register f
// -Bit 7: "zero"
// -Bit 6: "subtraction"
// -Bit 5: "half carry"
// -Bit 4: "carry"
struct flags {
	bool zero;       // Z
	bool subtraction; // N
	bool half_carry; // H
	bool carry;      // C
};

enum JumpTest {
	JUMP_TEST_NONE,
	JUMP_TEST_ZERO,       // Z flag set
	JUMP_TEST_NOT_ZERO,   // Z flag not set
	JUMP_TEST_CARRY,      // C flag set
	JUMP_TEST_NOT_CARRY,  // C flag not set
	JUMP_TEST_HALF_CARRY, // H flag set
	JUMP_TEST_NOT_HALF_CARRY,
	JUMP_TEST_ALWAYS
};

struct MemoryBus {
	uint8_t rom [0x10000]; // 64KB addressable space
	size_t rom_size;
	size_t ram_size; // Size of RAM for MBCs that support it
	uint8_t current_rom_bank;
	uint8_t current_ram_bank; // Current RAM bank for MBCs that support it
	uint8_t rom_bank_low;
	uint8_t rom_bank_high; // For MBC1, bank2 is used for ROM banking
	uint8_t *rom_banks;
	uint8_t *cart_ram; // RAM for MBCs that support it
	uint16_t num_rom_banks; // Number of ROM banks
	bool banking; //internal use
	bool ram_banking_toggle; // Use RAM banking for MBCs that support it
	uint8_t mbc_type;
	uint8_t memory_model;
	
	// MBC3 RTC registers
	struct {
		uint8_t seconds;    // 0-59
		uint8_t minutes;    // 0-59
		uint8_t hours;      // 0-23
		uint8_t days_low;   // Lower 8 bits of day counter
		uint8_t days_high;  // Bit 0: MSB of day counter, Bit 6: Halt flag, Bit 7: Day counter carry
		uint8_t selected;   // Currently selected RTC register (0x08-0x0C)
		bool rtc_enabled;   // Whether RTC is enabled
		time_t last_update; // Last time the RTC was updated
	} rtc;
};

struct CPU {
	struct registers regs;
	uint16_t pc; // Program Counter
	uint16_t sp; // Stack Pointer
	struct MemoryBus bus;
	struct flags f; // Flags register
	bool halted; // Halt state
	bool ime;
	bool ime_pending; // IME pending state
	uint8_t cycles; // Number of cycles to execute
	uint16_t divider_cycles; // Divider cycles for timer
};

/* MACROS FOR QUICK ACCESS */
// h and L are less used then HL
#define GET_H(cpu) ((cpu)->regs.hl >> 8)
#define GET_L(cpu) ((cpu)->regs.hl & 0xFF)

#define SET_H(cpu, value) \
	((cpu)->regs.hl = ((value) << 8) | (GET_L(cpu)))
#define SET_L(cpu, value) \
	((cpu)->regs.hl = (GET_H(cpu) << 8) | (value))

#define GET_DE(cpu) \
	((cpu)->regs.d << 8 | (cpu)->regs.e)

#define SET_DE(cpu, value) \
	do { \
		(cpu)->regs.d = (value) >> 8; \
		(cpu)->regs.e = (value) & 0xFF; \
	} while (0)

#define GET_BC(cpu) \
	((cpu)->regs.b << 8 | (cpu)->regs.c)
#define SET_BC(cpu, value) \
	do { \
		(cpu)->regs.b = (value) >> 8; \
		(cpu)->regs.c = (value) & 0xFF; \
	} while (0)


#define PACK_FLAGS(cpu) ( \
	((cpu)->f.zero         ? (1 << 7) : 0) | \
	((cpu)->f.subtraction  ? (1 << 6) : 0) | \
	((cpu)->f.half_carry   ? (1 << 5) : 0) | \
	((cpu)->f.carry        ? (1 << 4) : 0))

#define UNPACK_FLAGS(cpu, value) do { \
	(cpu)->f.zero        = ((value) & (1 << 7)) != 0; \
	(cpu)->f.subtraction = ((value) & (1 << 6)) != 0; \
	(cpu)->f.half_carry  = ((value) & (1 << 5)) != 0; \
	(cpu)->f.carry       = ((value) & (1 << 4)) != 0; \
} while(0)

#define GET_AF(cpu) \
	(((cpu)->regs.a << 8) | (PACK_FLAGS(cpu) & 0xF0))  // lower nibble always 0

#define SET_AF(cpu, value) do { \
	(cpu)->regs.a = ((value) >> 8) & 0xFF; \
	UNPACK_FLAGS(cpu, (value) & 0xF0); \
} while (0)

#define ADD(x,y) ((x) + (y))
#define SUB(x,y) ((x) - (y))
#define INC(x) ((x) + 1)
#define DEC(x) ((x) - 1)
#define READ_BYTE(cpu, addr) \
    (cpu->bus.banking && (addr) >= 0x4000 && (addr) < 0x8000 ? \
    /* Handle banked ROM access (0x4000-0x7FFF) */ \
    ((cpu)->bus.rom_banks ? \
        ((cpu)->bus.mbc_type == 5 ? \
            /* MBC5: simple bank calculation (0-511 valid) */ \
            ((uint32_t)((addr)-0x4000) + (((cpu)->bus.current_rom_bank % \
                ((cpu)->bus.num_rom_banks ? (cpu)->bus.num_rom_banks : 1)) * 0x4000) < \
                (cpu)->bus.num_rom_banks * 0x4000 ? \
                (cpu)->bus.rom_banks[(addr)-0x4000 + (((cpu)->bus.current_rom_bank % \
                    ((cpu)->bus.num_rom_banks ? (cpu)->bus.num_rom_banks : 1)) * 0x4000)] : 0xFF) : \
            /* Other MBCs: bank 0 = bank 1, banks 1-127 valid */ \
            ((uint32_t)((addr)-0x4000) + ((((cpu)->bus.current_rom_bank == 0 ? 1 : \
                (cpu)->bus.current_rom_bank) - 1) * 0x4000) < (cpu)->bus.num_rom_banks * 0x4000 ? \
                (cpu)->bus.rom_banks[(addr)-0x4000 + ((((cpu)->bus.current_rom_bank == 0 ? 1 : \
                    (cpu)->bus.current_rom_bank) - 1) * 0x4000)] : 0xFF)) : 0xFF) : \
    ((addr) >= 0xA000 && (addr) <= 0xBFFF && (cpu)->bus.cart_ram && (cpu)->bus.ram_banking_toggle) ? \
    /* Handle banked RAM access (0xA000-0xBFFF) */ \
    (((addr)-0xA000) < (cpu)->bus.ram_size ? \
        ((cpu)->bus.mbc_type == 5 ? \
            /* MBC5 has direct RAM bank selection with 16 possible banks */ \
            (cpu)->bus.cart_ram[((addr)-0xA000) + (((cpu)->bus.current_ram_bank % \
                ((cpu)->bus.ram_size / 0x2000 ? (cpu)->bus.ram_size / 0x2000 : 1)) * 0x2000)] : \
            /* MBC1 RAM banking depends on memory model */ \
            (cpu)->bus.cart_ram[((addr)-0xA000) + ((cpu->bus.memory_model == 1 ? \
                (cpu)->bus.current_ram_bank % ((cpu)->bus.ram_size / 0x2000 ? \
                    (cpu)->bus.ram_size / 0x2000 : 1) : 0) * 0x2000)]) : \
        0xFF) : \
    /* Regular memory access */ \
    cpu->bus.rom[(addr)]) // Read from banked ROM, cartridge RAM, or internal memory

void dma_transfer(struct CPU *cpu, uint8_t value); // Ensure proper declaration of dma_transfer
#define WRITE_BYTE(cpu, addr, value)                                         \
    do {                                                                     \
        if ((addr) == 0xFF04) { /* DIV reset */                              \
            (cpu)->bus.rom[addr] = 0;                                        \
            (cpu)->divider_cycles = 0;                                       \
        } else if ((addr) == 0xFF46) {                                       \
            dma_transfer(cpu, value);                                        \
        } else if ((addr) >= 0xE000 && (addr) < 0xFE00) {                    \
            /* Echo RAM write also writes to WRAM */                         \
            (cpu)->bus.rom[addr] = (value);                                  \
            (cpu)->bus.rom[addr - 0x2000] = (value);                         \
        } else if ((addr) >= 0xA000 && (addr) <= 0xBFFF && (cpu)->bus.cart_ram) {  \
            /* Write to cartridge RAM if enabled */                          \
            if ((cpu)->bus.ram_banking_toggle) {                             \
                uint32_t ram_addr = ((addr) - 0xA000);                       \
                /* When RAM banking is used, select the correct bank */      \
                if ((cpu)->bus.mbc_type == 1 && (cpu)->bus.memory_model == 1) { \
                    ram_addr += ((cpu)->bus.rom_bank_high % (cpu)->bus.ram_size / 0x2000) * 0x2000; \
                }                                                            \
                if (ram_addr < (cpu)->bus.ram_size) {                        \
                    (cpu)->bus.cart_ram[ram_addr] = (value);                 \
                }                                                            \
            }                                                                \
        } else if ((addr) < 0x8000) { /* MBC registers */                   \
            if (!(cpu)->bus.banking) break;                                  \
            switch((cpu)->bus.mbc_type) {                                    \
                case 1: /* MBC1 */                                          \
                {                                                           \
                    if ((addr) < 0x2000) { /* RAM enable */                 \
                        (cpu)->bus.ram_banking_toggle = (((value) & 0x0F) == 0x0A); \
                    } else if ((addr) < 0x4000) { /* ROM bank lower 5 bits */ \
                        uint8_t w_val = (value) & 0x1F;                    \
                        if (w_val == 0) w_val = 1;                         \
                        (cpu)->bus.rom_bank_low = w_val;                   \
                        /* Update current ROM bank */                       \
                        if ((cpu)->bus.memory_model == 0) {                \
                            /* Mode 0: ROM banking, combine high and low bits */ \
                            (cpu)->bus.current_rom_bank = ((((cpu)->bus.rom_bank_high & 0x03) << 5) | (cpu)->bus.rom_bank_low); \
                        } else {                                           \
                            /* Mode 1: ROM simple banking, only use low bits */ \
                            (cpu)->bus.current_rom_bank = (cpu)->bus.rom_bank_low; \
                        }                                                  \
                        /* Always ensure we stay within valid ROM banks */ \
                        (cpu)->bus.current_rom_bank %= (cpu)->bus.num_rom_banks; \
                        if ((cpu)->bus.current_rom_bank == 0) (cpu)->bus.current_rom_bank = 1; \
                    } else if ((addr) < 0x6000) { /* ROM bank upper bits or RAM bank */ \
                        (cpu)->bus.rom_bank_high = (value) & 0x03;         \
                        if ((cpu)->bus.memory_model == 0) {                \
                            /* Mode 0: Update ROM bank with combined bits */ \
                            (cpu)->bus.current_rom_bank = ((((cpu)->bus.rom_bank_high & 0x03) << 5) | (cpu)->bus.rom_bank_low); \
                            (cpu)->bus.current_rom_bank %= (cpu)->bus.num_rom_banks; \
                            if ((cpu)->bus.current_rom_bank == 0) (cpu)->bus.current_rom_bank = 1; \
                        } else {                                           \
                            /* Mode 1: Update RAM bank */ \
                            (cpu)->bus.current_ram_bank = (cpu)->bus.rom_bank_high & 0x03; \
                        }                                                  \
                    } else if ((addr) < 0x8000) { /* ROM/RAM mode select */ \
                        (cpu)->bus.memory_model = (value) & 0x01;          \
                        /* Re-calculate ROM/RAM banks when mode changes */ \
                        if ((cpu)->bus.memory_model == 0) {                \
                            /* Mode 0: ROM banking, combine high and low bits */ \
                            (cpu)->bus.current_rom_bank = ((((cpu)->bus.rom_bank_high & 0x03) << 5) | (cpu)->bus.rom_bank_low); \
                            (cpu)->bus.current_ram_bank = 0; /* RAM banking disabled in mode 0 */ \
                        } else {                                           \
                            /* Mode 1: ROM simple banking, RAM banking enabled */ \
                            (cpu)->bus.current_rom_bank = (cpu)->bus.rom_bank_low; \
                            (cpu)->bus.current_ram_bank = (cpu)->bus.rom_bank_high & 0x03; \
                        }                                                  \
                        (cpu)->bus.current_rom_bank %= (cpu)->bus.num_rom_banks; \
                        if ((cpu)->bus.current_rom_bank == 0) (cpu)->bus.current_rom_bank = 1; \
                    }                                                      \
                    break;                                                 \
                }                                                          \
                case 3: /* MBC3 */                                         \
                {                                                          \
                    if ((addr) < 0x2000) { /* RAM/RTC enable */            \
                        /* 0x0000-0x1FFF: RAM/RTC Enable (0x0A to enable, any other value to disable) */ \
                        (cpu)->bus.ram_banking_toggle = (((value) & 0x0F) == 0x0A); \
                    } else if ((addr) < 0x4000) { /* ROM bank select (0x2000-0x3FFF) */ \
                        /* Set the ROM bank number (1-127) */              \
                        uint8_t bank = (value) & 0x7F;                     \
                        if (bank == 0) bank = 1;                           \
                        (cpu)->bus.current_rom_bank = bank;                \
                        /* Ensure we don't exceed available ROM banks */   \
                        if ((cpu)->bus.current_rom_bank >= (cpu)->bus.num_rom_banks) { \
                            (cpu)->bus.current_rom_bank %= (cpu)->bus.num_rom_banks; \
                            if ((cpu)->bus.current_rom_bank == 0) (cpu)->bus.current_rom_bank = 1; \
                        }                                                  \
                    } else if ((addr) < 0x6000) { /* RAM bank or RTC register select (0x4000-0x5FFF) */ \
                        if ((value) <= 0x03) {                             \
                            /* Select RAM bank (0-3) */                     \
                            (cpu)->bus.current_ram_bank = (value);          \
                            /* Ensure RAM bank is valid */                  \
                            if ((cpu)->bus.ram_size > 0) {                  \
                                uint8_t num_ram_banks = (cpu)->bus.ram_size / 0x2000; \
                                if (num_ram_banks > 0) {                    \
                                    (cpu)->bus.current_ram_bank %= num_ram_banks; \
                                }                                           \
                            }                                               \
                        } else if ((value) >= 0x08 && (value) <= 0x0C) {   \
                        }                                                  \
                    } else if ((addr) < 0x8000) { /* RTC latch (0x6000-0x7FFF) */ \
                        /* Latch RTC data on 0->1 transition */            \
                        static uint8_t prev_value = 0;                      \
                        if (prev_value == 0x00 && (value) == 0x01) {       \
                            /* Update RTC values to current time */         \
                        }                                                  \
                        prev_value = (value);                              \
                    }                                                      \
                    break;                                                 \
                }                                                          \
                case 5: /* MBC5 */                                         \
                {                                                          \
                    if ((addr) < 0x2000) { /* RAM enable */                \
                        /* 0x0000-0x1FFF: RAM Enable (0x0A to enable, any other value to disable) */ \
                        (cpu)->bus.ram_banking_toggle = (((value) & 0x0F) == 0x0A); \
                    } else if ((addr) < 0x3000) { /* ROM bank lower 8 bits (0x2000-0x2FFF) */ \
                        /* Set the lower 8 bits of the ROM bank number */  \
                        (cpu)->bus.rom_bank_low = (value);                 \
                        /* Update current ROM bank (preserving 9th bit) */ \
                        (cpu)->bus.current_rom_bank = ((cpu)->bus.current_rom_bank & 0x100) | (value); \
                        /* Debug log for ROM bank changes */ \
                        /*fprintf(stderr, "MBC5: Set ROM bank low bits to %02X, current_rom_bank: %03X\n", value, (cpu)->bus.current_rom_bank);*/ \
                    } else if ((addr) < 0x4000) { /* ROM bank bit 8 (0x3000-0x3FFF) */ \
                        /* Set the 9th bit of the ROM bank number */       \
                        (cpu)->bus.rom_bank_high = (value) & 0x01;         \
                        /* Update current ROM bank (preserving lower 8 bits) */ \
                        (cpu)->bus.current_rom_bank = ((cpu)->bus.current_rom_bank & 0xFF) | (((value) & 0x01) << 8); \
                        /* Debug log for ROM bank changes */ \
                        /*fprintf(stderr, "MBC5: Set ROM bank high bit to %01X, current_rom_bank: %03X\n", (value) & 0x01, (cpu)->bus.current_rom_bank);*/ \
                    } else if ((addr) < 0x6000) { /* RAM bank number (0x4000-0x5FFF) */ \
                        /* MBC5 supports up to 16 RAM banks (0-15) */      \
                        (cpu)->bus.current_ram_bank = (value) & 0x0F;      \
                        /* Ensure we don't exceed available RAM banks */   \
                        if ((cpu)->bus.ram_size > 0) {                    \
                            uint8_t num_ram_banks = (cpu)->bus.ram_size / 0x2000; \
                            if (num_ram_banks > 0) {                       \
                                (cpu)->bus.current_ram_bank %= num_ram_banks; \
                                /* Debug log for RAM bank changes */ \
                                /*fprintf(stderr, "MBC5: Set RAM bank to %01X (max: %01X)\n", (cpu)->bus.current_ram_bank, num_ram_banks-1);*/ \
                            } else {                                       \
                                (cpu)->bus.current_ram_bank = 0;           \
                            }                                             \
                        }                                                 \
                    }                                                      \
                    break;                                                 \
                }                                                          \
                default:                                                   \
                    /* Handle other MBC types */                           \
                    break;                                                 \
            }                                                              \
        } else {                                                            \
            /* Regular memory write */                                      \
            (cpu)->bus.rom[(addr)] = (value);                               \
        }                                                                  \
    } while (0)


#define READ_WORD(cpu, addr) \
	((READ_BYTE(cpu, (addr)) | (READ_BYTE(cpu, (addr) + 1) << 8)))
#define WRITE_WORD(cpu, addr, value) \
	do { \
		WRITE_BYTE(cpu, (addr), (value) & 0xFF); \
		WRITE_BYTE(cpu, (addr) + 1, (value) >> 8); \
	} while (0)


// pre declaration
/* Read and execute instructions
	This function reads the next instruction from the program counter,
	decodes it, and executes it. It updates the program counter and flags
	as necessary.
	@param cpu Pointer to the CPU structure.
	@param opcode The opcode to execute.
	@return void
*/
void exec_inst(struct CPU *cpu, uint8_t opcode);

/* Execute CB-prefixed instructions
	This function handles the execution of instructions that are prefixed with
	the CB opcode. These instructions typically involve bit manipulation and
	rotations.
	@param cpu Pointer to the CPU structure.
	@param opcode The CB-prefixed opcode to execute.
	@return void
*/
void cpu_interrupt_jump(struct CPU *cpu, uint16_t vector);

void _exec_cb_inst(struct CPU *cpu, uint8_t opcode);

void cpu_init(struct CPU *cpu, struct MemoryBus *bus);

void step_cpu(struct CPU *cpu);

void cpu_handle_interrupts(struct CPU *cpu);



#endif // _CPU_H
#ifndef _CPU_H
#define _CPU_H

#define LOGGING

#ifdef LOGGING
#define LOG(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...) ((void)0)
#endif


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


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
	uint8_t *rom_banks;
	uint8_t *cart_ram; // RAM for MBCs that support it
	bool rom_banking_toggle; // Use ROM banking for MBCs that support it
	bool ram_enabled; // Use RAM banking for MBCs that support it
	uint8_t mbc1_mode;
	uint8_t rom_bank_hi;
	uint8_t rom_bank_lo;
	uint8_t mbc_type;
	uint8_t num_ram_banks;
	uint8_t num_rom_banks;
};

struct CPU {
	struct registers regs;
	uint16_t pc; // Program Counter
	uint16_t sp; // Stack Pointer
	struct MemoryBus bus;
	struct flags f; // Flags register
	bool halted; // Halt state
	bool ime; // Interrupt Master Enable
	bool ime_pending; // IME pending state
	uint8_t cycles; // Number of cycles to execute
	uint16_t divider_cycles; // Divider cycles for timer
	uint16_t tima_counter; // Timer counter for TIMA register
	uint8_t bootrom[256]; // Boot ROM
	bool bootrom_enabled; // Boot ROM enabled state
	uint8_t p1_actions; // joypad actions (buttons)
	uint8_t p1_directions; // joypad directions (up, down, left, right)
	bool dma_transfer; // DMA transfer flag
	uint8_t selected_rtc_register; // Currently selected RTC register (0x08-0x0C for MBC3)
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

uint8_t read_joypad(struct CPU *cpu);

static inline uint8_t READ_BYTE(struct CPU *cpu, uint16_t addr) {
	if (cpu->bootrom_enabled && addr < 0x0100) {
		return *(cpu->bootrom + addr);
	}
	if (addr == 0xFF00) {
		return read_joypad(cpu);
	}
	if (cpu->bus.current_rom_bank && addr >= 0x4000 && addr < 0x8000) {
		if (cpu->bus.mbc_type == 1 && cpu->bus.mbc1_mode) {
			return *(cpu->bus.rom_banks + ((cpu->bus.current_rom_bank & 0x1F) - 1)
				* 0x4000 + (addr - 0x4000));
		} else {
			return *(cpu->bus.rom_banks + (cpu->bus.current_rom_bank - 1) * 0x4000 + 
					(addr - 0x4000));
		}
	}
	if (0xA000 <= addr && addr < 0xC000) {
		if (cpu->bus.ram_enabled) {
			/* Check if MBC3 has an RTC register selected */
			if (cpu->bus.mbc_type == 3 && (cpu->bus.current_ram_bank >= 0x08)) {
				/* Return RTC register value (not implemented - return 0 for now) */
				printf("RTC register read not implemented, returning 0xFF\n");
				return 0xFF; // Placeholder for RTC register reads
			}
			
			/* Regular cartridge RAM access */
			if (cpu->bus.cart_ram) {
				uint16_t offset;
				if (cpu->bus.mbc_type == 1){
					if (cpu->bus.ram_size <= 0x2000) {
						// 2KB or 8KB RAM: wrap around using modulo
						offset = (addr - 0xA000) % cpu->bus.ram_size;
					} else if (cpu->bus.mbc1_mode == 1 && cpu->bus.ram_size >= 0x8000) {
						// Mode 1, 32KB RAM: support 4 banks
						offset = (cpu->bus.current_ram_bank * 0x2000) + (addr - 0xA000);
					} else {
						// Mode 0: always use RAM bank 0
						offset = addr - 0xA000;
					}
				} else {
					offset = (cpu->bus.current_ram_bank * 0x2000) + (addr - 0xA000);
				}
				// Bounds check
				if (offset < cpu->bus.ram_size) {
					return *(cpu->bus.cart_ram + offset); // Read from cart RAM
				}
			}
		}
		return 0xFF;
	}

	if (0x8000 <= addr && addr < 0xA000) { // VRAM
		if (cpu->dma_transfer) {
			return *(cpu->bus.rom + addr);
		}
		if ((*(cpu->bus.rom + 0xFF41) & 0x03) == 0x03) { // blocked in mode 3
			return 0xFF; // Return dummy value if VRAM is blocked
		}
		return *(cpu->bus.rom + addr); // Read from VRAM
	}
	if (0xFE00 <= addr && addr < 0xFEA0) { // OAM
		// if (cpu->dma_transfer == true) {
		// 	return cpu->bus.rom[addr];
		// }
		uint8_t stat_mode = *(cpu->bus.rom + 0xFF41) & 0x03;
		if (stat_mode == 0x02 || stat_mode == 0x03) {
			return 0xFF; // Block reads in mode 2 and 3
		}
		return *(cpu->bus.rom + addr); // Read from OAM
	}
	if (0xE000 <= addr && addr < 0xFE00) { // Echo RAM
		return *(cpu->bus.rom + (addr - 0x2000)); // Read from echo RAM
	}
	return *(cpu->bus.rom + addr);
}

void dma_transfer(struct CPU *cpu, uint8_t value); // Ensure proper declaration of dma_transfer for WRITE_BYTE

static inline void WRITE_BYTE(struct CPU *cpu, uint16_t addr, uint8_t value) {
	if (cpu->bootrom_enabled && (addr < 0x0100 || (addr >= 0x8000 && addr < 0xA000))) {
		if (0x8000 <= addr && addr < 0xA000) {
			// Allow bootrom to write to VRAM
			*(cpu->bus.rom + addr) = value;
		}
		*(cpu->bootrom + addr) = value;
		return;
	} else if (addr < 0x8000) {
		switch(cpu->bus.mbc_type) {
			case 1: { // MBC1
				if (addr < 0x2000) {
					cpu->bus.ram_enabled = ((value & 0x0F) == 0x0A);
				} else if (addr < 0x4000) {
					cpu->bus.rom_bank_lo = value & 0x1F;
					if ((cpu->bus.rom_bank_lo & 0x1F) == 0) {
						cpu->bus.rom_bank_lo = 1; // only if lower bits are 0
					}
				} else if (addr < 0x6000) {
					cpu->bus.rom_bank_hi = value & 0x03;
					if (cpu->bus.mbc1_mode == 0) {
						cpu->bus.current_ram_bank = 0;
					} else {
						cpu->bus.current_ram_bank = cpu->bus.rom_bank_hi;
					}
				} else if (addr < 0x8000) {
					cpu->bus.mbc1_mode = value & 0x01;
				}
				
				// Always update the final bank after any change
				if (cpu->bus.mbc1_mode == 0) {
					// ROM banking mode
					cpu->bus.current_rom_bank = (cpu->bus.rom_bank_hi << 5) | (cpu->bus.rom_bank_lo & 0x1F);
					if (cpu->bus.current_rom_bank == 0) {
						cpu->bus.current_rom_bank = 1;
					}
					cpu->bus.current_rom_bank %= cpu->bus.num_rom_banks;
				} else {
					// RAM banking mode — upper bits ignored
					cpu->bus.current_rom_bank = cpu->bus.rom_bank_lo & 0x1F;
					if (cpu->bus.current_rom_bank == 0) {
						cpu->bus.current_rom_bank = 1;
					}
					cpu->bus.current_rom_bank %= cpu->bus.num_rom_banks;
				}
				return;
			}
			case 3: /* MBC3 */
			{
				if (addr < 0x2000) { /* RAM/RTC enable */
					/* 0x0000-0x1FFF: RAM/RTC Enable (0x0A to enable, any other value to disable) */
					cpu->bus.ram_enabled = ((value & 0x0F) == 0x0A);
				} else if (addr < 0x4000) { /* ROM bank select (0x2000-0x3FFF) */
					/* Set the ROM bank number (1-127) */
					uint8_t bank = value & 0x7F;
					if (bank == 0) bank = 1;
					cpu->bus.current_rom_bank = bank;
					/* Ensure we don't exceed available ROM banks */
					if (cpu->bus.current_rom_bank >= cpu->bus.num_rom_banks) {
						cpu->bus.current_rom_bank %= cpu->bus.num_rom_banks;
						if (cpu->bus.current_rom_bank == 0) cpu->bus.current_rom_bank = 1;
					}
				} else if (addr < 0x6000) { /* RAM bank or RTC register select (0x4000-0x5FFF) */
					cpu->bus.current_ram_bank = value; // 0-3 for RAM banks, 8-12 for RTC registers
				} else if (addr < 0x8000) { /* RTC latch (0x6000-0x7FFF) */
					/* Latch RTC data on 0->1 transition */
					static uint8_t prev_value = 0;
					if (prev_value == 0x00 && value == 0x01) {
						/* Update RTC values to current time */
					}
					prev_value = value;
				}
				return;
			}
			case 5: /* MBC5 */
			{
				if (addr < 0x2000) { /* RAM enable */
					cpu->bus.ram_enabled = ((value & 0x0F) == 0x0A);
					printf("RAM banking %s\n", cpu->bus.ram_enabled ? "enabled" : "disabled");
				} else if (addr < 0x3000) { /* ROM bank lower 8 bits */
					cpu->bus.current_rom_bank = (cpu->bus.current_rom_bank & 0x100) | (value & 0xFF);
				} else if (addr < 0x4000) { /* ROM bank bit 8 */
					cpu->bus.current_rom_bank = (cpu->bus.current_rom_bank & 0xFF) | ((value & 0x01) << 8);
				} else if (addr < 0x6000) { /* RAM bank */
					cpu->bus.current_ram_bank = value & 0x0F;
				}
				break;
			}
			default: /* Other MBCs */
				// *(cpu->bus.rom + addr) = value; // Writes to ROM are NOT allowed
				break;
		}
	} else if (addr < 0xA000) {
		if (cpu->dma_transfer) {
			*(cpu->bus.rom + addr) = value;
		}
		if ((*(cpu->bus.rom + 0xFF41) & 0x03) == 0x03) { // blocked in mode 3
			return; // Return dummy value if VRAM is blocked
		}
		*(cpu->bus.rom + addr) = value;
	} else if (addr < 0xC000) {
		/* Write to cartridge RAM or RTC registers if enabled */
		if (cpu->bus.ram_enabled) {
			/* Check if MBC3 has an RTC register selected */
			if (cpu->bus.mbc_type == 3 && cpu->selected_rtc_register >= 0x08 && cpu->selected_rtc_register <= 0x0C) {
				/* Write to RTC register (not implemented - ignore for now) */
				return;
			}
			
			/* Regular cartridge RAM write */
			if (cpu->bus.cart_ram) {
				uint16_t offset;
				if (cpu->bus.mbc_type == 1){
					if (cpu->bus.ram_size <= 0x2000) {
						// 2KB or 8KB RAM: wrap around using modulo
						offset = (addr - 0xA000) % cpu->bus.ram_size;
					} else if (cpu->bus.mbc1_mode == 1 && cpu->bus.ram_size >= 0x8000) {
						// Mode 1, 32KB RAM: support 4 banks
						offset = (cpu->bus.current_ram_bank * 0x2000) + (addr - 0xA000);
					} else {
						// Mode 0: always use RAM bank 0
						offset = addr - 0xA000;
					}
				} else {
					offset = (cpu->bus.current_ram_bank * 0x2000) + (addr - 0xA000);
				}

				// Bounds check
				if (offset < cpu->bus.ram_size) {
					*(cpu->bus.cart_ram + offset) = value;
				}
				return;
			}
		}
	} else if (addr < 0xE000) { // WRAM
		*(cpu->bus.rom + addr) = value; // Write to WRAM
	} else if (addr < 0xFE00) { // Echo RAM (0xE000-0xFDFF)
		*(cpu->bus.rom + (addr - 0x2000)) = value;
	} else if (addr < 0xFEA0) { // OAM
		if (cpu->dma_transfer == true) {
			*(cpu->bus.rom + addr) = value;
			return;
		}
		uint8_t stat_mode = *(cpu->bus.rom + 0xFF41) & 0x03;
		if (stat_mode == 0x02 || stat_mode == 0x03) {
			// Block writes in mode 2 and 3
			return;
		}
		*(cpu->bus.rom + addr) = value;
	} else if (addr == 0xFF0F) { /* Interrupt Flag */
		*(cpu->bus.rom + addr) = value | 0xE0; /* Only lower 5 bits are used */
	} else if (addr == 0xFF50) { /* Bootrom */
		if (cpu->bootrom_enabled) {
			printf("Boot ROM disabled by write to 0xFF50 with value 0x%02X\n", value);
		}
		cpu->bootrom_enabled = false; /* Any write to 0xFF50 disables the bootrom */
	} else if (addr == 0xFF04) { /* DIV reset */
		*(cpu->bus.rom + addr) = 0;
		cpu->divider_cycles = 0;
	} else if (addr == 0xFF42 || addr == 0xFF43) {
		uint8_t stat_mode = *(cpu->bus.rom + 0xFF41) & 0x03;
		if (stat_mode == 0x03) {
			return;
		}
		*(cpu->bus.rom + addr) = value; // Write to SC registers
	} else if (addr == 0xFF46) { /*DMA transfer*/
		dma_transfer(cpu, value);
		*(cpu->bus.rom + addr) = value;
	} else if (addr == 0xFF00) { /* P1 register */
		/* Update joypad state */
		*(cpu->bus.rom + addr) = (*(cpu->bus.rom + 0xFF00) & 0xCF) | (value & 0x30);
	} else {
		// rest of the I/O registers/HRAM
		*(cpu->bus.rom + addr) = value;
	}
	// *(cpu->bus.rom + addr) = value; // Write to memory bus
}


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
static inline void cpu_interrupt_jump(struct CPU *cpu, uint16_t vector);

static inline void _exec_cb_inst(struct CPU *cpu, uint8_t opcode);

void cpu_init(struct CPU *cpu, struct MemoryBus *bus);

int cpu_handle_interrupts(struct CPU *cpu);

static inline void step_cpu(struct CPU *cpu) {


    cpu->cycles = 4;
    if (cpu->halted) {
        uint8_t if_reg = cpu->bus.rom[0xFF0F];
        uint8_t ie_reg = cpu->bus.rom[0xFFFF];
        if ((if_reg & ie_reg)) {
            cpu->halted = false; // Wake up even if IME is 0
            if (cpu->ime) {
            // If IME is set, handle interrupts
                cpu_handle_interrupts(cpu);
            }
        } else {
            cpu->cycles = 4;
            return;
    }
    }

    if (cpu->ime) {
        if (!cpu_handle_interrupts(cpu)){
            return;
        }
    }
    if (cpu->ime_pending) {
        cpu->ime = true; // Set IME to true if pending
        cpu->ime_pending = false; // Clear pending state
    }

    uint8_t opcode = READ_BYTE(cpu, cpu->pc);
    cpu->pc++; // Increment PC to point to the next instruction
    exec_inst(cpu, opcode);

}


#endif // _CPU_H
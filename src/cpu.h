#ifndef _CPU_H
#define _CPU_H


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
	bool banking; //internal use
	bool ram_banking_toggle; // Use RAM banking for MBCs that support it
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
	bool ime;
	bool ime_pending; // IME pending state
	uint8_t cycles; // Number of cycles to execute
	uint16_t divider_cycles; // Divider cycles for timer
	uint16_t tima_counter; // Timer counter for TIMA register
	uint8_t bootrom[256]; // Boot ROM
	bool bootrom_enabled;
	uint8_t p1_actions;
	uint8_t p1_directions;
	bool dma_transfer;
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
		return cpu->bootrom[addr];
	}
	if (addr == 0xFF00) {
		return read_joypad(cpu);
	}
	if (cpu->bus.current_rom_bank && addr >= 0x4000 && addr < 0x8000) {
		if (cpu->bus.current_rom_bank == 1) {
			return cpu->bus.rom[addr];
		} else {
			return cpu->bus.rom_banks[(cpu->bus.current_rom_bank - 2) * 0x4000 + 
				(addr - 0x4000)];
		}
	}
	if (0xA000 <= addr && addr < 0xC000) {
		if (cpu->bus.ram_banking_toggle && cpu->bus.cart_ram) {
			return cpu->bus.cart_ram[(cpu->bus.current_ram_bank * 0x2000) + 
				(addr - 0xA000)];
		}
	}
	if (0x8000 <= addr && addr < 0xA000) { // VRAM
		if (cpu->dma_transfer) {
			return cpu->bus.rom[addr];
		}
		if ((cpu->bus.rom[0xFF41] & 0x03) == 0x03) { // blocked in mode 3
			return 0xFF; // Return dummy value if VRAM is blocked
		}
		return cpu->bus.rom[addr]; // Read from VRAM
	}
	if (0xFE00 <= addr && addr < 0xFEA0) { // OAM
		if (cpu->dma_transfer == true) {
			return cpu->bus.rom[addr];
		}
		uint8_t stat_mode = cpu->bus.rom[0xFF41] & 0x03;
		if (stat_mode == 0x02 || stat_mode == 0x03) {
			return 0xFF; // Block reads in mode 2 and 3
		}
		return cpu->bus.rom[addr]; // Read from OAM
	}
	return cpu->bus.rom[addr];
}

void dma_transfer(struct CPU *cpu, uint8_t value); // Ensure proper declaration of dma_transfer for WRITE_BYTE

static inline void WRITE_BYTE(struct CPU *cpu, uint16_t addr, uint8_t value) {
	if (cpu->bootrom_enabled && addr < 0x0100) {
		cpu->bootrom[addr] = value;
		return;
	} else if (addr < 0x8000) {
		switch(cpu->bus.mbc_type) {
			case 1: /* MBC1 */
			{
				if (addr < 0x2000) { /* RAM enable */
					cpu->bus.ram_banking_toggle = ((value & 0x0F) == 0x0A);
				} else if (addr < 0x4000) { /* ROM bank select (0x2000-0x3FFF) */
					/* Set the ROM bank number (1-127) */
					uint8_t bank = value & 0x1F;
					if (bank == 0) bank = 1;
					cpu->bus.current_rom_bank = bank;
					/* Ensure we don't exceed available ROM banks */
					if (cpu->bus.current_rom_bank >= cpu->bus.num_rom_banks) {
						cpu->bus.current_rom_bank %= cpu->bus.num_rom_banks;
						if (cpu->bus.current_rom_bank == 0) cpu->bus.current_rom_bank = 1;
					}
				} else if (addr < 0x6000) { /* RAM bank select (0x4000-0x5FFF) */
					if (cpu->bus.ram_banking_toggle && value <= 0x03) {
						/* Select RAM bank (0-3) */
						cpu->bus.current_ram_bank = value;
						/* Ensure RAM bank is valid */
						if (cpu->bus.ram_size > 0) {
							uint8_t num_ram_banks = cpu->bus.ram_size / 0x2000;
							if (num_ram_banks > 0) {
								cpu->bus.current_ram_bank %= num_ram_banks;
							}
						}
					}
				} else if (addr < 0x8000) { /* ROM mode (0x6000-0x7FFF) */
					/* Switch between ROM and RAM mode */
					if (value & 0x01) {
						cpu->bus.banking = true;
					} else {
						cpu->bus.banking = false;
						cpu->bus.current_rom_bank = 0;
					}
				}
				break;
			}
			case 3: /* MBC3 */
			{
				if (addr < 0x2000) { /* RAM/RTC enable */
					/* 0x0000-0x1FFF: RAM/RTC Enable (0x0A to enable, any other value to disable) */
					cpu->bus.ram_banking_toggle = ((value & 0x0F) == 0x0A);
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
					if (value <= 0x03) {
						/* Select RAM bank (0-3) */
						cpu->bus.current_ram_bank = value;
						/* Ensure RAM bank is valid */
						if (cpu->bus.ram_size > 0) {
							uint8_t num_ram_banks = cpu->bus.ram_size / 0x2000;
							if (num_ram_banks > 0) {
								cpu->bus.current_ram_bank %= num_ram_banks;
							}
						}
					} else if (value >= 0x08 && value <= 0x0C) {
						// RTC register select (not implemented)
					}
				} else if (addr < 0x8000) { /* RTC latch (0x6000-0x7FFF) */
					/* Latch RTC data on 0->1 transition */
					static uint8_t prev_value = 0;
					if (prev_value == 0x00 && value == 0x01) {
						/* Update RTC values to current time */
					}
					prev_value = value;
				}
				break;
			}
			case 5: /* MBC5 */
			{
				if (addr < 0x2000) { /* RAM enable */
					cpu->bus.ram_banking_toggle = ((value & 0x0F) == 0x0A);
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
				cpu->bus.rom[addr] = value;
				break;
		}
	} else if (addr < 0xA000) {
		cpu->bus.rom[addr] = value; // Write to VRAM
	} else if (addr < 0xC000) {
		/* Write to cartridge RAM if enabled */
		if (cpu->bus.ram_banking_toggle && cpu->bus.cart_ram) {
			size_t w_offset = (cpu->bus.current_ram_bank * 0x2000) + (addr - 0xA000);
			if (w_offset < cpu->bus.ram_size) {
				cpu->bus.cart_ram[w_offset] = value;
			}
		}
	} else if (addr < 0xE000) { // WRAM
		cpu->bus.rom[addr] = value; // Write to WRAM
	} else if (addr < 0xFE00) { // Echo RAM (0xE000-0xFDFF)
		cpu->bus.rom[addr] = value;
		cpu->bus.rom[addr - 0x2000] = value;
	} else if (addr < 0xFEA0) { // OAM
		if (cpu->dma_transfer == true) {
			cpu->bus.rom[addr] = value;
			return;
		}
		uint8_t stat_mode = cpu->bus.rom[0xFF41] & 0x03;
		if (stat_mode == 0x02 || stat_mode == 0x03) {
			// Block writes in mode 2 and 3
			return;
		}
		cpu->bus.rom[addr] = value;
	} else if (addr == 0xFF0F) { /* Interrupt Flag */
		cpu->bus.rom[addr] = value | 0xE0; /* Only lower 5 bits are used */
	} else if (addr == 0xFF50) { /* Bootrom */
		if (cpu->bootrom_enabled) {
			printf("Boot ROM disabled by write to 0xFF50 with value 0x%02X\n", value);
		}
		cpu->bootrom_enabled = false; /* Any write to 0xFF50 disables the bootrom */
	} else if (addr == 0xFF04) { /* DIV reset */
		cpu->bus.rom[addr] = 0;
		cpu->divider_cycles = 0;
	} else if (addr == 0xFF46) { /*DMA transfer*/
		dma_transfer(cpu, value);
		cpu->bus.rom[addr] = value;
	} else if (addr == 0xFF00) { /* P1 register */
		/* Update joypad state */
		cpu->bus.rom[addr] = (cpu->bus.rom[0xFF00] & 0xCF) | (value & 0x30);
	} else {
		// rest of the I/O registers/HRAM
		cpu->bus.rom[addr] = value;
	}
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
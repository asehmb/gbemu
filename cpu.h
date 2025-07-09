#ifndef _CPU_H
#define _CPU_H


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
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
	uint8_t *rom_banks;
	uint8_t *cart_ram; // RAM for MBCs that support it
	bool banking; //internal use
	bool use_banking; // Use RAM banking for MBCs that support it
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
    cpu->bus.rom_banks[((addr-0x4000) + (cpu->bus.current_rom_bank * 0x4000))] : \
    cpu->bus.rom[(addr)]) // Read from RAM if banking is enabled, otherwise read from ROM

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
		} else if ((addr) < 0x8000) { /* Don't allow writes to ROM */       \
			/* Ignore ROM writes */                                         \
		} else { \
			switch((cpu->bus.mbc_type)) { \
				case 5: /* MBC5 */ \
				{\
				if ((addr) < 0x2000) { /* RAM enable */                       \
					(cpu)->bus.use_banking = (((value) & 0x0F) == 0x0A);             \
				} else if ((addr) < 0x3000) { /* ROM bank lower 8 bits */           \
					(cpu)->bus.current_rom_bank = ((cpu)->bus.current_rom_bank & 0x100) | ((value) & 0xFF); \
				} else if ((addr) < 0x4000) { /* ROM bank bit 8 */                   \
					(cpu)->bus.current_rom_bank = ((cpu)->bus.current_rom_bank & 0xFF) | (((value) & 0x01) << 8); \
				} else if ((addr) < 0x6000) { /* RAM bank */                         \
					(cpu)->bus.current_ram_bank = (value) & 0x0F;                    \
				} else if ((addr) >= 0xA000 && (addr) < 0xC000) {                    \
					if ((cpu)->bus.use_banking && (cpu)->bus.cart_ram) {                 \
						size_t w_offset = ((cpu)->bus.current_ram_bank * 0x2000) + ((addr) - 0xA000); \
						if (w_offset < (cpu)->bus.ram_size) {                          \
							(cpu)->bus.cart_ram[w_offset] = (value);                        \
						}                                                            \
					} \
				break; \
				} \
				default: /* Other MBCs */ \
					(cpu)->bus.rom[(addr)] = (value);                                \
			} \
			}                                                            \
		}                                                                    \
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
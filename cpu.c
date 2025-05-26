
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define FLAG_ZERO      0x80 // 1000 0000
#define FLAG_SUBTRACTION 0x40 // 0100 0000
#define FLAG_HALF_CARRY 0x20 // 0010 0000
#define FLAG_CARRY     0x10 // 0001 0000

struct registers{
    uint8_t a; // Accumulator
    uint8_t b;
    uint8_t c; // Counter
    uint8_t d; // Data
    uint8_t e; // Extended
    uint8_t f; // Flags
    uint8_t h; // High
    uint8_t l; // Low
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

inline uint8_t flagToByte(struct flags *f) {
    return (f->zero ? FLAG_ZERO : 0) |
           (f->subtraction ? FLAG_SUBTRACTION : 0) |
           (f->half_carry ? FLAG_HALF_CARRY : 0) |
           (f->carry ? FLAG_CARRY : 0);
}

inline void byteToFlag(uint8_t byte, struct flags *f) {
    f->zero = (byte & FLAG_ZERO) != 0;
    f->subtraction = (byte & FLAG_SUBTRACTION) != 0;
    f->half_carry = (byte & FLAG_HALF_CARRY) != 0;
    f->carry = (byte & FLAG_CARRY) != 0;
}


// Use 2 registers to store a 16-bit value (af, bc, de, hl)
static inline void set_virtual(uint8_t *reg1, uint8_t *reg2, uint16_t value) {

    *reg1 = (value >> 8) & 0xFF; // Set high byte
    *reg2 = value & 0xFF; // Set low byte
}

static inline uint16_t get_virtual(uint8_t reg1, uint8_t reg2) {
    return ((uint16_t)(reg1) << 8) | (reg2); // Combine high and low bytes
}

// Arithmetic operations

static inline void add(struct registers *reg, uint8_t b, struct flags *f) {
    uint16_t result = reg->a + b;
    f->zero = ((result & 0xFF) == 0); // Check if result is zero
    f->subtraction = false; // N flag always false for ADD
    f->half_carry = ((reg->a & 0x0F) + (b & 0x0F)) > 0x0F; // H flag
    f->carry = result > 0xFF; // C flag
    reg->a = result & 0xFF; // Store the result in register A
}

static inline void addHL(struct registers *reg, uint8_t b, struct flags *f) {
    uint16_t result = get_virtual(reg->h, reg->l) + b;
    f->zero = ((result & 0xFF) == 0); // Check if result is zero
    f->subtraction = false; // N flag always false for ADD
    f->half_carry = ((reg->l & 0x0F) + (b & 0x0F)) > 0x0F; // H flag
    f->carry = result > 0xFFFF; // C flag
    set_virtual(&reg->h, &reg->l, result & 0xFFFF); // Store the result in HL
}

static inline void adc(struct registers *reg, uint8_t b, struct flags *f) {
    uint8_t carry_in = f->carry ? 1 : 0;
    uint16_t result = (uint16_t)reg->a + (uint16_t)b + carry_in; //add with carry

    f->zero = ((result & 0xFF) == 0); // Z flag
    f->subtraction = false;           // N flag is always false for ADC
    f->half_carry = ((reg->a & 0x0F) + (b & 0x0F) + carry_in) > 0x0F; // H flag
    f->carry = result > 0xFF;         // C flag
    reg->a = result & 0xFF;           // Store the result in register A
}

static inline void sub(struct registers *reg, uint8_t b, struct flags *f) {
    uint16_t result = reg->a - b;
    f->zero = ((result & 0xFF) == 0); // Check if result is zero
    f->subtraction = true; // N flag is set for subtraction
    f->half_carry = ((reg->a & 0x0F) < (b & 0x0F)); // H flag
    f->carry = result > 0xFF; // C flag
    reg->a = result & 0xFF; // Store the result in register A
}

static inline void sbc(struct registers *reg, uint8_t b, struct flags *f) {
    uint8_t carry_in = f->carry ? 1 : 0;
    uint16_t result = (uint16_t)reg->a - (uint16_t)b - carry_in;

    f->zero = ((result & 0xFF) == 0); // Z flag
    f->subtraction = true;            // N flag is set for SBC
    f->half_carry = ((reg->a & 0x0F) < ((b & 0x0F) + carry_in)); // H flag
    f->carry = result > 0xFF;         // C flag
    reg->a = result & 0xFF;           // Store the result in register A
}

static inline void and(struct registers *reg, uint8_t b, struct flags *f) {
    reg->a &= b; // Perform bitwise AND
    f->zero = (reg->a == 0); // Z flag
    f->subtraction = false;   // N flag is always false for AND
    f->half_carry = true;     // H flag is always set for AND
    f->carry = false;         // C flag is always false for AND
}
static inline void xor(struct registers *reg, uint8_t b, struct flags *f) {
    reg->a ^= b; // Perform bitwise XOR
    f->zero = (reg->a == 0); // Z flag
    f->subtraction = false;   // N flag is always false for XOR
    f->half_carry = false;    // H flag is always false for XOR
    f->carry = false;         // C flag is always false for XOR
}
static inline void or(struct registers *reg, uint8_t b, struct flags *f) {
    reg->a |= b; // Perform bitwise OR
    f->zero = (reg->a == 0); // Z flag
    f->subtraction = false;   // N flag is always false for OR
    f->half_carry = false;    // H flag is always false for OR
    f->carry = false;         // C flag is always false for OR
}
static inline uint8_t cp(struct registers *reg, uint8_t b, struct flags *f) {
    uint16_t result = reg->a - b;
    f->zero = ((result & 0xFF) == 0); // Z flag
    f->subtraction = true;            // N flag is set for CP
    f->half_carry = ((reg->a & 0x0F) < (b & 0x0F)); // H flag
    f->carry = result > 0xFF;         // C flag
    return result & 0xFF;             // Return the result for further use if needed
}
static inline void inc(uint8_t *target, struct flags *f) {
    uint16_t result = *target + 1;
    f->zero = ((result & 0xFF) == 0); // Z flag
    f->subtraction = false;           // N flag is always false for INC
    f->half_carry = ((*target & 0x0F) == 0x0F); // H flag
    f->carry = false;                 // C flag is always false for INC
    *target = result & 0xFF;               // Store the result in the register
}
static inline void dec(uint8_t *target, struct flags *f) {
    uint16_t result = *target - 1;
    f->zero = ((result & 0xFF) == 0); // Z flag
    f->subtraction = true;            // N flag is set for DEC
    f->half_carry = ((*target & 0x0F) == 0); // H flag
    f->carry = false;                 // C flag is always false for DEC
    *target = result & 0xFF;               // Store the result in the register
}
static inline void ccf(struct registers *reg, struct flags *f) {
    // toggle carry flag
    if (f->carry) {
        f->carry = false; // If carry is set, clear it
    } else {
        f->carry = true;  // If carry is not set, set it
    }
}
static inline void scf(struct flags *f) {
    f->carry = true;      // Set carry flag
    f->subtraction = false; // N flag is always false for SCF
    f->half_carry = false;  // H flag is always false for SCF
}
static inline void rra(struct registers *reg, struct flags *f) {
    // Rotate A right through carry
    uint8_t old_carry = f->carry ? 0x01 : 0x00; // Save old carry state
    f->carry = (reg->a & 0x01) != 0; // Set carry flag based on bit 0 of A
    reg->a = (reg->a >> 1) | (old_carry << 7); // Rotate right and set new A value
    f->zero = (reg->a == 0); // Update zero flag
}
static inline void rla(struct registers *reg, struct flags *f) {
    // Rotate A left through carry
    uint8_t old_carry = f->carry ? 0x80 : 0x00; // Save old carry state
    f->carry = (reg->a & 0x80) != 0; // Set carry flag based on bit 7 of A
    reg->a = (reg->a << 1) | (old_carry >> 7); // Rotate left and set new A value
    f->zero = (reg->a == 0); // Update zero flag
}
static inline void rrla(struct registers *reg, struct flags *f) {
    // Rotate A left without affecting the carry flag
    uint8_t a7 = reg->a & 0x80; // Save bit 7 of A
    reg->a = (reg->a << 1) | (a7 >> 7);
    f->carry = (a7 != 0); // Set carry flag based on bit 7 of A
    f->zero = (reg->a == 0); // Update zero flag
}
static inline void rlca(struct registers *reg, struct flags *f) {
    // Rotate A left through carry without affecting the carry flag
    uint8_t a7 = reg->a & 0x80; // Save bit 7 of A
    reg->a = (reg->a << 1) | (a7 >> 7); // Rotate left and set new A value
    f->carry = (a7 != 0); // Set carry flag based on bit 7 of A
    f->zero = (reg->a == 0); // Update zero flag
}
static inline void cpl(struct registers *reg, struct flags *f) {
    // Complement A
    reg->a = ~reg->a; // Bitwise NOT operation
    f->zero = (reg->a == 0); // Update zero flag
    f->subtraction = true; // N flag is set for CPL
    f->half_carry = true; // H flag is always set for CPL
    f->carry = false; // C flag is always false for CPL
}
static inline void bit(uint8_t bit, uint8_t value, struct flags *f) {
    f->zero = ((value & (1 << bit)) == 0); // Set zero flag if the bit is not set
    f->subtraction = false; // N flag is always false for BIT
    f->half_carry = true; // H flag is always set for BIT
}
static inline void reset(uint8_t bit, uint8_t *value) {
    *value &= ~(1 << bit); // Clear the specified bit
}
static inline void set(uint8_t bit, uint8_t *value) {
    *value |= (1 << bit); // Set the specified bit to 1
}
static inline void srl(uint8_t *value, struct flags *f) {
    f->carry = (*value & 0x01) != 0; // Set carry flag based on bit 0
    *value >>= 1; // Shift right
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for SRL
    f->half_carry = false; // H flag is always false for SRL
}
static inline void sll(uint8_t *value, struct flags *f) {
    f->carry = (*value & 0x80) != 0; // Set carry flag based on bit 7
    *value <<= 1; // Shift left
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for SLA
    f->half_carry = false; // H flag is always false for SLA
}
static inline void rr(uint8_t *value, struct flags *f) {
    uint8_t old_carry = f->carry ? 0x01 : 0x00; // Save old carry state
    f->carry = (*value & 0x01) != 0; // Set carry flag based on bit 0
    *value = (*value >> 1) | (old_carry << 7); // Rotate right and set new value
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for RR
    f->half_carry = false; // H flag is always false for RR
}
static inline void rl(uint8_t *value, struct flags *f) {
    uint8_t old_carry = f->carry ? 0x80 : 0x00; // Save old carry state
    f->carry = (*value & 0x80) != 0; // Set carry flag based on bit 7
    *value = (*value << 1) | (old_carry >> 7); // Rotate left and set new value
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for RL
    f->half_carry = false; // H flag is always false for RL
}
static inline void rrc(uint8_t *value, struct flags *f) {
    f->carry = (*value & 0x01) != 0; // Set carry flag based on bit 0
    *value = (*value >> 1) | ((*value & 0x01) << 7); // Rotate right with carry
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for RRC
    f->half_carry = false; // H flag is always false for RRC
}
static inline void rlc(uint8_t *value, struct flags *f) {
    f->carry = (*value & 0x80) != 0; // Set carry flag based on bit 7
    *value = (*value << 1) | ((*value & 0x80) >> 7); // Rotate left with carry
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for RLC
    f->half_carry = false; // H flag is always false for RLC
}
static inline void sra(uint8_t *value, struct flags *f) {
    f->carry = (*value & 0x01) != 0; // Set carry flag based on bit 0
    *value = (*value >> 1) | ((*value & 0x80)); // Shift right, keep sign bit
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for SRA
    f->half_carry = false; // H flag is always false for SRA
}
static inline void sla(uint8_t *value, struct flags *f) {
    f->carry = (*value & 0x80) != 0; // Set carry flag based on bit 7
    *value = (*value << 1); // Shift left
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for SLA
    f->half_carry = false; // H flag is always false for SLA
}
static inline void swap(uint8_t *value, struct flags *f) {
    *value = ((*value & 0x0F) << 4) | ((*value & 0xF0) >> 4); // Swap nibbles
    f->zero = (*value == 0); // Update zero flag
    f->subtraction = false; // N flag is always false for SWAP
    f->half_carry = false; // H flag is always false for SWAP
    f->carry = false; // C flag is always false for SWAP
}

struct MemoryBus {
    uint8_t *memory;
    size_t size;
};

struct CPU {
    struct registers regs;
    uint16_t pc; // Program Counter
    uint16_t sp; // Stack Pointer
    struct MemoryBus bus;
    struct flags f; // Flags register
};

struct Instruction {
    uint8_t opcode;
    const char *mnemonic; // Instruction mnemonic (e.g., "ADD", "SUB")
    void (*execute)(struct CPU *cpu);
};



static inline uint8_t read_byte(struct MemoryBus *bus, uint16_t address) {
    if (address < bus->size) {
        return bus->memory[address];
    }
    fprintf(stderr, "Memory read out of bounds at address: 0x%04X\n", address);
    return 0; // Return 0 or handle error appropriately
}

// two possible ways to do execute
//  1. create helper functions for each opcode
//  2. give each function the same parameters and use a function pointer

struct Instruction instructions[] = {
    {0x00, "NOP", NULL}, // No operation
    {0x01, "LD BC,nn", NULL}, // Load immediate value into BC
    {0x02, "LD (BC),A", NULL}, // Load A into memory at address in BC
    {0x03, "INC BC", NULL}, // Increment BC
    {0x04, "INC B", NULL}, // Increment B
    {0x05, "DEC B", NULL}, // Decrement B
    {0x06, "LD B,n", NULL}, // Load immediate value into B
    {0x07, "RLCA", NULL}, // Rotate A left through carry
    // Add more instructions as needed...
};

struct Instruction* opcode_table[256] = { NULL };

// Initialize the opcode table with instruction pointers for fast lookup
static inline void init_opcode_table() {
    size_t count = sizeof(instructions) / sizeof(struct Instruction);
    for (size_t i = 0; i < count; i++) {
        opcode_table[instructions[i].opcode] = &instructions[i];
    }
}

static inline void step(struct CPU* cpu) {
    uint8_t opcode = read_byte(&cpu->bus, cpu->pc++);
    if (opcode == 0xCB) {
        opcode = read_byte(&cpu->bus, cpu->pc++);
    }
    struct Instruction* instr = opcode_table[opcode];

    if (instr) {
        if (instr->execute) {
            instr->execute(cpu);
        } else {
            printf("Executing: %s (no-op function)\n", instr->mnemonic);
        }
    } else {
        fprintf(stderr, "Unknown opcode: 0x%02X at PC: 0x%04X\n", opcode, cpu->pc - 1);
    }
}



int main() {
    struct registers regs = {0};
    set_virtual(&regs.a, &regs.f, 0x1234);

    return 0;
}
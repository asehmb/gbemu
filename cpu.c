
#include "cpu.h"
#include <stdbool.h>


static void cpu_init(struct CPU *cpu, struct MemoryBus *bus) {
    cpu->regs.a = 0x00; // A register starts with 0x00
    cpu->regs.b = 0x00; // B register starts with 0x00
    cpu->regs.c = 0x00; // C register starts with 0x00
    cpu->regs.d = 0x00; // D register starts with 0x00
    cpu->regs.e = 0x00; // E register starts with 0x00
    cpu->regs.f = FLAG_ZERO; // F register starts with zero flag set
    cpu->regs.hl = 0x00; // HL register starts with 0x00

    cpu->cycles = 0; // Initialize cycles to 0

    cpu->pc = 0x0000; // Program Counter starts at 0x00
    cpu->sp = 0x0000; // Stack Pointer starts at 0x00

    cpu->bus = *bus;
    cpu->f.zero = true;
    cpu->f.subtraction = false;
    cpu->f.half_carry = false;
    cpu->f.carry = false;

    cpu->halted = false;
    cpu->ime = false;
}


static void step_cpu(struct CPU *cpu) {
    cpu->cycles = 4;
    if (cpu->halted) {
        // If CPU is halted, just return without executing an instruction
        return;
    }

    uint8_t opcode = cpu->bus.memory[cpu->pc++];
    exec_inst(cpu, opcode);

}



static inline void cpu_interrupt_jump(struct CPU *cpu, uint16_t vector) {
    // Push PC onto stack
    cpu->sp -= 2;
    WRITE_BYTE(cpu, cpu->sp, cpu->pc & 0xFF);
    WRITE_BYTE(cpu, cpu->sp + 1, cpu->pc >> 8);
    cpu->pc = vector;
    cpu->ime = false; // Disable IME until EI
    cpu->halted = false; // Resume CPU if halted
}

void cpu_handle_interrupts(struct CPU *cpu) {
    if (cpu->ime) {
        // Check if any interrupts are enabled
        uint8_t interrupt_flags = cpu->bus.memory[0xFFFF]; // Read interrupt flags from memory
        if (interrupt_flags & 0x01) { // V-Blank Interrupt
            cpu_interrupt_jump(cpu, 0x0040);
            interrupt_flags &= ~0x01; // Clear the V-Blank flag
        } else if (interrupt_flags & 0x02) { // LCDC Interrupt
            cpu_interrupt_jump(cpu, 0x0048);
            interrupt_flags &= ~0x02; // Clear the LCDC flag
        } else if (interrupt_flags & 0x04) { // Timer Interrupt
            cpu_interrupt_jump(cpu, 0x0050);
            interrupt_flags &= ~0x04; // Clear the Timer flag
        } else if (interrupt_flags & 0x08) { // Serial Interrupt
            cpu_interrupt_jump(cpu, 0x0058);
            interrupt_flags &= ~0x08; // Clear the Serial flag
        } else if (interrupt_flags & 0x10) { // Joypad Interrupt
            cpu_interrupt_jump(cpu, 0x0060);
            interrupt_flags &= ~0x10; // Clear the Joypad flag
        }
        cpu->bus.memory[0xFFFF] = interrupt_flags; // Write back updated flags
    }
}





static void exec_inst(struct CPU *cpu, uint8_t opcode) {
    switch (opcode) {
        case 0x00: // NOP
            break;
        case 0x01: // LD BC,nn
            SET_BC(cpu, (cpu->bus.memory[cpu->pc] | (cpu->bus.memory[cpu->pc + 1] << 8)));
            cpu->pc += 2; // Increment PC by 2 to skip the immediate value
            cpu->cycles += 8; // LD BC,nn takes 12 cycles
            break;
        case 0x02: // LD (BC),A
            cpu->bus.memory[GET_BC(cpu)] = cpu->regs.a;
            cpu->cycles += 4; // LD (BC),A takes 8 cycles
            break;
        case 0x03: // INC BC
            SET_BC(cpu, INC(GET_BC(cpu)));
            cpu->cycles += 4; // INC BC takes 8 cycles
            break;
        case 0x04: // INC B
            cpu->regs.b = INC(cpu->regs.b);
            cpu->f.zero = (cpu->regs.b == 0);
            cpu->f.half_carry = ((cpu->regs.b - 1) & 0x0F) == 0x0F; // Check half carry
            cpu->f.subtraction = false; // N flag is always false for INC
            cpu->cycles += 4; // INC B takes 8 cycles
            break;
        case 0x05: // DEC B
            cpu->regs.b = DEC(cpu->regs.b);
            cpu->f.zero = (cpu->regs.b == 0);
            cpu->f.half_carry = ((cpu->regs.b + 1) & 0x0F) == 0x00; // Check half carry
            cpu->f.subtraction = true; // N flag is set for DEC
            break;
        case 0x06: // LD B,n
            cpu->regs.b = cpu->bus.memory[cpu->pc++];

            cpu->cycles += 4; // LD B,n takes 12 cycles
            break;
        case 0x07: // RLCA
            {
                bool carry = (cpu->regs.a & 0x80) != 0; // Check if bit 7 is set
                cpu->regs.a = (cpu->regs.a << 1) | carry; // Rotate left
                cpu->f.zero = false; // Z flag is always false for RLCA
                cpu->f.subtraction = false; // N flag is always false for RLCA
                cpu->f.half_carry = false; // H flag is always false for RLCA
                cpu->f.carry = carry; // C flag is set if bit 7 was set
            }
            break;
        case 0x08: // LD (nn),SP
            {
                uint16_t address = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2; // Increment PC by 2 to skip the immediate value
                WRITE_WORD(cpu, address, cpu->sp);
                cpu->cycles += 16; // LD (nn),SP takes 20 cycles
            }
            break;
        case 0x09: // ADD HL,BC
            {
                uint16_t hl = cpu->regs.hl;
                uint16_t bc = GET_BC(cpu);
                uint32_t result = hl + bc;
                cpu->f.carry = (result > 0xFFFF); // Set C flag if overflow
                cpu->f.half_carry = ((hl & 0xFFF) + (bc & 0xFFF)) > 0xFFF; // Check half carry
                cpu->f.subtraction = false; // N flag is always false for ADD
                cpu->regs.hl = result & 0xFFFF; // Store the result in HL
                cpu->cycles += 4;

            }
            break;
        case 0x0A: // LD A,(BC)
            cpu->regs.a = cpu->bus.memory[GET_BC(cpu)];
            cpu->cycles += 4;

            break;
        case 0x0B: // DEC BC
            SET_BC(cpu, DEC(GET_BC(cpu)));
            cpu->cycles += 4;

            break;
        case 0x0C: // INC C
            cpu->regs.c = INC(cpu->regs.c);
            cpu->f.zero = (cpu->regs.c == 0);
            cpu->f.half_carry = ((cpu->regs.c - 1) & 0x0F) == 0x0F; // Check half carry
            cpu->f.subtraction = false; // N flag is always false for INC
            break;
        case 0x0D: // DEC C
            cpu->regs.c = DEC(cpu->regs.c);
            cpu->f.zero = (cpu->regs.c == 0);
            cpu->f.half_carry = ((cpu->regs.c + 1) & 0x0F) == 0x00; // Check half carry
            cpu->f.subtraction = true; // N flag is set for DEC
            break;
        case 0x0E: // LD C,n
            cpu->regs.c = cpu->bus.memory[cpu->pc++];
            cpu->cycles += 4;

            break;
        case 0x0F: // RRCA
            {
                bool carry = (cpu->regs.a & 0x01) != 0; // Check if bit 0 is set
                cpu->regs.a = (cpu->regs.a >> 1) | (carry << 7); // Rotate right
                cpu->f.zero = false; // Z flag is always false for RRCA
                cpu->f.subtraction = false; // N flag is always false for RRCA
                cpu->f.half_carry = false; // H flag is always false for RRCA
                cpu->f.carry = carry; // C flag is set if bit 0 was set
            }
            break;
        case 0x10: // STOP
            cpu->halted = true; // Set halted state
            break;
        case 0x11: // LD DE,nn
            SET_DE(cpu, (cpu->bus.memory[cpu->pc] | (cpu->bus.memory[cpu->pc + 1] << 8)));
            cpu->pc += 2; // Increment PC by 2 to skip the immediate value
            cpu->cycles += 8; // LD DE,nn takes 12 cycles
            break;
        case 0x12: // LD (DE),A
            cpu->bus.memory[GET_DE(cpu)] = cpu->regs.a;
            cpu->cycles += 4;

            break;
        case 0x13: // INC DE
            SET_DE(cpu, INC(GET_DE(cpu)));
            cpu->cycles += 4;

            break;
        case 0x14: // INC D
            cpu->regs.d = INC(cpu->regs.d);
            cpu->f.zero = (cpu->regs.d == 0);
            cpu->f.half_carry = ((cpu->regs.d - 1) & 0x0F) == 0x0F; // Check half carry
            cpu->f.subtraction = false; // N flag is always false for INC
            break;
        case 0x15: // DEC D
            cpu->regs.d = DEC(cpu->regs.d);
            cpu->f.zero = (cpu->regs.d == 0);
            cpu->f.half_carry = ((cpu->regs.d + 1) & 0x0F) == 0x00;
            cpu->f.subtraction = true;
            break;

        case 0x16: // LD D,n
            cpu->regs.d = cpu->bus.memory[cpu->pc++];
            cpu->cycles += 4;
            break;

        case 0x17: // RLA
            {
                bool carry_in = cpu->f.carry;
                bool carry_out = (cpu->regs.a & 0x80) != 0;
                cpu->regs.a = (cpu->regs.a << 1) | (carry_in ? 1 : 0);
                cpu->f.carry = carry_out;
                cpu->f.zero = false;
                cpu->f.subtraction = false;
                cpu->f.half_carry = false;
            }
            break;

        case 0x18: // JR n
            {
                int8_t offset = (int8_t)cpu->bus.memory[cpu->pc++];
                cpu->pc += offset;
                cpu->cycles += 8; // JR takes 12 cycles
            }
            break;

        case 0x19: // ADD HL,DE
            {
                uint16_t hl = cpu->regs.hl;
                uint16_t de = GET_DE(cpu);
                uint32_t result = hl + de;
                cpu->f.carry = (result > 0xFFFF);
                cpu->f.half_carry = ((hl & 0xFFF) + (de & 0xFFF)) > 0xFFF;
                cpu->f.subtraction = false;
                cpu->regs.hl = result & 0xFFFF;
                cpu->cycles += 4; // ADD HL,DE takes 8 cycles
            }
            break;

        case 0x1A: // LD A,(DE)
            cpu->regs.a = cpu->bus.memory[GET_DE(cpu)];
            cpu->cycles += 4;
            break;

        case 0x1B: // DEC DE
            SET_DE(cpu, DEC(GET_DE(cpu)));
            cpu->cycles += 4;
            break;

        case 0x1C: // INC E
            cpu->regs.e = INC(cpu->regs.e);
            cpu->f.zero = (cpu->regs.e == 0);
            cpu->f.half_carry = ((cpu->regs.e - 1) & 0x0F) == 0x0F;
            cpu->f.subtraction = false;
            break;

        case 0x1D: // DEC E
            cpu->regs.e = DEC(cpu->regs.e);
            cpu->f.zero = (cpu->regs.e == 0);
            cpu->f.half_carry = ((cpu->regs.e + 1) & 0x0F) == 0x00;
            cpu->f.subtraction = true;
            break;

        case 0x1E: // LD E,n
            cpu->regs.e = cpu->bus.memory[cpu->pc++];
            cpu->cycles += 4;
            break;

        case 0x1F: // RRA
            {
                bool carry_in = cpu->f.carry;
                bool carry_out = (cpu->regs.a & 0x01) != 0;
                cpu->regs.a = (cpu->regs.a >> 1) | (carry_in ? 0x80 : 0);
                cpu->f.carry = carry_out;
                cpu->f.zero = false;
                cpu->f.subtraction = false;
                cpu->f.half_carry = false;
            }
            break;

        case 0x20: // JR NZ,n
            {
                int8_t offset = (int8_t)cpu->bus.memory[cpu->pc++];
                if (!cpu->f.zero) {
                    cpu->pc += offset;
                    cpu->cycles += 8; // JR NZ,n takes 12 cycles if taken
                } else {
                    cpu->cycles += 4; // JR NZ,n takes 8 cycles if not taken
                }
            }
            break;

        case 0x21: // LD HL,nn
            cpu->regs.hl = (cpu->bus.memory[cpu->pc] | (cpu->bus.memory[cpu->pc + 1] << 8));
            cpu->pc += 2;
            cpu->cycles += 8; // LD HL,nn takes 12 cycles
            break;

        case 0x22: // LD (HL+),A
            cpu->bus.memory[cpu->regs.hl++] = cpu->regs.a;
            cpu->cycles += 4;
            break;

        case 0x23: // INC HL
            cpu->regs.hl = INC(cpu->regs.hl);
            cpu->cycles += 4;
            break;

        case 0x24: // INC H
            SET_H(cpu, INC(GET_H(cpu)));
            cpu->f.zero = (GET_H(cpu) == 0);
            cpu->f.half_carry = ((GET_H(cpu) - 1) & 0x0F) == 0x0F; // Check half carry
            cpu->f.subtraction = false; // N flag is always false for INC
            break;
        case 0x25: // DEC H
            SET_H(cpu, DEC(GET_H(cpu)));
            cpu->f.zero = (GET_H(cpu) == 0);
            cpu->f.half_carry = ((GET_H(cpu) + 1) & 0x0F) == 0x00; // Check half carry
            cpu->f.subtraction = true; // N flag is set for DEC
            break;

        case 0x26: // LD H,n
            SET_H(cpu, cpu->bus.memory[cpu->pc++]);
            cpu->cycles += 4;
            break;

        case 0x27: // DAA
            {
                uint8_t a = cpu->regs.a;
                bool carry = cpu->f.carry;
                bool half_carry = cpu->f.half_carry;
                bool subtraction = cpu->f.subtraction;

                uint8_t correction = 0;

                if (!subtraction) {
                    if (half_carry || (a & 0x0F) > 9) {
                        correction |= 0x06;
                    }
                    if (carry || a > 0x99) {
                        correction |= 0x60;
                        carry = true;
                    }
                    a += correction;
                } else {
                    if (half_carry) correction |= 0x06;
                    if (carry) correction |= 0x60;
                    a -= correction;
                }

                cpu->regs.a = a;
                cpu->f.zero = (a == 0);
                cpu->f.half_carry = false;
                cpu->f.carry = carry;
            }
            break;

        case 0x28: // JR Z,n
            {
                int8_t offset = (int8_t)cpu->bus.memory[cpu->pc++];
                if (cpu->f.zero) {
                    cpu->pc += offset;
                    cpu->cycles += 8; // JR Z,n takes 8 cycles if taken
                } else {
                    cpu->cycles += 4; // JR Z,n takes 4 cycles if not taken
                }
            }
            break;

        case 0x29: // ADD HL,HL
            {
                uint16_t hl = cpu->regs.hl;
                uint32_t result = hl + hl;
                cpu->f.carry = (result > 0xFFFF);
                cpu->f.half_carry = ((hl & 0xFFF) + (hl & 0xFFF)) > 0xFFF;
                cpu->f.subtraction = false;
                cpu->regs.hl = result & 0xFFFF;
                cpu->cycles += 4;
            }
            break;

        case 0x2A: // LD A,(HL+)
            cpu->regs.a = cpu->bus.memory[cpu->regs.hl++];
            cpu->cycles += 4;
            break;

        case 0x2B: // DEC HL
            cpu->regs.hl = DEC(cpu->regs.hl);
            cpu->cycles += 4;
            break;

        case 0x2C: // INC L
            SET_L(cpu, GET_L(cpu) + 1);
            cpu->f.zero = (GET_L(cpu) == 0);
            cpu->f.half_carry = ((GET_L(cpu) - 1) & 0x0F) == 0x0F; // Check half carry
            cpu->f.subtraction = false; // N flag is always false for INC
            break;

        case 0x2D: // DEC L
            SET_L(cpu, DEC(GET_L(cpu)));
            cpu->f.zero = (GET_L(cpu) == 0);
            cpu->f.half_carry = ((GET_L(cpu) + 1) & 0x0F) == 0x00; // Check half carry
            cpu->f.subtraction = true; // N flag is set for DEC
            break;

        case 0x2E: // LD L,n
            SET_L(cpu, cpu->bus.memory[cpu->pc++]);
            cpu->cycles += 4;

            break;

        case 0x2F: // CPL (complement A)
            cpu->regs.a = ~cpu->regs.a;
            cpu->f.subtraction = true;
            cpu->f.half_carry = true;
            break;


        case 0x30: // JR NC, r8 (Jump relative if carry flag is 0)
        {
            int8_t offset = (int8_t)cpu->bus.memory[cpu->pc++];
            if (!cpu->f.carry) {
                cpu->pc += offset;
                cpu->cycles += 8; // JR NC, r8 takes 12 cycles if taken
            } else cpu->cycles += 4;
        }
        break;

        case 0x31: // LD SP, nn (Load immediate 16-bit into SP)
        {
            cpu->sp = cpu->bus.memory[cpu->pc] | (cpu->bus.memory[cpu->pc + 1] << 8);
            cpu->pc += 2;
            cpu->cycles += 8; // LD SP, nn takes 12 cycles
        }
        break;

        case 0x32: // LD (HL-), A (Store A into address HL, then decrement HL)
        {
            WRITE_BYTE(cpu, cpu->regs.hl, cpu->regs.a);
            cpu->regs.hl--;
            cpu->cycles += 4;
        }
        break;

        case 0x33: // INC SP
            cpu->sp++;
            cpu->cycles += 4;
            break;

        case 0x34: // INC (HL)
        {
            uint8_t val = READ_BYTE(cpu, cpu->regs.hl);
            val++;
            WRITE_BYTE(cpu, cpu->regs.hl, val);
            cpu->f.zero = (val == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = ((val & 0x0F) == 0);
            cpu->cycles += 8;
        }
        break;

        case 0x35: // DEC (HL)
        {
            uint8_t val = READ_BYTE(cpu, cpu->regs.hl);
            val--;
            WRITE_BYTE(cpu, cpu->regs.hl, val);
            cpu->f.zero = (val == 0);
            cpu->f.subtraction = true;
            cpu->f.half_carry = ((val & 0x0F) == 0x0F);
            cpu->cycles += 8; // DEC (HL) takes 12 cycles
        }
        break;

        case 0x36: // LD (HL), n
        {
            uint8_t value = cpu->bus.memory[cpu->pc++];
            WRITE_BYTE(cpu, cpu->regs.hl, value);
            cpu->cycles += 8; // LD (HL), n takes 12 cycles
        }
        break;

        case 0x37: // SCF (Set Carry Flag)
            cpu->f.carry = true;
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            break;

        case 0x38: // JR C, r8 (Jump relative if carry flag set)
        {
            int8_t offset = (int8_t)cpu->bus.memory[cpu->pc++];
            if (cpu->f.carry) {
                cpu->pc += offset;
                cpu->cycles += 8; // JR C, r8 takes 12 cycles if taken
            } else cpu->cycles += 4;
        }
        break;

        case 0x39: // ADD HL, SP
        {
            uint32_t result = cpu->regs.hl + cpu->sp;
            cpu->f.carry = (result > 0xFFFF);
            cpu->f.half_carry = ((cpu->regs.hl & 0xFFF) + (cpu->sp & 0xFFF)) > 0xFFF;
            cpu->f.subtraction = false;
            cpu->regs.hl = result & 0xFFFF;
            cpu->cycles += 4;
        }
        break;

        case 0x3A: // LD A, (HL-)
        {
            cpu->regs.a = READ_BYTE(cpu, cpu->regs.hl);
            cpu->regs.hl--;
            cpu->cycles += 4;
        }
        break;

        case 0x3B: // DEC SP
            cpu->sp--;
            cpu->cycles += 4;
            break;

        case 0x3C: // INC A
            cpu->regs.a++;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = ((cpu->regs.a & 0x0F) == 0);
            break;

        case 0x3D: // DEC A
            cpu->regs.a--;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = true;
            cpu->f.half_carry = ((cpu->regs.a & 0x0F) == 0x0F);
            break;

        case 0x3E: // LD A, n
            cpu->regs.a = cpu->bus.memory[cpu->pc++];
            cpu->cycles += 4;
            break;

        case 0x3F: // CCF (Complement Carry Flag)
            cpu->f.carry = !cpu->f.carry;
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            break;
        case 0x40: // LD B,B
            cpu->regs.b = cpu->regs.b;
            break;

        case 0x41: // LD B,C
            cpu->regs.b = cpu->regs.c;
            break;

        case 0x42: // LD B,D
            cpu->regs.b = cpu->regs.d;
            break;

        case 0x43: // LD B,E
            cpu->regs.b = cpu->regs.e;
            break;

        case 0x44: // LD B,H
            cpu->regs.b = GET_H(cpu);
            break;

        case 0x45: // LD B,L
            cpu->regs.b = GET_L(cpu);
            break;

        case 0x46: // LD B,(HL)
            cpu->regs.b = READ_BYTE(cpu, cpu->regs.hl);
            break;

        case 0x47: // LD B,A
            cpu->regs.b = cpu->regs.a;
            break;

        case 0x48: // LD C,B
            cpu->regs.c = cpu->regs.b;
            break;

        case 0x49: // LD C,C
            cpu->regs.c = cpu->regs.c;
            break;

        case 0x4A: // LD C,D
            cpu->regs.c = cpu->regs.d;
            break;

        case 0x4B: // LD C,E
            cpu->regs.c = cpu->regs.e;
            break;

        case 0x4C: // LD C,H
            cpu->regs.c = GET_H(cpu);
            break;

        case 0x4D: // LD C,L
            cpu->regs.c = GET_L(cpu);
            break;

        case 0x4E: // LD C,(HL)
            cpu->regs.c = READ_BYTE(cpu, cpu->regs.hl);
            cpu->cycles += 4;
            break;

        case 0x4F: // LD C,A
            cpu->regs.c = cpu->regs.a;
            break;

        case 0x50: // LD D,B
            cpu->regs.d = cpu->regs.b;
            // No flags affected
            break;

        case 0x51: // LD D,C
            cpu->regs.d = cpu->regs.c;
            // No flags affected
            break;

        case 0x52: // LD D,D
            // No flags affected
            break;

        case 0x53: // LD D,E
            cpu->regs.d = cpu->regs.e;
            // No flags affected
            break;

        case 0x54: // LD D,H
            cpu->regs.d = GET_H(cpu);
            // No flags affected
            break;

        case 0x55: // LD D,L
            cpu->regs.d = GET_L(cpu);
            // No flags affected
            break;

        case 0x56: // LD D,(HL)
            cpu->regs.d = READ_BYTE(cpu, cpu->regs.hl);
            // No flags affected
            break;

        case 0x57: // LD D,A
            cpu->regs.d = cpu->regs.a;
            // No flags affected
            break;

        case 0x58: // LD E,B
            cpu->regs.e = cpu->regs.b;
            // No flags affected
            break;

        case 0x59: // LD E,C
            cpu->regs.e = cpu->regs.c;
            // No flags affected
            break;

        case 0x5A: // LD E,D
            cpu->regs.e = cpu->regs.d;
            // No flags affected
            break;

        case 0x5B: // LD E,E
            // No flags affected
            break;

        case 0x5C: // LD E,H
            cpu->regs.e = GET_H(cpu);
            // No flags affected
            break;

        case 0x5D: // LD E,L
            cpu->regs.e = GET_L(cpu);
            // No flags affected
            break;

        case 0x5E: // LD E,(HL)
            cpu->regs.e = READ_BYTE(cpu, cpu->regs.hl);
            cpu->cycles += 4;
            break;

        case 0x5F: // LD E,A
            cpu->regs.e = cpu->regs.a;
            // No flags affected
            break;
        case 0x60: // LD H,B
            SET_H(cpu, cpu->regs.b);
            // No flags affected
            break;

        case 0x61: // LD H,C
            SET_H(cpu, cpu->regs.c);
            // No flags affected
            break;

        case 0x62: // LD H,D
            SET_H(cpu, cpu->regs.d);
            // No flags affected
            break;

        case 0x63: // LD H,E
            SET_H(cpu, cpu->regs.e);
            // No flags affected
            break;

        case 0x64: // LD H,H
            // No flags affected
            break;

        case 0x65: // LD H,L
            SET_H(cpu, GET_L(cpu));
            // No flags affected
            break;

        case 0x66: // LD H,(HL)
            SET_H(cpu, READ_BYTE(cpu, cpu->regs.hl));
            // No flags affected
            break;

        case 0x67: // LD H,A
            SET_H(cpu, cpu->regs.a);
            // No flags affected
            break;

        case 0x68: // LD L,B
            SET_L(cpu, cpu->regs.b);
            // No flags affected
            break;

        case 0x69: // LD L,C
            SET_L(cpu, cpu->regs.c);
            // No flags affected
            break;

        case 0x6A: // LD L,D
            SET_L(cpu, cpu->regs.d);
            // No flags affected
            break;

        case 0x6B: // LD L,E
            SET_L(cpu, cpu->regs.e);
            // No flags affected
            break;

        case 0x6C: // LD L,H
            SET_L(cpu, GET_H(cpu));
            // No flags affected
            break;

        case 0x6D: // LD L,L
            // No flags affected
            break;

        case 0x6E: // LD L,(HL)
            SET_L(cpu, READ_BYTE(cpu, cpu->regs.hl));
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x6F: // LD L,A
            SET_L(cpu, cpu->regs.a);
            // No flags affected
            break;

        case 0x70: // LD (HL),B
            WRITE_BYTE(cpu, cpu->regs.hl, cpu->regs.b);
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x71: // LD (HL),C
            WRITE_BYTE(cpu, cpu->regs.hl, cpu->regs.c);
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x72: // LD (HL),D
            WRITE_BYTE(cpu, cpu->regs.hl, cpu->regs.d);
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x73: // LD (HL),E
            WRITE_BYTE(cpu, cpu->regs.hl, cpu->regs.e);
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x74: // LD (HL),H
            WRITE_BYTE(cpu, cpu->regs.hl, GET_H(cpu));
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x75: // LD (HL),L
            WRITE_BYTE(cpu, cpu->regs.hl, GET_L(cpu));
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x76: // HALT
            cpu->halted = true;
            // No flags affected
            break;

        case 0x77: // LD (HL),A
            WRITE_BYTE(cpu, cpu->regs.hl, cpu->regs.a);
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x78: // LD A,B
            cpu->regs.a = cpu->regs.b;
            // No flags affected
            break;

        case 0x79: // LD A,C
            cpu->regs.a = cpu->regs.c;
            // No flags affected
            break;

        case 0x7A: // LD A,D
            cpu->regs.a = cpu->regs.d;
            // No flags affected
            break;

        case 0x7B: // LD A,E
            cpu->regs.a = cpu->regs.e;
            // No flags affected
            break;

        case 0x7C: // LD A,H
            cpu->regs.a = GET_H(cpu);
            // No flags affected
            break;

        case 0x7D: // LD A,L
            cpu->regs.a = GET_L(cpu);
            // No flags affected
            break;

        case 0x7E: // LD A,(HL)
            cpu->regs.a = READ_BYTE(cpu, cpu->regs.hl);
            // No flags affected
            cpu->cycles += 4;
            break;

        case 0x7F: // LD A,A
            // No flags affected
            break;

        case 0x80: // ADD A,B
            {
                uint16_t result = cpu->regs.a + cpu->regs.b;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.b & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x81: // ADD A,C
            {
                uint16_t result = cpu->regs.a + cpu->regs.c;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.c & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;
                case 0x82: // ADD A,D
            {
                uint16_t result = cpu->regs.a + cpu->regs.d;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.d & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x83: // ADD A,E
            {
                uint16_t result = cpu->regs.a + cpu->regs.e;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.e & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x84: // ADD A,H
            {
                uint16_t result = cpu->regs.a + GET_H(cpu);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (GET_H(cpu) & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x85: // ADD A,L
            {
                uint16_t result = cpu->regs.a + GET_L(cpu);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (GET_L(cpu) & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x86: // ADD A,(HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                uint16_t result = cpu->regs.a + value;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (value & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x87: // ADD A,A
            {
                uint16_t result = cpu->regs.a + cpu->regs.a;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.a & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x88: // ADC A,B
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + cpu->regs.b + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.b & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x89: // ADC A,C
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + cpu->regs.c + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.c & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x8A: // ADC A,D
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + cpu->regs.d + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.d & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x8B: // ADC A,E
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + cpu->regs.e + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.e & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;
                case 0x8C: // ADC A,H
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + GET_H(cpu) + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (GET_H(cpu) & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x8D: // ADC A,L
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + GET_L(cpu) + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (GET_L(cpu) & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x8E: // ADC A,(HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + value + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (value & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x8F: // ADC A,A
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a + cpu->regs.a + carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (cpu->regs.a & 0xF) + carry) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x90: // SUB B
            {
                uint16_t result = cpu->regs.a - cpu->regs.b;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.b & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.b);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x91: // SUB C
            {
                uint16_t result = cpu->regs.a - cpu->regs.c;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.c & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.c);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x92: // SUB D
            {
                uint16_t result = cpu->regs.a - cpu->regs.d;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.d & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.d);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x93: // SUB E
            {
                uint16_t result = cpu->regs.a - cpu->regs.e;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.e & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.e);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x94: // SUB H
            {
                uint16_t result = cpu->regs.a - GET_H(cpu);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (GET_H(cpu) & 0xF));
                cpu->f.carry = (cpu->regs.a < GET_H(cpu));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x95: // SUB L
            {
                uint16_t result = cpu->regs.a - GET_L(cpu);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (GET_L(cpu) & 0xF));
                cpu->f.carry = (cpu->regs.a < GET_L(cpu));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x96: // SUB (HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                uint16_t result = cpu->regs.a - value;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (value & 0xF));
                cpu->f.carry = (cpu->regs.a < value);
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x97: // SUB A
            {
                cpu->f.zero = 1;
                cpu->f.subtraction = true;
                cpu->f.half_carry = 0;
                cpu->f.carry = 0;
                cpu->regs.a = 0;
            }
            break;
                case 0x98: // SBC A,B
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - cpu->regs.b - carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((cpu->regs.b & 0xF) + carry));
                cpu->f.carry = (cpu->regs.a < (cpu->regs.b + carry));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x99: // SBC A,C
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - cpu->regs.c - carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((cpu->regs.c & 0xF) + carry));
                cpu->f.carry = (cpu->regs.a < (cpu->regs.c + carry));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x9A: // SBC A,D
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - cpu->regs.d - carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((cpu->regs.d & 0xF) + carry));
                cpu->f.carry = (cpu->regs.a < (cpu->regs.d + carry));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x9B: // SBC A,E
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - cpu->regs.e - carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((cpu->regs.e & 0xF) + carry));
                cpu->f.carry = (cpu->regs.a < (cpu->regs.e + carry));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x9C: // SBC A,H
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - GET_H(cpu) - carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((GET_H(cpu) & 0xF) + carry));
                cpu->f.carry = (cpu->regs.a < (GET_H(cpu) + carry));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x9D: // SBC A,L
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - GET_L(cpu) - carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((GET_L(cpu) & 0xF) + carry));
                cpu->f.carry = (cpu->regs.a < (GET_L(cpu) + carry));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x9E: // SBC A,(HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - value - carry;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((value & 0xF) + carry));
                cpu->f.carry = (cpu->regs.a < (value + carry));
                cpu->regs.a = result & 0xFF;
            }
            break;

        case 0x9F: // SBC A,A
            {
                uint16_t carry = cpu->f.carry ? 1 : 0;
                uint16_t result = cpu->regs.a - cpu->regs.a - carry;
                cpu->f.zero = 1;
                cpu->f.subtraction = true;
                cpu->f.half_carry = 0;
                cpu->f.carry = (carry != 0);
                cpu->regs.a = 0;
            }
            break;

        case 0xA0: // AND B
            {
                cpu->regs.a &= cpu->regs.b;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;

        case 0xA1: // AND C
            {
                cpu->regs.a &= cpu->regs.c;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;

        case 0xA2: // AND D
            {
                cpu->regs.a &= cpu->regs.d;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;

        case 0xA3: // AND E
            {
                cpu->regs.a &= cpu->regs.e;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;

        case 0xA4: // AND H
            {
                cpu->regs.a &= GET_H(cpu);
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;

        case 0xA5: // AND L
            {
                cpu->regs.a &= GET_L(cpu);
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;

        case 0xA6: // AND (HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                cpu->regs.a &= value;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;

        case 0xA7: // AND A
            {
                cpu->regs.a &= cpu->regs.a;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
            }
            break;
        case 0xA8: // XOR B
            cpu->regs.a ^= cpu->regs.b;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xA9: // XOR C
            cpu->regs.a ^= cpu->regs.c;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xAA: // XOR D
            cpu->regs.a ^= cpu->regs.d;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xAB: // XOR E
            cpu->regs.a ^= cpu->regs.e;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xAC: // XOR H
            cpu->regs.a ^= GET_H(cpu);
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xAD: // XOR L
            cpu->regs.a ^= GET_L(cpu);
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xAE: // XOR (HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                cpu->regs.a ^= value;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = false;
                cpu->f.carry = false;
            }
            break;

        case 0xAF: // XOR A
            cpu->regs.a ^= cpu->regs.a;
            cpu->f.zero = 1;
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB0: // OR B
            cpu->regs.a |= cpu->regs.b;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB1: // OR C
            cpu->regs.a |= cpu->regs.c;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB2: // OR D
            cpu->regs.a |= cpu->regs.d;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB3: // OR E
            cpu->regs.a |= cpu->regs.e;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB4: // OR H
            cpu->regs.a |= GET_H(cpu);
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB5: // OR L
            cpu->regs.a |= GET_L(cpu);
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB6: // OR (HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                cpu->regs.a |= value;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = false;
                cpu->f.carry = false;
            }
            break;

        case 0xB7: // OR A
            cpu->regs.a |= cpu->regs.a;
            cpu->f.zero = (cpu->regs.a == 0);
            cpu->f.subtraction = false;
            cpu->f.half_carry = false;
            cpu->f.carry = false;
            break;

        case 0xB8: // CP B
            {
                uint16_t result = cpu->regs.a - cpu->regs.b;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.b & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.b);
            }
            break;

        case 0xB9: // CP C
            {
                uint16_t result = cpu->regs.a - cpu->regs.c;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.c & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.c);
            }
            break;

        case 0xBA: // CP D
            {
                uint16_t result = cpu->regs.a - cpu->regs.d;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.d & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.d);
            }
            break;

        case 0xBB: // CP E
            {
                uint16_t result = cpu->regs.a - cpu->regs.e;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (cpu->regs.e & 0xF));
                cpu->f.carry = (cpu->regs.a < cpu->regs.e);
            }
            break;

        case 0xBC: // CP H
            {
                uint16_t result = cpu->regs.a - GET_H(cpu);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (GET_H(cpu) & 0xF));
                cpu->f.carry = (cpu->regs.a < GET_H(cpu));
            }
            break;

        case 0xBD: // CP L
            {
                uint16_t result = cpu->regs.a - GET_L(cpu);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (GET_L(cpu) & 0xF));
                cpu->f.carry = (cpu->regs.a < GET_L(cpu));
            }
            break;

        case 0xBE: // CP (HL)
            {
                uint8_t value = READ_BYTE(cpu, cpu->regs.hl);
                uint16_t result = cpu->regs.a - value;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (value & 0xF));
                cpu->f.carry = (cpu->regs.a < value);
            }
            break;

        case 0xBF: // CP A
            cpu->f.zero = 1;
            cpu->f.subtraction = true;
            cpu->f.half_carry = 0;
            cpu->f.carry = 0;
            break;
        case 0xC0: // RET NZ
            if (!cpu->f.zero) {
                cpu->pc = READ_WORD(cpu, cpu->sp);
                cpu->sp += 2;
                cpu->cycles += 16;
            } else {
                cpu->cycles +=4;
            }
            break;

        case 0xC1: // POP BC
            cpu->regs.c = READ_BYTE(cpu, cpu->sp++);
            cpu->regs.b = READ_BYTE(cpu, cpu->sp++);
            cpu->cycles += 8;
            break;

        case 0xC2: // JP NZ,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (!cpu->f.zero) {
                    cpu->pc = addr;
                    cpu->cycles += 12;
                } else cpu->cycles += 8;
            }
            break;

        case 0xC3: // JP nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc = addr;
                cpu->cycles += 12;
            }
            break;

        case 0xC4: // CALL NZ,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (!cpu->f.zero) {
                    cpu->sp -= 2;
                    WRITE_WORD(cpu, cpu->sp, cpu->pc);
                    cpu->pc = addr;
                    cpu->cycles += 20;
                }
                else cpu->cycles += 8;
            }
            break;

        case 0xC5: // PUSH BC
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, (cpu->regs.b << 8) | cpu->regs.c);
            cpu->cycles += 12;
            break;

        case 0xC6: // ADD A,n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                uint16_t result = cpu->regs.a + value;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (value & 0xF)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
                cpu->cycles += 4;
            }
            break;

        case 0xC7: // RST 00H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x00;
            cpu->cycles += 12;
            break;

        case 0xC8: // RET Z
            if (cpu->f.zero) {
                cpu->pc = READ_WORD(cpu, cpu->sp);
                cpu->sp += 2;
                cpu->cycles += 16;
            } else cpu->cycles += 4;
            break;

        case 0xC9: // RET
            cpu->pc = READ_WORD(cpu, cpu->sp);
            cpu->sp += 2;
            cpu->cycles += 12;
            break;

        case 0xCA: // JP Z,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (cpu->f.zero) {
                    cpu->pc = addr;
                    cpu->cycles += 12;
                } else cpu->cycles += 8;
            }
            break;
        case 0xCB: // cb prefix
            // Handle CB-prefixed opcodes here
            _exec_cb_inst(cpu, opcode);
            cpu->pc++;
            break;

        case 0xCC: // CALL Z,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (cpu->f.zero) {
                    cpu->sp -= 2;
                    WRITE_WORD(cpu, cpu->sp, cpu->pc);
                    cpu->pc = addr;
                    cpu->cycles += 20;
                } else cpu->cycles += 8;
            }
            break;

        case 0xCD: // CALL nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                cpu->sp -= 2;
                WRITE_WORD(cpu, cpu->sp, cpu->pc);
                cpu->pc = addr;
                cpu->cycles += 20;
            }
            break;

        case 0xCE: // ADC A,n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                uint16_t result = cpu->regs.a + value + (cpu->f.carry ? 1 : 0);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) + (value & 0xF) + (cpu->f.carry ? 1 : 0)) > 0xF;
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
                cpu->cycles += 4;
            }
            break;

        case 0xCF: // RST 08H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x08;
            cpu->cycles += 12;
            break;
        case 0xD0: // RET NC
            if (!cpu->f.carry) {
                cpu->pc = READ_WORD(cpu, cpu->sp);
                cpu->sp += 2;
                cpu->cycles += 16;
            } else cpu->cycles += 4;
            break;

        case 0xD1: // POP DE
            cpu->regs.e = READ_BYTE(cpu, cpu->sp++);
            cpu->regs.d = READ_BYTE(cpu, cpu->sp++);
            cpu->cycles += 8;
            break;

        case 0xD2: // JP NC,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (!cpu->f.carry) {
                    cpu->pc = addr;
                    cpu->cycles += 12;
                } else cpu->cycles += 8;
            }
            break;

        case 0xD3: // (unofficial, usually NOP or illegal)
            break;

        case 0xD4: // CALL NC,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (!cpu->f.carry) {
                    cpu->sp -= 2;
                    WRITE_WORD(cpu, cpu->sp, cpu->pc);
                    cpu->pc = addr;
                    cpu->cycles += 20;
                } else cpu->cycles += 8;
            }
            break;

        case 0xD5: // PUSH DE
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, (cpu->regs.d << 8) | cpu->regs.e);
            cpu->cycles += 12;
            break;

        case 0xD6: // SUB n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                uint16_t result = cpu->regs.a - value;
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (value & 0xF));
                cpu->f.carry = (cpu->regs.a < value);
                cpu->regs.a = result & 0xFF;
                cpu->cycles += 4;
            }
            break;

        case 0xD7: // RST 10H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x10;
            cpu->cycles += 12;
            break;

        case 0xD8: // RET C
            if (cpu->f.carry) {
                cpu->pc = READ_WORD(cpu, cpu->sp);
                cpu->sp += 2;
                cpu->cycles += 16;
            } else cpu->cycles += 4;
            break;

        case 0xD9: // RETI
            cpu->pc = READ_WORD(cpu, cpu->sp);
            cpu->sp += 2;
            cpu->ime = true;  // Enable interrupts
            cpu->cycles += 12;
            break;

        case 0xDA: // JP C,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (cpu->f.carry) {
                    cpu->pc = addr;
                    cpu->cycles += 12;
                } else cpu->cycles += 8;
            }
            break;

        case 0xDB: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xDC: // CALL C,nn
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                if (cpu->f.carry) {
                    cpu->sp -= 2;
                    WRITE_WORD(cpu, cpu->sp, cpu->pc);
                    cpu->pc = addr;
                    cpu->cycles += 20;
                } else cpu->cycles += 8;
            }
            break;

        case 0xDD: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xDE: // SBC A,n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                uint16_t result = cpu->regs.a - value - (cpu->f.carry ? 1 : 0);
                cpu->f.zero = ((result & 0xFF) == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < ((value & 0xF) + (cpu->f.carry ? 1 : 0)));
                cpu->f.carry = (result > 0xFF);
                cpu->regs.a = result & 0xFF;
                cpu->cycles += 4;
            }
            break;

        case 0xDF: // RST 18H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x18;
            cpu->cycles += 12;
            break;
        case 0xE0: // LDH (n),A
            {
                uint8_t offset = cpu->bus.memory[cpu->pc++];
                WRITE_BYTE(cpu, 0xFF00 + offset, cpu->regs.a);
                cpu->cycles += 12;
            }
            break;

        case 0xE1: // POP HL
            SET_L(cpu, READ_BYTE(cpu, cpu->sp++));
            SET_H(cpu, READ_BYTE(cpu, cpu->sp++));
            cpu->cycles += 8;
            break;

        case 0xE2: // LD (C),A
            WRITE_BYTE(cpu, 0xFF00 + cpu->regs.c, cpu->regs.a);
            cpu->cycles += 4;
            break;

        case 0xE3: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xE4: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xE5: // PUSH HL
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->regs.hl);
            cpu->cycles += 12;
            break;

        case 0xE6: // AND n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                cpu->regs.a &= value;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = true;
                cpu->f.carry = false;
                cpu->cycles += 4;
            }
            break;

        case 0xE7: // RST 20H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x20;
            cpu->cycles += 12;
            break;

        case 0xE8: // ADD SP,n
            {
                int8_t offset = (int8_t)cpu->bus.memory[cpu->pc++];
                uint16_t result = cpu->sp + offset;
                cpu->f.zero = false;
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->sp & 0xF) + (offset & 0xF)) > 0xF;
                cpu->f.carry = ((cpu->sp & 0xFF) + (offset & 0xFF)) > 0xFF;
                cpu->sp = result;
                cpu->cycles += 12;
            }
            break;

        case 0xE9: // JP (HL)
            cpu->pc = cpu->regs.hl;
            cpu->cycles += 4;
            break;

        case 0xEA: // LD (nn),A
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                WRITE_BYTE(cpu, addr, cpu->regs.a);
                cpu->cycles += 12;
            }
            break;

        case 0xEB: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xEC: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xED: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xEE: // XOR n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                cpu->regs.a ^= value;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = false;
                cpu->f.carry = false;
                cpu->cycles += 4;
            }
            break;

        case 0xEF: // RST 28H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x28;
            cpu->cycles += 12;
            break;
        case 0xF0: // LDH A,(n)
            {
                uint8_t offset = cpu->bus.memory[cpu->pc++];
                cpu->regs.a = READ_BYTE(cpu, 0xFF00 + offset);
                cpu->cycles += 8;
            }
            break;

        case 0xF1: // POP AF
            {
                uint16_t af = READ_WORD(cpu, cpu->sp);
                cpu->sp += 2;
                SET_AF(cpu, af);
                cpu->cycles += 8;
            }
            break;

        case 0xF2: // LD A,(C)
            cpu->regs.a = READ_BYTE(cpu, 0xFF00 + cpu->regs.c);
            break;

        case 0xF3: // DI
            cpu->ime = false;  // Clear the interrupt master enable flag
            break;

        case 0xF4: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xF5: // PUSH AF
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, GET_AF(cpu));
            cpu->cycles += 12;
            break;

        case 0xF6: // OR n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                cpu->regs.a |= value;
                cpu->f.zero = (cpu->regs.a == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = false;
                cpu->f.carry = false;
                cpu->cycles += 4;
            }
            break;

        case 0xF7: // RST 30H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x30;
            cpu->cycles += 12;
            break;

        case 0xF8: // LD HL,SP+n
            {
                int8_t offset = (int8_t)cpu->bus.memory[cpu->pc++];
                uint16_t result = cpu->sp + offset;
                cpu->f.zero = false;
                cpu->f.subtraction = false;
                cpu->f.half_carry = ((cpu->sp & 0xF) + (offset & 0xF)) > 0xF;
                cpu->f.carry = ((cpu->sp & 0xFF) + (offset & 0xFF)) > 0xFF;
                cpu->regs.hl = result;
                cpu->cycles += 8;
            }
            break;

        case 0xF9: // LD SP,HL
            cpu->sp = cpu->regs.hl;
            cpu->cycles += 4;
            break;

        case 0xFA: // LD A,(nn)
            {
                uint16_t addr = READ_WORD(cpu, cpu->pc);
                cpu->pc += 2;
                cpu->regs.a = READ_BYTE(cpu, addr);
                cpu->cycles += 12;
            }
            break;

        case 0xFB: // EI
            // Enable interrupts (implementation depends on interrupt logic)
            cpu->ime = true;  // Set the interrupt master enable flag
            break;

        case 0xFC: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xFD: // (unofficial, usually NOP or illegal)
            // Technically an illegal opcode on the Game Boy
            break;

        case 0xFE: // CP n
            {
                uint8_t value = cpu->bus.memory[cpu->pc++];
                uint8_t result = cpu->regs.a - value;
                cpu->f.zero = (result == 0);
                cpu->f.subtraction = true;
                cpu->f.half_carry = ((cpu->regs.a & 0xF) < (value & 0xF));
                cpu->f.carry = (cpu->regs.a < value);
                cpu->cycles += 4;
            }
            break;

        case 0xFF: // RST 38H
            cpu->sp -= 2;
            WRITE_WORD(cpu, cpu->sp, cpu->pc);
            cpu->pc = 0x38;
            cpu->cycles += 12;
            break;

        default:
            fprintf(stderr, "Unknown opcode: 0x%02X\n", opcode);
            break;
    }
}

static void _exec_cb_inst(struct CPU *cpu, uint8_t opcode) {
    cpu->cycles = 8;
    uint8_t reg = (opcode & 0x0F); // lower nibble of the opcode
    uint8_t instruction = (opcode & 0xF0) >> 4; // upper nibble of the opcode
    uint8_t *reg_ptr = NULL;
    bool left = true;
    uint8_t is_h = 0; // 1 if h 2 if L 0 if some other register
    switch (reg) {
        case 0x00: reg_ptr = &cpu->regs.b; break;
        case 0x01: reg_ptr = &cpu->regs.c; break;
        case 0x02: reg_ptr = &cpu->regs.d; break;
        case 0x03: reg_ptr = &cpu->regs.e; break;
        case 0x04: reg_ptr = (uint8_t*)(&cpu->regs.hl) + 1; is_h = 1; break; // H is the high byte of HL
        case 0x05: reg_ptr = (uint8_t*)(&cpu->regs.hl); is_h = 2; break; // L is the low byte of HL
        case 0x06: reg_ptr = &cpu->bus.memory[cpu->regs.hl]; cpu->cycles = 16; break; // (HL) is a memory address
        case 0x07: reg_ptr = &cpu->regs.a; break;
        case 0x08: reg_ptr = &cpu->regs.b; left = false; break;
        case 0x09: reg_ptr = &cpu->regs.c; left = false; break;
        case 0x0A: reg_ptr = &cpu->regs.d; left = false; break;
        case 0x0B: reg_ptr = &cpu->regs.e; left = false; break;
        case 0x0C: reg_ptr = (uint8_t*)(&cpu->regs.hl) + 1; left = false; is_h = 1; break; // H is the high byte of HL
        case 0x0D: reg_ptr = (uint8_t*)(&cpu->regs.hl); left = false; is_h = 2; break; // L is the low byte of HL
        case 0x0E: reg_ptr =&cpu->bus.memory[cpu->regs.hl]; left = false; cpu->cycles = 16; break;
        case 0x0F: reg_ptr = &cpu->regs.a; left = false; break;
        default:
            fprintf(stderr, "Invalid CB register: 0x%02X\n", reg);
            return;
    }
    if (left) {
        switch (instruction) {
            case 0x00: // RLC
                uint8_t old_value = *reg_ptr;
                *reg_ptr = (*reg_ptr << 1) | (*reg_ptr >> 7);
                cpu->f.zero = (*reg_ptr == 0);
                cpu->f.subtraction = false;
                cpu->f.half_carry = false;
                cpu->f.carry = (old_value & 0x80) != 0;
                break;

            case 0x01: // RL
                {
                    uint8_t old_value = *reg_ptr;
                    uint8_t carry = cpu->f.carry ? 1 : 0;
                    *reg_ptr = (*reg_ptr << 1) | carry;
                    cpu->f.zero = (*reg_ptr == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = false;
                    cpu->f.carry = (old_value & 0x80) != 0;
                }
                break;
            case 0x02: // SLA
                {
                    *reg_ptr <<= 1;
                    cpu->f.zero = (*reg_ptr == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = false;
                    cpu->f.carry = (*reg_ptr & 0x80) != 0;
                }
                break;
            case 0x03: // SWAP
                {
                    uint8_t temp = *reg_ptr;
                    *reg_ptr = (*reg_ptr >> 4) | (*reg_ptr << 4);
                    cpu->f.zero = (*reg_ptr == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = false;
                    cpu->f.carry = false; // No carry for SWAP
                }
                break;
            case 0x04: // BIT 0, reg
                {
                    uint8_t bit = (*reg_ptr >> 0) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;

                }
                break;
            case 0x05: // BIT 2, reg
                {
                    uint8_t bit = (*reg_ptr >> 2) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;

                }
                break;
            case 0x06: // BIT 4, reg
                {
                    uint8_t bit = (*reg_ptr >> 4) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;

                }
                break;
            case 0x07: // BIT 6, reg
                {
                    uint8_t bit = (*reg_ptr >> 6) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;
                }
                break;
            case 0x08: // RES 0, reg
                {
                    *reg_ptr &= ~(1 << 0); // Clear bit 0

                }
                break;
            case 0x09: // RES 2, reg
                {
                    *reg_ptr &= ~(1 << 2); // Clear bit 2

                }
                break;
            case 0x0A: // RES 4, reg
                {
                    *reg_ptr &= ~(1 << 4); // Clear bit 4

                }
                break;
            case 0x0B: // RES 6, reg
                {
                    *reg_ptr &= ~(1 << 6); // Clear bit 6

                }
                break;
            case 0x0C: // SET 0, reg
                {
                    *reg_ptr |= (1 << 0); // Set bit 0

                }
                break;
            case 0x0D: // SET 2, reg
                {
                    *reg_ptr |= (1 << 2); // Set bit 2

                }
                break;
            case 0x0E: // SET 4, reg
                {
                    *reg_ptr |= (1 << 4); // Set bit 4

                }
                break;
            case 0x0F: // SET 6, reg
                {
                    *reg_ptr |= (1 << 6); // Set bit 6

                }
                break;
            default:
                fprintf(stderr, "Unknown CB instruction: 0x%02X\n", opcode);
                return;
        }
    } else {
        switch (instruction) {
            case 0x00: // RRC
                {
                    uint8_t old_value = *reg_ptr;
                    *reg_ptr = (*reg_ptr >> 1) | ((*reg_ptr & 0x01) << 7);
                    cpu->f.zero = (*reg_ptr == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = false;
                    cpu->f.carry = (old_value & 0x01) != 0; // Carry is the last bit
                }
                break;
            case 0x01: // RR
                {
                    uint8_t old_value = *reg_ptr;
                    uint8_t carry = cpu->f.carry ? 0x80 : 0; // Carry is the last bit
                    *reg_ptr = (*reg_ptr >> 1) | carry;
                    cpu->f.zero = (*reg_ptr == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = false;
                    cpu->f.carry = (old_value & 0x01) != 0; // Carry is the last bit
                }
                break;
            case 0x02: // SRA
                {
                    *reg_ptr >>= 1;
                    cpu->f.zero = (*reg_ptr == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = false;
                    cpu->f.carry = (*reg_ptr & 0x01) != 0; // Carry is the last bit
                }
                break;
            case 0x03: // SWAP
                {
                    uint8_t temp = *reg_ptr;
                    *reg_ptr = (*reg_ptr >> 4) | (*reg_ptr << 4);
                    cpu->f.zero = (*reg_ptr == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = false;
                    cpu->f.carry = false; // No carry for SWAP
                }
                break;
            case 0x04: // BIT 1, reg
                {
                    uint8_t bit = (*reg_ptr >> 1) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;
                }
                break;
            case 0x05: // BIT 3, reg
                {
                    uint8_t bit = (*reg_ptr >> 3) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;
                }
                break;
            case 0x06: // BIT 5, reg
                {
                    uint8_t bit = (*reg_ptr >> 5) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;
                }
                break;
            case 0x07: // BIT 7, reg
                {
                    uint8_t bit = (*reg_ptr >> 7) & 1;
                    cpu->f.zero = (bit == 0);
                    cpu->f.subtraction = false;
                    cpu->f.half_carry = true;
                }
                break;
            case 0x08: // RES 1, reg
                {
                    *reg_ptr &= ~(1 << 1); // Clear bit 1

                }
                break;
            case 0x09: // RES 3, reg
                {
                    *reg_ptr &= ~(1 << 3); // Clear bit 3

                }
                break;
            case 0x0A: // RES 5, reg
                {
                    *reg_ptr &= ~(1 << 5); // Clear bit 5

                }
                break;
            case 0x0B: // RES 7, reg
                {
                    *reg_ptr &= ~(1 << 7); // Clear bit 7

                }
                break;
            case 0x0C: // SET 1, reg
                {
                    *reg_ptr |= (1 << 1); // Set bit 1

                }
                break;
            case 0x0D: // SET 3, reg
                {
                    *reg_ptr |= (1 << 3); // Set bit 3

                }
                break;
            case 0x0E: // SET 5, reg
                {
                    *reg_ptr |= (1 << 5); // Set bit 5

                }
                break;
            case 0x0F: // SET 7, reg
                {
                    *reg_ptr |= (1 << 7); // Set bit 7

                }
                break;
            default:
                fprintf(stderr, "Unknown CB instruction: 0x%02X\n", opcode);
                return;
            }
        }

    // sync HL with h and l
    if (is_h !=0){
        if (is_h == 1) {
            // H was modified, update high byte of hl
            cpu->regs.hl = (cpu->regs.hl & 0x00FF) | (*reg_ptr << 8);
        } else if (is_h == 2) {
            // L was modified, update low byte of hl
            cpu->regs.hl = (cpu->regs.hl & 0xFF00) | (*reg_ptr);
        }
    }
    // Update the CPU state after executing the instruction
    return;
}

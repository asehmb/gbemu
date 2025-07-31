

#define ALLOW_ROM_WRITES
#include "../src/cpu.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

char *read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    return buffer;
}

int main(int argc, char *argv[]) {
    char* file_path;
    if (argc == 2) {
        file_path = argv[1];
    } else {
        fprintf(stderr, "Expected 1 file.\n");
        return 1;
    }
    char *json_data = read_file(file_path);
    if (!json_data) {
        perror("Failed to read JSON");
        return 1;
    }
    unsigned int op1 = 0, op2 = 0;
    int count = sscanf(file_path, "%x %x", &op1, &op2);

    if (count == 1) {
        // single-byte opcode
        printf("Opcode: 0x%02X\n", op1);
    } else if (count == 2) {
        // two-byte opcode (prefix + sub-opcode)
        printf("Prefix: 0x%02X, Opcode: 0x%02X\n", op1, op2);
    }
    struct CPU *cpu = malloc(sizeof(struct CPU));
    struct MemoryBus bus; // leave bus uninitialized for now
    cpu_init(cpu, &bus);

    cpu->bootrom_enabled = false;  // unless testing boot ROM

    cpu->bus.cart_ram = malloc(0x2000); // Cartridge RAM
    if (!cpu->bus.cart_ram) {
        fprintf(stderr, "Failed to allocate cartridge RAM\n");
        free(json_data);
        return 1;
    }
    memset(cpu->bus.cart_ram, 0, 0x2000);
    
    cpu->bus.ram_enabled = true;
    cpu->bus.rom_banking_toggle = true; // Disable ROM banking for testing
    cpu->bus.rom_banks = malloc(0x4000); // 1 ROM banks of 16KB
    if (!cpu->bus.rom_banks) {
        fprintf(stderr, "Failed to allocate ROM banks\n");
        free(cpu->bus.cart_ram);
        free(json_data);
        return 1;
    }
    memset(cpu->bus.rom_banks, 0, 0x4000);

    cpu->bus.mbc_type = 0; // simplest: no memory bank controller
    cpu->bus.ram_size = 0x2000;
    cpu->bus.current_ram_bank = 0;
    cpu->bus.current_rom_bank = 1; // Use bank 1 for testing
    cpu->bus.num_rom_banks = 2;
    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        printf("Error before: %s\n", cJSON_GetErrorPtr());
        return 1;
    }

    int json_size = cJSON_GetArraySize(root); // 1000
    for (int i = 0; i< json_size; i++) {
        if (i == 120){
            //breakpoint
        }
        if(i){
            // Save pointers before reinitializing
            uint8_t *saved_cart_ram = cpu->bus.cart_ram;
            uint8_t *saved_rom_banks = cpu->bus.rom_banks;
            
            cpu_init(cpu, &bus);               // reset CPU + memory
            cpu->bootrom_enabled = false;

            // Restore the allocated memory pointers
            cpu->bus.cart_ram = saved_cart_ram;
            cpu->bus.rom_banks = saved_rom_banks;

            // Allocate or clear RAM for this test
            memset(cpu->bus.rom, 0, 0x10000);
            if (cpu->bus.cart_ram) {
                memset(cpu->bus.cart_ram, 0, 0x2000);
            }
            if (cpu->bus.rom_banks) {
                memset(cpu->bus.rom_banks, 0, 0x4000);
            }
            
            // Reset banking settings for each test
            cpu->bus.ram_enabled = true;
            cpu->bus.rom_banking_toggle = true; // Disable ROM banking for testing
            cpu->bus.mbc_type = 0; // No MBC for testing
            cpu->bus.ram_size = 0x2000;
            cpu->bus.current_ram_bank = 0;
            cpu->bus.current_rom_bank = 1; // Use bank 1 for testing
            cpu->bus.num_rom_banks = 2;
        }
        cJSON *ind_item = cJSON_GetArrayItem(root, i);
        cJSON *name = cJSON_GetObjectItem(ind_item, "name");
        // initial values
        cJSON *initial = cJSON_GetObjectItem(ind_item, "initial");
        cJSON *init_pc = cJSON_GetObjectItem(initial, "pc");
        cJSON *init_sp = cJSON_GetObjectItem(initial, "sp");
        cJSON *init_a = cJSON_GetObjectItem(initial, "a");
        cJSON *init_f = cJSON_GetObjectItem(initial, "f");
        cJSON *init_b = cJSON_GetObjectItem(initial, "b");
        cJSON *init_c = cJSON_GetObjectItem(initial, "c");
        cJSON *init_d = cJSON_GetObjectItem(initial, "d");
        cJSON *init_e = cJSON_GetObjectItem(initial, "e");
        cJSON *init_h = cJSON_GetObjectItem(initial, "h");
        cJSON *init_l = cJSON_GetObjectItem(initial, "l");
        cJSON *init_ime = cJSON_GetObjectItem(initial, "ime");
        cJSON *init_ie = cJSON_GetObjectItem(initial, "ie");
        cJSON *init_ram = cJSON_GetObjectItem(initial, "ram");
        size_t ram_size = cJSON_GetArraySize(init_ram);
        // final values
        cJSON *final = cJSON_GetObjectItem(ind_item, "final");
        cJSON *final_pc = cJSON_GetObjectItem(final, "pc");
        cJSON *final_sp = cJSON_GetObjectItem(final, "sp");
        cJSON *final_a = cJSON_GetObjectItem(final, "a");
        cJSON *final_f = cJSON_GetObjectItem(final, "f");
        cJSON *final_b = cJSON_GetObjectItem(final, "b");
        cJSON *final_c = cJSON_GetObjectItem(final, "c");
        cJSON *final_d = cJSON_GetObjectItem(final, "d");
        cJSON *final_e = cJSON_GetObjectItem(final, "e");
        cJSON *final_h = cJSON_GetObjectItem(final, "h");
        cJSON *final_l = cJSON_GetObjectItem(final, "l");
        cJSON *final_ime = cJSON_GetObjectItem(final, "ime");
        if (final_pc->valueint == 0xFF01) {
            // breakpoint
        }

        // setup the cpu
        cpu->pc = init_pc->valueint;
        cpu->sp = init_sp->valueint;
        cpu->regs.a = init_a->valueint;
        cpu->regs.f = init_f->valueint;
        UNPACK_FLAGS(cpu, cpu->regs.f); // Unpack flags from F register
        cpu->regs.b = init_b->valueint;
        cpu->regs.c = init_c->valueint;
        cpu->regs.d = init_d->valueint;
        cpu->regs.e = init_e->valueint;
        SET_H(cpu, init_h->valueint);
        SET_L(cpu, init_l->valueint);
        cpu->ime = init_ime->valueint;
        cpu->bus.rom[0xFFFF] = init_ie->valueint;

        // load initial RAM/ROM values
        for (int j = 0; j < ram_size; j++) {
            cJSON *pair = cJSON_GetArrayItem(init_ram, j);
            int address = cJSON_GetArrayItem(pair, 0)->valueint;
            int value   = cJSON_GetArrayItem(pair, 1)->valueint;
            WRITE_BYTE(cpu, address, value);
        }

        // run opcode
        uint8_t opcode = READ_BYTE(cpu, cpu->pc);
        cpu->pc++; // Increment PC to point to the next instruction
        exec_inst(cpu, opcode);

        // check final state
        cpu->regs.f = PACK_FLAGS(cpu); // Update flags after execution
        int fail = 0;

        if (cpu->pc != final_pc->valueint) {
            fprintf(stderr, "PC mismatch: expected 0x%04X, got 0x%04X ",
                    final_pc->valueint, cpu->pc);
            fail = 1;
        }
        if (cpu->sp != final_sp->valueint) {
            fprintf(stderr, "SP mismatch: expected 0x%04X, got 0x%04X ",
                    final_sp->valueint, cpu->sp);
            fail = 1;
        }
        if (cpu->regs.a != final_a->valueint) {
            fprintf(stderr, "A mismatch: expected 0x%02X, got 0x%02X",
                    final_a->valueint, cpu->regs.a);
            fail = 1;
        }
        if (cpu->regs.f != final_f->valueint) {
            fprintf(stderr, "F mismatch: expected 0x%02X, got 0x%02X ",
                    final_f->valueint, cpu->regs.f);
            fail = 1;
        }
        if (cpu->regs.b != final_b->valueint) {
            fprintf(stderr, "B mismatch: expected 0x%02X, got 0x%02X ",
                    final_b->valueint, cpu->regs.b);
            fail = 1;
        }
        if (cpu->regs.c != final_c->valueint) {
            fprintf(stderr, "C mismatch: expected 0x%02X, got 0x%02X ",
                    final_c->valueint, cpu->regs.c);
            fail = 1;
        }
        if (cpu->regs.d != final_d->valueint) {
            fprintf(stderr, "D mismatch: expected 0x%02X, got 0x%02X ",
                    final_d->valueint, cpu->regs.d);
            fail = 1;
        }
        if (cpu->regs.e != final_e->valueint) {
            fprintf(stderr, "E mismatch: expected 0x%02X, got 0x%02X ",
                    final_e->valueint, cpu->regs.e);
            fail = 1;
        }
        if (GET_H(cpu) != final_h->valueint) {
            fprintf(stderr, "H mismatch: expected 0x%02X, got 0x%02X ",
                    final_h->valueint, GET_H(cpu));
            fail = 1;
        }
        if (GET_L(cpu) != final_l->valueint) {
            fprintf(stderr, "L mismatch: expected 0x%02X, got 0x%02X ",
                    final_l->valueint, GET_L(cpu));
            fail = 1;
        }
        if (cpu->ime != final_ime->valueint) {
            fprintf(stderr, "IME mismatch: expected %d, got %d ",
                    final_ime->valueint, cpu->ime);
            fail = 1;
        }
        if (fail) {
            fprintf(stderr, "Test failed for %s ", name->valuestring);
            fprintf(stderr, "at PC=0x%04X\n", final_pc->valueint);
        }
    }
    if (cpu->bus.cart_ram) {
        free(cpu->bus.cart_ram);
    }
    if (cpu->bus.rom_banks) {
        free(cpu->bus.rom_banks);
    }
    free(cpu);
    free(json_data);
    cJSON_Delete(root);
    return 0;

}
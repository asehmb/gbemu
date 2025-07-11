#include "timer.h"
#include "cpu.h"
#include <stdint.h>

void step_timer(struct Timer *timer, struct CPU *cpu) {
    // Update DIV register every 256 cycles (16384 Hz)
    cpu->divider_cycles += cpu->cycles;
    while (cpu->divider_cycles >= 256) {
        cpu->divider_cycles -= 256;
        cpu->bus.rom[0xFF04]++;
    }

    // Timer control register
    uint8_t tac = READ_BYTE(cpu, 0xFF07);
    if (tac & 0x04) {  // Timer enabled
        uint16_t freq;
        switch (tac & 0x03) {
            case 0: freq = 1024; break;     // 4096 Hz
            case 1: freq = 16; break;       // 262144 Hz
            case 2: freq = 64; break;       // 65536 Hz
            case 3: freq = 256; break;     // 16384 Hz
        }

        cpu->tima_counter += cpu->cycles;
        while (cpu->tima_counter >= freq) {
            cpu->tima_counter -= freq;
            uint8_t tima = READ_BYTE(cpu, 0xFF05);
            if (tima == 0xFF) {
                WRITE_BYTE(cpu, 0xFF05, READ_BYTE(cpu, 0xFF06)); // Reload with TMA
                WRITE_BYTE(cpu, 0xFF0F, READ_BYTE(cpu, 0xFF0F) | 0x04); // interrupt
            } else {
                WRITE_BYTE(cpu, 0xFF05, tima + 1);
            }
        }
    } else {
        // Reset tima_cycles when timer is disabled
        cpu->tima_counter = 0;
    }
}
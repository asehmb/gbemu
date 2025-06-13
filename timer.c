#include "timer.h"
#include "cpu.h"
#include <stdint.h>


void step_timer(struct Timer *timer, struct CPU *cpu) {
    // Always update DIV at 16384 Hz (every 256 cycles)
    cpu->divider_cycles += cpu->cycles;
    if (timer->divider_cycles >= 256) {
        timer->divider_cycles -= 256;
        uint8_t div = READ_BYTE(cpu, 0xFF04);
        cpu->bus.memory[0xFF04] = div + 1;
    }

    // Only update TIMA if timer is enabled
    uint8_t control = READ_BYTE(cpu, 0xFF07);
    if (control & 0x04) {
        uint16_t freq;
        switch (control & 0x03) {
            case 0: freq = 1024; break;     // 4096 Hz
            case 1: freq = 16; break;       // 262144 Hz
            case 2: freq = 64; break;       // 65536 Hz
            case 3: freq = 256; break;      // 16384 Hz
        }

        timer->main_clock -= cpu->cycles;
        while (timer->main_clock <= 0) {
            timer->main_clock += freq;
            uint8_t tima = READ_BYTE(cpu, 0xFF05);
            if (tima == 0xFF) {
                WRITE_BYTE(cpu, 0xFF05, READ_BYTE(cpu, 0xFF06)); // Load modulo
                WRITE_BYTE(cpu, 0xFF0F, READ_BYTE(cpu, 0xFF0F) | 0x04); // Request interrupt
            } else {
                WRITE_BYTE(cpu, 0xFF05, tima + 1);
            }
        }
    }
}

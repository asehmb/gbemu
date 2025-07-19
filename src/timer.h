

#ifndef _TIMER_H
#define _TIMER_H
#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

#define CLOCK_SPEED 4194304


struct Timer {
    // REGISTERS
    // CLOCKS
    uint32_t tima_cycles; // Main clock
    uint32_t div_cycles; // Cycles for divider increment
};

void step_timer(struct Timer *timer, struct CPU *cpu);


#endif
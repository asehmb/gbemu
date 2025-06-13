

#ifndef _TIMER_H
#define _TIMER_H
#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"

#define CLOCK_SPEED 4194304


struct Timer {
    // REGISTERS


    // CLOCKS
    uint32_t main_clock; // Main clock
    uint32_t sub_clock; // Sub clock
    uint32_t divider_cycles; // Cycles for divider increment
};

void step_timer(struct Timer *timer, struct CPU *cpu);


#endif
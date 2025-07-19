
#include "input.h"
#include "cpu.h"


// Write either 0x10 or 0x20 to JOYP: this will activate either bit 4 or 5, one of the column lines;
// Wait a few cycles for the row connections to propagate to JOYP;
// Check the low four bits of JOYP, to find which rows were active for this column

void joypad_update(struct CPU *cpu) {
    // Read the current state of the joypad
    uint8_t joypad_state = GB_JOYPAD(cpu);
    // Check if any buttons are pressed
    if (joypad_state != 0x0F) {
        // If any button is pressed, clear the interrupt flag
        cpu->bus.rom[0xFF0F] &= ~0x10; // Clear Joypad interrupt flag
    } else {
        // If no buttons are pressed, set the interrupt flag
        cpu->bus.rom[0xFF0F] |= 0x10; // Set Joypad interrupt flag
    }
}

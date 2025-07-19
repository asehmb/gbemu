
#ifndef _INPUT_H
#define _INPUT_H


// inputs will be written to by the joypad (the actual buttons dependent on hardware)
#define INPUT_JOYPAD 0xFF00 // Joypad register address
#define INPUT_JOYPAD_MASK 0x0F // Mask for joypad buttons
#define GB_DOWN(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x01) // Down button
#define GB_UP(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x02) // Up button
#define GB_LEFT(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x04) // Left button
#define GB_RIGHT(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x08) // Right button
#define GB_A(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x10) // A button
#define GB_B(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x20) // B button
#define GB_START(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x40) // Start button
#define GB_SELECT(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & 0x80) // Select button
#define GB_JOYPAD(cpu) ((cpu)->bus.rom[INPUT_JOYPAD] & INPUT_JOYPAD_MASK) // Read joypad state



#endif
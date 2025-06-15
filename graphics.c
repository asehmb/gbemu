#include "graphics.h"
#include <stdbool.h>
#include <stdint.h>

uint8_t inline read_vram(struct GPU *gpu, uint16_t addr) {
    if (addr < VRAM_BEGIN || addr > VRAM_END) {
        fprintf(stderr, "VRAM access out of bounds: 0x%04X\n", addr);
        return 0; // Return 0 for out of bounds access
    }
    if (gpu->mode == 3) return 0xFF;  // Block during rendering

    return gpu->vram[addr];
}

void inline write_vram(struct GPU *gpu, uint16_t addr, uint8_t value) {
    if (addr < VRAM_BEGIN || addr > VRAM_END) {
        fprintf(stderr, "VRAM access out of bounds: 0x%04X\n", addr);
        return; // Ignore out of bounds writes
    }
    gpu->vram[addr] = value; // vram is mapped to 0x8000-0x9FFF of the memory bus
}

void render_scanline(struct GPU *gpu, int line) {
    if (line < 0 || line >= SCREEN_HEIGHT) return;
    if (!(LCDC(gpu) & 0x80)) return;
    // Clear scanline to background color first
    uint8_t bg_color = (BGP(gpu) & 0x03); // Default color 0
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        gpu->framebuffer[line * SCREEN_WIDTH + x] = bg_color;
    }
    if ((LCDC(gpu) & 0x01) == 0x01) {
        // RENDER TILES
        render_tile(gpu);
    }
    if ((LCDC(gpu) & 0x02) == 0x02) {
        // RENDER SPRITES
        render_sprites(gpu);
    }

}



void step_gpu(struct GPU *gpu, int cycles) {
    gpu->mode_clock += cycles;

    switch (gpu->mode) {
        case 2: // OAM Search (80 cycles)
            if (gpu->mode_clock >= 80) {
                gpu->mode_clock -= 80;
                gpu->mode = 3; // Pixel Transfer
                // No STAT interrupt for mode 3
            }
            break;

        case 3: // Pixel Transfer (172 cycles)
            if (gpu->mode_clock >= 172) {
                gpu->mode_clock -= 172;
                gpu->mode = 0; // HBlank

                // Trigger HBlank STAT interrupt if enabled
                if (STAT(gpu) & 0x08) {
                    REQUEST_INTERRUPT(gpu, 0x02);
                }

                // Render scanline at the END of pixel transfer
                render_scanline(gpu, LY(gpu));
            }
            break;

        case 0: // HBlank (204 cycles)
            if (gpu->mode_clock >= 204) {
                gpu->mode_clock -= 204;
                LY(gpu)++;

                // LY==LYC comparison (critical for dmg-acid2)
                if (LY(gpu) == LYC(gpu)) {
                    STAT(gpu) |= 0x04; // Set coincidence flag
                    if (STAT(gpu) & 0x40) { // LYC interrupt enabled
                        REQUEST_INTERRUPT(gpu, 0x02);
                    }
                } else {
                    STAT(gpu) &= ~0x04; // Clear coincidence flag
                }

                if (LY(gpu) == 144) {
                    // Enter VBlank
                    gpu->mode = 1;
                    gpu->mode_clock = 0;
                    REQUEST_INTERRUPT(gpu, 0x01); // VBlank interrupt

                    // Trigger VBlank STAT interrupt if enabled
                    if (STAT(gpu) & 0x10) {
                        REQUEST_INTERRUPT(gpu, 0x02);
                    }

                    gpu->should_render = true;
                } else {
                    // Back to OAM Search
                    gpu->mode = 2;

                    // Trigger OAM STAT interrupt if enabled
                    if (STAT(gpu) & 0x20) {
                        REQUEST_INTERRUPT(gpu, 0x02);
                    }
                }
            }
            break;

        case 1: // VBlank (4560 cycles total - 10 lines)
            if (gpu->mode_clock >= 456) {
                gpu->mode_clock -= 456;
                LY(gpu)++;

                // LY==LYC comparison during VBlank
                if (LY(gpu) == LYC(gpu)) {
                    STAT(gpu) |= 0x04;
                    if (STAT(gpu) & 0x40) {
                        REQUEST_INTERRUPT(gpu, 0x02);
                    }
                } else {
                    STAT(gpu) &= ~0x04;
                }

                if (LY(gpu) > 153) {
                    LY(gpu) = 0;
                    gpu->mode = 2; // Back to OAM Search
                    gpu->window_line = 0; // Reset window line
                    // Trigger OAM STAT interrupt if enabled
                    if (STAT(gpu) & 0x20) {
                        REQUEST_INTERRUPT(gpu, 0x02);
                    }
                }
            }
            break;
    }

    // Update STAT mode bits (bits 0-1)
    STAT(gpu) = (STAT(gpu) & 0xFC) | (gpu->mode & 0x03);
}
/*
    LCDC (0xFF40) - LCD Control Register
    Bit 7 - LCD Display Enable (0=Off, 1=On)
    Bit 6 - Window Tile Map Display Select (0=9800-9BFF, 1=9C00-9FFF)
    Bit 5 - Window Display Enable (0=Off, 1=On)
    Bit 4 - BG & Window Tile Data Select (0=8800-97FF, 1=8000-8FFF)
    Bit 3 - BG Tile Map Display Select (0=9800-9BFF, 1=9C00-9FFF)
    Bit 2 - OBJ (Sprite) Size (0=8x8, 1=8x16)
    Bit 1 - OBJ (Sprite) Display Enable (0=Off, 1=On)
    Bit 0 - BG Display (for CGB see below) (0=Off, 1=On)
 */
void render_tile(struct GPU *gpu) {
    uint8_t lcdc = LCDC(gpu);
    uint8_t ly = LY(gpu);
    uint16_t tile_data;
    bool window_enabled = (lcdc & 0x20) != 0;

    uint8_t scx = SCX(gpu);
    uint8_t scy = SCY(gpu);
    uint8_t wx = WX(gpu) - 7;
    uint8_t wy = WY(gpu);

    uint16_t memory_region = (lcdc & 0x10) ? 0x8000 : 0x8800;
    bool use_signed_tiles = (lcdc & 0x10) == 0;
    bool window_rendered_this_line = false;


    for (int pixel = 0; pixel < SCREEN_WIDTH; pixel++) {
        bool using_window = window_enabled && (ly >= wy) && (wx <= pixel);
        if (using_window) window_rendered_this_line = true;

        tile_data = using_window
            ? ((lcdc & 0x40) ? 0x9C00 : 0x9800) // Window Tile Map
            : ((lcdc & 0x08) ? 0x9C00 : 0x9800); // BG Tile Map

        uint16_t tile_addr;
        uint8_t x_pos = using_window ? (pixel - wx) : (pixel + scx);
        uint8_t y_pos = using_window ? gpu->window_line : (ly + scy);


        uint8_t tile_index = read_vram(gpu, tile_data + (y_pos / 8) * 32 + (x_pos / 8));
        tile_addr = memory_region + (use_signed_tiles ? (int8_t)tile_index + 128 : tile_index)*16;
        /*
        pixel# = 1 2 3 4 5 6 7 8
        data 2 = 1 0 1 0 1 1 1 0
        data 1 = 0 0 1 1 0 1 0 1

        Pixel 1 colour id: 10
        Pixel 2 colour id: 00
        Pixel 3 colour id: 11
        Pixel 4 colour id: 01
        Pixel 5 colour id: 10
        Pixel 6 colour id: 11
        Pixel 7 colour id: 10
        Pixel 8 colour id: 01
        */

        uint8_t line_in_tile = y_pos % 8;
        uint8_t data2 = read_vram(gpu, tile_addr + line_in_tile * 2);
        uint8_t data1 = read_vram(gpu, tile_addr + line_in_tile * 2 + 1);
        uint8_t bit_index = 7 - (x_pos % 8);
        uint8_t color_index = ((data2 >> bit_index) & 1) << 1 |
                            ((data1 >> bit_index) & 1);

        gpu->framebuffer[ly * SCREEN_WIDTH + pixel] = color_index;

    }
    if (window_rendered_this_line) {
        gpu->window_line++;
    }
}



void render_sprites(struct GPU *gpu) {
    uint8_t lcdc = LCDC(gpu);
    bool use_8x16 = (lcdc & 0x04) != 0;

    for (int i = 0; i < 40; i++) {
        struct oam_entry *sprite = &gpu->oam_entries[i];

        // OAM format: (Y, X, Tile Index, Attributes)
        // Y and X are offset by 16 and 8 respectively
        int sprite_y = sprite->y - 16;
        int sprite_x = sprite->x - 8;

        if (sprite->y == 0 || sprite->x == 0) continue; // Off-screen
        if (LY(gpu) < sprite_y || LY(gpu) >= sprite_y + (use_8x16 ? 16 : 8)) continue; // Not on this scanline

        uint8_t tile_index = sprite->tile_index;
        uint8_t flags = sprite->flags;
        bool y_flip = flags & 0x40;
        bool x_flip = flags & 0x20;
        bool use_obp1 = flags & 0x10;

        // For 8x16 mode, the lower bit of tile_index is ignored
        if (use_8x16) tile_index &= 0xFE;

        // Determine line within tile
        int line = LY(gpu) - sprite_y;
        if (y_flip) {
            line = (use_8x16 ? 15 : 7) - line;
        }

        uint16_t tile_addr = 0x8000 + tile_index * 16 + line * 2;
        uint8_t tile_data1 = read_vram(gpu, tile_addr);
        uint8_t tile_data2 = read_vram(gpu, tile_addr + 1);

        for (int px = 0; px < 8; px++) {
            int screen_x = sprite_x + (x_flip ? (7 - px) : px);
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            uint8_t bit_index = 7 - px;
            uint8_t color_index =
                ((tile_data2 >> bit_index) & 1) << 1 |
                ((tile_data1 >> bit_index) & 1);

            // Color index 0 is transparent
            if (color_index == 0) continue;

            // Fetch color from OBP0 or OBP1
            uint8_t palette = use_obp1 ? OBP1(gpu) : OBP0(gpu);
            uint8_t final_color = (palette >> (color_index * 2)) & 0x03;

            // Write to framebuffer (you may want to respect BG priority here)
            gpu->framebuffer[LY(gpu) * SCREEN_WIDTH + screen_x] = final_color;
        }
    }
}



void dma_transfer(struct GPU *gpu, uint8_t value) {
    // DMA transfer source address = value << 8 (e.g., 0xXX00)
    uint16_t source = value << 8;

    // DMA copies 160 bytes (40 sprites * 4 bytes each)
    for (int i = 0; i < 160; i++) {
        uint8_t data = read_vram(gpu, source + i);
        // Write into OAM byte-wise (4 bytes per sprite)
        uint8_t sprite_index = i / 4;
        uint8_t byte_index = i % 4;

        switch (byte_index) {
            case 0: gpu->oam_entries[sprite_index].y = data; break;
            case 1: gpu->oam_entries[sprite_index].x = data; break;
            case 2: gpu->oam_entries[sprite_index].tile_index = data; break;
            case 3: gpu->oam_entries[sprite_index].flags = data; break;
        }
    }
}

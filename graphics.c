#include "graphics.h"

uint8_t read_vram(struct GPU *gpu, uint16_t addr) {
    if (addr < VRAM_BEGIN || addr > VRAM_END) {
        fprintf(stderr, "VRAM access out of bounds: 0x%04X\n", addr);
        return 0; // Return 0 for out of bounds access
    }
    return gpu->vram[addr - VRAM_BEGIN];
}

void write_vram(struct GPU *gpu, uint16_t addr, uint8_t value) {
    if (addr < VRAM_BEGIN || addr > VRAM_END) {
        fprintf(stderr, "VRAM access out of bounds: 0x%04X\n", addr);
        return; // Ignore out of bounds writes
    }

    size_t index = addr-VRAM_BEGIN;
    gpu->vram[index] = value;

    if (addr < 0x8000 || addr >= 0x9800) return; // Only update tile data range
    size_t normalized_index = index & 0xFFFE;

    uint8_t byte1 = gpu->vram[normalized_index];
    uint8_t byte2 = gpu->vram[normalized_index+1];

    size_t tile_index = index/ 16; // Each tile is 16 bytes
    size_t row_index = (normalized_index % 16) / 2;

    gpu->tiles[tile_index].data[row_index*2] = byte1;
    gpu->tiles[tile_index].data[row_index*2 +1] = byte2;


}

void render_tile_line(struct GPU *gpu, int tile_index, uint8_t palette) {
    // Render a single line of a tile to the framebuffer
    Tile *tile = &gpu->tiles[tile_index];
    uint8_t *framebuffer = gpu->framebuffer;

    for (int x = 0; x < 8; x++) {
        int pixel = (tile->data[LY(gpu) * 2] >> (7 - x)) & 1;
        pixel |= ((tile->data[LY(gpu) * 2 + 1] >> (7 - x)) & 1) << 1;

        // Apply palette
        uint8_t color = (palette >> (pixel * 2)) & 0x03;

        int screen_x = (SCX(gpu) * 8 + x) % SCREEN_WIDTH;
        int screen_y = (SCY(gpu)) * 8 + LY(gpu) % SCREEN_HEIGHT;

        if (screen_x < SCREEN_WIDTH && screen_y < SCREEN_HEIGHT) {
            framebuffer[screen_y * SCREEN_WIDTH + screen_x] = color;
        }
    }
}

void maybe_trigger_stat_interrupt(struct GPU *gpu, int mode) {
    uint8_t stat = STAT(gpu);

    // Mode flags in STAT register:
    // Bit 3: HBlank
    // Bit 4: VBlank
    // Bit 5: OAM
    uint8_t stat_interrupt_enabled = 0;

    switch (mode) {
        case 0: // HBlank
            stat_interrupt_enabled = (stat & 0x08);
            break;
        case 1: // VBlank
            stat_interrupt_enabled = (stat & 0x10);
            break;
        case 2: // OAM
            stat_interrupt_enabled = (stat & 0x20);
            break;
    }

    if (stat_interrupt_enabled) {
        REQUEST_INTERRUPT(gpu, 0x02); // STAT interrupt
    }
}


void step_gpu(struct GPU *gpu, int cycles) {
    gpu->mode_clock += cycles;

    switch (gpu->mode) {
        case 2: // OAM Search
            if (gpu->mode_clock >= 80) {
                gpu->mode_clock -= 80;
                gpu->mode = 3; // Pixel Transfer
                // Pixel Transfer doesn't trigger STAT interrupt.
            }
            break;

        case 3: // Pixel Transfer
            if (gpu->mode_clock >= 172) {
                gpu->mode_clock -= 172;
                gpu->mode = 0; // HBlank
                maybe_trigger_stat_interrupt(gpu, 0); // HBlank STAT interrupt

                // Render current scanline
                render_background(gpu);
                render_window(gpu);
                render_sprites(gpu);
            }
            break;

        case 0: // HBlank
            if (gpu->mode_clock >= 204) {
                gpu->mode_clock -= 204;
                LY(gpu)++;

                // LY==LYC check
                if (LY(gpu) == LYC(gpu)) {
                    STAT(gpu) |= 0x04; // Coincidence flag
                    if (STAT(gpu) & 0x40) {
                        REQUEST_INTERRUPT(gpu, 0x02); // STAT interrupt
                    }
                } else {
                    STAT(gpu) &= ~0x04;
                }

                if (LY(gpu) == 144) {
                    gpu->mode = 1; // VBlank
                    REQUEST_INTERRUPT(gpu, 0x01); // VBlank interrupt
                    maybe_trigger_stat_interrupt(gpu, 1); // VBlank STAT interrupt
                } else {
                    gpu->mode = 2; // OAM Search
                    maybe_trigger_stat_interrupt(gpu, 2); // OAM STAT interrupt
                }
            }
            break;

        case 1: // VBlank
            if (gpu->mode_clock >= 456) {
                gpu->mode_clock -= 456;
                LY(gpu)++;

                if (LY(gpu) > 153) {
                    LY(gpu) = 0;
                    gpu->mode = 2; // Back to OAM Search
                    maybe_trigger_stat_interrupt(gpu, 2); // OAM STAT interrupt

                    // LY==LYC check
                    if (LY(gpu) == LYC(gpu))  {
                        STAT(gpu) |= 0x04;
                        if (STAT(gpu) & 0x40) {
                            REQUEST_INTERRUPT(gpu, 0x02); // STAT interrupt
                        }
                    } else {
                        STAT(gpu) &= ~0x04;
                    }
                } else {
                    // LY==LYC check during VBlank
                    if (LY(gpu) == LYC(gpu))  {
                        STAT(gpu) |= 0x04;
                        if (STAT(gpu) & 0x40) {
                            REQUEST_INTERRUPT(gpu, 0x02); // STAT interrupt
                        }
                    } else {
                        STAT(gpu) &= ~0x04;
                    }
                }
            }
            break;
    }

    // Update STAT mode bits
    STAT(gpu) = (STAT(gpu) & 0xFC) | (gpu->mode & 0x03);
}


/*
    * bottom = (SCY+143) % 256
    * right = (SCX+159) % 256
    * top = SCY % 256
    * left = SCX % 256
 */

void render_background(struct GPU *gpu) {
    uint8_t lcdc = LCDC(gpu);
    if (!LCDC_LCD_ENABLE(lcdc)) return; // LCD is disabled

    int scy = SCY(gpu);
    int scx = SCX(gpu);

    // Select background tile map base address
    uint16_t bg_tile_map_base = (lcdc & 0x08) ? 0x9C00 : 0x9800;

    // Select tile data base address
    uint16_t tile_data_base;
    int signed_indexing;
    if (lcdc & 0x10) {
        tile_data_base = 0x8000;
        signed_indexing = 0;
    } else {
        tile_data_base = 0x8800;
        signed_indexing = 1;
    }

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int bg_y = (scy + y) & 0xFF; // wraparound
        int tile_y = bg_y / 8;
        int pixel_y = bg_y % 8;

        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int bg_x = (scx + x) & 0xFF; // wraparound
            int tile_x = bg_x / 8;
            int pixel_x = bg_x % 8;

            // Fetch tile index from background map
            uint16_t map_index = bg_tile_map_base + tile_y * 32 + tile_x;
            uint8_t tile_index = read_vram(gpu, map_index);

            // Handle signed vs unsigned tile index
            uint16_t tile_addr;
            if (signed_indexing) {
                tile_addr = 0x9000 + (int8_t)tile_index * 16;
            } else {
                tile_addr = 0x8000 + tile_index * 16;
            }

            // Fetch the tile row bytes
            uint8_t byte1 = read_vram(gpu, tile_addr + pixel_y * 2);
            uint8_t byte2 = read_vram(gpu, tile_addr + pixel_y * 2 + 1);

            // Get color
            uint8_t color = (byte1 >> (7 - pixel_x)) & 1;
            color |= ((byte2 >> (7 - pixel_x)) & 1) << 1;

            gpu->framebuffer[y * SCREEN_WIDTH + x] = color;
        }
    }
}


void render_window(struct GPU *gpu) {
    uint8_t lcdc = LCDC(gpu);
    if (!LCDC_WINDOW_ENABLE(lcdc)) return; // Window is disabled

    int wy = WY(gpu) - 16; // Window Y position (0-143)
    int wx = WX(gpu) - 7; // Game Boy offset correction

    // Select window tile map base address
    uint16_t win_tile_map_base = (lcdc & 0x40) ? 0x9C00 : 0x9800;

    // Select tile data base address
    uint16_t tile_data_base;
    int signed_indexing;
    if (lcdc & 0x10) {
        tile_data_base = 0x8000;
        signed_indexing = 0;
    } else {
        tile_data_base = 0x8800;
        signed_indexing = 1;
    }

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        if (y < wy) continue; // Above window

        int win_y = y - wy;
        int tile_y = win_y / 8;
        int pixel_y = win_y % 8;

        for (int x = 0; x < SCREEN_WIDTH; x++) {
            if (x < wx) continue; // Left of window

            int win_x = x - wx;
            int tile_x = win_x / 8;
            int pixel_x = win_x % 8;

            // Fetch tile index from window map
            uint16_t map_index = win_tile_map_base + tile_y * 32 + tile_x;
            uint8_t tile_index = read_vram(gpu, map_index);

            // Handle signed vs unsigned tile index
            uint16_t tile_addr;
            if (signed_indexing) {
                tile_addr = 0x9000 + (int8_t)tile_index * 16;
            } else {
                tile_addr = 0x8000 + tile_index * 16;
            }

            // Fetch the tile row bytes
            uint8_t byte1 = read_vram(gpu, tile_addr + pixel_y * 2);
            uint8_t byte2 = read_vram(gpu, tile_addr + pixel_y * 2 + 1);

            // Get color
            uint8_t color = (byte1 >> (7 - pixel_x)) & 1;
            color |= ((byte2 >> (7 - pixel_x)) & 1) << 1;

            gpu->framebuffer[y * SCREEN_WIDTH + x] = color;
        }
    }
}


void render_sprites(struct GPU *gpu) {
    uint8_t lcdc = LCDC(gpu);
    if (!LCDC_OBJ_ENABLE(lcdc)) return; // Sprites are disabled

    int sprite_height = (lcdc & 0x04) ? 16 : 8; // 8x16 or 8x8

    for (int i = 0; i < 40; i++) {
        struct oam_entry *sprite = &gpu->oam_entries[i];
        int sprite_y = sprite->y - 16; // Adjust Y coordinate
        int sprite_x = sprite->x - 8;  // Adjust X coordinate

        // Skip if sprite is out of screen bounds
        if (sprite_y >= SCREEN_HEIGHT || sprite_x >= SCREEN_WIDTH) continue;
        if (sprite_y + sprite_height <= 0 || sprite_x + 8 <= 0) continue;

        // Skip if current line is not within the sprite's vertical range
        if (LY(gpu) < sprite_y || LY(gpu) >= (sprite_y + sprite_height)) continue;

        uint8_t tile_index = sprite->tile_index;
        uint8_t flags = sprite->flags;

        // Flip Y
        int line = LY(gpu) - sprite_y;
        if (flags & 0x40) { // Bit 6: flip Y
            line = sprite_height - 1 - line;
        }

        // If using 8x16 sprites, ignore bottom bit of tile index
        if (sprite_height == 16) {
            tile_index &= 0xFE;
        }

        uint16_t tile_addr = 0x8000 + tile_index * 16;
        uint8_t byte1 = read_vram(gpu, tile_addr + line * 2);
        uint8_t byte2 = read_vram(gpu, tile_addr + line * 2 + 1);

        for (int x = 0; x < 8; x++) {
            int pixel_x = x;
            if (flags & 0x20) { // Bit 5: flip X
                pixel_x = 7 - x;
            }

            uint8_t color = (byte1 >> (7 - pixel_x)) & 1;
            color |= ((byte2 >> (7 - pixel_x)) & 1) << 1;

            // Skip transparent pixels
            if (color == 0) continue;

            int screen_x = sprite_x + x;
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            // Priority (bit 7) — if set, background color 1-3 has priority
            if ((flags & 0x80) && gpu->framebuffer[LY(gpu) * SCREEN_WIDTH + screen_x] != 0) {
                continue; // Skip drawing this sprite pixel
            }

            // Select sprite palette
            uint8_t palette = (flags & 0x10) ? OBP1(gpu) : OBP0(gpu);
            uint8_t final_color = (palette >> (color * 2)) & 0x03;

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

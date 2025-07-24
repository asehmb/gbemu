#include "graphics.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t inline read_vram(struct GPU *gpu, uint16_t addr) {
    if (addr < VRAM_BEGIN || addr > VRAM_END) {
        fprintf(stderr, "VRAM access out of bounds: 0x%04X\n", addr);
        return 0; // Return 0 for out of bounds access
    }
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
    const uint8_t ly = LY(gpu);
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
        uint8_t data1 = read_vram(gpu, tile_addr + line_in_tile * 2);     // Low bit plane
        uint8_t data2 = read_vram(gpu, tile_addr + line_in_tile * 2 + 1); // High bit plane
        uint8_t bit_index = 7 - (x_pos % 8);
        uint8_t color_index = ((data2 >> bit_index) & 1) << 1 |
                            ((data1 >> bit_index) & 1);
        uint8_t mapped_color = (BGP(gpu) >> (color_index * 2)) & 0x03; // Map color index to BGP

        gpu->framebuffer[ly * SCREEN_WIDTH + pixel] = mapped_color;

    }
    if (window_rendered_this_line) {
        gpu->window_line++;
    }
}

typedef struct {
    uint8_t index;
    uint8_t x;
    uint8_t y;
    uint8_t tile_index;
    uint8_t flags;
} SpriteInfo;

int sprite_cmp(const void *a, const void *b) {
    const SpriteInfo *s1 = (const SpriteInfo *)a;
    const SpriteInfo *s2 = (const SpriteInfo *)b;
    // Game Boy sprite priority: lower index = higher priority
    // If X positions are the same, use index for priority
    if (s1->x != s2->x) return s2->x - s1->x;
    return s2->index - s1->index;

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
void render_sprites(struct GPU *gpu) {
    /* OAM location:
        0xFE00 - 0xFE9F (40 sprites, each 4 bytes)
        Each sprite has:
        - Y coordinate (0-143)
        - X coordinate (0-159)
        - Tile index (0-255)
        - Flags:
            Bit 7: Priority (0=above BG, 1=below BG)Bit7: Sprite to Background Priority
            Bit6: Y flip
            Bit5: X flip
            Bit4: Palette number
            Bit3-0: Not used in standard gameboy
    */
    uint8_t lcdc = LCDC(gpu);
    bool use8x16_sprites = (lcdc & 0x04) != 0; // check if 1x1 or 1x2 sprites are used
    uint8_t ly = LY(gpu);
    size_t drawn = 0;
    SpriteInfo to_draw[10];

    for (size_t sprite_index = 0; sprite_index < 40 && drawn < 10; sprite_index++) {
        uint8_t index = sprite_index * 4;
        uint8_t y_pos = gpu->vram[0xFE00 + index] - 16; // Y coordinate (subtract 16 for top margin)
        uint8_t x_pos = gpu->vram[0xFE00 + index + 1] - 8; // X coordinate (subtract 8 for left margin)
        uint8_t y_size = use8x16_sprites ? 16 : 8; // Sprite height
        // check if sprite is on the current line
        if (ly >= y_pos && ly < y_pos + y_size) {
            to_draw[drawn++] = (SpriteInfo){
                                    .index = sprite_index,
                                    .x = x_pos,
                                    .y = y_pos,
                                    .tile_index = gpu->vram[0xFE00 + index + 2],
                                    .flags = gpu->vram[0xFE00 + index + 3]
                                }; // Store sprite index and X position

        }
    }

    // sort sprites by X position and then by index for priority
    qsort(to_draw, drawn, sizeof(SpriteInfo), sprite_cmp);
    for (size_t i = 0; i < drawn; i++) {
        uint8_t base = to_draw[i].index * 4;
        uint8_t obp = (to_draw[i].flags & 0x10) ? OBP1(gpu) : OBP0(gpu); // Object Palette

        bool y_flip = (to_draw[i].flags & 0x40) != 0; // Y flip
        bool x_flip = (to_draw[i].flags & 0x20) != 0; // X flip
        bool priority = (to_draw[i].flags & 0x80) != 0; // Priority

        uint8_t y_size = use8x16_sprites ? 16 : 8; // Sprite height
        uint8_t line_in_sprite = ly - to_draw[i].y; // Line in sprite (0-15 for 8x16, 0-7 for 8x8)
        if (y_flip) line_in_sprite = y_size - 1 - line_in_sprite; // Flip Y coordinate

        // For 8x16 sprites, pick tile_index or tile_index+1 depending on line_in_sprite
        if (use8x16_sprites) {
            if (line_in_sprite < 8) {
                // top tile
                to_draw[i].tile_index &= 0xFE;
            } else {
                // bottom tile
                to_draw[i].tile_index = (to_draw[i].tile_index & 0xFE) + 1;
                line_in_sprite -= 8;
            }
        }

        line_in_sprite *= 2; // Each line has 2 bytes (8 pixels each)

        uint16_t tile_addr = 0x8000 + (to_draw[i].tile_index * 16) + line_in_sprite;
        uint8_t data1 = read_vram(gpu, tile_addr);     // Low bit plane
        uint8_t data2 = read_vram(gpu, tile_addr + 1); // High bit plane
        for (int pixel = 0; pixel < 8; pixel++) {
            uint8_t color_index;
            if (x_flip) {
                color_index = ((data2 >> pixel) & 1) << 1 |
                                ((data1 >> pixel) & 1);
            } else {
                color_index = ((data2 >> (7 - pixel)) & 1) << 1 |
                                    ((data1 >> (7 - pixel)) & 1);
            }
            int pixel_x = to_draw[i].x + pixel;
            if (pixel_x < 0 || pixel_x >= SCREEN_WIDTH) continue;

            uint8_t bg_pixel = gpu->framebuffer[ly * SCREEN_WIDTH + pixel_x]; // check if pixel is already drawn

            // Skip if this is a transparent sprite pixel (color index 0)
            if (color_index == 0) continue; // Transparent pixel, skip
            
            // Apply sprite-to-BG priority (bit 7 of flags)
            if (priority && bg_pixel != (BGP(gpu) & 0x03)) continue; // Behind non-transparent BG
            
            uint8_t color_index_with_palette = (obp >> (color_index * 2)) & 0x03; // Get color index from palette

            // Set pixel in framebuffer
            gpu->framebuffer[ly * SCREEN_WIDTH + pixel_x] = color_index_with_palette;
        }
    }
}

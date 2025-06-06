

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define WHITE 0b11
#define DARK_GRAY 0b10
#define LIGHT_GRAY 0b01
#define BLACK 0b00

#define VRAM_BEGIN 0x8000
#define VRAM_END 0x9FFF
#define VRAM_SIZE (VRAM_END - VRAM_BEGIN + 1)

#define OAM_BEGIN 0xFE00
#define OAM_END 0xFE9F
#define OAM_SIZE (OAM_END - OAM_BEGIN + 1)

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144

#define COLOUR_FROM_PALETTE(palette, color) \
    ((palette) == WHITE ? 0xFFFFFF : \
     (palette) == DARK_GRAY ? 0xAAAAAA : \
     (palette) == LIGHT_GRAY ? 0x555555 : \
     0x000000) // BLACK

#define LCDC_LCD_ENABLE(lcdc)             (((lcdc) >> 7) & 0x1) // mask with 0x80
#define LCDC_WINDOW_TILE_MAP(lcdc)        (((lcdc) >> 6) & 0x1) // mask with 0x40
#define LCDC_WINDOW_ENABLE(lcdc)          (((lcdc) >> 5) & 0x1) // mask with 0x20
#define LCDC_BG_WIN_TILE_DATA(lcdc)       (((lcdc) >> 4) & 0x1) // mask with 0x10
#define LCDC_BG_TILE_MAP(lcdc)            (((lcdc) >> 3) & 0x1) // mask with 0x08
#define LCDC_OBJ_SIZE(lcdc)               (((lcdc) >> 2) & 0x1) // mask with 0x04
#define LCDC_OBJ_ENABLE(lcdc)             (((lcdc) >> 1) & 0x1) // mask with 0x02
#define LCDC_BG_DISPLAY(lcdc)             ((lcdc) & 0x1) // mask with 0x01

#define OAM_FLAGS_PRIORITY(flags)        (((flags) >> 7) & 0x1)
#define OAM_FLAGS_Y_FLIP(flags)          (((flags) >> 6) & 0x1)
#define OAM_FLAGS_X_FLIP(flags)          (((flags) >> 5) & 0x1)
#define OAM_FLAGS_PALETTE(flags)         (((flags) >> 4) & 0x1)

#define REQUEST_INTERRUPT(gpu, flag) \
    (gpu->cpu_bus[0xFF0F] |= (flag))

typedef struct {
    uint8_t data[16];  // each row = 2 bytes (8 pixels × 2 bits)
} Tile;

struct oam_entry {
    uint8_t y;        // Y coordinate (0-255)
    uint8_t x;        // X coordinate (0-255)
    uint8_t tile_index; // Tile index (0-255)
    uint8_t flags;    // Flags (palette, priority, y-flip, x-flip)
};

struct GPU {
    uint8_t vram[VRAM_SIZE]; // Video RAM
    Tile tiles[384]; // 384 tiles, each 16 bytes (8x8 pixels)
    uint8_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT]; // Framebuffer for rendering
    uint8_t registers[0xFF]; // GPU registers (0xFF40 - 0xFF4B)
    struct oam_entry oam_entries[40]; // Object Attribute Memory (OAM)
    uint8_t *cpu_bus;

    uint8_t lcdc;             // LCD Control (0xFF40)
    uint8_t stat;             // LCD Status (0xFF41)
    uint8_t scy, scx;         // Scroll Y/X (0xFF42/0xFF43)
    uint8_t ly, lyc;          // Current Line, Compare Line (0xFF44/0xFF45)
    uint8_t dma;              // DMA Transfer (0xFF46)
    uint8_t bgp;              // BG Palette (0xFF47)
    uint8_t obp0, obp1;       // Object Palettes (0xFF48/0xFF49)
    uint8_t wy, wx;           // Window Y/X (0xFF4A/0xFF4B)


    uint8_t mode; // Current mode (0, 1, 2, or 3)
    int mode_clock; // Mode clock for timing (up to 456 cycles)

    uint8_t tile_index; // Current tile index for rendering



};

void step_gpu(struct GPU *gpu, int cycles);

static inline uint8_t read_vram(struct GPU *gpu, uint16_t addr);

void render_background(struct GPU *gpu);
void render_window(struct GPU *gpu);
void render_sprites(struct GPU *gpu);


static void maybe_trigger_stat_interrupt(struct GPU *gpu, int mode);


static inline void write_vram(struct GPU *gpu, uint16_t addr, uint8_t value);
#endif // GRAPHICS_H
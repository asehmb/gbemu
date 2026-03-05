# gbemu

A C-based Nintendo Game Boy emulator project with:
- an SDL runtime (`sdl/gbemu`)
- a debug runtime (`debug/gbemu`)
- an SM83 JSON-driven CPU instruction tester (`sm83_tester/gbemu`)

## Current status

This is an actively developed emulator focused on CPU/PPU/timer correctness and ROM compatibility.
It is usable for experimentation and development, but not aimed at polished end-user features.

Previously tested ROMs include:
- Pokemon Blue
- Dr. Mario
- Tetris
- The Legend of Zelda: Link's Awakening

## Project layout

```text
src/           Core emulator implementation (CPU, graphics, timer, ROM, input)
sdl/           Main SDL frontend
debug/         SDL frontend with extra debug tooling/logging
sm83_tester/   JSON test runner for SM83 instruction-state tests
tools/         Utility scripts (including SM83 batch runner)
testing/       Local test assets/log outputs
```

## Requirements

- `clang` (or compatible C compiler)
- `make`
- SDL2 development libraries
- cJSON development libraries (required for `sm83` target)

### Platform notes

The `makefile` includes platform handling for:
- macOS (Homebrew include/lib paths)
- Linux (`pkg-config` + system packages)
- Windows (MSYS2/MinGW via `pkg-config`)

## Build

Show available targets:

```bash
make help
```

Build all available targets:

```bash
make
```

Build individual targets:

```bash
make sdl
make debug
make sm83
```

Clean artifacts:

```bash
make clean
```

## Run

### SDL emulator

```bash
./sdl/gbemu <rom.gb> [bootrom.bin] [save_file.sav]
```

- If `save_file.sav` is omitted, battery-backed cartridges will auto-derive a `.sav` path from the ROM name.
- If `bootrom.bin` is provided, the emulator attempts to run the boot ROM first.

### Debug build

```bash
./debug/gbemu <rom.gb> [bootrom.bin]
```

Writes debug output files under `testing/` (for example `testing/test.log` and `testing/memory_dump.txt`).

### SM83 JSON tester

```bash
./sm83_tester/gbemu <test.json>
```

The binary stays silent on passing test cases and prints output on mismatches.

## Controls (SDL frontend)

- D-pad: Arrow keys
- A: `Z`
- B: `X`
- Select: `Space`
- Start: `Enter`

## Automated CPU instruction testing

CI runs SM83 instruction tests via `.github/workflows/sm83-test.yml`.

To run locally:

```bash
git clone https://github.com/SingleStepTests/sm83.git sm83-tests
make clean && make sm83
./tools/run_sm83.sh sm83-tests/v1 ./sm83_tester/gbemu
```

## Implementation overview

- `cpu.c/.h`: SM83 execution core, memory bus access, interrupt handling, DMA, opcode dispatch
- `graphics.c/.h`: scanline-based LCD pipeline (background/window/sprites)
- `timer.c/.h`: DIV/TIMA timing and timer interrupt generation
- `rom.c/.h`: cartridge loading, ROM/RAM bank setup, boot ROM loading, save file IO
- `input.c/.h`: joypad state mapping

## Limitations

- No save states or rewind features
- Not all cartridge hardware edge cases are fully implemented
- Focus is emulator development/testing rather than full end-user QoL features

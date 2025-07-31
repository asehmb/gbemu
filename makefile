# Compiler and flags
CC = clang
# Use -g -O0 for debugging, -O2 for release builds
BASE_CFLAGS = -O2 -march=native -flto -DNDEBUG -Wall -pthread -I$(SRC_DIR)
# BASE_CFLAGS = -g -O0 -march=native -Wall -pthread -I$(SRC_DIR)

# Debug flags for debug and testing targets
DEBUG_CFLAGS_BASE = -g -O0 -march=native -Wall -pthread -I$(SRC_DIR)

# Platform detection
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Platform-specific settings
ifeq ($(UNAME_S),Linux)
    # Linux settings
    PKG_CONFIG ?= pkg-config
    SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2")
    SDL2_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || echo "-lSDL2 -lSDL2main")
    CJSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcjson 2>/dev/null || echo "")
    CJSON_LDFLAGS := $(shell $(PKG_CONFIG) --libs libcjson 2>/dev/null || echo "-lcjson")
    PROFILER_LDFLAGS := 
else ifeq ($(UNAME_S),Darwin)
    # macOS settings
    ifeq ($(UNAME_M),arm64)
        # Apple Silicon
        HOMEBREW_PREFIX := /opt/homebrew
    else
        # Intel Mac
        HOMEBREW_PREFIX := /usr/local
    endif
    SDL2_CFLAGS := -I$(HOMEBREW_PREFIX)/include/SDL2
    SDL2_LDFLAGS := -L$(HOMEBREW_PREFIX)/lib -lSDL2 -lSDL2main
    CJSON_CFLAGS := -I$(HOMEBREW_PREFIX)/include/cjson
    CJSON_LDFLAGS := -L$(HOMEBREW_PREFIX)/lib -lcjson
    PROFILER_LDFLAGS := -lprofiler
else ifeq ($(UNAME_S),MINGW64_NT-10.0)
    # Windows (MSYS2/MinGW)
    PKG_CONFIG ?= pkg-config
    SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || echo "-I/mingw64/include/SDL2")
    SDL2_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || echo "-lSDL2 -lSDL2main")
    CJSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcjson 2>/dev/null || echo "")
    CJSON_LDFLAGS := $(shell $(PKG_CONFIG) --libs libcjson 2>/dev/null || echo "-lcjson")
    PROFILER_LDFLAGS :=
else
    # Default/unknown platform - try pkg-config first, fallback to standard locations
    PKG_CONFIG ?= pkg-config
    SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2")
    SDL2_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || echo "-lSDL2 -lSDL2main")
    CJSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcjson 2>/dev/null || echo "")
    CJSON_LDFLAGS := $(shell $(PKG_CONFIG) --libs libcjson 2>/dev/null || echo "-lcjson")
    PROFILER_LDFLAGS :=
endif

# Directories
SRC_DIR = src
BUILD_DIR = build

# Shared source files (always compiled the same way)
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC_FILES))

# SM83-specific object files with ALLOW_ROM_WRITES
SM83_OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%_sm83.o, $(SRC_FILES))

# Debug-specific object files with debug flags
DEBUG_OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%_debug.o, $(SRC_FILES))

# Target configurations
# SDL target
SDL_DIR = sdl
SDL_TARGET = $(SDL_DIR)/gbemu
SDL_CFLAGS = $(BASE_CFLAGS) $(SDL2_CFLAGS)
SDL_LDFLAGS = $(SDL2_LDFLAGS) $(PROFILER_LDFLAGS)
SDL_MAIN_OBJ = $(BUILD_DIR)/sdl_main.o

# Terminal/CLI target (example for future)
CLI_DIR = cli
CLI_TARGET = $(CLI_DIR)/gbemu
CLI_CFLAGS = $(BASE_CFLAGS)
CLI_LDFLAGS = $(PROFILER_LDFLAGS)
CLI_MAIN_OBJ = $(BUILD_DIR)/cli_main.o

SM83_DIR = sm83_tester
SM83_TARGET = $(SM83_DIR)/gbemu
SM83_CFLAGS = $(DEBUG_CFLAGS_BASE) $(CJSON_CFLAGS)
SM83_LDFLAGS = $(PROFILER_LDFLAGS) $(CJSON_LDFLAGS)
SM83_MAIN_OBJ = $(BUILD_DIR)/sm83_tester.o

# Debug target (same as SDL but with debug main.c)
DEBUG_DIR = debug
DEBUG_TARGET = $(DEBUG_DIR)/gbemu
DEBUG_CFLAGS = $(DEBUG_CFLAGS_BASE) $(SDL2_CFLAGS)
DEBUG_LDFLAGS = $(SDL2_LDFLAGS) $(PROFILER_LDFLAGS)
DEBUG_MAIN_OBJ = $(BUILD_DIR)/debug_main.o

.PHONY: all clean sdl cli sm83 debug available-targets help

# Check what targets are available
AVAILABLE_TARGETS = sdl sm83 debug
ifneq ($(wildcard $(CLI_DIR)/main.c),)
AVAILABLE_TARGETS += cli
endif
# Default target builds all available targets
all: $(AVAILABLE_TARGETS)

# Show available targets
help:
	@echo "Available targets:"
	@echo "  all     - Build all available targets: $(AVAILABLE_TARGETS)"
	@echo "  sdl     - Build SDL version"
	@echo "  cli     - Build CLI version (if cli/main.c exists)"
	@echo "  sm83    - Build SM83 Tester"
	@echo "  debug   - Build Debug version (with extra debugging features)"
	@echo "  clean   - Clean build artifacts"
	@echo "  help    - Show this help message"

# SDL target
sdl: $(SDL_TARGET)

# CLI target (only if cli/main.c exists)
cli: $(CLI_TARGET)

sm83: $(SM83_TARGET)

debug: $(DEBUG_TARGET)

# SDL binary
$(SDL_TARGET): $(OBJ_FILES) $(SDL_MAIN_OBJ)
	@mkdir -p $(SDL_DIR)
	$(CC) $(SDL_CFLAGS) $^ -o $@ $(SDL_LDFLAGS)

# CLI binary (if cli/main.c exists)
$(CLI_TARGET): $(OBJ_FILES) $(CLI_MAIN_OBJ)
	@mkdir -p $(CLI_DIR)
	$(CC) $(CLI_CFLAGS) $^ -o $@ $(CLI_LDFLAGS)

# SM83 binary
$(SM83_TARGET): $(SM83_OBJ_FILES) $(SM83_MAIN_OBJ)
	@mkdir -p $(SM83_DIR)
	$(CC) $(SM83_CFLAGS) $^ -o $@ $(SM83_LDFLAGS)

# Debug binary
$(DEBUG_TARGET): $(DEBUG_OBJ_FILES) $(DEBUG_MAIN_OBJ)
	@mkdir -p $(DEBUG_DIR)
	$(CC) $(DEBUG_CFLAGS) $^ -o $@ $(DEBUG_LDFLAGS)


# Compile shared src/*.c files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BASE_CFLAGS) -c $< -o $@

# Compile SM83-specific src/*.c files with ALLOW_ROM_WRITES
$(BUILD_DIR)/%_sm83.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS_BASE) -DALLOW_ROM_WRITES -c $< -o $@

# Compile Debug-specific src/*.c files with debug flags
$(BUILD_DIR)/%_debug.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS_BASE) -c $< -o $@

# Compile SDL main.c
$(SDL_MAIN_OBJ): $(SDL_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(SDL_CFLAGS) -c $< -o $@

# Compile CLI main.c (if it exists)
$(CLI_MAIN_OBJ): $(CLI_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CLI_CFLAGS) -c $< -o $@

# Compile SM83 main.c
$(SM83_MAIN_OBJ): $(SM83_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(SM83_CFLAGS) -c $< -o $@

# Compile Debug main.c
$(DEBUG_MAIN_OBJ): $(DEBUG_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) -c $< -o $@


# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SDL_TARGET) $(CLI_TARGET) $(SM83_TARGET) $(DEBUG_TARGET)
